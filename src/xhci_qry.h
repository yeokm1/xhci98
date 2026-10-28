/*
 * xhci_qry.h - the answer of xhci98.sys's query request, IOCTL_XHCI98_QUERY
 * (design record 13 section 8.12; design record 25 revision 12): the
 * driver's log ring, its counter block and the controller's PCI
 * configuration, asked of the controller's own device (\\.\XHCI98HC<k>
 * on NT, \\.\HCD<n> on 98 SE and ME; roadmap-usb4 44.81) in the
 * framing xhcicm.sys's IOCTL_XHCICM_LOG uses (src\cm\cm_query.h CM_QL_*).
 * Shared byte for byte by the driver (hcd_door.c, xhci_qry.c), XHCITR.EXE
 * and the host tests, so it uses no DDK or Windows type: constants only.
 *
 * Every field is a little-endian 32-bit unsigned value at a fixed offset,
 * or a byte string; a reader takes bytes, never a structure overlay. A
 * layout change is a new XHQ_VERSION. Version 2 (3.3.0.0; roadmap-usb4
 * tasks 44.8 and 44.35, design record 13 section 8.12) grows the header to
 * 80 bytes - its first 64 version 1's, word for word - with the driver's
 * version and the storage transport part; a reader of version 2 takes
 * version 1 too, which has neither. Version 3 (3.3.0.0; roadmap-usb4 task
 * 44.80) grows it to 88 bytes - its first 80 version 2's - with the ports
 * part; a reader of version 3 takes versions 1 and 2 too.
 */

#ifndef XHCI_QRY_H
#define XHCI_QRY_H

/*
 * CTL_CODE(FILE_DEVICE_UNKNOWN 0x22, 0xA48, METHOD_BUFFERED,
 * FILE_ANY_ACCESS): a function code in the vendor range, beside xhcicm's
 * 0xA40 to 0xA42, and none of the USB hub or USBUSER codes the door already
 * answers. No input. Opened with no access rights; a standard user may
 * read. Not gated by the door's enable step: what it serves follows
 * XhciLogVerbosity instead (below).
 *   - output buffer < XHQ_HDR_BYTES_V1: STATUS_BUFFER_TOO_SMALL, nothing;
 *   - output buffer < the whole answer: STATUS_SUCCESS with the header
 *     alone, XHQ_O_BYTES the size needed (at most XHQ_MAX_BYTES) - only
 *     its first XHQ_HDR_BYTES_V1 bytes in a buffer under XHQ_HDR_BYTES,
 *     so a version 1 reader's 64-byte ask still reads the flags;
 *   - otherwise STATUS_SUCCESS, the whole answer.
 * A driver from before it answers STATUS_INVALID_DEVICE_REQUEST.
 */
#define XHQ_IOCTL               0x00222920UL
#define XHQ_MAGIC               0x51434858UL    /* 'XHCQ' */
#define XHQ_VERSION             3UL
#define XHQ_VERSION_1           1UL     /* a 64-byte header, no version
                                           word and no transport part   */
#define XHQ_VERSION_2           2UL     /* an 80-byte header, no ports
                                           part                         */

#define XHQ_HDR_BYTES           88UL
#define XHQ_HDR_BYTES_V1        64UL
#define XHQ_HDR_BYTES_V2        80UL
#define XHQ_O_MAGIC             0UL
#define XHQ_O_VERSION           4UL
#define XHQ_O_BYTES             8UL     /* the whole answer; in a header
                                           alone, the size to ask for */
#define XHQ_O_FLAGS             12UL    /* XHQ_F_* */
#define XHQ_O_VERBOSITY         16UL    /* XhciLogVerbosity applied */
#define XHQ_O_VERBOSITY_READ    20UL    /* ... as read from the registry */
#define XHQ_O_CTR_OFFSET        24UL    /* the counter block: XHCIHC_COUNTERS */
#define XHQ_O_CTR_BYTES         28UL    /* as little-endian words, 0 below
                                           level 1 */
#define XHQ_O_LOG_OFFSET        32UL    /* the log's text, oldest first; */
#define XHQ_O_LOG_BYTES         36UL    /* 0 below level 2 and at level 4 */
#define XHQ_O_PCI_OFFSET        40UL    /* configuration space 00h..FFh, */
#define XHQ_O_PCI_BYTES         44UL    /* 256 when read, else 0 */
#define XHQ_O_APPENDS           48UL    /* records the ring accepted */
#define XHQ_O_DROPPED           52UL    /* bytes the ring's wrap overwrote */
#define XHQ_O_PCI_STATE         56UL    /* XHQ_PCI_* */
#define XHQ_O_FLAVOUR           60UL    /* XHCI_SNAPSHOT_FLAVOUR_* (xhci.h) */
/* Version 2. */
#define XHQ_O_DRIVER_VERSION    64UL    /* the driver's version, a byte a
                                           field from the most
                                           significant (xhci_verpk.h) */
