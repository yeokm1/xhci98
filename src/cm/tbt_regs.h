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
 * were added from ICM-PROTOCOL.md.
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
 * 2.0 (40h). Design record 21 section 5.1 refuses any nonzero version. */
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

#endif /* TBT_REGS_H */
