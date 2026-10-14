/*
 * qual.h - shared definitions for the Phase 0 DOS hardware qualification tool.
 *
 * Register offsets and bit positions are transcribed from
 * docs/usb-xhci-info/xhci-data-structures.md (itself verified against the xHCI 1.2c spec
 * PDF). Do not edit a bit position here without checking that document.
 *
 * Build target: Open Watcom C, 32-bit flat model, DOS/32A embedded as the
 * EXE stub (see MAKEFILE) - DOS/4GW-compatible zero-based flat + DPMI.
 */

#ifndef QUAL_H
#define QUAL_H

/* The package version, from the one place it is edited (task 14.1.10). The
 * path is relative to this file, which is what a quoted include means to all
 * three of this project's compilers; the header itself is `#define` lines and
 * comments only, so Open Watcom takes it as readily as MSVC and rc.exe do. */
#include "../src/xhci_version.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;

/* **This tracks the driver's package version, not a version line of its own.**
 * The qualifier is published inside a release directory beside `xhci98.sys`
 * (`releases\<version>\xhciqual\`), and answering "which build is this" with a
 * different number from the one on the box is how a bug report gets tied to the
 * wrong artifact. So it is not a number this file gets to choose: since task
 * 14.1.10 it expands to `src\xhci_version.h`'s, which is the same macro
 * `src\xhci98.rc` puts in the driver's own resource and the same one
 * `src\xhci98.inf`'s DriverVer is gated against. **Do not put a literal back
 * here** - nothing in this tool's build would notice, and the number is printed
 * into every log a user saves and sends back. See
 * docs\contributing\build-and-test.md, "Versioning the driver".
 *
 * The tool's own former numbering (v0.8 to v0.11) survives only in
 * xhciqual\README.md's change log and in the results write-ups under
 * xhciqual\results\, which are records of which build produced a fact sheet and
 * are not rewritten. */
#define TOOL_VERSION XHCI_VER_STR

/* Short build identifier so a saved log or a photographed crash screen can
 * be tied back to the exact binary and its MAP file (docs/contributing/lessons.md). */
#define TOOL_BUILD (__DATE__ " " __TIME__)

/* ------------------------------------------------------------------ */
/* PCI discovery (pci.c)                                              */
/* ------------------------------------------------------------------ */

#define PCI_CLASS_OHCI 0x0C0310UL
#define PCI_CLASS_EHCI 0x0C0320UL
#define PCI_CLASS_XHCI 0x0C0330UL

#define HC_OHCI 1
#define HC_EHCI 2
#define HC_XHCI 3

#define HC_MASK_OHCI 0x01
#define HC_MASK_EHCI 0x02
#define HC_MASK_XHCI 0x04
#define HC_MASK_ALL  (HC_MASK_OHCI | HC_MASK_EHCI | HC_MASK_XHCI)

/* PCI Command register bits (config 0x04) */
#define PCI_CMD_MSE        0x0002   /* Memory Space Enable */
#define PCI_CMD_BME        0x0004   /* Bus Master Enable */
#define PCI_CMD_INTX_OFF   0x0400   /* Interrupt Disable (blocks INTx) */

typedef struct {
    u8  hctype;
    u8  bus, dev, fn;
    u16 vid, did;
    u8  rev;
    u8  ipin;           /* config 0x3D - 0: no INTx, needs polling (36.10) */
    u8  iline;          /* config 0x3C - legacy 8259 IRQ */
    u16 cmd_orig;       /* PCI Command as found (restored on exit) */
    u16 cmd_effective;  /* after active enable; unchanged in probe-only */
    int bme_ok;         /* -1 unknown (probe-only), 0 stuck, 1 settable */
    u32 bar_lo, bar_hi; /* raw BAR0 (and BAR1 if 64-bit) */
    u32 bar_phys;       /* decoded base address */
    int bar_is64, bar_pref;
    int has_pm, has_msi, has_msix, has_pcie;
    int msi_enabled;
    int msix_enabled;       /* MSI-X Message Control bit 15, as found */
    /* PCI Power Management capability (valid if has_pm). Read-only: the
     * qualifier never writes PMCSR - transitioning a D3 device back to D0 is
     * the driver's job, and --probe-only must take no ownership. */
    u16 pmc;            /* cap+2: PME_Support [15:11], D2 [10], D1 [9],
                         * version [2:0] */
    u16 pmcsr;          /* cap+4: PME_Status [15], PME_En [8],
                         * No_Soft_Reset [3], state [1:0] */
    u8  pm_state;       /* PMCSR[1:0]: 0=D0 .. 3=D3hot */
    /* Subsystem IDs (0x2C/0x2E): identify the board or laptop the silicon is
     * fitted to, which the VID/DID alone does not. Quirks can key on them. */
    u16 subsys_vid, subsys_did;
    /* PCI Status (0x06). The error bits are sticky and RW1C, so a set bit may
     * predate the tool by an entire boot - status_orig is therefore evidence
     * about the machine, not the run. status_final is re-read after the active
     * tests; a bit that turns on in between was caused by the tool's own
     * traffic and is the only bus-error evidence this tool can attribute. */
    u16 status_orig, status_final;
    int status_rechecked;
    /* Intel 7/8-series port-routing config regs (valid if quirk QF_XUSB2PR) */
    u32 xusb2pr, xusb2prm, usb3_pssen, usb3prm;
    /*
     * **Why the mapper gave up, in its own words, or 0 if it did not.**
     * `report_mmio_dead` reconstructs the cause from PCI state - BAR above
     * 4 GB, unassigned BAR, D-state, MSE clear - and ends at "cause:
     * undetermined" for anything else. Three of the mapper's refusals are
     * "anything else": an I/O-space BAR, a CAPLENGTH of 0 or an HCIVERSION
     * below 0.90, and a register block whose RTSOFF/DBOFF/PORTSC reach past
     * the fixed 64 KB window this tool maps. All three are readings about the
     * *tool* or about a controller misdescribing itself, and all three were
     * being reported as "DISQUALIFIED: BAR0 MMIO not accessible ... cause:
     * undetermined" - a tool limit read as dead silicon (the 2026-09-16
     * audit's C5). Points at a string literal; never freed.
     */
    const char *mmio_reason;
    /*
     * **And whether that reason is about the TOOL rather than the
     * controller.** Two of the three recorded refusals are about a controller
     * misdescribing itself - an I/O-space BAR, a CAPLENGTH of 0 - and stay
     * disqualifying. The third, a register block reaching past the fixed
     * 64 KB window this tool maps, is a limit of this tool on a controller
     * that may be perfectly sound, so it must reach the verdict as CANNOT SAY
     * and not as a disqualification. Without this the reason string said "a
     * tool limit, not a controller fault" while the verdict below it still
     * disqualified the part.
     */
    int mmio_tool_limit;
} PCIINFO;

u32  pci_read32(u8 bus, u8 dev, u8 fn, u8 off);
u16  pci_read16(u8 bus, u8 dev, u8 fn, u8 off);
u8   pci_read8(u8 bus, u8 dev, u8 fn, u8 off);
void pci_write32(u8 bus, u8 dev, u8 fn, u8 off, u32 val);
void pci_write16(u8 bus, u8 dev, u8 fn, u8 off, u16 val);
extern u32 pci_config_write_count;
int  pci_scan_usb(PCIINFO *out, int max, int family_mask);
void pci_read_static(PCIINFO *p);        /* A2-A4, A6: fills the rest */
void pci_recheck_status(PCIINFO *p);     /* re-read 0x06 after active tests */
int  pci_disable_bus_master(PCIINFO *p); /* restore original bits, BME clear */
const char *hc_name(int hctype);

