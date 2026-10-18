/*
 * quirks.c - VID/DID -> known-quirk lookup and its report.
 *
 * **Linux is the authority for this table.** Entries come from Linux
 * drivers/usb/host/xhci-pci.c and pci-quirks.c in the local mirror,
 * external/linux, at the commit external/README.md pins. Vendor ids Linux
 * names by constant were read in the mirror's include/linux/pci_ids.h at the
 * same commit (ATI 1002, AMD 1022, NEC 1033, TI 104C, VIA 1106, Cadence 17CD
 * and its USBSSP 0200, Renesas 1912, ASMedia 1B21, Zhaoxin 1D17, Intel 8086
 * and Panther Point 1E31); Fresco Logic 1B73, Etron 1B6F and Phytium 1DB7 are
 * defined in xhci-pci.c itself. Citations are file:line in that copy. Nothing
 * of Linux's code is copied here: each row states the ids and what Linux sets
 * for them. Design record 20 (docs/contributing/design/20-linux-xhci-quirks.md)
 * is the survey the rows follow; its section 3.1 is the id list of each quirk
 * the driver takes, and section 6.5 what this tool reports of them.
 *
 * Flags are of four kinds:
 *
 *   A quirk the driver takes (record 20 section 3.1), one QF_ per registry
 *   value, printed naming the value and its Linux flag: QF_BROKEN_MSI
 *   (XhciMsiAvoid), QF_STREAMS_OFF, QF_RESUME_REINIT, QF_SHUTDOWN_EHCI,
 *   QF_SHUTDOWN_RESET (off by default in the driver), QF_SUSPEND_DELAY,
 *   QF_SLOW_SUSPEND, QF_ASM_FLOWCTL, QF_INTERVAL7, QF_INTERVAL9 and
 *   QF_INTERVAL_VENDOR (both XhciIntervalCap), QF_STUCK_LINK, QF_RING_PAD,
 *   QF_CTRL_NOOP, QF_ISOCH_SPURIOUS, QF_SPARSE_CLEAR. The rows carry Linux's
 *   ids exactly; no revision is matched (record 20 section 6.3), so
 *   QF_SLOW_SUSPEND is on every revision of 1B73:1000 where Linux keys it on
 *   revision 04.
 *
 *   A Linux bit the driver covers without a value of its own (record 20
 *   section 3.2), or does not need: QF_SPURIOUS (XHCI_SPURIOUS_SUCCESS),
 *   QF_BEI (XHCI_AVOID_BEI), QF_PME_STUCK (XHCI_PME_STUCK_QUIRK),
 *   QF_ZHAOXIN_HOST, QF_FW_UPLOAD (xhci-pci-renesas.c's ROM-less firmware
 *   load, which the driver does not do).
 *
 *   A Linux bit the driver does not take (record 20 section 3.3), named so a
 *   report says so: QF_NT_SW_BW (Q18, XHCI_SW_BW_CHECKING) and QF_NT_AMD_PLL
 *   (Q19, XHCI_AMD_PLL_FIX).
 *
 *   This tool's own reading of a Linux mechanism, with no per-ID bit of its
 *   own: QF_XUSB2PR marks the PCH generations pci-quirks.c's Intel port
 *   switchover has an EHCI to switch from (it is applied to every Intel
 *   xHCI and keyed on an EHCI being present, not on an ID); QF_FW_SPI is
 *   what XHCI_NEC_HOST's Get Firmware command implies for the uPD720200 (the
 *   firmware is on the card and nothing is uploaded); QF_AVOID on Etron is
 *   this project's verdict on the part, beside the Linux bits it carries.
 *
 *   Defined and printed but set by no row since the 2026-09-17 audit:
 *   QF_BULK64K (an ASM1042 claim no source supported), QF_CMD_RETRY (a
 *   NEC/Renesas claim the mirror has no counterpart for) and QF_COMPLIANCE
 *   (XHCI_COMP_MODE_QUIRK is keyed on DMI strings, not an ID).
 *
 * Two row forms. An exact row names a vendor and a device. A vendor-wide row
 * names a vendor and QUIRK_ANY_DID, and stands for what Linux sets on every
 * device of that vendor (xhci-pci.c tests `pdev->vendor` alone): VIA 1106,
 * Zhaoxin 1D17 and AMD 1022. **Both are reported**: a controller's exact row
 * first, then its vendor's row, each with its own lines, so neither hides
 * the other (VIA's VL800 is XHCI_BROKEN_STREAMS by its id and
 * XHCI_RESET_ON_RESUME by its vendor). There is at most one vendor-wide row
 * per vendor, and a flag is put on whichever row Linux keys it on, never
 * both, so nothing prints twice (test\test_quirks.c checks both rules).
 * quirk_find() returns the exact row alone - the flag tests elsewhere in the
 * tool (QF_XUSB2PR in C7, QF_BROKEN_MSI in the verdict) and C7's "Intel xHCI
 * not in the table" note key on it - and quirk_find_vendor() the vendor-wide
 * one.
 *
 * Grow this table as real controllers are tested, and from Linux only.
 */

