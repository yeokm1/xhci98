/*
 * cm_qnet.h - the networking answer of xhcicm.sys's query device
 * (design record 35 section 17; roadmap-usb4.md task 48.18): Thunderbolt
 * networking's state, this adapter, each link with another computer, and
 * the network driver's counters, as the connection manager's thread last
 * built it. Shared byte for byte by the driver (cm_qnet.c, cm_net.c,
 * cm_qryos.c), XHCITR.EXE (xhcitr\tr_net.c) and the host tests, so it
 * uses no DDK or Windows type.
 *
 * Every field is a little-endian 32-bit unsigned value at a fixed offset,
 * or a byte string; the readers take bytes, never a structure overlay.
 * Each part is found through the header and has its own size, so a part
 * may grow at its end (its new words reserved and 0 before) without a new
 * CM_QN_VERSION; any other change is a new version, which a reader that
 * does not know it refuses.
 */

#ifndef CM_QNET_H
#define CM_QNET_H

/*
 * A sixth request on the query device (cm_query.h's):
 * CTL_CODE(FILE_DEVICE_UNKNOWN 0x22, 0xA45, METHOD_BUFFERED,
 * FILE_ANY_ACCESS), no input, opened with no access rights. CM_Q_IOCTL's
 * sizing rules with CM_QN_HDR_BYTES for CM_Q_HDR_BYTES, and its
 * removed-device answer; STATUS_DEVICE_NOT_READY before the thread's
 * first pass has built one. A driver from before task 48.18 answers
 * STATUS_INVALID_DEVICE_REQUEST.
 */
#define CM_QN_IOCTL             0x00222914UL
#define CM_QN_MAGIC             0x4E4D4358UL    /* 'XCMN' */
#define CM_QN_VERSION           1UL

/* The header. */
#define CM_QN_HDR_BYTES         64UL
#define CM_QN_O_MAGIC           0UL
#define CM_QN_O_VERSION         4UL
#define CM_QN_O_BYTES           8UL     /* the whole answer; in a header
                                           alone, the size to ask for */
#define CM_QN_O_FLAGS           12UL    /* CM_QN_F_* */
#define CM_QN_O_EPOCH_LO        16UL    /* CM_Q_O_EPOCH_* of this start */
#define CM_QN_O_EPOCH_HI        20UL
#define CM_QN_O_PASS            24UL    /* as CM_Q_O_PASS */
#define CM_QN_O_PASS_AGE_MS     28UL    /* as CM_Q_O_PASS_AGE_MS */
#define CM_QN_O_NET_VALUE       32UL    /* XhciCmNet as this start read it */
#define CM_QN_O_E2E_VALUE       36UL    /* XhciCmNetE2E, the same */
#define CM_QN_O_CM              40UL    /* CM_ST_CM_* of this start */
#define CM_QN_O_SVC_OFFSET      44UL    /* the service block */
#define CM_QN_O_SVC_BYTES       48UL
#define CM_QN_O_LINK_OFFSET     52UL    /* the first link entry */
#define CM_QN_O_LINK_SHAPE      56UL    /* count 15:0, entry bytes 31:16 */
#define CM_QN_O_CLIENT_OFFSET   60UL    /* the client block, 0 for none; its
                                           bytes are its own first word */

#define CM_QN_F_STOPPING        0x01UL  /* the NHI is stopping */
#define CM_QN_F_PDO             0x02UL  /* the network PDO exists and was
                                           reported */
#define CM_QN_F_OPEN            0x04UL  /* a network driver holds the
                                           service open */
#define CM_QN_F_UP              0x08UL  /* the service is up */
#define CM_QN_F_MAC             0x10UL  /* this domain's MAC is read */
#define CM_QN_F_CLIENT          0x20UL  /* the client block follows */
#define CM_QN_F_LINKS_CUT       0x40UL  /* more peer records than entries */

/* The service block: the adapter's, one per NHI. A driver before task
 * 48.38 wrote CM_QN_SVC_MIN_BYTES, word 92 reserved 0 (the poll alone,
 * CM_QN_IRQ_POLLED); the interrupt's kicks were added at its end. */
