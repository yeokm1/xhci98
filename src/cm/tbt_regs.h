/*
 * tbt_regs.h - the host interface registers, ring descriptor fields and
 * control packet fields xhcicm.sys uses, and the Alpine Ridge firmware
 * connection manager's status, mode and mailbox registers (roadmap-usb4.md
 * task 39-0.2; design record 21 section 4.1).
 *
 * Lifted from usb4-emu's tools/usb4cm/core/u4regs.h, whose notice follows,
 * and transcribed again against the USB4 Specification 2.0 (USB Promoter
 * Group, November 2025; the PDF is fetched per machine into the git-ignored
 * docs/references/usb4_202511/, see docs/references/README.md). Each define
 * carries its citation: "Spec" is that specification, "ICM" is usb4-emu's
 * docs/ICM-PROTOCOL.md (branch main, read at a68bc74; the same text as the
 * eba0cf1 design record 21 cites), whose [obs] and [inf] tags are kept.
 * Since the owner's decision of 2026-10-15 the Linux and FreeBSD
 * Thunderbolt drivers are interface documentation under design record 20's
 * rule (facts taken and cited by file and symbol, no function body
 * copied): every offset below was cross-checked against Linux's
 * drivers/thunderbolt/nhi_regs.h (mirror pinned at c6859eed) and FreeBSD's
 * sys/dev/thunderbolt/nhi_reg.h (pinned at 9d50245a), which agree with
 * the specification; the Ice Lake family's vendor-specific configuration
 * registers at the end are Linux's alone ("Linux nhi_regs.h").
 *
 * C89, no bitfields: offsets, shifts and masks over ULONG words, as
 * xhci_hw.h does for the xHCI.
 *
 * ------------------------------------------------------------------------
 * Original notice of u4regs.h:
 *
 *   u4regs.h - register offsets and fields used by the usb4cm core.
 *
 *   Copyright (c) 2026 usb4-emu contributors
 *   SPDX-License-Identifier: MIT
 *
 *   Written from scratch for the usb4-emu project. Host interface layout
 *   from USB4 Specification 2.0 section 12.6; router, adapter and path
 *   configuration spaces from section 8 (field positions cross-checked
 *   against tbtools registers.json). C89, no bitfields: dwords plus
 *   shift/mask macros, as in xhci98.
 *
 * The usb4-emu project's MIT licence (its LICENSE file, "MIT License,
 * Copyright (c) 2026 Yeo Kheng Meng"):
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ------------------------------------------------------------------------
 *
 * The changes made here: the router, adapter and path configuration space
 * fields the ICM client never touches were left out - which takes with them
 * everything u4regs.h's notice says was cross-checked against tbtools'
 * registers.json: nothing below rests on tbtools, and every field is cited
 * to the specification or to ICM-PROTOCOL.md (the USB4 software
 * connection manager of Phase 40 adds what it needs); every name took this
 * driver's TBT_ prefix; the receive descriptor's Buffer Overflow bit, the
 * Total Paths limit, the Interrupt Mask register and the Ring Control
 * fields were added from the specification; the Alpine Ridge registers
 * were added from ICM-PROTOCOL.md. On 2026-10-15 (roadmap-usb4.md task
 * 40-0.1) the router, adapter, path, TMU, PCIe, USB3, USB4 port and
 * notification fields design record 22 uses were added at the end, each
 * cited to the specification and cross-checked against Linux tb_regs.h and
 * tb_msgs.h, with the Thunderbolt 3 vendor capabilities from Linux alone.
 */

#ifndef TBT_REGS_H
#define TBT_REGS_H

/* ====================================================================== */
/* Host interface BAR0: Spec 12.6.2, Table 12-10                          */
/* ====================================================================== */

/*
 * Every register is one aligned 32-bit word, and a connection manager
 * accesses BAR0 one DW at a time (Spec 12.6, its connection manager note).
 */

/* Host Interface Capabilities, Spec 12.6.3.1.1, Table 12-11. ICM 2.1 reads
 * 0x0000000c on Alpine Ridge [obs]: 12 paths, version 0. */
#define TBT_HI_CAPS                 0x39640UL
#define TBT_HI_CAPS_PATHS(v)        ((ULONG)(v) & 0x7FFUL)        /* 10:0 Total Paths */
#define TBT_HI_CAPS_PATHS_MAX       21UL    /* Table 12-11: "shall not exceed a value of 21" */
#define TBT_HI_CAPS_VERSION(v)      (((ULONG)(v) >> 16) & 0xFFUL) /* 23:16 Host Interface Version */
/* Table 12-11: 000b/00000b is "USB4 Ver. 1.0 or TBT3", 010b/00000b is USB4
 * 2.0 (40h). Design record 21 section 1.4.2 test 4 takes 0 on Alpine Ridge
 * and below 40h on the other ICM rows (tbt_hw.c). */
#define TBT_HI_VERSION_TBT3         0x00UL
#define TBT_HI_VERSION_2            0x40UL  /* Linux nhi_regs.h REG_CAPS_VERSION_2 */

/* Host Router Reset, Spec 12.6.3.1.2, Table 12-12: bit 0 HRR, R/W SC. Not
 * used by the ICM client (ICM 2.6: interface version 0, no handshake). */
#define TBT_HI_HR_RESET             0x39898UL
/* Host Interface Reset, Spec 12.6.3.1.3, Table 12-13: bit 0 RST, R/W SC,
 * complete within tHIReset (Table 12-38: 10 ms max). */
#define TBT_HI_RESET                0x39858UL
#define TBT_HI_RESET_RST            0x00000001UL
#define TBT_HI_RESET_WAIT_MS        10UL
/* Host Interface Control, Spec 12.6.3.1.4, Table 12-14; "DMA misc" in ICM
 * 2.1, 0x01000016 on Alpine Ridge [obs], read for the log only (record 21
 * section 4.1). Bit 17 is Disable ISR Auto-Clear. */
#define TBT_HI_CONTROL              0x39864UL

/* Transmit Descriptor Ring n, Spec 12.6.3.2, Table 12-10 (n = HopID). */
#define TBT_TX_RING(n)              (0x00000UL + 0x10UL * (ULONG)(n))
/* Receive Descriptor Ring n, Spec 12.6.3.3, Table 12-10. */
#define TBT_RX_RING(n)              (0x08000UL + 0x10UL * (ULONG)(n))
/* Within a ring's block: */
#define TBT_RING_BASE_LO            0x0UL   /* Tables 12-17, 12-22: 16-byte aligned */
#define TBT_RING_BASE_HI            0x4UL   /* Tables 12-18, 12-23: always 0 here */
#define TBT_RING_INDEX              0x8UL   /* Tables 12-19, 12-24 */
#define TBT_RING_SIZE_REG           0xCUL   /* Tables 12-20, 12-25 */
/*
 * Producer and Consumer Indexes: Consumer 15:0, Producer 31:16. On a TX
 * ring the host writes the Producer and reads the Consumer (Table 12-19);
 * on an RX ring the host writes the Consumer and reads the Producer (Table
 * 12-24). Either side leaves one descriptor vacant.
 */
