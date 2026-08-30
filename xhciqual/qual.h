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
    u8  ipin;           /* config 0x3D - 0 means MSI-only: disqualifying */
    u8  iline;          /* config 0x3C - legacy 8259 IRQ */
    u16 cmd_orig;       /* PCI Command as found (restored on exit) */
    u16 cmd_effective;  /* after active enable; unchanged in probe-only */
    int bme_ok;         /* -1 unknown (probe-only), 0 stuck, 1 settable */
    u32 bar_lo, bar_hi; /* raw BAR0 (and BAR1 if 64-bit) */
    u32 bar_phys;       /* decoded base address */
    int bar_is64, bar_pref;
    int has_pm, has_msi, has_msix, has_pcie;
    int msi_enabled;
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

/* ------------------------------------------------------------------ */
/* Quirk table (quirks.c)                                             */
/* ------------------------------------------------------------------ */

#define QF_XUSB2PR    0x0001  /* Intel 7/8-series EHCI<->xHCI port routing */
#define QF_FW_UPLOAD  0x0002  /* Renesas uPD720201/202: fw upload on ROM-less cards */
#define QF_FW_SPI     0x0004  /* NEC uPD720200: fw from on-card SPI flash */
#define QF_SPURIOUS   0x0008  /* spurious-success: trust residual length only */
#define QF_BEI        0x0010  /* mishandles BEI in isoch TRBs */
#define QF_COMPLIANCE 0x0020  /* SS compliance-mode lockup (USB3-only, FYI) */
#define QF_PME_STUCK  0x0040  /* PME wake latch bug (bites Win2000 power mgmt) */
#define QF_BULK64K    0x0080  /* keep bulk TRB chains under 64 KB */
#define QF_AVOID      0x0100  /* known-unreliable silicon */
#define QF_CMD_RETRY  0x0200  /* retry TRB Error command completions once */
#define QF_BROKEN_MSI 0x0400  /* broken MSI (INTx anyway - harmless for us) */

typedef struct {
    u16 vid, did;
    u16 flags;
    const char *name;
} QUIRK;

const QUIRK *quirk_find(u16 vid, u16 did);

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
    u32 psi[MAX_PSI];     /* raw Protocol Speed ID dwords (spec 7.2.2.1.2) */
} PROTOCAP;

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
void report_close(void);
void qprintf(const char *fmt, ...);
/* The durable sinks only - a file and a serial line have no 25-row bound, so
 * the quick scan's screen truncation must not be theirs (repo audit D1). */
void qlogprintf(const char *fmt, ...);
int  report_logging(void);             /* is any durable sink open? */
void report_flush(void);               /* force log+console out to durable */
void report_controller(CTRL *c, int active_ran);
int  final_verdict(CTRL *c, int active_ran);   /* 1 qualified, 0 not */
/* ------------------------------------------------------------------ */
/* mmiodiag.c - pure PCIINFO -> text diagnosis, host-testable (no MMIO,  */
/* no port I/O, no DOS services; see xhciqual/test/test_mmiodiag.c)   */
/* ------------------------------------------------------------------ */

void report_pci_pm(const PCIINFO *p);          /* A4: PM cap contents */
void report_pci_status(const PCIINFO *p);      /* A2: sticky bus-error bits */
void report_mmio_dead(const PCIINFO *p);       /* why MMIO reads as absent */
int  report_mmio_unavailable(const PCIINFO *p, int active_requested);

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

#endif /* QUAL_H */