#define CM_QN_SVC_BYTES         104UL
#define CM_QN_SVC_MIN_BYTES     96UL
#define CM_QN_S_STATE           0UL     /* CM_QN_S_* */
#define CM_QN_S_LINK            4UL     /* the link it runs on, 0 while down */
#define CM_QN_S_MAC             8UL     /* 6 bytes, then 2 reserved */
#define CM_QN_S_PEER_MAC        16UL    /* 6 bytes, then 2 reserved; while up */
#define CM_QN_S_PEER_STNS       24UL    /* the peer's prtcstns, while up */
#define CM_QN_S_OUR_STNS        28UL    /* the prtcstns this host offers */
#define CM_QN_S_E2E             32UL    /* E2E in use on the rings, while up */
#define CM_QN_S_MBPS            36UL    /* the link's whole rate, 0 unknown */
#define CM_QN_S_LANES           40UL    /* 1 or 2, 0 unknown */
#define CM_QN_S_MAX_FRAME       44UL
#define CM_QN_S_TX_DESC         48UL    /* while up */
#define CM_QN_S_RX_DESC         52UL
#define CM_QN_S_GENERATION      56UL
#define CM_QN_S_RX_FRAMES       60UL    /* handed to the network driver */
#define CM_QN_S_RX_ERRORS       64UL    /* of them CRC Error or Overflow */
#define CM_QN_S_RX_DROPPED      68UL    /* dropped here: bad EOF, unheld */
#define CM_QN_S_TX_FRAMES       72UL    /* transmit descriptors posted */
#define CM_QN_S_TX_REFUSED      76UL    /* reservations refused, stale */
#define CM_QN_S_LINK_UPS        80UL
#define CM_QN_S_LINK_DOWNS      84UL
#define CM_QN_S_RINGS_FAILED    88UL
#define CM_QN_S_IRQ_STATE       92UL    /* CM_QN_IRQ_* (record 35 5.5) */
#define CM_QN_S_IRQ_KICKS       96UL    /* the interrupt DPC's kicks */
/* 100..103 reserved, 0 */

/* The data ring's interrupt (record 35 section 5.5, task 48.38). */
#define CM_QN_IRQ_POLLED        0UL     /* checked every clock tick */
#define CM_QN_IRQ_OPEN          1UL     /* the interrupt open */
#define CM_QN_IRQ_LATCHED       2UL     /* fell back to the poll */

#define CM_QN_STATE_OFF         0UL     /* XhciCmNet 0, or no network PDO */
#define CM_QN_STATE_NO_DRIVER   1UL     /* the PDO is there, no Open */
#define CM_QN_STATE_WAITING     2UL     /* open, no service up */
#define CM_QN_STATE_UP          3UL

/* One link entry: a peer record in use, in the record's own order. A
 * driver before revision 23 wrote entries of CM_QN_LINK_MIN_BYTES; the
 * whole texts (CM_QN_LF_TEXT_L) were added at the entry's end. */
#define CM_QN_LINK_BYTES        292UL
#define CM_QN_LINK_MIN_BYTES    160UL
#define CM_QN_MAX_LINKS         4UL
#define CM_QN_L_LINK            0UL     /* the USB4 port, or the firmware
                                           CM record's link number */
#define CM_QN_L_FLAGS           4UL     /* CM_QN_LF_* */
#define CM_QN_L_DISC_STATE      8UL     /* TBT_XDP_S_* (record 35 3.5) */
#define CM_QN_L_NET_STATE       12UL    /* TBT_NET_S_* (record 35 4) */
#define CM_QN_L_UUID            16UL    /* 4 words, TBT_UUID's order */
#define CM_QN_L_VENDOR_ID       32UL
#define CM_QN_L_DEVICE_ID       36UL
#define CM_QN_L_DEVICE_REV      40UL
#define CM_QN_L_PRTCSTNS        44UL
#define CM_QN_L_VENDOR_TEXT     48UL    /* CM_QN_TEXT_BYTES, NUL-padded */
#define CM_QN_L_DEVICE_TEXT     81UL    /* the same */
/* 114, 115 reserved, 0 */
#define CM_QN_L_MBPS            116UL   /* 0 unknown */
#define CM_QN_L_LANES           120UL   /* 0 unknown */
#define CM_QN_L_SENT            124UL   /* inter-domain packets */
#define CM_QN_L_RECEIVED        128UL
#define CM_QN_L_RESENDS         132UL
#define CM_QN_L_MALFORMED       136UL
#define CM_QN_L_LOGINS          140UL   /* sent 15:0, failed 31:16 */
#define CM_QN_L_PEER_LOGINS     144UL   /* answered 15:0, refused 31:16 */
#define CM_QN_L_SERVICES_UP     148UL
#define CM_QN_L_SERVICE_FAILS   152UL
#define CM_QN_L_RESTARTS        156UL
#define CM_QN_L_VENDOR_TEXT_L   160UL   /* CM_QN_TEXT_L_BYTES, NUL-padded,
                                           with CM_QN_LF_TEXT_L */