/*
 * Task 36.11: the Intel PCH interrupt routing, read only (route.c and
 * pci.c's route_read). Filled for an xHCI before anything active runs; the
 * registers exist only on the reviewed RCBA chipsets (the driver's table,
 * src/xhci_pirq.c), and elsewhere nothing past the xHCI's own header is
 * read. `state` says how far the read went.
 */
#define ROUTE_NOT_READ      0   /* not looked at                          */
#define ROUTE_NA_XHCI       1   /* not a reviewed xHCI: nothing read      */
#define ROUTE_NA_SLOT       2   /* a reviewed id, not at 00:14.0          */
#define ROUTE_NA_LPC        3   /* 00:1F.0 not this xHCI's LPC: id only   */
#define ROUTE_RCBA_INVALID  4   /* LPC read; RCBA disabled or invalid     */
#define ROUTE_MAP_FAILED    5   /* the RCBA page not mapped               */
#define ROUTE_READ          6   /* D20IP and D20IR read                   */

typedef struct {
    int state;
    u32 xhci_id;        /* the xHCI's 00h: vendor 15:0, device 31:16      */
    u8  xhci_pin;       /* its 3Dh                                        */
    u32 lpc_id;         /* 00:1F.0's 00h                                  */
    u32 rcba;           /* its F0h                                        */
    u32 pirq_ad;        /* its 60h-63h, PIRQA-D_ROUT                      */
    u32 pirq_eh;        /* its 68h-6Bh, PIRQE-H_ROUT                      */
    u32 d20ip;          /* RCBA+3128h                                     */
    u16 d20ir;          /* RCBA+3160h                                     */
} ROUTEINFO;

/* Reads only: configuration space through 0xCF8/0xCFC and one DPMI mapping
 * of the RCBA page; no write of any kind. */
void route_read(const PCIINFO *p, ROUTEINFO *r);

/*
 * Task 39-0.6: the BIOS PCI IRQ routing table ("$PIR"; Microsoft, "PCI IRQ
 * Routing Table Specification", version 1.0), read once per run (route.c
 * parses, pci.c's pir_read reads). Machine-wide, not per controller: it is
 * the table Windows 98's PCI IRQ steering routes a function from when POST
 * gave it no Interrupt Line, and the question it answers on the bench is
 * whether a device number has an entry at all (00:1C, the PCH root ports,
 * the Thunderbolt root port's on the ThinkPad P14s Gen 1).
 */
#define PIR_NOT_READ      0   /* not looked at                             */
#define PIR_NONE          1   /* no "$PIR" on a 16-byte boundary           */
#define PIR_OK            2   /* found, and every check passed             */
#define PIR_BAD_VERSION   3   /* a "$PIR" whose version is not 1.0 (0100h) */
#define PIR_BAD_SIZE      4   /* table size not above 32 or not a multiple
                                 of 16                                     */
#define PIR_TRUNCATED     5   /* the table runs past the end of the area   */
#define PIR_BAD_CHECKSUM  6   /* bytes do not sum to 0 (dumped anyway)     */

#define PIR_HDR_LEN       32
#define PIR_SLOT_LEN      16
#define PIR_DUMP_MAX      128 /* entries dumped; all are searched          */
#define PIR_COPY_MAX      0x10000UL /* the size is a word: any table fits  */
#define PIR_RP_DEV        0x1C /* the device number named in the report   */

typedef struct {
    int state;
    u32 phys;           /* where the table (or the failing candidate) is   */
    u16 version;        /* its 04h: 0100h for 1.0                          */
    u16 size;           /* its 06h, bytes                                  */
    u32 nslots;         /* (size - 32) / 16 when the size is usable        */
    int candidates;     /* "$PIR" signatures seen on 16-byte boundaries    */
    u8  table[PIR_COPY_MAX];
    /* 00:1C.0-7 as found in configuration space (pci.c), reads only:
     * rp_id 0FFFFFFFFh for an absent function. */
    int rp_read;
    u32 rp_id[8];
    u8  rp_hdr[8];      /* 0Eh, header type                                */
    u8  rp_pin[8];      /* 3Dh                                             */
    u8  rp_line[8];     /* 3Ch                                             */
} PIRINFO;

/* pci.c: the F0000h-FFFFFh image read directly (linear == physical below
 * 1 MB) and 00:1C.0-7's ids, header types and interrupt registers through
 * 0xCF8/0xCFC; no write of any kind. */
void pir_read(PIRINFO *pi);

/* ------------------------------------------------------------------ */
/* Quirk table (quirks.c)                                             */
/* ------------------------------------------------------------------ */

/*
 * One bit per quirk in a 32-bit word (u32: Open Watcom's flat model makes
 * it an unsigned long, 32 bits, as the host compilers do). quirks.c's header says
 * which bits are quirks the driver takes, which Linux bits the driver covers
 * or does not take, and which are this tool's own. Bits 0-10 are the
 * original sixteen-bit word's and keep their values; bits 11 on are
 * roadmap-hcd task 38.10's (design record 20 section 6.5). A taken quirk's
 * comment names the driver's registry value and Linux's flag.
 */