#define TBT_RING_CONS(v)            ((ULONG)(v) & 0xFFFFUL)
#define TBT_RING_PROD(v)            (((ULONG)(v) >> 16) & 0xFFFFUL)
#define TBT_RING_PROD_SHIFT         16
/* Ring Size 15:0 (both); RX Data Buffer Size 27:16, 0 meaning 4096 (Table
 * 12-25). ICM 2.1 [obs]: TX 0x0000000a, RX 0x0100000a on Alpine Ridge. */
#define TBT_RING_SIZE_MASK          0xFFFFUL
#define TBT_RX_BUFSIZE_SHIFT        16

/* Ring Control, TX Spec 12.6.3.2.5 Table 12-21, RX 12.6.3.3.5 Table 12-26. */
#define TBT_TX_CTRL(n)              (0x19800UL + 0x20UL * (ULONG)(n))
#define TBT_RX_CTRL(n)              (0x29800UL + 0x20UL * (ULONG)(n))
#define TBT_RING_CTRL_VALID         0x80000000UL    /* 31 Ring Valid */
#define TBT_RING_CTRL_RAW           0x40000000UL    /* 30 Raw Mode */
#define TBT_RING_CTRL_NS            0x20000000UL    /* 29 No-snoop; never set here */
#define TBT_RING_CTRL_E2E           0x10000000UL    /* 28 E2E Flow Control Enable; never set here */
/* PDF Bit Masks, Spec 12.6.3.3.6, Table 12-27: no effect in Raw Mode. The
 * specification asks bits 0, 15, 16 and 31 to be written 0; the Alpine
 * Ridge capture shows the Linux client writing all ones to ring 0 [obs] ICM
 * 2.1, and record 21 section 5.1 step 6 follows the capture for the ICM
 * client, so the caller chooses (tbt_ring.h). */
#define TBT_RX_PDF(n)               (0x29804UL + 0x20UL * (ULONG)(n))
#define TBT_RX_PDF_ALL_ONES         0xFFFFFFFFUL
#define TBT_RX_PDF_SPEC             0x7FFE7FFEUL

/*
 * Interrupt Mask, Spec 12.6.3.4.4, Table 12-31: R/W, 1 = the cause raises
 * an interrupt, default 0; the specification packs 3N causes (TX, RX, RX
 * vacancy). ICM 2.1 calls the first DW "ring interrupt enable 0" and reads
 * it 0x00001001 with the Linux client bound, 0 unbound [obs]. This driver
 * polls (record 21 section 4.3, decision D3) and masks every cause the
 * specification packs - TX, RX and RX vacancy, 3N bits (Spec 12.5.1, Figure
 * 12-8), since a start with no reset cannot rely on the defaults (Linux
 * nhi.c nhi_disable_interrupts covers 2N, RING_INTERRUPT_REG_COUNT) - by
 * each host's own contract (Linux nhi.c nhi_mask_interrupt): on Intel
 * hosts, whose interrupt status clears itself (pci.c nhi_pci_check_quirks,
 * QUIRK_AUTO_CLEAR_INT), by writing the mask register itself; elsewhere
 * through Interrupt Mask Clear (Spec 12.6.3.4.5, Table 12-32).
 */
#define TBT_HI_IMR                  0x38200UL   /* REG_RING_INTERRUPT_BASE */
#define TBT_HI_IMC                  0x38208UL   /* REG_RING_INTERRUPT_MASK_CLEAR_BASE */
#define TBT_HI_IMR_DWORDS(paths)    ((3UL * (ULONG)(paths) + 31UL) / 32UL)
/* Interrupt Status, Spec 12.6.3.4.1: read-to-clear unless auto-clear is
 * disabled, so never read by this driver (ICM 2.1 notes the capture tool
 * skipped it for the same reason). */
#define TBT_HI_ISR                  0x37800UL

/* ====================================================================== */
/* Ring descriptors, 16 bytes, little-endian DWs (Spec 12.1.1)            */
/* ====================================================================== */

#define TBT_DESC_BYTES              16UL
#define TBT_DESC_ADDR_LO            0UL     /* DW0: Tables 12-6, 12-7, 12-8 */
#define TBT_DESC_ADDR_HI            4UL     /* DW1 */
#define TBT_DESC_DW2                8UL     /* DW2: the control and status word */
#define TBT_DESC_DW3                12UL    /* DW3: reserved */

/* DW2 of a transmit descriptor, Spec 12.3.1, Table 12-6. */
#define TBT_DESC_LEN_MASK           0x00000FFFUL    /* 11:0 Data Length, 0 = 4096 */
#define TBT_DESC_EOF_SHIFT          12              /* 15:12 EOF PDF (the PDF in Raw Mode) */
#define TBT_DESC_SOF_SHIFT          16              /* 19:16 SOF PDF, ignored in Raw Mode */
#define TBT_DESC_PDF_MASK           0xFUL
#define TBT_DESC_DD                 0x00200000UL    /* 21 Descriptor Done */
#define TBT_DESC_RS                 0x00400000UL    /* 22 Request Status */
#define TBT_DESC_IE                 0x00800000UL    /* 23 Interrupt Enable; never set here */
/* DW2 of a receive descriptor posted back by the host interface, Spec
 * 12.4.1, Table 12-8 (Length, EOF PDF and DD as above): */
#define TBT_DESC_RX_CRC_ERR         0x00100000UL    /* 20 CRC Error, Frame Mode only */
#define TBT_DESC_RX_BO              0x00400000UL    /* 22 Buffer Overflow (Spec 12.4.2.2) */
/* DW2 of a receive descriptor posted by the host, Table 12-7: RS must be 1,
 * DD 0. */

/* ====================================================================== */
/* Control packets, Spec 6.4.2                                            */
/* ====================================================================== */

/* PDF values of control packets (Spec 6.4.2.3 to 6.4.2.12, each packet's
 * Header row). */
#define TBT_PDF_READ                0x1UL   /* Read Request and Response */
#define TBT_PDF_WRITE               0x2UL   /* Write Request and Response */
#define TBT_PDF_NOTIFY              0x3UL   /* Notification Packet */
#define TBT_PDF_NOTIFY_ACK          0x4UL
#define TBT_PDF_HOTPLUG             0x5UL
/* ICM frames on ring 0, ICM 2.4 and 3 [inf] (observed by name as ICM_EVENT,
 * ICM_CMD and ICM_RESP in the trace, ICM 3 [obs]). */
#define TBT_PDF_ICM_EVENT           0xAUL
#define TBT_PDF_ICM_CMD             0xBUL
#define TBT_PDF_ICM_RESP            0xCUL