#include <stddef.h>
#include "qual.h"

static const QUIRK quirk_table[] = {
    /* Intel. xhci-pci.c:345-349 gives every Intel xHCI XHCI_AVOID_BEI, so
     * QF_BEI is on every row. QF_PME_STUCK follows xhci-pci.c:371-380's
     * list (Sunrise Point-H/LP, Cherryview, Apollo Lake, Denverton, Comet
     * Lake PCH-H, and the Broxton 0AA8/1AA8 this table does not carry);
     * until the 2026-09-17 audit it also sat on 8C31, 8CB1, 8D31, A2AF and
     * A36D, which the list does not name. XHCI_COMP_MODE_QUIRK is keyed on
     * DMI strings (HP Z420/Z620/Z820/Z1, xhci.c:431-447), not on a PCI ID,
     * so no row can carry QF_COMPLIANCE.
     *
     * The 7/8/9-series and C610 rows: pci-quirks.c's port switchover
     * (usb_enable_intel_xhci_ports) is applied to every Intel xHCI and does
     * nothing where no Intel EHCI exists, so keying QF_XUSB2PR on the four
     * PCH generations that ship an EHCI is this tool's own reading; Skylake
     * and later have no EHCI and no XUSB2PR.
     *
     * Panther Point 1E31 (xhci-pci.c:350-363): XHCI_SPURIOUS_REBOOT
     * (QF_SHUTDOWN_EHCI), XHCI_SW_BW_CHECKING (Q18, not taken) and
     * XHCI_EP_LIMIT_QUIRK, which the driver's pool bound covers (record 20
     * section 3.2) and which has no flag. The -LP parts 9C31 and 9CB1
     * (xhci-pci.c:365-370) get XHCI_SPURIOUS_REBOOT and XHCI_SPURIOUS_WAKEUP.
     * XHCI_MISSING_CAS (QF_STUCK_LINK) is xhci-pci.c:390-396's five ids.
     *
     * Fleet: 9D2F is the one row bare-metal runs in results/ had printed
     * (ThinkPad E460) when 35-T.8 was done; the other fleet machine, a Comet
     * Lake-LP 02ED, has no row because the mirror has none (lessons.md,
     * results/p14s-gen1-*). */
    { 0x8086, 0x1E31,
      QF_XUSB2PR | QF_BEI | QF_SHUTDOWN_EHCI | QF_NT_SW_BW,
      "Intel Panther Point (7-series PCH)" },
    { 0x8086, 0x8C31, QF_XUSB2PR | QF_BEI,
      "Intel Lynx Point (8-series PCH)" },
    /* The -LP parts (roadmap-hcd task 34.3): xhci-pci.c:44-45 names them. */
    { 0x8086, 0x9C31,
      QF_XUSB2PR | QF_BEI | QF_SHUTDOWN_EHCI | QF_SHUTDOWN_RESET,
      "Intel Lynx Point-LP (8-series PCH-LP)" },
    { 0x8086, 0x8CB1, QF_XUSB2PR | QF_BEI,
      "Intel Wildcat Point (9-series PCH)" },
    { 0x8086, 0x9CB1,
      QF_XUSB2PR | QF_BEI | QF_SHUTDOWN_EHCI | QF_SHUTDOWN_RESET,
      "Intel Wildcat Point-LP (9-series PCH-LP)" },
    { 0x8086, 0x8D31, QF_XUSB2PR | QF_BEI,
      "Intel Wellsburg (C610 PCH)" },
    { 0x8086, 0xA12F, QF_BEI | QF_PME_STUCK | QF_STUCK_LINK,
      "Intel Sunrise Point-H (100-series)" },
    { 0x8086, 0x9D2F, QF_BEI | QF_PME_STUCK | QF_STUCK_LINK,
      "Intel Sunrise Point-LP (100-series)" },
    { 0x8086, 0xA2AF, QF_BEI, "Intel Union Point (200-series)" },
    { 0x8086, 0xA36D, QF_BEI, "Intel Cannon Point (300-series)" },
    { 0x8086, 0xA3AF, QF_BEI | QF_PME_STUCK,
      "Intel Comet Lake PCH-H (400-series)" },
    /* SoC parts, xhci-pci.c:46-52 (roadmap-hcd task 38.10). 22B5 also gets
     * XHCI_SSIC_PORT_UNUSED (Q17, deferred: record 20 section 3.0) and,
     * with 5AA8, XHCI_INTEL_USB_ROLE_SW (not applicable); no flag here for
     * either. */
    { 0x8086, 0x22B5, QF_BEI | QF_PME_STUCK | QF_STUCK_LINK,
      "Intel Cherryview (Cherry Trail / Braswell SoC)" },
    { 0x8086, 0x5AA8, QF_BEI | QF_PME_STUCK | QF_STUCK_LINK,
      "Intel Apollo Lake SoC" },
    { 0x8086, 0x19D0, QF_BEI | QF_PME_STUCK | QF_STUCK_LINK,
      "Intel Denverton SoC" },

    /* NEC / Renesas. Linux's XHCI_NEC_HOST (every 0x1033 xHCI) does two
     * things: it queues a vendor NEC Get Firmware command at start and logs
     * the version the card's own firmware answers with, which is what QF_FW_SPI
     * reports (nothing is uploaded), and it sets the chain bit on isoch link
     * TRBs (not taken, record 20 section 3.3). It sets no spurious-success or
     * command-retry quirk; the row carried QF_SPURIOUS | QF_CMD_RETRY until
     * the 2026-09-17 audit. */
    { 0x1033, 0x0194, QF_FW_SPI,
      "NEC uPD720200/200A (fw on card SPI flash)" },
    /* The two Renesas parts: xhci-pci-renesas.c loads firmware into a
     * ROM-less card (QF_FW_UPLOAD); xhci-pci.c:427-435 gives both
     * XHCI_ZERO_64B_REGS (not applicable, no flag) and the uPD720202
     * XHCI_RESET_ON_RESUME as well. Both rows carried QF_CMD_RETRY until the
     * 2026-09-17 audit; the mirror has no command-retry quirk. */
    { 0x1912, 0x0014, QF_FW_UPLOAD,
      "Renesas uPD720201 (fw upload if ROM-less)" },
    { 0x1912, 0x0015, QF_FW_UPLOAD | QF_RESUME_REINIT,
      "Renesas uPD720202 (fw upload if ROM-less)" },

    /* ASMedia. Linux's PCI_DEVICE_ID_ASMEDIA_* names (xhci-pci.c:90-95):
     * 0x1142 is the ASM1042A and 0x1242 the ASM1142; 1142, 1242, 2142 and
     * 3242 carry XHCI_NO_64BIT_SUPPORT, which cannot matter to a
     * 32-bit-only driver, and the ASM1042A alone also
     * XHCI_ASMEDIA_MODIFY_FLOWCONTROL (xhci-pci.c:472-474). The ASM1042
     * gets XHCI_SPURIOUS_SUCCESS and XHCI_BROKEN_STREAMS (xhci-pci.c:453-461)
     * and reports HCIVERSION 0.96, below the driver's floor today. It
     * carried QF_BULK64K instead until the 2026-09-17 audit; no source in
     * this repository or in Linux supports a 64 KB bulk limit on it. The
     * ASM3042 gets XHCI_RESET_ON_RESUME (xhci-pci.c:476-478). */
    { 0x1B21, 0x1042, QF_SPURIOUS | QF_STREAMS_OFF, "ASMedia ASM1042" },
    { 0x1B21, 0x1142, QF_ASM_FLOWCTL,   "ASMedia ASM1042A" },
    { 0x1B21, 0x1242, 0,                "ASMedia ASM1142" },
    { 0x1B21, 0x2142, 0,                "ASMedia ASM2142" },
    { 0x1B21, 0x3042, QF_RESUME_REINIT, "ASMedia ASM3042" },

    /* Fresco Logic, PCI vendor 0x1B73. (The rows carried 0x1D5C, Fresco's
     * USB-IF vendor ID, until the 2026-09-17 audit, so no Fresco controller
     * ever matched.) xhci-pci.c:256-288: XHCI_BROKEN_MSI for the FL1000/PDK
     * and the FL1400, at every revision; XHCI_BROKEN_STREAMS for the FL1000
     * and the FL1009; XHCI_SLOW_SUSPEND for the FL1000 at revision 04 (and
     * the deprecated XHCI_RESET_EP_QUIRK at revision 00, which acts
     * nowhere). */
    { 0x1B73, 0x1000, QF_BROKEN_MSI | QF_STREAMS_OFF | QF_SLOW_SUSPEND,
      "Fresco Logic FL1000 (PDK)" },
    { 0x1B73, 0x1009, QF_STREAMS_OFF, "Fresco Logic FL1009" },
    { 0x1B73, 0x1100, 0,              "Fresco Logic FL1100" },
    { 0x1B73, 0x1400, QF_BROKEN_MSI,  "Fresco Logic FL1400" },

    /* VIA Labs, PCI vendor 0x1106. (The rows carried 0x2109, VIA's USB-IF
     * vendor ID, with the VL812/VL813 hub product IDs, until the 2026-09-17
     * audit.) Linux gives every VIA xHCI XHCI_RESET_ON_RESUME
     * (xhci-pci.c:436-437: the vendor-wide row below), the VL800
     * XHCI_BROKEN_STREAMS (xhci-pci.c:444-446) and the VL805
     * XHCI_TRB_OVERFETCH (xhci-pci.c:448-451). */
    { 0x1106, 0x3432, QF_STREAMS_OFF, "VIA Labs VL800" },
    { 0x1106, 0x3483, QF_RING_PAD,    "VIA Labs VL805" },
    { 0x1106, QUIRK_ANY_DID, QF_RESUME_REINIT, "VIA (every 1106 xHCI)" },

    /* Zhaoxin, PCI vendor 0x1D17 (xhci-pci.c:493-504): every part
     * XHCI_ZHAOXIN_HOST (covered, record 20 section 3.2) and
     * XHCI_LPM_SUPPORT (not applicable); 9202 XHCI_RESET_ON_RESUME and
     * XHCI_TRB_OVERFETCH, 9203 XHCI_TRB_OVERFETCH. */
    { 0x1D17, 0x9202, QF_RESUME_REINIT | QF_RING_PAD, "Zhaoxin xHCI 9202" },
    { 0x1D17, 0x9203, QF_RING_PAD,                    "Zhaoxin xHCI 9203" },
    { 0x1D17, QUIRK_ANY_DID, QF_ZHAOXIN_HOST, "Zhaoxin (every 1D17 xHCI)" },

    /* Etron. xhci-pci.c:418-425 gives both parts XHCI_ETRON_HOST (the
     * driver's XhciControlLinkNoOp and XhciIsochSpuriousSuccess),
     * XHCI_RESET_ON_RESUME, XHCI_BROKEN_STREAMS and XHCI_NO_SOFT_RETRY; the
     * driver's soft retry (design record 17 section 4.2) already excludes
     * them, so that one has no flag. */
    { 0x1B6F, 0x7023,
      QF_AVOID | QF_STREAMS_OFF | QF_RESUME_REINIT | QF_CTRL_NOOP |
      QF_ISOCH_SPURIOUS,
      "Etron EJ168 (Linux: no soft retry)" },
    { 0x1B6F, 0x7052,
      QF_AVOID | QF_STREAMS_OFF | QF_RESUME_REINIT | QF_CTRL_NOOP |
      QF_ISOCH_SPURIOUS,
      "Etron EJ188 (Linux: no soft retry)" },

    /* TI, PCI vendor 0x104C: XHCI_LIMIT_ENDPOINT_INTERVAL_7 on 8241
     * (xhci-pci.c:480-481), the TUSB73x0 of record 20 section 3.1. Linux's
     * forced BIOS handoff also names it (pci-quirks.c:1190-1196; Q15,
     * dropped). */
    { 0x104C, 0x8241, QF_INTERVAL7, "TI TUSB73x0" },

    /* ATI, PCI vendor 0x1002: the Navi10 graphics card's USB-C controller
     * gets XHCI_LIMIT_ENDPOINT_INTERVAL_9 (xhci-pci.c:304-306), which the
     * driver's XhciIntervalCap takes on it as on AMD's. */
    { 0x1002, 0x7316, QF_INTERVAL9, "ATI Navi10 USB-C (graphics card)" },

    /* Cadence USBSSP, PCI vendor 0x17CD: XHCI_CDNS_SCTX_QUIRK
     * (xhci-pci.c:506-508), which the driver answers with XhciStreamsOff
     * (record 20 section 3.3, Q20). */
    { 0x17CD, 0x0200, QF_STREAMS_OFF,
      "Cadence USBSSP (Linux: XHCI_CDNS_SCTX_QUIRK)" },

    /* Phytium, PCI vendor 0x1DB7 (xhci-pci.c:60-61): XHCI_RESET_ON_RESUME
     * (xhci-pci.c:439-441). Not x86 (record 20 section 3.0); listed because
     * the driver's match carries it. */
    { 0x1DB7, 0xDC27, QF_RESUME_REINIT, "Phytium xHCI (not an x86 part)" },

    /* AMD, PCI vendor 0x1022 (roadmap-hcd task 35-T.8; design record 17
     * section 4.8; task 38.10). The driver caps an interrupt endpoint's
     * Interval at 8 on EVERY AMD controller under XhciIntervalCap (record 17
     * section 4.7), wider than Linux's XHCI_LIMIT_ENDPOINT_INTERVAL_9 list at
     * xhci-pci.c:293-302, whose eight ids are the first rows here: the
     * vendor-wide row's QF_INTERVAL_VENDOR says so once for every AMD id,
     * listed or not. XHCI_SUSPEND_DELAY: 145C, 15E0, 15E1, 43BB
     * (xhci-pci.c:315-320). Raven 15E0/15E1 also get XHCI_SNPS_BROKEN_SUSPEND
     * (covered), Raven2 15E5 XHCI_DISABLE_SPARSE and XHCI_RESET_ON_RESUME
     * (xhci-pci.c:326-329). The Promontory A parts get XHCI_U2_DISABLE_WAKE
     * (xhci-pci.c:334-339; covered); 43B9 and 43BB also XHCI_NO_SOFT_RETRY
     * (xhci-pci.c:488-491), which the driver's soft retry honours. 43F7 gets
     * only XHCI_DEFAULT_PM_RUNTIME_ALLOW (xhci-pci.c:331-332). The Promontory
     * 21 parts 43FC/43FD go to Linux's separate xhci-pci-prom21 driver when
     * it is built (xhci-pci.c:699-714), and the AM5 and X570 lines are
     * otherwise unlisted in the mirror. XHCI_AMD_PLL_FIX (xhci-pci.c:311-313)
     * is set on every AMD xHCI when pci-quirks.c:153-194 and 209-240 find an
     * SB700 at revision 3Bh or below, an SB800, a Hudson-2 or a Bolton SMBus
     * device; this tool does not look for that device, so the vendor-wide
     * row says when it would apply (Q19, not taken). */
    { 0x1022, 0x13ED, 0, "AMD Ariel Type-C (Linux: interval cap 9)" },
    { 0x1022, 0x13EE, 0, "AMD Ariel Type-A (Linux: interval cap 9)" },
    { 0x1022, 0x148C, 0, "AMD Starship (Linux: interval cap 9)" },
    { 0x1022, 0x15D4, 0, "AMD Fireflight (Linux: interval cap 9)" },
    { 0x1022, 0x15D5, 0, "AMD Fireflight (Linux: interval cap 9)" },
    { 0x1022, 0x15E0, QF_SUSPEND_DELAY,
      "AMD Raven (Linux: interval cap 9)" },
    { 0x1022, 0x15E1, QF_SUSPEND_DELAY,
      "AMD Raven (Linux: interval cap 9)" },
    { 0x1022, 0x15E5, QF_RESUME_REINIT | QF_SPARSE_CLEAR,
      "AMD Raven2 (Linux: interval cap 9)" },
    { 0x1022, 0x145C, QF_SUSPEND_DELAY, "AMD xHCI 145C" },
    { 0x1022, 0x43B9, 0, "AMD Promontory A (Linux: no soft retry)" },
    { 0x1022, 0x43BA, 0, "AMD Promontory A" },
    { 0x1022, 0x43BB, QF_SUSPEND_DELAY,
      "AMD Promontory A (Linux: no soft retry)" },
    { 0x1022, 0x43BC, 0, "AMD Promontory A" },
    { 0x1022, 0x43F7, 0, "AMD xHCI 43F7" },
    { 0x1022, 0x43FC, 0, "AMD Promontory 21" },
    { 0x1022, 0x43FD, 0, "AMD Promontory 21" },
    { 0x1022, QUIRK_ANY_DID, QF_INTERVAL_VENDOR | QF_NT_AMD_PLL,
      "AMD (every 1022 xHCI)" },

    { 0, 0, 0, NULL }
};