#define QF_XUSB2PR         0x00000001UL /* Intel 7/8/9-series and C610 EHCI<->xHCI port routing */
#define QF_FW_UPLOAD       0x00000002UL /* Renesas uPD720201/202: fw upload on ROM-less cards */
#define QF_FW_SPI          0x00000004UL /* NEC uPD720200: fw from on-card SPI flash */
#define QF_SPURIOUS        0x00000008UL /* XHCI_SPURIOUS_SUCCESS: covered on every controller */
#define QF_BEI             0x00000010UL /* XHCI_AVOID_BEI: covered (BEI never set) */
#define QF_COMPLIANCE      0x00000020UL /* SS compliance-mode lockup (DMI-keyed in Linux, no row sets it) */
#define QF_PME_STUCK       0x00000040UL /* XHCI_PME_STUCK_QUIRK (bites Win2000 power mgmt) */
#define QF_BULK64K         0x00000080UL /* keep bulk TRB chains under 64 KB (no row sets it) */
#define QF_AVOID           0x00000100UL /* known-unreliable silicon */
#define QF_CMD_RETRY       0x00000200UL /* retry TRB Error command completions once (no row sets it) */
#define QF_BROKEN_MSI      0x00000400UL /* XhciMsiAvoid (XHCI_BROKEN_MSI); verdict.c reads it */
#define QF_STREAMS_OFF     0x00000800UL /* XhciStreamsOff (XHCI_BROKEN_STREAMS; 17CD:0200's XHCI_CDNS_SCTX_QUIRK) */
#define QF_RESUME_REINIT   0x00001000UL /* XhciResumeReinit (XHCI_RESET_ON_RESUME) */
#define QF_SHUTDOWN_EHCI   0x00002000UL /* XhciIntelShutdownToEhci (XHCI_SPURIOUS_REBOOT) */
#define QF_SHUTDOWN_RESET  0x00004000UL /* XhciShutdownReset (XHCI_SPURIOUS_WAKEUP), driver default off */
#define QF_SUSPEND_DELAY   0x00008000UL /* XhciSuspendHaltDelay (XHCI_SUSPEND_DELAY) */
#define QF_SLOW_SUSPEND    0x00010000UL /* XhciSuspendHaltTimeout (XHCI_SLOW_SUSPEND), every revision */
#define QF_ASM_FLOWCTL     0x00020000UL /* XhciAsmediaFlowControl (XHCI_ASMEDIA_MODIFY_FLOWCONTROL) */
#define QF_INTERVAL7       0x00040000UL /* XhciTiIntervalCap (XHCI_LIMIT_ENDPOINT_INTERVAL_7) */
#define QF_INTERVAL9       0x00080000UL /* XhciIntervalCap (XHCI_LIMIT_ENDPOINT_INTERVAL_9), 1002:7316 */
#define QF_INTERVAL_VENDOR 0x00100000UL /* XhciIntervalCap's reach: every AMD 1022 controller */
#define QF_STUCK_LINK      0x00200000UL /* XhciStuckLinkReset (XHCI_MISSING_CAS) */
#define QF_RING_PAD        0x00400000UL /* XhciStreamRingPad (XHCI_TRB_OVERFETCH) */
#define QF_CTRL_NOOP       0x00800000UL /* XhciControlLinkNoOp (XHCI_ETRON_HOST, part) */
#define QF_ISOCH_SPURIOUS  0x01000000UL /* XhciIsochSpuriousSuccess (XHCI_ETRON_HOST, part) */
#define QF_SPARSE_CLEAR    0x02000000UL /* XhciAmdSuspendBitClear (XHCI_DISABLE_SPARSE) */
#define QF_ZHAOXIN_HOST    0x04000000UL /* XHCI_ZHAOXIN_HOST: covered (speeds from PSIs) */
#define QF_NT_SW_BW        0x08000000UL /* XHCI_SW_BW_CHECKING: not taken (record 20 Q18) */
#define QF_NT_AMD_PLL      0x10000000UL /* XHCI_AMD_PLL_FIX: not taken (record 20 Q19) */

/* A vendor-wide row's device: it matches every device of its vendor.
 * 0xFFFF is never a present function's device id (a configuration read of
 * an absent function returns all ones), so no exact row can need it. */
#define QUIRK_ANY_DID 0xFFFFU

/*
 * The driver's HCIVERSION floor: xhci98 refuses a controller below it
 * (src/xhci_caps.c, XhciCheckCapDword0 against XHCI_HCIVERSION_1_0, which
 * returns XHCI_HC_BAD_VERSION). 1.0 today; roadmap-hcd task 38.11 moves it
 * to 0.96 (0x0096), and this follows it then. The report says when a listed
 * part is below it, so no value it names is read as acting on that part.
 */
#define QUIRK_HCI_FLOOR 0x0100U

typedef struct {
    u16 vid, did;          /* did QUIRK_ANY_DID: a vendor-wide row */
    u32 flags;
    const char *name;
} QUIRK;

/* The exact row for vid:did, or NULL; never a vendor-wide row. */
const QUIRK *quirk_find(u16 vid, u16 did);
/* The vendor-wide row for vid (at most one per vendor), or NULL. */
const QUIRK *quirk_find_vendor(u16 vid);
/* Row i of the table (0-based), or NULL past the end; for the host test. */
const QUIRK *quirk_row(int i);
/* The "quirks:" lines for one controller: its exact row, then its vendor's
 * row, then the HCIVERSION floor note when a listed part is below it
 * (hciver_ok 0: HCIVERSION was not read, so nothing is said of it). Pure;
 * pf is qprintf in the tool (the QPRINTF_FN type below). */
void quirk_report(u16 vid, u16 did, int hciver_ok, u16 hciversion,
                  void (*pf)(const char *fmt, ...));


/* ------------------------------------------------------------------ */
/* xHCI MMIO register map (xhcicap.c / bringup.c)                     */
/* ------------------------------------------------------------------ */

/* Capability registers, BAR0 + offset */
#define XCAP_CAPLENGTH   0x00
#define XCAP_HCIVERSION  0x02
#define XCAP_HCSPARAMS1  0x04
#define XCAP_HCSPARAMS2  0x08
#define XCAP_HCSPARAMS3  0x0C
#define XCAP_HCCPARAMS1  0x10
#define XCAP_DBOFF       0x14
#define XCAP_RTSOFF      0x18
/* HCCPARAMS2 (spec 5.3.9, Table 5-16) is not reachable the way every register
 * above it is: it sits at 1Ch, so a controller whose CAPLENGTH stops there has
 * put its operational registers on this address and reading it would report
 * USBCMD as a capability word. Gated on CAPLENGTH >= 20h, which is the same
 * gate the driver applies (src/xhci.h, XHCI_CAP_HCCPARAMS2_BYTES). */
#define XCAP_HCCPARAMS2      0x1C
#define XCAP_HCCPARAMS2_END  0x20

/* Operational registers, BAR0 + CAPLENGTH + offset */
#define XOP_USBCMD    0x00
#define XOP_USBSTS    0x04
#define XOP_PAGESIZE  0x08
#define XOP_DNCTRL    0x14
#define XOP_CRCR      0x18
#define XOP_DCBAAP    0x30
#define XOP_CONFIG    0x38
#define XOP_PORTSC(n) (0x400 + 0x10 * ((n) - 1))   /* n is 1-based */

/* USBCMD bits */
#define CMD_RUN    0x00000001UL
#define CMD_HCRST  0x00000002UL
#define CMD_INTE   0x00000004UL

/* USBSTS bits */
#define STS_HCH   0x00000001UL
#define STS_HSE   0x00000004UL   /* RW1C */
#define STS_EINT  0x00000008UL   /* RW1C */
#define STS_PCD   0x00000010UL   /* RW1C */
#define STS_CNR   0x00000800UL
#define STS_HCE   0x00001000UL

/* CRCR low-dword bits */
#define CRCR_RCS  0x00000001UL

/* PORTSC bits */
#define PSC_CCS   (1UL << 0)
#define PSC_PED   (1UL << 1)    /* RW1C: writing 1 DISABLES the port */
#define PSC_PR    (1UL << 4)    /* RW1S: write 1 starts reset */
#define PSC_WPR   (1UL << 31)   /* RW1S: write 1 starts a WARM reset */
#define PSC_PP    (1UL << 9)
#define PSC_SPEED(v) (((v) >> 10) & 0xF)   /* 1=FS 2=LS 3=HS 4=SS */
#define PSC_LWS   (1UL << 16)
#define PSC_CSC   (1UL << 17)   /* RW1C */
#define PSC_PEC   (1UL << 18)   /* RW1C */
#define PSC_WRC   (1UL << 19)   /* RW1C */
#define PSC_OCC   (1UL << 20)   /* RW1C */
#define PSC_PRC   (1UL << 21)   /* RW1C */
#define PSC_PLC   (1UL << 22)   /* RW1C */
#define PSC_CEC   (1UL << 23)   /* RW1C */
#define PSC_CHANGE_BITS (PSC_CSC | PSC_PEC | PSC_WRC | PSC_OCC | \
                         PSC_PRC | PSC_PLC | PSC_CEC)