/*
 * Route String, Spec 6.4.2.2, Table 6-1 and Figure 6-5: 64 bits as two
 * DWs, High first. TopologyID 55:0, Rsvd 62:56, CM 63 (1 on a packet from a
 * router to the connection manager). Level n is byte n of the TopologyID,
 * its Adapter # in bits 5:0; a level of 0 ends the string.
 */
#define TBT_ROUTE_HI_TOPOLOGY       0x00FFFFFFUL    /* 55:32 */
#define TBT_ROUTE_HI_CM             0x80000000UL    /* 63 */
#define TBT_ROUTE_LEVEL_ADAPTER     0x3FUL
#define TBT_ROUTE_LEVELS            7UL

/* Read Request DW 3, Spec 6.4.2.3, Table 6-2; the Read Response echoes it
 * (Table 6-3) with the Adapter Num of the arrival adapter when CS is the
 * router space. */
#define TBT_CTL_ADDR_MASK           0x1FFFUL        /* 12:0 Address (DW) */
#define TBT_CTL_SIZE_SHIFT          13              /* 18:13 Read Size, 1..60 */
#define TBT_CTL_SIZE_MASK           0x3FUL
#define TBT_CTL_SIZE_MAX            60UL
#define TBT_CTL_ADP_SHIFT           19              /* 24:19 Adapter Num */
#define TBT_CTL_ADP_MASK            0x3FUL
#define TBT_CTL_CS_SHIFT            25              /* 26:25 Configuration Space */
#define TBT_CTL_CS_MASK             0x3UL
#define TBT_CTL_SN_SHIFT            27              /* 28:27 Sequence Number */
#define TBT_CTL_SN_MASK             0x3UL
/* The fields a response must echo: Address, Size, CS and SN, not Adapter. */
#define TBT_CTL_ECHO_MASK           0x1E07FFFFUL
#define TBT_CS_PATH                 0x0UL
#define TBT_CS_ADAPTER              0x1UL
#define TBT_CS_ROUTER               0x2UL
#define TBT_CS_COUNTERS             0x3UL
/* Notification Packet DW 3, Spec 6.4.2.7, Table 6-6: Event Code 7:0. */
#define TBT_NOTIFY_EVENT(v)         ((ULONG)(v) & 0xFFUL)

/*
 * Control Packet CRC, Spec 6.4.2.2 Table 6-1: width 32, polynomial
 * 1EDC6F41h (CRC-32C, 82F63B78h reflected), init FFFFFFFFh, RefIn and
 * RefOut true, XorOut FFFFFFFFh, over the Route String and Control Data in
 * increasing DW order; Appendix A.2, Table A-1, gives the worked vectors.
 * The trailer DW is the CRC value stored most significant byte first, as
 * every other DW of a buffer is (Spec 12.1.1).
 */
#define TBT_CRC32C_POLY_REFLECTED   0x82F63B78UL

/* ====================================================================== */
/* Alpine Ridge firmware connection manager registers, ICM 2.1 to 2.3     */
/* ====================================================================== */

/* Firmware status, ICM 2.2: 0x800001a1 at every step [obs]. */
#define TBT_ICM_FW_STATUS           0x39944UL
#define TBT_ICM_FW_ICM_ENABLED      0x00000001UL    /* bit 0 [obs/inf] */
#define TBT_ICM_FW_NVM_AUTH_DONE    0x80000000UL    /* bit 31 [obs/inf] */
/* Bits 1, 2 and 30 are the firmware start path's (ICM 2.7; Linux nhi_regs.h
 * REG_FW_STS_ICM_EN_INVERT, _ICM_EN_CPU, _CIO_RESET_REQ); never written. */

/* Inbound mailbox, ICM 2.3: data word, then the command word. */
#define TBT_ICM_INMAIL_DATA         0x39900UL
#define TBT_ICM_INMAIL_CMD          0x39904UL
#define TBT_ICM_INMAIL_CODE_MASK    0x000000FFUL    /* 7:0 command code */
#define TBT_ICM_INMAIL_ERROR        0x40000000UL    /* 30 error */
#define TBT_ICM_INMAIL_REQUEST      0x80000000UL    /* 31 request, cleared on completion */

/* Outbound mailbox, ICM 2.3: operating mode in bits 11:8; 0x800003f5
 * observed [obs]. */
#define TBT_ICM_OUTMAIL             0x3990CUL
#define TBT_ICM_OUTMAIL_MODE(v)     (((ULONG)(v) >> 8) & 0xFUL)
#define TBT_ICM_MODE_SAFE           0UL     /* NVM upgrade only [inf] */
#define TBT_ICM_MODE_AUTH           1UL     /* [inf] */
#define TBT_ICM_MODE_ENDPOINT       2UL     /* [inf] */
#define TBT_ICM_MODE_CM             3UL     /* connection-manager mode, observed 3 [obs] */

/* Mailbox command codes, ICM 2.3's table. */
#define TBT_ICM_MBOX_SAVE_DEVICES   0x05UL  /* system suspend, before 0x07 [inf] */
#define TBT_ICM_MBOX_DISCONNECT_PCIE 0x06UL /* tears every PCIe tunnel down [inf] */
#define TBT_ICM_MBOX_DRIVER_UNLOADS 0x07UL  /* unbind and suspend [obs] */
#define TBT_ICM_MBOX_ALLOW_ALL      0x23UL  /* every bind, before driver ready [obs] */
/* Handshake timing, ICM 2.3 [inf]: poll every 10-20 us for up to 500 ms. */
#define TBT_ICM_MBOX_TIMEOUT_MS     500UL
/* NVM authentication poll, ICM 2.2 [inf]: up to 60 reads 50 ms apart. */
#define TBT_ICM_AUTH_POLLS          60UL
#define TBT_ICM_AUTH_POLL_MS        50UL

/* ====================================================================== */
/* Ice Lake family NHI, PCI configuration space (Linux nhi_regs.h)       */
/* ====================================================================== */

/*
 * The integrated controllers (Ice Lake to Meteor Lake) run their firmware
 * only while "force power" is set in a vendor-specific configuration
 * register, and say it is up through another (Linux nhi_regs.h VS_CAP_22,
 * VS_CAP_9; pci.c icl_nhi_force_power: DMA delay 0x22, then up to 350
 * reads of FW_READY about 3 ms apart), and program LTR on every power-up
 * (icl_nhi_resume) and hand the link controller to Sx through a mailbox
 * (icl_nhi_suspend_noirq). Linux gives these hooks to every integrated host
 * from Ice Lake to Wildcat Lake, firmware CM or not (pci.c nhi_ids,
 * icl_nhi_ops). tbt_hw.c runs them through the driver's configuration
 * accessor before the connection manager is chosen.
 */