const QUIRK *quirk_find(u16 vid, u16 did)
{
    const QUIRK *q;

    if (did == QUIRK_ANY_DID)
        return NULL;
    for (q = quirk_table; q->name != NULL; q++) {
        if (q->vid == vid && q->did == did)
            return q;
    }
    return NULL;
}

const QUIRK *quirk_find_vendor(u16 vid)
{
    const QUIRK *q;

    for (q = quirk_table; q->name != NULL; q++) {
        if (q->vid == vid && q->did == QUIRK_ANY_DID)
            return q;
    }
    return NULL;
}

const QUIRK *quirk_row(int i)
{
    int n;

    for (n = 0; quirk_table[n].name != NULL; n++) {
        if (n == i)
            return &quirk_table[n];
    }
    return NULL;
}

/* One item: what it is on the first line, what it means on the second, so
 * each line fits the 80-column DOS screen the pager counts rows on. */
static void item(QPRINTF_FN pf, const char *head, const char *body)
{
    pf("    - %s\n", head);
    if (body != NULL)
        pf("      %s\n", body);
}

/* One row's lines. A taken quirk names the driver's registry value first and
 * Linux's flag in parentheses (record 20 section 6.5); a covered or untaken
 * one names the Linux flag and says which it is. */
static void quirk_lines(const QUIRK *q, QPRINTF_FN pf)
{
    u32 f = q->flags;

    if (f & QF_XUSB2PR)
        item(pf, "EHCI<->xHCI port mux (XUSB2PR)",
             "run C7 before judging dead ports");
    if (f & QF_FW_UPLOAD)
        item(pf, "needs firmware upload if the card is ROM-less",
             "xhci98 does not upload firmware");
    if (f & QF_FW_SPI)
        item(pf, "firmware from on-card SPI flash (no upload)", NULL);

    /* Taken by the driver: value (Linux flag), then what the driver does. */
    if (f & QF_BROKEN_MSI)
        item(pf, "XhciMsiAvoid (Linux XHCI_BROKEN_MSI)",
             "the line or polling, never MSI");
    if (f & QF_STREAMS_OFF)
        item(pf, "XhciStreamsOff (Linux XHCI_BROKEN_STREAMS)",
             "no streams on this controller");
    if (f & QF_RESUME_REINIT)
        item(pf, "XhciResumeReinit (Linux XHCI_RESET_ON_RESUME)",
             "no save/restore; resume re-initializes");
    if (f & QF_SHUTDOWN_EHCI)
        item(pf, "XhciIntelShutdownToEhci (Linux XHCI_SPURIOUS_REBOOT)",
             "ports routed to EHCI at shutdown");
    if (f & QF_SHUTDOWN_RESET)
        item(pf, "XhciShutdownReset (Linux XHCI_SPURIOUS_WAKEUP)",
             "HCRST after the shutdown's halt; driver default 0, off");
    if (f & QF_SUSPEND_DELAY)
        item(pf, "XhciSuspendHaltDelay (Linux XHCI_SUSPEND_DELAY)",
             "a pause before the suspend's halt");
    if (f & QF_SLOW_SUSPEND)
        item(pf, "XhciSuspendHaltTimeout (Linux XHCI_SLOW_SUSPEND)",
             "the suspend's halt bound 640 ms; every revision");
    if (f & QF_ASM_FLOWCTL)
        item(pf, "XhciAsmediaFlowControl (Linux "
                 "XHCI_ASMEDIA_MODIFY_FLOWCONTROL)",
             "vendor flow-control write after each reset and resume");
    if (f & QF_INTERVAL7)
        item(pf, "XhciTiIntervalCap (Linux XHCI_LIMIT_ENDPOINT_INTERVAL_7)",
             "HS and above: interrupt Interval 7 and up programmed as 6");
    if (f & QF_INTERVAL9)
        item(pf, "XhciIntervalCap (Linux XHCI_LIMIT_ENDPOINT_INTERVAL_9)",
             "interrupt Interval 9 and up programmed as 8");
    if (f & QF_INTERVAL_VENDOR)
        item(pf, "XhciIntervalCap (Linux XHCI_LIMIT_ENDPOINT_INTERVAL_9)",
             "Linux lists eight AMD ids; the driver caps every AMD one");
    if (f & QF_STUCK_LINK)
        item(pf, "XhciStuckLinkReset (Linux XHCI_MISSING_CAS)",
             "a USB3 port stuck in Polling warm-reset after resume");
    if (f & QF_RING_PAD)
        item(pf, "XhciStreamRingPad (Linux XHCI_TRB_OVERFETCH)",
             "padding after a stream block; the controller overfetches");
    if (f & QF_CTRL_NOOP)
        item(pf, "XhciControlLinkNoOp (Linux XHCI_ETRON_HOST, part)",
             "SS control Setup and Data kept off the Link TRB");
    if (f & QF_ISOCH_SPURIOUS)
        item(pf, "XhciIsochSpuriousSuccess (Linux XHCI_ETRON_HOST, part)",
             "isoch Success after an error on the same TRB dropped");
    if (f & QF_SPARSE_CLEAR)
        item(pf, "XhciAmdSuspendBitClear (Linux XHCI_DISABLE_SPARSE)",
             "bit 17 of BAR offset C12Ch cleared before suspend");

    /* Covered by the driver with no value of its own. */
    if (f & QF_SPURIOUS)
        item(pf, "Linux XHCI_SPURIOUS_SUCCESS: covered on every controller",
             "spurious Success after a short packet on the same TD");
    if (f & QF_BEI)
        item(pf, "Linux XHCI_AVOID_BEI: covered",
             "BEI in isoch TRBs; xhci98 never sets it");
    if (f & QF_PME_STUCK)
        item(pf, "Linux XHCI_PME_STUCK_QUIRK: PME wake latch bug",
             "matters on Win2000 power transitions");
    if (f & QF_ZHAOXIN_HOST)
        item(pf, "Linux XHCI_ZHAOXIN_HOST: covered",
             "USB 3.1 Minor Revision; xhci98 reads speeds from PSIs");

    /* Not taken by the driver. */
    if (f & QF_NT_SW_BW)
        item(pf, "Linux XHCI_SW_BW_CHECKING: not taken by the driver",
             "the controller's own periodic admission is relied on");
    if (f & QF_NT_AMD_PLL)
        item(pf, "Linux XHCI_AMD_PLL_FIX: not taken by the driver",
             "applies on an SB700 (rev 3Bh or below), SB800, Hudson-2 "
             "or Bolton");

    /* This project's own entries. */
    if (f & QF_COMPLIANCE)
        item(pf, "SS compliance-mode lockup (Linux XHCI_COMP_MODE_QUIRK)",
             "Linux keys it on DMI strings");
    if (f & QF_BULK64K)
        item(pf, "keep bulk TRB chains under 64 KB", NULL);
    if (f & QF_CMD_RETRY)
        item(pf, "retry TRB Error command completions once", NULL);
    if (f & QF_AVOID)
        item(pf, "known-unreliable silicon: avoid as test platform", NULL);
}

void quirk_report(u16 vid, u16 did, int hciver_ok, u16 hciversion,
                  QPRINTF_FN pf)
{
    const QUIRK *q = quirk_find(vid, did);
    const QUIRK *v = quirk_find_vendor(vid);

    if (q == NULL && v == NULL) {
        pf("  quirks: none known for this VID/DID\n");
        return;
    }
    if (q != NULL) {
        pf("  quirks: %s\n", q->name);
        quirk_lines(q, pf);
    }
    if (v != NULL) {
        pf("  quirks: %s\n", v->name);
        quirk_lines(v, pf);
    }
    /* A listed part the driver will not start: say so, so none of the
     * values above is read as acting on it. */
    if (hciver_ok && hciversion < QUIRK_HCI_FLOOR)
    {
        pf("    - HCIVERSION %X.%02X is below xhci98's floor %X.%02X\n",
           hciversion >> 8, hciversion & 0xFF,
           QUIRK_HCI_FLOOR >> 8, QUIRK_HCI_FLOOR & 0xFF);
        pf("      refused at every XhciAccept096: none of the above "
           "acts on it\n");
    }
}