/* Bits 2 and 29:28 - RsvdZ on the USB2 protocol ports this tool exercises, so
 * a read-modify-write must not carry them back. Same value as the driver's
 * XHCI_PORTSC_RSVDZ_MASK; see xhci-data-structures.md's safe-write rule. */
#define PSC_RSVDZ 0x30000004UL

/* Runtime registers, BAR0 + RTSOFF + offset (interrupter 0 only) */
#define XRT_IMAN    0x20
#define XRT_IMOD    0x24
#define XRT_ERSTSZ  0x28
#define XRT_ERSTBA  0x30
#define XRT_ERDP    0x38

#define IMAN_IP  0x00000001UL   /* RW1C */
#define IMAN_IE  0x00000002UL
#define ERDP_EHB 0x00000008UL   /* RW1C */

/*
 * **RsvdP masks, so a composed write preserves what it does not own** (repo
 * audit D2).
 *
 * `docs/usb-xhci-info/xhci-data-structures.md` section 3 states the rule the
 * driver obeys everywhere: an RsvdP field is read-modify-write, "software
 * preserves the value read". This tool was writing composed literals into six
 * register families and zeroing every reserved bit with them - and a
 * qualification tool that zeroes RsvdP can mis-verdict exactly the odd silicon
 * it exists to vet, on a machine where the tool's answer is the only evidence
 * anybody will have.
 *
 * The values are the driver's own (`src/xhci.h`), which is where they were
 * transcribed against the specification. USBSTS is not here because its writes
 * are RW1C and already correct.
 */
#define USBCMD_RSVDP  0xFFFE1070UL
#define CONFIG_RSVDP  0xFFFFFC00UL
#define CRCR_RSVDP    0x00000030UL
#define ERSTSZ_RSVDP  0xFFFF0000UL
#define ERSTBA_RSVDP  0x0000003FUL
#define IMAN_RSVDP    0xFFFFFFFCUL

/*
 * USBLEGCTLSTS (Table 7-5 p.479): RsvdP 3:1, 12:5 and 19:17; 28:21 is RsvdZ;
 * 16 and 20 are RO shadows; 31:29 are RW1C. The enables are 0, 4, 13, 14, 15.
 * The handoff write composes this register too, and its cleanup write must
 * not carry the RW1C bits back as ones.
 */
#define LEGCTL_RSVDP    0x000E1FEEUL
#define LEGCTL_ENABLES  0x0000E011UL

/* Extended capability IDs */
#define XECP_ID_LEGSUP 1
#define XECP_ID_PROTO  2

/* USBLEGSUP (extended cap ID 1) DW0 bits */
#define LEGSUP_BIOS_OWNED (1UL << 16)
#define LEGSUP_OS_OWNED   (1UL << 24)

/* ------------------------------------------------------------------ */
/* TRBs                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    u32 p0;    /* DW0 - parameter lo  */
    u32 p1;    /* DW1 - parameter hi (always 0: 32-bit machine) */
    u32 st;    /* DW2 - status        */
    u32 ctl;   /* DW3 - cycle / type / flags */
} TRB;

#define TRB_C            0x00000001UL
#define TRB_LINK_TC      0x00000002UL
#define TRB_TYPE(t)      (((u32)(t) & 0x3F) << 10)
#define TRB_GET_TYPE(c)  (((c) >> 10) & 0x3F)
#define TRB_GET_CODE(s)  (((s) >> 24) & 0xFF)
#define TRB_GET_SLOT(c)  (((c) >> 24) & 0xFF)
#define TRB_GET_PORT(p0) (((p0) >> 24) & 0xFF)
#define TRB_GET_RESID(s) ((s) & 0xFFFFFFUL)

#define TRB_T_SETUP     2
#define TRB_T_DATA      3
#define TRB_T_STATUS    4
#define TRB_T_LINK      6
#define TRB_T_ENA_SLOT  9
#define TRB_T_DIS_SLOT  10
#define TRB_T_ADDR_DEV  11
#define TRB_T_EVAL_CTX  13
#define TRB_T_NOOP_CMD  23
#define TRB_T_XFER_EVT  32
#define TRB_T_CMD_DONE  33
#define TRB_T_PORT_EVT  34
#define TRB_T_HC_EVT    37

/* transfer-TRB DW3 bits (Setup/Data/Status, spec 6.4.1.2) */
#define TRB_ISP          0x00000004UL
#define TRB_IOC          0x00000020UL
#define TRB_IDT          0x00000040UL
#define TRB_DIR_IN       0x00010000UL   /* Data/Status Stage DIR */
#define TRB_TRT_IN       0x00030000UL   /* Setup Stage: IN data stage */
#define TRB_TRT_NONE     0x00000000UL   /* Setup Stage: no data stage */

/* command-TRB DW3 fields */
#define TRB_SLOT(s)      ((u32)(s) << 24)
#define TRB_SLOTTYPE(t)  ((u32)(t) << 16)

#define CC_SUCCESS   1
#define CC_STALL     6
#define CC_SHORT_PKT 13

/* ------------------------------------------------------------------ */
/* Port topology classification                                       */
/* ------------------------------------------------------------------ */

#define PC_NONE        0
#define PC_USB2_ONLY   1   /* managed by the driver */
#define PC_USB2_PAIRED 2   /* managed: USB2 companion of a USB3 connector */
#define PC_USB3_PAIRED 3   /* unmanaged (SuperSpeed out of scope) */
#define PC_USB3_ORPHAN 4   /* unmanaged, no USB2 companion */

#define MAX_XHCI   8
#define MAX_CONTROLLERS 24
#define MAX_PORTS  64
#define MAX_PROTO  8
#define MAX_PSI    15   /* PSIC is 4 bits; retain every advertised entry */

typedef struct {
    u8  major, minor;     /* BCD: 0x02 = USB2, 0x03 = USB3 */
    u8  portoff, portcnt; /* 1-based first port, count */
    u8  slottype;
    u8  psic;             /* advertised count */
    u8  npsi;             /* entries actually read into psi[] */
    u32 off;              /* BAR0-relative byte offset of its header */
    u32 psi[MAX_PSI];     /* raw Protocol Speed ID dwords (spec 7.2.2.1.2) */
} PROTOCAP;

/*
 * Raw extended-capability chain, kept so the report can print the register
 * words behind its decoded fact sheet and a log can become a host replay
 * vector (README, "Raw extended-capability dump"). Captured during the one
 * read-only walk in xhci_read_caps(), because the report is printed after
 * qual_cleanup() and must not touch MMIO again. Bounded three ways: the
 * capabilities recorded, the dwords per capability and the dwords in all.
 */
#define XCAP_DUMP_CAPS    32
#define XCAP_DUMP_PER_CAP 32
#define XCAP_DUMP_TOTAL   256
/*
 * Supported Protocol and USB Legacy Support capabilities are still recorded
 * once those bounds are spent, from a reserve of their own: header plus PSI
 * dwords (4 + PSIC) for each of MAX_PROTO protocols, and USBLEGSUP's two.
 * The HP EliteBook 850 G5's 8086:15DB spent the general bound on vendor
 * capabilities before its USB 3.1 table (xhciqual/results/
 * hp850g5-2026-10-06), and the table is what the dump is for.
 */