#define TBT_ICL_VS_CAP_9            0xC8UL          /* VS_CAP_9 */
#define TBT_ICL_VS_CAP_9_FW_READY   0x80000000UL    /* VS_CAP_9_FW_READY */
#define TBT_ICL_VS_CAP_10           0xCCUL          /* VS_CAP_10: fused UUID, low */
#define TBT_ICL_VS_CAP_11           0xD0UL          /* VS_CAP_11: fused UUID, high */
#define TBT_ICL_VS_CAP_15           0xE0UL          /* VS_CAP_15: LTR written */
#define TBT_ICL_VS_CAP_16           0xE4UL          /* VS_CAP_16: maximum LTR, 15:0 */
#define TBT_ICL_VS_CAP_18           0xECUL          /* VS_CAP_18: TBT2PCIe */
#define TBT_ICL_VS_CAP_18_DONE      0x00000001UL
#define TBT_ICL_VS_CAP_19           0xF0UL          /* VS_CAP_19: PCIe2TBT */
#define TBT_ICL_VS_CAP_19_VALID     0x00000001UL
#define TBT_ICL_VS_CAP_19_CMD_SHIFT 1
#define TBT_ICL_VS_CAP_19_CMD_MASK  0x000000FEUL
#define TBT_ICL_VS_CAP_22           0xFCUL          /* VS_CAP_22 */
#define TBT_ICL_VS_CAP_22_FORCE_POWER 0x00000002UL  /* VS_CAP_22_FORCE_POWER */
#define TBT_ICL_VS_CAP_22_DMA_DELAY_MASK 0xFF000000UL /* VS_CAP_22_DMA_DELAY_MASK */
#define TBT_ICL_VS_CAP_22_DMA_DELAY_SHIFT 24
#define TBT_ICL_DMA_DELAY           0x22UL          /* pci.c icl_nhi_force_power */
#define TBT_ICL_FW_READY_POLLS      350UL           /* pci.c icl_nhi_force_power */
#define TBT_ICL_FW_READY_POLL_MS    3UL
/* LC mailbox commands, nhi_regs.h enum icl_lc_mailbox_cmd; pci.c
 * ICL_LC_MAILBOX_TIMEOUT. */
#define TBT_ICL_LC_GO2SX            0x02UL
#define TBT_ICL_LC_GO2SX_NO_WAKE    0x03UL
#define TBT_ICL_LC_PREPARE_FOR_RESET 0x21UL
#define TBT_ICL_LC_TIMEOUT_MS       500UL

/* ====================================================================== */
/* The USB4 software connection manager (Phase 40, task 40-0.1)           */
/* ====================================================================== */

/*
 * Everything below is what design record 22 uses, transcribed from the USB4
 * Specification 2.0 ("Spec", November 2025) by section and table, and
 * cross-checked field by field against Linux drivers/thunderbolt/tb_regs.h
 * and tb_msgs.h (mirror pinned at c6859eed755df351a3978b33cb92365f9b3e8f06,
 * GPL-2.0; facts only, no code). Where the two differ the specification
 * wins and the difference is noted. The Thunderbolt 3 router's vendor
 * capabilities (Plug Events, link controller) are Linux's alone ("Linux
 * tb_regs.h"), since the specification does not define them. "Guide" is
 * the USB4 Connection Manager Guide 2.0 of the same bundle.
 */

/* ---------------------------------------------------------------------- */
/* Control packets, Spec 6.4.2 (USB4 additions)                           */
/* ---------------------------------------------------------------------- */

/* PDF of the Enhanced Notification Acknowledgement, Spec 6.4.2.9 Table 6-8
 * (Linux has no such packet). */
#define TBT_PDF_NOTIFY_ACK_ENH      0x8UL

/*
 * Notification Packet DW 3, Spec 6.4.2.7 Table 6-6 (Linux tb_msgs.h
 * struct cfg_error_pkg: error 8 bits, port 6, reserved 16, pg 2): Event
 * Code 7:0, Event Info 13:8 (the adapter for most codes, Table 6-12),
 * Sequence 14, PG 31:30.
 */
#define TBT_NOTIFY_INFO(v)          (((ULONG)(v) >> 8) & 0x3FUL)
#define TBT_NOTIFY_INFO_SHIFT       8
#define TBT_NOTIFY_SQ               0x00004000UL
#define TBT_NOTIFY_PG_SHIFT         30
#define TBT_NOTIFY_PG_PLUG          2UL     /* Table 6-6: 10b Hot Plug Event */
#define TBT_NOTIFY_PG_UNPLUG        3UL     /* 11b Hot Unplug Event; Linux TB_CFG_ERROR_PG_HOT_* */
/* Enhanced Notification Acknowledgement DW 2: Sequence in bit 0 (Table 6-8). */
#define TBT_NOTIFY_ACK_ENH_SQ       0x00000001UL

/* Hot Plug Event Packet DW 2, Spec 6.4.2.10 Table 6-9 (Linux struct
 * cfg_event_pkg): Adapter Num 5:0, UPG 31 (1 = unplug). */
#define TBT_HOTPLUG_ADAPTER(v)      ((ULONG)(v) & 0x3FUL)
#define TBT_HOTPLUG_UPG             0x80000000UL

/* Notification event codes, Spec Table 6-12 (Linux tb_msgs.h enum
 * tb_cfg_error names the same values; it lacks ERR_NUA, ERR_LEN and
 * ERR_PLUG, which the specification defines). */
#define TBT_EV_ERR_CONN             0UL
#define TBT_EV_ERR_LINK             1UL
#define TBT_EV_ERR_ADDR             2UL
#define TBT_EV_ERR_ADP              4UL
#define TBT_EV_HP_ACK               7UL
#define TBT_EV_ERR_ENUM             8UL
#define TBT_EV_ERR_NUA              9UL
#define TBT_EV_ERR_LEN              11UL
#define TBT_EV_ERR_HEC              12UL
#define TBT_EV_ERR_FC               13UL
#define TBT_EV_ERR_PLUG             14UL
#define TBT_EV_ERR_LOCK             15UL
#define TBT_EV_DP_BW                32UL
#define TBT_EV_ROP_CMPLT            33UL
#define TBT_EV_POP_CMPLT            34UL
#define TBT_EV_PCIE_WAKE            35UL
#define TBT_EV_DP_CON_CHANGE        36UL
#define TBT_EV_DPTX_DISCOVERY       37UL
#define TBT_EV_LINK_RECOVERY        38UL
#define TBT_EV_ASYM_LINK            39UL

/* ---------------------------------------------------------------------- */
/* Router configuration space, Spec 8.2.1.1 Table 8-3                     */
/* ---------------------------------------------------------------------- */

#define TBT_ROUTER_CS_0             0UL     /* 15:0 Vendor ID, 31:16 Product ID */
#define TBT_ROUTER_CS_1             1UL
#define TBT_ROUTER_CS_2             2UL     /* TopologyID Low */
#define TBT_ROUTER_CS_3             3UL
#define TBT_ROUTER_CS_4             4UL
#define TBT_ROUTER_CS_5             5UL
#define TBT_ROUTER_CS_6             6UL
#define TBT_ROUTER_CS_7             7UL     /* UUID (High) */
#define TBT_ROUTER_CS_8             8UL     /* UUID (Low) */
#define TBT_ROUTER_CS_9             9UL     /* first of 16 Router Operation data DWs */
#define TBT_ROUTER_CS_25            25UL    /* Metadata */
#define TBT_ROUTER_CS_26            26UL    /* Opcode, Status, ONS, OV */