#define CM_QN_L_DEVICE_TEXT_L   225UL   /* the same */
/* 290, 291 reserved, 0 */

/* 32 characters of 0x20..0x7E (any other byte written '?'), a NUL, and
 * NULs to the end. */
#define CM_QN_TEXT_BYTES        33UL
#define CM_QN_TEXT_CHARS        32UL
/* The whole texts as the parser keeps them (TBT_XDP_TEXT_MAX), the same
 * rules: 64 characters, a NUL, NULs to the end. */
#define CM_QN_TEXT_L_BYTES      65UL
#define CM_QN_TEXT_L_CHARS      64UL

#define CM_QN_LF_UUID           0x01UL  /* the peer's UUID is known */
#define CM_QN_LF_BLOCK          0x02UL  /* its properties block was read */
#define CM_QN_LF_USB4NET        0x04UL  /* it offers USB4NET */
#define CM_QN_LF_SERVICE        0x08UL  /* this link carries the service */
#define CM_QN_LF_FIRMWARE       0x10UL  /* the firmware CM's record */
#define CM_QN_LF_WIDTH_READ     0x20UL  /* the rate and lanes read by this
                                           driver (the software CM) */
#define CM_QN_LF_UNANNOUNCED    0x40UL  /* the firmware CM's record made
                                           with no announcement: found,
                                           no service (record 35 3.16) */
#define CM_QN_LF_TEXT_L         0x80UL  /* the whole texts at
                                           CM_QN_L_*_TEXT_L (an entry of
                                           CM_QN_LINK_BYTES at least) */

/* The client block: XHCI_CM_NET_STATS (src\xhci_cmnet.h) word for word,
 * its first word the block's bytes. */
#define CM_QN_CLIENT_MAX        96UL
#define CM_QN_C_BYTES           0UL
#define CM_QN_C_DRIVER          4UL     /* CM_QN_DRV_* */
#define CM_QN_C_MTU             8UL
#define CM_QN_C_FLAGS           12UL    /* CM_QN_CF_* */
#define CM_QN_C_TX_PACKETS      16UL
#define CM_QN_C_TX_BYTES_LO     20UL
#define CM_QN_C_TX_BYTES_HI     24UL
#define CM_QN_C_RX_PACKETS      28UL
#define CM_QN_C_RX_BYTES_LO     32UL
#define CM_QN_C_RX_BYTES_HI     36UL
#define CM_QN_C_TX_ERRORS       40UL
#define CM_QN_C_RX_ERRORS       44UL
#define CM_QN_C_RX_NO_BUFFER    48UL
#define CM_QN_C_TX_DOWN         52UL
#define CM_QN_C_RX_NOT_UP       56UL
#define CM_QN_C_RX_SPLIT        60UL
#define CM_QN_C_TX_FULL_WAITS   64UL
#define CM_QN_C_KNOWN_BYTES     68UL    /* the fields above */

#define CM_QN_DRV_NDIS5         1UL     /* xhcinet.sys */
#define CM_QN_DRV_NDIS6         2UL     /* xhcinet6.sys */

#define CM_QN_CF_BYTES32        0x01UL  /* the byte counts wrap at 2^32 */
#define CM_QN_CF_ASLEEP         0x02UL  /* the adapter is in D3 or paused */

#define CM_QN_MAX_BYTES (CM_QN_HDR_BYTES + CM_QN_SVC_BYTES + \
                         CM_QN_MAX_LINKS * CM_QN_LINK_BYTES + CM_QN_CLIENT_MAX)
#define CM_QN_ASK_BYTES         1024UL  /* the reader's first buffer */

#define CM_QN_UNKNOWN           0xFFFFFFFFUL

#endif /* CM_QNET_H */