#define XCAP_KEPT_CAPS    (MAX_PROTO + 1)
#define XCAP_KEPT_DWORDS  (MAX_PROTO * (4 + MAX_PSI) + 2)

/* CTRL.xcap_stop: why the record ends where it does */
#define XCAP_STOP_END      0   /* next pointer 0: the chain's own end */
#define XCAP_STOP_ONES     1   /* a header read all ones: recording stops */
#define XCAP_STOP_FULL     2   /* XCAP_DUMP_CAPS or XCAP_DUMP_TOTAL reached */
#define XCAP_STOP_WALK     3   /* the walk itself stopped (window or guard) */

typedef struct {
    u32 off;      /* BAR0-relative byte offset of the header dword */
    u8  id;       /* Capability ID, header bits 7:0 */
    u8  next;     /* Next Capability Pointer, header bits 15:8, in dwords */
    u16 first;    /* index of the header dword in CTRL.xdump[] */
    u16 ndw;      /* dwords recorded, header included */
    u16 want;     /* dwords the capability spans, as far as it is known */
} XCAPREC;

/* ------------------------------------------------------------------ */
/* C8 device identification results                                   */
/* ------------------------------------------------------------------ */

#define MAX_DEVS   8
#define DEVSTR_LEN 34   /* truncated UTF-16LE -> ASCII string descriptor */

typedef struct {
    u8  port, slot, addr, speed;    /* speed = PORTSC PSIV */
    u16 vid, pid, bcddev, bcdusb;
    u8  cls, sub, protocol;         /* device descriptor class triple */
    u8  icls, isub, iproto;         /* first interface class triple */
    int nifc;
    u8  mps0, nconf;
    char manuf[DEVSTR_LEN];
    char product[DEVSTR_LEN];
    char note[64];                  /* failure note when ok == 0 */
    int ok;
} DEVINFO;

/* ------------------------------------------------------------------ */
/* Verdicts                                                           */
/* ------------------------------------------------------------------ */

#define V_SKIP 0
#define V_PASS 1
#define V_WARN 2
#define V_FAIL 3
#define V_POLL 4   /* C4: no working interrupt, completed polled (36.10) */

const char *verdict_name(int v);

#define USB_SPEED_UNKNOWN 0
#define USB_SPEED_LOW     1
#define USB_SPEED_FULL    2
#define USB_SPEED_HIGH    3
#define USB_SPEED_SUPER   4

/* ------------------------------------------------------------------ */
/* Per-controller context                                             */
/* ------------------------------------------------------------------ */

#define CMD_TRBS 64
#define EVT_TRBS 256
#define MAX_SCRATCHPAD_PAGES 128   /* conventional-memory budget cap */

typedef struct {
    PCIINFO      pci;
    const QUIRK *quirk;

    /* set when this is a --poll-only active run: exercise C1/C2/C3/C6 by
     * foreground polling but never install a protected-mode ISR (C4 SKIP) */
    int poll_only;

    /* MMIO mapping */
    volatile u8 *base;   /* BAR0 */
    volatile u8 *op;     /* BAR0 + CAPLENGTH */
    volatile u8 *rt;     /* BAR0 + RTSOFF */
    volatile u8 *db;     /* BAR0 + DBOFF */

    /* Tier B facts */
    int mmio_ok;
    u8  caplength;
    u16 hciversion;
    u32 hcs1, hcs2, hcs3, hcc1;
    u32 hcc2;                /* HCCPARAMS2, valid only when hcc2_ok */
    int hcc2_ok;             /* 0 = CAPLENGTH does not reach it, or all ones */
    u32 dboff, rtsoff, pagesize;
    int maxslots, maxintrs, maxports;
    int spbufs;              /* Max Scratchpad Buffers */
    int csz;                 /* context size: 32 or 64 bytes */
    int ac64, ppc;
    /* HCCPARAMS2 bits 0-2, the three transcribed in
     * docs/usb-xhci-info/xhci-data-structures.md. FSC is the one that decides
     * whether this driver may take a Save State at all - see report.c. */
    int u3c, cmc, fsc;
    u32 legsup_off;          /* BAR0-relative byte offset, 0 = not present */
    u32 legctl_orig;
    int saw_debug_cap;
    PROTOCAP proto[MAX_PROTO];
    int nproto;
    u32 xecp_off;            /* HCCPARAMS1 xECP in bytes, 0 = no chain */
    int nxcap;               /* entries in xcap[] */
    int xcap_stop;           /* XCAP_STOP_* */
    int xcap_full;           /* the general bound was reached */
    int nxdump;              /* dwords used in xdump[] */
    XCAPREC xcap[XCAP_DUMP_CAPS + XCAP_KEPT_CAPS];
    u32 xdump[XCAP_DUMP_TOTAL + XCAP_KEPT_DWORDS];
    u8  portclass[MAX_PORTS + 1];   /* 1-based */
    int usb2_ports;                 /* count of managed (PC_USB2_*) ports */

    /* DMA structures (conventional memory, linear == physical) */
    u32 *dcbaa;    u32 dcbaa_phys;
    u32 *erst;     u32 erst_phys;
    u32 *sparray;  u32 sparray_phys;
    TRB *cmd;      u32 cmd_phys;   int cmd_enq;  int cmd_pcs;
    TRB *evt;      u32 evt_phys;   int evt_deq;  int evt_ccs;

    /* Tier C measurements */
    u32 reset_ms;
    u32 noop_ms;
    u32 port_events;         /* Port Status Change events seen */
    u32 irq_port_events;     /* ISR count attributable to port events */
    u32 irq_isr_count;       /* C4 ISR entries for this controller */
    u32 irq_foreign_count;   /* C4 not-ours entries for this controller */

    /* C8 device identification (informational) */
    DEVINFO devs[MAX_DEVS];
    int ndevs;

    /* verdicts */
    int v_bar;       /* A3: BAR below 4 GB */
    int v_pin;       /* A4: INTx pin present, line sane */
    int v_handoff;   /* C1 */
    int v_reset;     /* C2 */
    int v_dma;       /* C3 */
    int v_irq;       /* C4 */
    int v_port;      /* C6 */
    int v_dev;       /* C8 - informational, never disqualifying */
    int cleanup_failed;
    int irq_selftest;
    int irq_no_event;        /* C4: the No-Op never completed, even polled */
    int irq_force_poll;      /* C4: POLL with IP never raised: mode 2 only */
    ROUTEINFO route;         /* task 36.11, read before any active test */
    long route_gsi;          /* --gsi N, or -1                           */
    char irq_note[80];
    char port_note[80];
    char handoff_note[80];
    char dma_note[80];
    char dev_note[80];
    char cleanup_note[80];
} CTRL;

/* EHCI/OHCI use different schedules and register sets, but report the
 * same qualification gates as xHCI. */
typedef struct {
    PCIINFO pci;
    int poll_only;          /* --poll-only: skip the EHCI CPU-ISR C4 test */
    volatile u8 *base;
    volatile u8 *op;
    int mmio_ok;
    u16 hciversion;
    u32 cap_a, cap_b;
    int maxports;
    int ppc;
    u32 legsup_off;
    u32 legsup_orig;
    u32 legctl_orig;
    u32 reset_ms;
    u32 dma_ms;
    u32 irq_isr_count;       /* C4 ISR entries for this controller */
    u32 irq_foreign_count;   /* C4 not-ours entries for this controller */
    void *dma_a;
    u32 dma_a_phys;
    void *dma_b;
    u32 dma_b_phys;
    void *dma_c;
    u32 dma_c_phys;
    int v_handoff;
    int v_reset;
    int v_dma;
    int v_irq;
    int v_port;
    int cleanup_failed;
    char handoff_note[80];
    char dma_note[80];
    char irq_note[80];
    char port_note[80];
    char cleanup_note[80];
} LEGACY_CTRL;