#define TBT_RTR0_VENDOR(v)          ((ULONG)(v) & 0xFFFFUL)
#define TBT_RTR0_DEVICE(v)          (((ULONG)(v) >> 16) & 0xFFFFUL)
/* ROUTER_CS_1 (Linux struct tb_regs_switch_header DWORD 1, same fields). */
#define TBT_RTR1_NEXT_CAP(v)        ((ULONG)(v) & 0xFFUL)           /* 7:0 */
#define TBT_RTR1_UP_ADP(v)          (((ULONG)(v) >> 8) & 0x3FUL)    /* 13:8 Upstream Adapter */
#define TBT_RTR1_MAX_ADP(v)         (((ULONG)(v) >> 14) & 0x3FUL)   /* 19:14 Max Adapter */
#define TBT_RTR1_DEPTH_SHIFT        20                              /* 22:20 Depth, R/W */
#define TBT_RTR1_DEPTH_MASK         0x00700000UL
#define TBT_RTR1_DEPTH(v)           (((ULONG)(v) >> 20) & 0x7UL)
/* ROUTER_CS_3: TopologyID High 23:0, TopologyID Valid 31 (Linux
 * ROUTER_CS_3_V). */
#define TBT_RTR3_TOPOLOGY_MASK      0x00FFFFFFUL
#define TBT_RTR3_V                  0x80000000UL
/* ROUTER_CS_4: Notification Timeout 7:0, CMUV 15:8, USB4 Version 31:24
 * (major 31:29, minor 28:24, Spec 8.2.1.1). CMUV 10h is "USB4 1.0" (Linux
 * ROUTER_CS_4_CMUV_V1). Design record 22 section 3.5: 20h is USB4 1.0 and
 * 40h USB4 2.0, 00h is a Thunderbolt 3 router, every other value reserved
 * (Linux USB4_VERSION_MAJOR_MASK reads the major only). */
#define TBT_RTR4_NOTIFY_TIMEOUT_MASK 0x000000FFUL
#define TBT_RTR4_NOTIFY_TIMEOUT_MAX 0xFFUL      /* Guide 3.2.1; Linux plug_events_delay = 0xff */
#define TBT_RTR4_CMUV_MASK          0x0000FF00UL
#define TBT_RTR4_CMUV_1_0           0x00001000UL
#define TBT_RTR4_VERSION(v)         (((ULONG)(v) >> 24) & 0xFFUL)
#define TBT_USB4_VERSION_TBT3       0x00UL
#define TBT_USB4_VERSION_1_0        0x20UL
#define TBT_USB4_VERSION_2_0        0x40UL
/* ROUTER_CS_5 (Linux ROUTER_CS_5_*). */
#define TBT_RTR5_CNS                0x00800000UL    /* 23 CM TBT3 Not Supported */
#define TBT_RTR5_PTO                0x01000000UL    /* 24 PCIe Tunneling On */
#define TBT_RTR5_UTO                0x02000000UL    /* 25 USB3 Tunneling On */
#define TBT_RTR5_HCO                0x04000000UL    /* 26 Internal Host Controller On */
#define TBT_RTR5_CV                 0x80000000UL    /* 31 Configuration Valid */
/* ROUTER_CS_6 (Linux ROUTER_CS_6_RR, _CR). */
#define TBT_RTR6_RR                 0x01000000UL    /* 24 Router Ready */
#define TBT_RTR6_CR                 0x02000000UL    /* 25 Configuration Ready */
/* ROUTER_CS_26, Spec 8.3.1 (Linux ROUTER_CS_26_*). */
#define TBT_RTR26_OPCODE_MASK       0x0000FFFFUL
#define TBT_RTR26_STATUS(v)         (((ULONG)(v) >> 24) & 0x3FUL)
#define TBT_RTR26_ONS               0x40000000UL
#define TBT_RTR26_OV                0x80000000UL

/* Router Operations, Spec 8.3.1 Table 8-26 (Linux enum usb4_switch_op has
 * 24h and 33h; 30h and 31h are the specification's alone). */
#define TBT_ROP_DROM_READ           0x24UL
#define TBT_ROP_PCIE_DOWN_MAPPING   0x30UL
#define TBT_ROP_GET_CAPABILITIES    0x31UL
#define TBT_ROP_BUFFER_ALLOC        0x33UL
#define TBT_ROP_DATA_DWORDS         16UL
/* DROM Read metadata, Table 8-43 (Linux usb4.c USB4_DROM_ADDRESS_MASK,
 * USB4_DROM_SIZE_MASK): DW address 14:2, Read Size 19:15 (1 to 16). */
#define TBT_ROP_DROM_ADDR_SHIFT     2
#define TBT_ROP_DROM_ADDR_MASK      0x00007FFCUL
#define TBT_ROP_DROM_SIZE_SHIFT     15
#define TBT_ROP_DROM_SIZE_MASK      0x000F8000UL
/* Get PCIe Downstream Entry Mapping completion metadata, Table 8-47:
 * Total Number of Entries 7:0, Entry Index 15:8; data, Table 8-48: DW 0
 * Native PCIe Link 0, PCIe Adapter Number 6:1, PCIE Segment ID Valid 15,
 * Segment ID 31:16; DW 1 Non-FPB Routing ID 15:0, FPB Routing ID 31:16. */
#define TBT_ROP_MAP_TOTAL(v)        ((ULONG)(v) & 0xFFUL)
#define TBT_ROP_MAP_INDEX(v)        (((ULONG)(v) >> 8) & 0xFFUL)
#define TBT_ROP_MAP_NATIVE          0x00000001UL
#define TBT_ROP_MAP_ADAPTER(v)      (((ULONG)(v) >> 1) & 0x3FUL)
#define TBT_ROP_MAP_SEG_VALID       0x00008000UL
#define TBT_ROP_MAP_RID(v)          ((ULONG)(v) & 0xFFFFUL)
/* Get Capabilities, Tables 8-49 to 8-51: metadata Capability Index 7:0;
 * completion metadata Max Capability Index 7:0, Capability Index 15:8,
 * Supported 30, Enabled 31; for index 0 the data DWs hold a 2-bit entry
 * per capability n at DW n/16, bit 2(n%16) supported and the next enabled. */