#define XHQ_O_XPORT_OFFSET      68UL    /* the storage transport part:  */
#define XHQ_O_XPORT_BYTES       72UL    /* records, at every level      */
#define XHQ_O_XPORT_RECORD      76UL    /* one record, in bytes         */
/* Version 3. */
#define XHQ_O_PORTS_OFFSET      80UL    /* the ports part: one byte per */
#define XHQ_O_PORTS_BYTES       84UL    /* root port, at every level    */

/*
 * The ports part (roadmap-usb4 44.80; design record 13 section 8.12): byte
 * n - 1 is root port n's XHQ_PT_*, the fastest that port can run as the
 * controller's Supported Protocol capability states it (XhciPortTop), for
 * HCSPARAMS1.MaxPorts ports; none while the controller is not started.
 * XHCITR's "can run faster" notice reads it (design record 25 section 7.7
 * item 10).
 */
#define XHQ_PORTS_MAX           255UL
#define XHQ_PT_NONE             0UL     /* no protocol group claims it  */
#define XHQ_PT_USB2             1UL     /* a USB 2.0 port               */
#define XHQ_PT_SS               2UL     /* USB 3.x, 5 Gbit/s at most    */
#define XHQ_PT_SSP              3UL     /* USB 3.x, SuperSpeedPlus      */

/*
 * The storage transport part (roadmap-usb4 44.35; design record 13 section
 * 10.7, "The decision kept"): one record per device record holding a
 * storage decision, in Slot ID order, at most XHQ_XPORT_MAX; the words of
 * the snapshot's XHCI_SNAPSHOT_HCD_X_* in the same order (test_xhq holds
 * them equal).
 */
#define XHQ_XPORT_RECORD_BYTES  40UL
#define XHQ_XPORT_MAX           32UL    /* XHCI_MAX_SLOTS */
#define XHQ_X_SLOT              0UL     /* Slot ID */
#define XHQ_X_PORT              4UL     /* root port, 1-based */
#define XHQ_X_ROUTE             8UL     /* Route String */
#define XHQ_X_IFACE             12UL    /* bInterfaceNumber; 0xFF a device
                                           PDO */
#define XHQ_X_TRANSPORT         16UL    /* XHCI_XPORT_NONE .. _REFUSED */
#define XHQ_X_WHY               20UL    /* XHCI_XPORT_WHY_* */
#define XHQ_X_ALTERNATE         24UL    /* the setting selected */
#define XHQ_X_INPUTS            28UL    /* XHCI_XPORT_F_* */
#define XHQ_X_TRIPLE            32UL    /* class 7:0, subclass 15:8,
                                           protocol 23:16 */
#define XHQ_X_COUNT             36UL    /* storage interfaces decided */

#define XHQ_F_COUNTERS          0x01UL  /* level >= 1: the counters served */
#define XHQ_F_RECORDING         0x02UL  /* level >= 2: the ring fills */
#define XHQ_F_REFUSED           0x04UL  /* the value read was out of range
                                           and refused (the default applied) */
#define XHQ_F_EMITTED           0x08UL  /* XhciLogDebugView drains the ring as
                                           it fills: the log is what it has
                                           not emitted yet */
#define XHQ_F_LOG_CUT           0x10UL  /* the oldest bytes may be the tail of
                                           a record: the log starts at the
                                           next one */
#define XHQ_F_WITHHELD          0x20UL  /* level 4: the ring holds kernel
                                           addresses and is not served */
#define XHQ_F_STARTED           0x40UL  /* the controller is started */
#define XHQ_F_FAILED            0x80UL  /* failed with no recovery coming:
                                           latched or contained, a
                                           terminal of XHQ_TERMINAL_LATCHED
                                           or more (roadmap-usb4 43.57) */

/* XHCI_TOL_TERMINAL_FAILURES (xhci_tol.h), which this header cannot
 * include for a tool; test_xhq holds the two equal. 1, a failure with a
 * recovery still owed, is the driver putting it right itself. */
#define XHQ_TERMINAL_LATCHED    2UL

#define XHQ_PCI_READ            1UL     /* the 256 bytes as read */
#define XHQ_PCI_FAILED          3UL     /* a read was refused */
#define XHQ_PCI_ABSENT          4UL     /* the first DWORD read all ones */

#define XHQ_PCI_BYTES           256UL
#define XHQ_CTR_MAX             2048UL  /* room for the block to grow */
#define XHQ_LOG_MAX             16384UL /* XHCI_LOG_RING_BYTES */
#define XHQ_MAX_BYTES           (XHQ_HDR_BYTES + XHQ_CTR_MAX + XHQ_LOG_MAX + \
                                 XHQ_PCI_BYTES +                         \
                                 XHQ_XPORT_MAX * XHQ_XPORT_RECORD_BYTES + \
                                 XHQ_PORTS_MAX)
#define XHQ_ASK_BYTES           32768UL /* the reader's first buffer */

#endif /* XHCI_QRY_H */