/* MMIO accessors: zero-based flat model - linear address is the pointer */
#define RD32(p)     (*(volatile u32 *)(p))
#define WR32(p, v)  (*(volatile u32 *)(p) = (u32)(v))
#define RD16(p)     (*(volatile u16 *)(p))
#define WR16(p, v)  (*(volatile u16 *)(p) = (u16)(v))
#define RD8(p)      (*(volatile u8 *)(p))

/* ------------------------------------------------------------------ */
/* xhcicap.c - mapping + Tier B                                       */
/* ------------------------------------------------------------------ */

void *dpmi_map_phys(u32 phys, u32 size);
void  dpmi_unmap_phys(void *linear);
void *dos_alloc(u32 bytes, u32 *phys);
void  dos_free_last(void);             /* undo the most recent dos_alloc */
void  dos_free_all(void);              /* release every conventional block */
void  dos_protect_all(void);           /* retain blocks unsafe to reuse */
int   xhci_map(CTRL *c);               /* map BAR0, sanity reads */
int   xhci_read_caps(CTRL *c);         /* B1-B7 */
void  xhci_classify_ports(CTRL *c);

/* ------------------------------------------------------------------ */
/* bringup.c - Tier C actives                                         */
/* ------------------------------------------------------------------ */

void *dma_alloc(u32 size, u32 align, u32 boundary, u32 *phys);
u32  portsc_read(CTRL *c, int port);
void portsc_write(CTRL *c, int port, u32 setbits);
void portsc_clear_changes(CTRL *c, int port, u32 changebits);
int  port_is_usb2(CTRL *c, int port);
const char *speed_name(u32 psiv);
/* speed text for a port, honoring the Protocol Speed ID table when the
 * controller advertises one (PSIC > 0); defaults apply only at PSIC = 0 */
const char *speed_name_port(CTRL *c, int port, u32 psiv);
int  speed_class_port(CTRL *c, int port, u32 psiv);
int  evt_next(CTRL *c, TRB *out);      /* nonblocking event-ring pop */
/* bounded foreground drain: pop every pending event, advancing ERDP+EHB,
 * counting command-completion and port-status-change events. Used by the
 * C6 wait/reset loops so port events never strand on the ring. */
int  evt_drain(CTRL *c, u32 *cmd_seen, u32 *port_seen);

/* command ring: enqueue (cycle bit added) + ring DB[0]; wait for the
 * matching Command Completion Event. elapsed/slotid may be 0. */
u32  cmd_submit(CTRL *c, u32 p0, u32 p1, u32 ctl);
int  cmd_wait(CTRL *c, u32 trb_phys, unsigned timeout_ms,
              u32 *elapsed, u8 *code, u8 *slotid);

int  qual_handoff(CTRL *c);            /* C1 */
int  qual_reset(CTRL *c);              /* C2 */
int  qual_dma(CTRL *c);                /* C3: rings + No-Op round trip */
int  qual_irq(CTRL *c);                /* C4/C5 */
int  qual_ports(CTRL *c, int wait_secs);        /* C6 */
void qual_intel_ports(CTRL *c, int allow_write);/* C7 */
void qual_cleanup(CTRL *c);            /* restore controller state */

/* ------------------------------------------------------------------ */
/* devid.c - C8 device identification (informational)                 */
/* ------------------------------------------------------------------ */

int  qual_devid(CTRL *c);              /* C8: identify devices on ports
                                          that passed the C6 reset */
const char *usb_class_name(u8 cls);

/* timing */
void msleep(unsigned ms);
u32  ticks_now(void);                  /* BIOS 18.2 Hz tick counter */
u32  ticks_to_ms(u32 dt);

/* ------------------------------------------------------------------ */
/* irq.c                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    volatile u32 count;
    volatile u32 foreign_count;
    volatile u32 op;
    volatile u32 rt;
    volatile u32 hctype;
    volatile u32 irq;
} IRQ_ISR_STATE;

extern IRQ_ISR_STATE irq_isr_state;

#define irq_count   (irq_isr_state.count)
#define irq_foreign (irq_isr_state.foreign_count)

int  irq_install(PCIINFO *p, int hctype, volatile u8 *base,
                 volatile u8 *aux);    /* hook vector; keep 8259 masked */
int  irq_unmask(void);                 /* expose the armed one-shot source */
int  irq_uninstall(void);
const char *irq_error_step(void);
u16  irq_error_code(void);
int  irq_elcr_level(int irq);          /* 1 = level-triggered per ELCR */
/* Mask one 8259 line with NO vector hooked, for a window in which a
 * controller interrupt is enabled but nothing is there to service it
 * (the 2026-09-07 audit's I3). One at a time; restore always. */
int  irq_line_mask(unsigned line);
void irq_line_restore(void);

/* ------------------------------------------------------------------ */
/* legacy.c - EHCI/OHCI qualification                                */
/* ------------------------------------------------------------------ */

int  legacy_map_and_read(LEGACY_CTRL *c);
void legacy_run(LEGACY_CTRL *c, int wait_secs);
void legacy_cleanup(LEGACY_CTRL *c);
void report_legacy_controller(LEGACY_CTRL *c, int active_ran);
int  legacy_final_verdict(LEGACY_CTRL *c, int active_ran);

/* ------------------------------------------------------------------ */
/* report.c                                                           */
/* ------------------------------------------------------------------ */

int  report_open(const char *logname, int use_serial, int paginate);
void report_open_handle(int dos_handle, int use_serial, int paginate);
int  report_log_ok(void);
void report_close(void);
void qprintf(const char *fmt, ...);
/* The durable sinks only - a file and a serial line have no 25-row bound, so
 * the quick scan's screen truncation must not be theirs (repo audit D1). */
void qlogprintf(const char *fmt, ...);
int  report_logging(void);             /* is any durable sink open? */
void report_flush(void);               /* force log+console out to durable */
void report_controller(CTRL *c, int active_ran);
/* ------------------------------------------------------------------ */
/* verdict.c - the xHCI verdict, pure (test/test_verdict.c)           */
/* ------------------------------------------------------------------ */

/* What C4 saw (task 36.10). no_line: nothing could be armed - Interrupt
 * Pin 0, no usable IRQ, or PCI Interrupt Disable stuck set. tool_failed: a
 * DPMI step failed. ip_seen: IP and EINT came up with the line masked.
 * event_seen: the No-Op's completion was found in the event ring. */
typedef struct {
    int no_line;
    int tool_failed;
    int ip_seen;
    int isr_fired;
    int foreign;
    int event_seen;
} C4_SEEN;

int  c4_verdict(const C4_SEEN *s);             /* V_PASS, V_POLL or V_FAIL */
int  c4_lets_ports(int v_irq, int poll_only);  /* may C6 and C8 run */
const char *msi_route_text(const PCIINFO *p, int broken_msi);
                                               /* the message route, words */