#define TBT_ROP_CAP_MAX_INDEX(v)    ((ULONG)(v) & 0xFFUL)
#define TBT_CAP_HOT_PLUG_FAILURE    1UL     /* 8.3.1.3.2.1 */
#define TBT_CAP_NOTIFY_SEQUENCE     2UL     /* 8.3.1.3.2.2 */
#define TBT_CAP_BA_PER_PORT         3UL     /* 8.3.1.3.2.3 */
#define TBT_CAP_RETRY_LIMIT         4UL     /* 8.3.1.3.2.4 */
#define TBT_CAP_CLX_LATENCY         5UL     /* 8.3.1.3.2.5 */
/* Buffer Allocation Request, Tables 8-56 to 8-58 (Linux usb4.c
 * USB4_BA_LENGTH_MASK, USB4_BA_INDEX_MASK, USB4_BA_VALUE_MASK): metadata
 * Lane Adapter Number 5:0; completion metadata Length 7:0; each data DW
 * Parameter Index 15:0 and value 31:16. */
#define TBT_ROP_BA_LENGTH(v)        ((ULONG)(v) & 0xFFUL)
#define TBT_ROP_BA_INDEX(v)         ((ULONG)(v) & 0xFFFFUL)
#define TBT_ROP_BA_VALUE(v)         (((ULONG)(v) >> 16) & 0xFFFFUL)
#define TBT_BA_MAX_USB3             1UL
#define TBT_BA_MIN_DP_AUX           2UL
#define TBT_BA_MIN_DP_MAIN          3UL
#define TBT_BA_MAX_PCIE             4UL
#define TBT_BA_MAX_HI               5UL

/* Router capability ids, Spec Table 8-2 (Linux enum tb_switch_cap). */
#define TBT_RCAP_TMU                0x03UL
#define TBT_RCAP_VSC                0x05UL  /* VSC and VSEC share the id */
/* Vendor specific capability headers (Spec 8.2.1.3, 8.2.1.4; Linux
 * struct tb_cap_extended_short and _long): DW 0 Next 7:0, Capability ID
 * 15:8, VSEC ID 23:16, Length 31:24; a Length of 0 is the long form whose
 * Next is DW 1 bits 15:0. */
#define TBT_CAP_NEXT(v)             ((ULONG)(v) & 0xFFUL)
#define TBT_CAP_ID(v)               (((ULONG)(v) >> 8) & 0xFFUL)
#define TBT_CAP_VSEC_ID(v)          (((ULONG)(v) >> 16) & 0xFFUL)
#define TBT_CAP_VSEC_LENGTH(v)      (((ULONG)(v) >> 24) & 0xFFUL)
#define TBT_CAP_LONG_NEXT(v)        ((ULONG)(v) & 0xFFFFUL)
/* The walk's bound: a pointer at or past this ends it (Linux cap.c
 * VSE_CAP_OFFSET_MAX for the long form; the router space's DW address is
 * 13 bits, Spec Table 6-2). */
#define TBT_CAP_WALK_LIMIT          16UL
/* Linux tb_regs.h enum tb_switch_vse_cap: Thunderbolt 3 vendor capabilities. */
#define TBT_VSEC_PLUG_EVENTS        0x01UL
#define TBT_VSEC_LINK_CONTROLLER    0x06UL

/* TMU Router Configuration Capability, Spec 8.2.1.2 (Linux TMU_RTR_CS_*). */
#define TBT_TMU_RTR_CS_0            0UL
#define TBT_TMU_RTR0_FREQ_WIND_SHIFT 16                 /* 26:16 Freq Measurement Window */
#define TBT_TMU_RTR0_FREQ_WIND_MASK 0x07FF0000UL
#define TBT_TMU_RTR0_TD             0x08000000UL        /* 27 Time Disruption */
#define TBT_TMU_RTR0_TSP_NS         0x20000000UL        /* 29 Time Sync Protocol Not Supported */
#define TBT_TMU_RTR0_UCAP           0x40000000UL        /* 30 Uni-Directional Capability */
#define TBT_TMU_RTR_CS_1            1UL     /* LocalTime Low: 31:16 the ns part (Linux) */
#define TBT_TMU_RTR_CS_2            2UL     /* LocalTime Middle */
#define TBT_TMU_RTR_CS_3            3UL     /* LocalTime High 15:0, TSPacketInterval 31:16 */
#define TBT_TMU_RTR3_INTERVAL_SHIFT 16
#define TBT_TMU_RTR3_INTERVAL_MASK  0xFFFF0000UL
#define TBT_TMU_RTR_CS_15           15UL    /* FreqAvg 5:0, DelayAvg 11:6, OffsetAvg 17:12, ErrorAvg 23:18 */
#define TBT_TMU_RTR15_AVG_MASK      0x00FFFFFFUL
#define TBT_TMU_RTR_CS_18           18UL    /* DeltaAvgConst 23:16 (version 2 routers) */
#define TBT_TMU_RTR18_DELTA_MASK    0x00FF0000UL
#define TBT_TMU_RTR_CS_22           22UL    /* Post Local Time Low */
#define TBT_TMU_RTR_CS_23           23UL    /* Post Local Time High */
#define TBT_TMU_RTR_CS_24           24UL    /* Post Time Low */
#define TBT_TMU_RTR_CS_25           25UL    /* Post Time High: writing it starts the post */
/* Spec 8.2.1.2's connection manager note: TSPacketInterval TMU_OFF 0,
 * HiFi 16, LowRes 1000 (Linux tmu.c tmu_rates). Guide Table 7-3 and
 * Linux tmu_params for HiFi: window 800, averaging constants 8. */
#define TBT_TMU_INTERVAL_OFF        0UL
#define TBT_TMU_INTERVAL_HIFI       16UL
#define TBT_TMU_FREQ_WINDOW_HIFI    800UL
#define TBT_TMU_AVG_CONST_HIFI      8UL

/* ---------------------------------------------------------------------- */
/* Adapter configuration space, Spec 8.2.2.1 Table 8-9 (DW 0 to 5)       */
/* ---------------------------------------------------------------------- */

#define TBT_ADP_CS_0                0UL
#define TBT_ADP_CS_1                1UL     /* 7:0 Next Capability Pointer */
#define TBT_ADP_CS_2                2UL     /* 23:0 Adapter Type */
#define TBT_ADP_CS_3                3UL
#define TBT_ADP_CS_4                4UL
#define TBT_ADP_CS_5                5UL
#define TBT_ADP_BASIC_DWORDS        6UL
#define TBT_ADP1_NEXT_CAP(v)        ((ULONG)(v) & 0xFFUL)
#define TBT_ADP2_TYPE(v)            ((ULONG)(v) & 0x00FFFFFFUL)
/* Adapter Type (Protocol 23:16, Version 15:8, Sub-type 7:0), Spec 8.2.2.1;
 * Linux enum tb_port_type. */
