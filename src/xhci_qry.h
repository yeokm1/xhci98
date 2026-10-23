/*
 * xhci_qry.h - the answer of xhci98.sys's query request, IOCTL_XHCI98_QUERY
 * (design record 13 section 8.12; design record 25 revision 12): the
 * driver's log ring, its counter block and the controller's PCI
 * configuration, asked of the controller's own device (\\.\HCD<n>) in the
 * framing xhcicm.sys's IOCTL_XHCICM_LOG uses (src\cm\cm_query.h CM_QL_*).
 * Shared byte for byte by the driver (hcd_door.c, xhci_qry.c), XHCITR.EXE
 * and the host tests, so it uses no DDK or Windows type: constants only.
 *
 * Every field is a little-endian 32-bit unsigned value at a fixed offset,
 * or a byte string; a reader takes bytes, never a structure overlay. A
 * layout change is a new XHQ_VERSION.
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
 *   - output buffer < XHQ_HDR_BYTES: STATUS_BUFFER_TOO_SMALL, nothing;
 *   - output buffer < the whole answer: STATUS_SUCCESS with the header
 *     alone, XHQ_O_BYTES the size needed (at most XHQ_MAX_BYTES);
 *   - otherwise STATUS_SUCCESS, the whole answer.
 * A driver from before it answers STATUS_INVALID_DEVICE_REQUEST.
 */
#define XHQ_IOCTL               0x00222920UL
#define XHQ_MAGIC               0x51434858UL    /* 'XHCQ' */
#define XHQ_VERSION             1UL

#define XHQ_HDR_BYTES           64UL
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
                                 XHQ_PCI_BYTES)
#define XHQ_ASK_BYTES           32768UL /* the reader's first buffer */

#endif /* XHCI_QRY_H */