void report_caveats(const CTRL *c);             /* the caveats, in full */

#define VERDICT_NOT        0
#define VERDICT_QUALIFIED  1
#define VERDICT_CAVEATS    2   /* QUALIFIED WITH CAVEATS: needs polling */

int  final_verdict(CTRL *c, int active_ran);   /* VERDICT_* */
/* 0 all qualified, 3 any with caveats and none not, 1 otherwise. */
int  run_exit_code(int any_not, int any_caveats);
/* ------------------------------------------------------------------ */
/* mmiodiag.c - pure PCIINFO -> text diagnosis, host-testable (no MMIO,  */
/* no port I/O, no DOS services; see xhciqual/test/test_mmiodiag.c)   */
/* ------------------------------------------------------------------ */

void report_pci_pm(const PCIINFO *p);          /* A4: PM cap contents */
void report_pci_status(const PCIINFO *p);      /* A2: sticky bus-error bits */
void report_mmio_dead(const PCIINFO *p);       /* why MMIO reads as absent */
int  report_mmio_unavailable(const PCIINFO *p, int active_requested);

/*
 * Task 35.1's first step: the Supported Protocol capabilities with each PSI
 * entry decoded, the USB 3 "no PSIV 4" warning (issue 11), and the raw
 * extended-capability chain. Pure: they print what xhci_read_caps() stored and
 * read no register. The printer is qprintf (screen and log) or qlogprintf (log
 * and serial only), so the quick scan can put the same lines in its log
 * without lengthening its one screen.
 */
typedef void (*QPRINTF_FN)(const char *fmt, ...);
void report_protocols(const CTRL *c, QPRINTF_FN pf);
int  psi_usb3_lacks_ss(const PROTOCAP *pr);    /* 1 = warn (issue 11) */
void report_xcap_dump(const CTRL *c, QPRINTF_FN pf);

/*
 * The extended-capability walk (B6, B7) and its raw recording, moved out of
 * xhcicap.c unchanged in behaviour so the host runner can drive the production
 * code through a checked fake reader. xecp_off is in bytes; window is the
 * mapped size in bytes (BAR_MAP_SIZE in the tool); rd returns the dword at a
 * byte offset (MMIO in the tool). Fills legsup_off, proto[], nproto,
 * saw_debug_cap and the xcap/xdump record.
 */
typedef u32 (*XCAP_RD_FN)(void *ctx, u32 off);
void xcap_walk(CTRL *c, u32 xecp_off, u32 window, XCAP_RD_FN rd, void *ctx);

/*
 * Task 11-V.8: what a **read-only** pass can honestly conclude about one
 * controller. Three outcomes, because a read-only pass cannot observe C2, C3 or
 * C4 and a two-way answer would have to lie about one of them.
 *
 * This is the ONE decision. `report_mmio_unavailable` and `final_verdict` both
 * consult it rather than repeating the conditions, which is what stops the
 * quick scan and the full run from ever disagreeing - a quick scan that says
 * qualified where a full run does not is worse than no quick scan at all.
 *
 * `active_requested` is what separates two readings of one observation: a clear
 * Memory Space Enable is a temporary configuration state on a read-only pass,
 * and a controller whose MSE could not be *set* on an active one.
 */
#define QUICK_LOOKS_OK      0   /* subject to the active tests               */
#define QUICK_DISQUALIFIED  1   /* a read-only pass genuinely sees this      */
#define QUICK_CANNOT_SAY    2   /* a state this pass may not change          */
#define QUICK_CAVEATS       3   /* needs polling: no line interrupt (36.10)  */

/* Which of two outcomes the quick scan's verdict takes: LOOKS_OK, then
 * CAVEATS, then DISQUALIFIED, then CANNOT_SAY. */
int quick_rank(int outcome);

int quick_classify(const PCIINFO *p, int mmio_ok, int usb2_ports,
                   int active_requested);

/* The MMIO half on its own, for the one caller that classifies a dead window
 * and must not answer anything else - `report_mmio_unavailable`. Folding the
 * Interrupt Pin check into that function would make it report a fault it was
 * not asked about; xhciqual/test/test_mmiodiag.c's D-state vectors are what
 * say so. */
int quick_classify_mmio(const PCIINFO *p, int active_requested);

/* The one-line reason behind that outcome, for the quick scan's single line
 * per controller. Never NULL; "" for QUICK_LOOKS_OK. */
const char *quick_reason(const PCIINFO *p, int mmio_ok, int usb2_ports,
                         int active_requested);

/* C7 routing verdict. UNDETERMINED is deliberately not merged with ROUTED:
 * "the evidence cannot show mis-routing" and "the ports are routed" lead to
 * opposite next actions on a machine that reports no connects. */
#define XUSB2PR_ROUTED        0    /* every switchable port is on xHCI */
#define XUSB2PR_NOT_ROUTED    1    /* some switchable port is still on EHCI */
#define XUSB2PR_UNDETERMINED  2    /* the words cannot answer the question */

int  report_xusb2pr(const PCIINFO *p);         /* C7: prints, then verdicts */

/* ------------------------------------------------------------------ */
/* route.c - the routing report's pure half (task 36.11), host-tested */
/* by xhciqual/test/test_route.c                                      */
/* ------------------------------------------------------------------ */

#define ROUTE_CMP_NONE          0   /* no GSI given                       */
#define ROUTE_CMP_MATCH         1
#define ROUTE_CMP_MISMATCH      2
#define ROUTE_CMP_NOT_COMPARED  3   /* no PIRQ, or a GSI in neither range */

/* ROUTE_READ to go on to the LPC, or ROUTE_NA_XHCI / ROUTE_NA_SLOT. */
int  route_scope(u32 xhci_id, u8 bus, u8 dev, u8 fn);
int  route_lpc_listed(u32 xhci_id, u32 lpc_id);
int  route_rcba_valid(u32 rcba);
/* The PIRQ (0-7) D20IR routes D20IP's pin to, or -1. */
int  route_pirq(const ROUTEINFO *r);
/* Against a GSI given separately: 16-23 the I/O APIC input PIRQA-H is,
 * below 16 an 8259 IRQ the PIRQ's PIRQx_ROUT must be enabled for and name.
 * gsi -1 is none. */
int  route_compare(const ROUTEINFO *r, long gsi);
void report_route(const ROUTEINFO *r, long gsi, QPRINTF_FN pf);
/* One phrase for the quick scan's screen. Never NULL. */
const char *route_summary(const ROUTEINFO *r, long gsi);

/* Task 39-0.6, pure. Scan `len` bytes at `area` (physical `area_phys`) on
 * 16-byte boundaries for "$PIR"; the first candidate that passes every
 * check wins, else the first candidate's failure is kept. Returns state. */
int  pir_scan(const u8 *area, u32 len, u32 area_phys, PIRINFO *pi);
/* The first entry (0-based) for bus/device, or -1; *count, when not NULL,
 * gets how many entries name it. Every entry is searched. Only for a
 * parsed table (PIR_OK or PIR_BAD_CHECKSUM). */
int  pir_find(const PIRINFO *pi, u8 bus, u8 dev, int *count);
/* The block: header, the entries (at most PIR_DUMP_MAX), and 00:1C named
 * explicitly, its functions printed whatever the table's state. */