#define TBT_ADP_TYPE_UNUSED         0x000000UL
#define TBT_ADP_TYPE_LANE           0x000001UL
#define TBT_ADP_TYPE_HI             0x000002UL
#define TBT_ADP_TYPE_DP_IN          0x0E0101UL
#define TBT_ADP_TYPE_DP_OUT         0x0E0102UL
#define TBT_ADP_TYPE_PCIE_DOWN      0x100101UL
#define TBT_ADP_TYPE_PCIE_UP        0x100102UL
#define TBT_ADP_TYPE_USB3_DOWN      0x200101UL
#define TBT_ADP_TYPE_USB3_UP        0x200102UL
/* ADP_CS_4 (Linux ADP_CS_4_*): Non Flow Controlled Buffers 9:0, Total
 * Buffers 29:20, Plugged 30, Lock 31. */
#define TBT_ADP4_NFC_BUFFERS(v)     ((ULONG)(v) & 0x3FFUL)
#define TBT_ADP4_TOTAL_BUFFERS(v)   (((ULONG)(v) >> 20) & 0x3FFUL)
#define TBT_ADP4_PLUGGED            0x40000000UL
#define TBT_ADP4_LOCK               0x80000000UL
/* ADP_CS_5: Max Input HopID 6:0, Max Output HopID 17:11, Link Credits
 * Allocated 28:22, Disable Hot Plug Events 31. Linux's struct
 * tb_regs_port_header gives the HopID fields 11 bits each (10:0, 21:11),
 * the Thunderbolt 3 width; bits 10:7 are reserved in USB4, so the
 * specification's 7-bit fields are read here. Linux ADP_CS_5_LCA_MASK,
 * ADP_CS_5_DHP agree. */
#define TBT_ADP5_MAX_IN_HOP(v)      ((ULONG)(v) & 0x7FUL)
#define TBT_ADP5_MAX_OUT_HOP(v)     (((ULONG)(v) >> 11) & 0x7FUL)
#define TBT_ADP5_LCA(v)             (((ULONG)(v) >> 22) & 0x7FUL)
#define TBT_ADP5_DHP                0x80000000UL

/* Adapter capability ids, Spec 8.2.2 (Linux enum tb_port_cap). */
#define TBT_ACAP_LANE               0x01UL
#define TBT_ACAP_TMU                0x03UL
#define TBT_ACAP_ADAPTER            0x04UL  /* PCIe, USB3, DP: "Adapter Configuration Capability" */
#define TBT_ACAP_VSC                0x05UL
#define TBT_ACAP_USB4_PORT          0x06UL

/* TMU Adapter Configuration Capability, Spec 8.2.2.2 (Linux TMU_ADP_CS_*). */
#define TBT_TMU_ADP_CS_3            3UL
#define TBT_TMU_ADP3_UDM            0x20000000UL    /* 29 EnableUniDirectionalMode */
#define TBT_TMU_ADP_CS_6            6UL
#define TBT_TMU_ADP6_DTS            0x00000002UL    /* 1 Disable Time Sync */
#define TBT_TMU_ADP_CS_8            8UL
#define TBT_TMU_ADP8_EUDM           0x00008000UL    /* 15 Enable Enhanced Uni-Directional Mode */
#define TBT_TMU_ADP_CS_9            9UL
#define TBT_TMU_ADP9_INTERVAL_MASK  0xFFFF0000UL    /* 31:16 AdapterTimeSyncInterval */

/* Lane Adapter Configuration Capability, Spec 8.2.2.3 (Linux LANE_ADP_CS_*). */
#define TBT_LANE_CS_0               0UL
#define TBT_LANE0_SLW_X2            0x00200000UL    /* 21: x2 supported (Linux SUPPORTED_WIDTH_DUAL at 20) */
#define TBT_LANE_CS_1               1UL
#define TBT_LANE1_TLW_MASK          0x00000030UL    /* 5:4 Target Link Width */
#define TBT_LANE1_TLW_SINGLE        0x00000010UL    /* 01b, the default */
#define TBT_LANE1_TLW_DUAL          0x00000030UL    /* 11b */
#define TBT_LANE1_CL0S_EN           0x00000400UL    /* 10 */
#define TBT_LANE1_CL1_EN            0x00000800UL    /* 11 */
#define TBT_LANE1_CL2_EN            0x00001000UL    /* 12 */
#define TBT_LANE1_CLX_EN            0x00001C00UL
#define TBT_LANE1_LD                0x00004000UL    /* 14 Lane Disable */
#define TBT_LANE1_LB                0x00008000UL    /* 15 Lane Bonding */
#define TBT_LANE1_SPEED(v)          (((ULONG)(v) >> 16) & 0xFUL)    /* 19:16 Current Link Speed */
#define TBT_LANE_SPEED_GEN2         0x8UL
#define TBT_LANE_SPEED_GEN3         0x4UL
#define TBT_LANE_SPEED_GEN4         0x2UL
#define TBT_LANE1_NLW(v)            (((ULONG)(v) >> 20) & 0x3FUL)   /* 25:20 Negotiated Link Width */
#define TBT_LANE_WIDTH_X1           0x01UL
#define TBT_LANE_WIDTH_X2           0x02UL
#define TBT_LANE1_STATE(v)          (((ULONG)(v) >> 26) & 0xFUL)    /* 29:26 Adapter State */
#define TBT_LANE_STATE_DISABLED     0UL
#define TBT_LANE_STATE_TRAINING     1UL
#define TBT_LANE_STATE_CL0          2UL
#define TBT_LANE_STATE_CL0S_TX      3UL
#define TBT_LANE_STATE_CL0S_RX      4UL
#define TBT_LANE_STATE_CL1          5UL
#define TBT_LANE_STATE_CL2          6UL
#define TBT_LANE_STATE_CLD          7UL

/* USB4 Port Capability, Spec 8.2.2.4 (Linux PORT_CS_18_*, PORT_CS_19_*). */
#define TBT_PORT_CS_18              18UL
#define TBT_PORT18_BE               0x00000100UL    /* 8 Bonding Enabled */
#define TBT_PORT18_TCM              0x00000200UL    /* 9 TBT3-Compatible Mode */
#define TBT_PORT_CS_19              19UL
#define TBT_PORT19_DPR              0x00000001UL    /* 0 Downstream Port Reset */
#define TBT_PORT19_PC               0x00000008UL    /* 3 USB4 Port is Configured */

/* PCIe Adapter Configuration Capability, Spec 8.2.2.7 (Linux
 * ADP_PCIE_CS_0_*, enum tb_pcie_ltssm_state). */
#define TBT_PCIE_CS_0               0UL
#define TBT_PCIE0_LTSSM(v)          (((ULONG)(v) >> 25) & 0xFUL)    /* 28:25 */
#define TBT_PCIE0_PE                0x80000000UL    /* 31 Path Enable */
#define TBT_LTSSM_DETECT            0x0UL
#define TBT_LTSSM_RECOVERY          0x4UL
#define TBT_LTSSM_RECOVERY_IDLE     0x5UL
#define TBT_LTSSM_L0                0x6UL
#define TBT_LTSSM_L1                0x7UL

