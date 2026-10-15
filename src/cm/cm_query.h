/*
 * cm_query.h - the answer of xhcicm.sys's query device to
 * IOCTL_XHCICM_QUERY (design record 25 section 4): the status record of
 * design record 24 (cm_status.h) as the connection manager's last pass
 * built it, framed by a header that says how old that pass is, and one
 * extension per router entry with what the record lacks (link rate and
 * width, link state, ids of deeper routers). Shared byte for byte by the
 * driver (cm_query.c, cm_qryos.c), XHCITR.EXE (xhcitr\tr_core.c) and the
 * host tests, so it uses no DDK or Windows type.
 *
 * Every field is a little-endian 32-bit unsigned value at a fixed offset,
 * or a byte string; the readers take bytes, never a structure overlay.
 * A layout change is a new CM_Q_VERSION: a reader refuses a version it
 * does not know.
 */

#ifndef CM_QUERY_H
#define CM_QUERY_H

/* The device: \Device\XhciCm<n>, \DosDevices\XHCICM<n>, n in 0..15, and
   the name "XHCICM<n>" as REG_SZ CM_Q_NAME_VALUE in the NHI's hardware key
   (beside XhciCmStatus). */
#define CM_Q_MAX_DEVICES        16UL
#define CM_Q_NAME_VALUE         "XhciCmQueryName"

/* CTL_CODE(FILE_DEVICE_UNKNOWN 0x22, 0xA40, METHOD_BUFFERED, FILE_ANY_ACCESS):
   no input; output an XCMQ answer. A buffer too small for the whole answer
   gets the header alone (CM_Q_HDR_BYTES, with CM_Q_O_BYTES the size
   needed) and STATUS_BUFFER_OVERFLOW; smaller than the header,
   STATUS_BUFFER_TOO_SMALL. */
#define CM_Q_IOCTL              0x00222900UL

#define CM_Q_MAGIC              0x514D4358UL    /* 'XCMQ' */
#define CM_Q_VERSION            1UL

/* The header. */
#define CM_Q_HDR_BYTES          64UL
#define CM_Q_O_MAGIC            0UL
#define CM_Q_O_VERSION          4UL
#define CM_Q_O_BYTES            8UL     /* the whole answer */
#define CM_Q_O_PASS             12UL    /* connection-manager passes completed,
                                           wraps; 0 before the first */
#define CM_Q_O_PASS_AGE_MS      16UL    /* ms since that pass completed,
                                           0xFFFFFFFF before the first */
#define CM_Q_O_LIVE             20UL    /* CM_LIVE_* (cm_live.h) */
#define CM_Q_O_FLAGS            24UL    /* CM_Q_F_* */
#define CM_Q_O_REC_OFFSET       28UL    /* the status record, CM_Q_HDR_BYTES */
#define CM_Q_O_EXT_OFFSET       32UL    /* the first router extension */
#define CM_Q_O_EXT_SHAPE        36UL    /* count 15:0, entry bytes 31:16 */
/* 40..63 reserved, 0 */

#define CM_Q_F_INERT            0x01UL  /* the thread waits with no timeout:
                                           nothing changes until a restart */
#define CM_Q_F_TRUNCATED        0x02UL  /* more routers than the record holds */

/* The answer is "fresh" for an eject (record 25 section 4.3, decision T7)
   when CM_Q_F_INERT is set or the pass is younger than this. */
#define CM_Q_FRESH_MS           3000UL

/* One router extension, in the record's router order. */
#define CM_Q_EXT_BYTES          32UL
#define CM_Q_X_RATE             0UL     /* Gbit/s per lane: 10, 20, 40;
                                           0 unknown */
#define CM_Q_X_WIDTH            4UL     /* lanes: 1, 2; 0 unknown */
#define CM_Q_X_LINK_STATE       8UL     /* the adapter's link state as read
                                           (USB4 LS field), CM_Q_UNKNOWN
                                           where not read */
#define CM_Q_X_GEN_RAW          12UL    /* the negotiated lane speed code as
                                           read, CM_Q_UNKNOWN where not read */
#define CM_Q_X_IDS              16UL    /* vendor 15:0, device 31:16 of a
                                           router the record has no ids for,
                                           0 unknown */
#define CM_Q_X_FLAGS            20UL    /* CM_Q_XF_* */
/* 24..31 reserved, 0 */

#define CM_Q_XF_RATE_READ       0x01UL  /* rate and width read from the lane
                                           adapter, not assumed */

#define CM_Q_UNKNOWN            0xFFFFFFFFUL

#endif /* CM_QUERY_H */