void report_pir(const PIRINFO *pi, QPRINTF_FN pf);
/* One line for the quick scan's screen, at most 79 characters. */
void report_pir_short(const PIRINFO *pi, QPRINTF_FN pf);

/* ------------------------------------------------------------------ */
/* sysinfo.c (pure, host-tested) and sysprobe.c (DOS): the system     */
/* description in the report header (roadmap-hcd.md task 36.48)      */
/* ------------------------------------------------------------------ */

/*
 * **NO IDENTIFIERS.** The header names the machine - maker, model, board,
 * BIOS, CPU, memory, DOS - so a pasted report says what it came from. It
 * never names the unit: SMBIOS serial numbers, UUIDs, asset tags, SKU and
 * part numbers (Type 1 serial/UUID/SKU, Type 2 serial/asset, Type 3
 * serial/asset, Type 4 serial/asset/part) are not read into SYSINFO at
 * all, so no printer can leak them; and a printed string that repeats one
 * of them (the same string index, or the same text) is withheld. Keep it
 * that way: a field added here is a field every pasted report publishes.
 */
#define SI_STR 65               /* 64 characters and the terminator      */

#define SMB_NONE        0       /* no entry point found                  */
#define SMB_OK          1       /* entry point valid, table read         */
#define SMB_ABOVE_4G    2       /* table above 4 GB: not mappable here   */
#define SMB_MAP_FAILED  3       /* DPMI would not map the table          */
#define SMB_EMPTY       4       /* entry point names an empty table      */

#define SMB_EP_DMI      1       /* legacy "_DMI_" alone (DMI 2.0)        */
#define SMB_EP_32       2       /* "_SM_" 32-bit entry point (2.1+)      */
#define SMB_EP_64       3       /* "_SM3_" 64-bit entry point (3.0+)     */

typedef struct {
    int skipped;                /* --no-sysinfo: nothing was read        */
    /* SMBIOS entry point (smb_scan) */
    int smb_state;
    int smb_kind;
    u32 smb_ep_phys;            /* where the anchor was found            */
    u8  smb_major, smb_minor;
    u32 table_lo, table_hi;     /* structure table physical address      */
    u32 table_len;              /* as the entry point gives it           */
    u16 table_count;            /* 2.x structure count; 0 for 3.0        */
    int other_ep_bad;           /* an anchor whose checksum failed       */
    /* the structure table (smb_parse_table) */
    u32 used_len;               /* bytes walked (capped at SMB_TABLE_MAX) */
    int nstructs;
    int broken;                 /* the walk stopped on a malformed entry  */
    u32 broken_at;              /* its offset in the table               */
    int ids_unchecked;          /* walk incomplete: strings withheld     */
    char bios_vendor[SI_STR], bios_version[SI_STR], bios_date[SI_STR];
    char sys_mfr[SI_STR], sys_product[SI_STR], sys_version[SI_STR];
    char sys_family[SI_STR];
    char board_mfr[SI_STR], board_product[SI_STR];
    char cpu_socket[SI_STR], cpu_version[SI_STR];
    int seen_t0, seen_t1, seen_t2, seen_t4;
    u32 mem17_kb;  int mem17_n;  /* summed module sizes                   */
    u32 mem19_kb;  int mem19_n;  /* summed mapped ranges                  */
    /* no SMBIOS: the ROM's own date (F000:FFF5) and model byte (FFFE) */
    char rom_date[9];
    u8  rom_model;
    int rom_read;
    /* CPUID */
    int has_cpuid;
    char cpu_vendor[13];
    char cpu_brand[49];
    u32 cpu_sig;                /* leaf 1 EAX                            */
    u32 cpu_family, cpu_model, cpu_stepping;
    /* memory */
    u32 e820_kb;  int e820_n;   /* usable ranges and their count         */
    /* DOS */
    int dos_read;
    u8  dos_major, dos_minor, dos_oem;
    int dos_true;               /* AX=3306h answered (DOS 5+)            */
    u8  dos_true_major, dos_true_minor;
    int win_state;              /* 0 none, 1 Windows/386 2.x, 2 version  */
    u8  win_major, win_minor;
} SYSINFO;

#define SMB_TABLE_MAX 0x10000UL /* bytes of structure table walked       */

/* Pure: the F0000h-FFFFFh image (or any window) at `area`, physical base
 * `area_phys`. Fills the entry-point fields of `si`; returns smb_kind or 0.
 * A valid "_SM3_" with a table below 4 GB wins; else "_SM_"; else "_DMI_". */
int  smb_scan(const u8 *area, u32 len, u32 area_phys, SYSINFO *si);
/* Pure: walk `len` bytes of structure table. Every length and string is
 * bounds-checked; a malformed entry ends the walk (si->broken) and keeps
 * what was read before it. */
void smb_parse_table(const u8 *t, u32 len, u16 count, SYSINFO *si);
/* Pure: CPUID results to text. Registers in the order EAX, EBX, ECX, EDX. */
void cpu_fill_vendor(SYSINFO *si, const u32 leaf0[4]);
void cpu_fill_signature(SYSINFO *si, u32 eax1);
void cpu_fill_brand(SYSINFO *si, const u32 brand[12]);
/* Pure: the ROM date bytes (8) and model byte, for the no-SMBIOS line. */
void rom_fill(SYSINFO *si, const u8 *date8, u8 model);
/* The header block (one line per item), and the quick scan's two lines,
 * each at most 79 characters. */
void report_sysinfo(const SYSINFO *si, QPRINTF_FN pf);
/* Once every field is filled, before any report: both reports checked as
 * printed against the identifiers smb_parse_table held, then wiped. */
void sysinfo_seal(SYSINFO *si);
void report_sysinfo_short(const SYSINFO *si, QPRINTF_FN pf);

/* sysprobe.c: reads only - the BIOS area, one DPMI mapping of the table,
 * CPUID, INT 15h E820, INT 21h 30h/3306h, INT 2Fh 1600h. */
void sysinfo_probe(SYSINFO *si);
/* sysprobe.c, task 38.1: INT 21h AH=5Bh ("create new file") made in real
 * mode through DPMI 0300h, so the handle DOS returns is the one handed
 * back. 0 with *handle set, or the DOS error code (50h: the name exists). */
unsigned sys_dos_creatnew(const char *name, int *handle);
/* sysprobe.c, DOS-1: chain the real-mode INT 24h so an Abort answer to
 * DOS's Abort, Retry, Fail reaches DOS as Fail and the tool ends itself,
 * through DOS/32A's exit. 1 installed, 0 not (the run goes on without it).
 * sys_crit_remove puts the previous vector back. */
int  sys_crit_install(void);
void sys_crit_remove(void);
/* sysinfo.c, pure: the --no-sysinfo state (owner, 2026-10-10) - nothing
 * read, and the header says the option skipped it. */
void sysinfo_skip(SYSINFO *si);
/* sysasm.asm: EFLAGS.ID toggles (1) or not; CPUID with every register but
 * EAX saved and restored (lesson-cpuid-ebx: CPUID clobbers EBX, which
 * Watcom's register convention, like MSVC's, expects preserved). */
int  sys_has_cpuid(void);
void sys_cpuid(u32 leaf, u32 *out4);
/* report.c: the console alone (paged), for the quick scan's screen lines
 * whose full form goes to the log and serial line through qlogprintf. */
void qconprintf(const char *fmt, ...);

#endif /* QUAL_H */