/* USB3 Adapter Configuration Capability, Spec 8.2.2.5 (Linux ADP_USB3_CS_*). */
#define TBT_USB3_CS_0               0UL
#define TBT_USB3_0_V                0x40000000UL    /* 30 Valid */
#define TBT_USB3_0_PE               0x80000000UL    /* 31 Path Enable */
#define TBT_USB3_CS_1               1UL
#define TBT_USB3_1_CUBW(v)          ((ULONG)(v) & 0xFFFUL)          /* 11:0 consumed up */
#define TBT_USB3_1_CDBW(v)          (((ULONG)(v) >> 12) & 0xFFFUL)  /* 23:12 consumed down */
#define TBT_USB3_1_HCA              0x80000000UL    /* 31 Host Controller Ack */
#define TBT_USB3_CS_2               2UL
#define TBT_USB3_2_BW_MASK          0x00FFFFFFUL    /* allocated up 11:0, down 23:12 */
#define TBT_USB3_2_ADBW_SHIFT       12
#define TBT_USB3_2_CMR              0x80000000UL    /* 31 Connection Manager Request */
#define TBT_USB3_CS_3               3UL
#define TBT_USB3_3_SCALE(v)         ((ULONG)(v) & 0x3FUL)           /* 5:0 */
#define TBT_USB3_CS_4               4UL
#define TBT_USB3_4_MSLR(v)          (((ULONG)(v) >> 12) & 0x7FUL)   /* 18:12 Maximum Supported Link Rate */
#define TBT_USB3_MSLR_20G           0x1UL   /* Linux ADP_USB3_CS_4_MSLR_20G; anything else 10 Gb/s */

/* ---------------------------------------------------------------------- */
/* Path configuration space, Spec 8.2.3: 2 DWs per HopID                  */
/* ---------------------------------------------------------------------- */

/* PATH_CS_0 (Linux struct tb_regs_hop DWORD 0, whose next_hop is 11 bits;
 * the specification's Output HopID is 6:0). */
#define TBT_PATH0_OUT_HOP(v)        ((ULONG)(v) & 0x7FUL)
#define TBT_PATH0_OUT_HOP_MASK      0x0000007FUL
#define TBT_PATH0_OUT_ADP(v)        (((ULONG)(v) >> 11) & 0x3FUL)
#define TBT_PATH0_OUT_ADP_SHIFT     11
#define TBT_PATH0_OUT_ADP_MASK      0x0001F800UL
#define TBT_PATH0_CREDITS(v)        (((ULONG)(v) >> 17) & 0x7FUL)
#define TBT_PATH0_CREDITS_SHIFT     17
#define TBT_PATH0_CREDITS_MASK      0x00FE0000UL
#define TBT_PATH0_VALID             0x80000000UL
/* PATH_CS_1. */
#define TBT_PATH1_WEIGHT_MASK       0x0000000FUL
#define TBT_PATH1_PRIO_SHIFT        8
#define TBT_PATH1_PRIO_MASK         0x00000700UL
#define TBT_PATH1_IFC               0x01000000UL
#define TBT_PATH1_EFC               0x02000000UL
#define TBT_PATH1_ISE               0x04000000UL
#define TBT_PATH1_ESE               0x08000000UL
#define TBT_PATH1_PP                0x10000000UL    /* 28 Pending Packets */
/* Guide Table 6-1 and Linux tunnel.c: PCIe and USB3 priority 3; weight 1
 * for PCIe (TB_PCI_WEIGHT), 2 for USB3 (TB_USB3_WEIGHT). */
#define TBT_PATH_PRIORITY_TUNNEL    3UL
#define TBT_PATH_WEIGHT_PCIE        1UL
#define TBT_PATH_WEIGHT_USB3        2UL
/* The first HopID a tunnelled path may use (Spec 8.2.3.2), the protocol
 * adapters' HopID (Linux TB_PCI_HOPID, TB_USB3_HOPID), and the floor
 * below which no PCIe path is built (Linux TB_MIN_PCIE_CREDITS). */
#define TBT_HOP_FIRST               8UL
#define TBT_HOP_PROTOCOL            8UL
#define TBT_HOP_MAX                 127UL
#define TBT_CREDITS_MIN_PCIE        6UL

/* ---------------------------------------------------------------------- */
/* Thunderbolt 3 vendor capabilities (Linux tb_regs.h, lc.c, switch.c)    */
/* ---------------------------------------------------------------------- */

/* Plug Events VSEC, DW 1 (VSC_CS_1): bits 6:2 the plug events, bit 2 the
 * USB disable (Linux TB_PLUG_EVENTS_USB_DISABLE; switch.c
 * tb_plug_events_active clears 6:2 and sets bit 2 except on Alpine Ridge). */
#define TBT_PLUG_EVENTS_CS_1        1UL
#define TBT_PLUG_EVENTS_MASK        0x0000007CUL
#define TBT_PLUG_EVENTS_USB_DISABLE 0x00000004UL
/* Link controller VSEC (Linux TB_LC_*): the common block. */
#define TBT_LC_DESC                 0x02UL
#define TBT_LC_DESC_SIZE(v)         (((ULONG)(v) >> 8) & 0xFFUL)        /* start of the per-port blocks */
#define TBT_LC_DESC_PORT_SIZE(v)    (((ULONG)(v) >> 16) & 0xFFFUL)
#define TBT_LC_FUSE                 0x03UL      /* four DWs of UUID (lc.c tb_lc_read_uuid) */
/* The per-port block of physical port (adapter - 1) / 2 (Linux
 * thunderbolt.h tb_phy_port_from_link). */
#define TBT_LC_PORT_ATTR            0x8DUL
#define TBT_LC_PORT_ATTR_BE         0x00001000UL    /* 12 bonding possible */
#define TBT_LC_SX_CTRL              0x96UL
#define TBT_LC_SX_CTRL_L1C          0x00010000UL    /* 16 odd adapter configured */
#define TBT_LC_SX_CTRL_L2C          0x00100000UL    /* 20 even adapter configured */
#define TBT_LC_SX_CTRL_UPSTREAM     0x40000000UL    /* 30 */
/* Linux switch.c tb_init_port: a Thunderbolt 3 lane adapter's control path
 * credits when its HopID 0 entry does not report them. */
#define TBT_TBT3_CTL_CREDITS        2UL
/* Linux tunnel.c tb_pci_init_credits: the protocol adapter's credits on a
 * router without USB4 credit allocation, and the lane defaults. */
#define TBT_TBT3_PROTO_CREDITS      7UL
#define TBT_CREDITS_BONDED          32UL
#define TBT_CREDITS_SINGLE          16UL

/* ---------------------------------------------------------------------- */
/* Host Router Reset, Spec 12.6.3.1.2 and Guide 3.5                       */
/* ---------------------------------------------------------------------- */

#define TBT_HI_HR_RESET_HRR         0x00000001UL

#endif /* TBT_REGS_H */
