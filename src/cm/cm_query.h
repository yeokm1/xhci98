/*
 * cm_query.h - the answer of xhcicm.sys's query device to
 * IOCTL_XHCICM_QUERY (design record 25 section 4): the status record of
 * design record 24 (cm_status.h) as the connection manager's thread last
 * published it, framed by a header that says which start of which NHI it
 * belongs to and how old that pass is, then one extension per router
 * entry with what the record lacks, then the NHI's instance id. Shared
 * byte for byte by the driver (cm_query.c, cm_qryos.c), XHCITR.EXE
 * (xhcitr\tr_core.c) and the host tests, so it uses no DDK or Windows
 * type.
 *
 * Every field is a little-endian 32-bit unsigned value at a fixed offset,
 * or a byte string; the readers take bytes, never a structure overlay.
 * A layout change is a new CM_Q_VERSION: a reader refuses a version it
 * does not know.
 */

#ifndef CM_QUERY_H
#define CM_QUERY_H

/* The device: \Device\XhciCm<n>, linked as \DosDevices\XHCICM<n> (created
   in the start IRP's system context, so global on XP and later), n the
   first free of 0..15; its name "XHCICM<n>" as REG_SZ CM_Q_NAME_VALUE in
   the NHI's hardware key, written after the link and deleted before it.
   A reader trusts the mapping only when the answer's instance id equals
   the devnode it read the value from (CM_Q_O_ID_*). */
#define CM_Q_MAX_DEVICES        16UL
#define CM_Q_NAME_VALUE         "XhciCmQueryName"

/* CTL_CODE(FILE_DEVICE_UNKNOWN 0x22, 0xA40, METHOD_BUFFERED, FILE_ANY_ACCESS),
   opened with no access rights; no input.
   - output buffer < CM_Q_HDR_BYTES: STATUS_BUFFER_TOO_SMALL, nothing
     returned;
   - output buffer < the whole answer: STATUS_SUCCESS with the header alone
     (Information = CM_Q_HDR_BYTES), CM_Q_O_BYTES the size needed; the
     reader asks again with that size (bounded by CM_Q_MAX_BYTES);
   - otherwise STATUS_SUCCESS, Information = CM_Q_O_BYTES.
   A reader parses no more than Information bytes. A query device whose NHI
   has stopped or gone answers every request with STATUS_DEVICE_REMOVED
   (STATUS_NO_SUCH_DEVICE on Windows 98 SE and ME) and no data. */
#define CM_Q_IOCTL              0x00222900UL
#define CM_Q_MAX_BYTES          65536UL
#define CM_Q_ASK_BYTES          8192UL  /* the reader's first buffer */

#define CM_Q_MAGIC              0x514D4358UL    /* 'XCMQ' */
#define CM_Q_VERSION            1UL

/* The header. */
#define CM_Q_HDR_BYTES          64UL
#define CM_Q_O_MAGIC            0UL
#define CM_Q_O_VERSION          4UL
#define CM_Q_O_BYTES            8UL     /* the whole answer */
#define CM_Q_O_PASS             12UL    /* snapshots published by the thread
                                           since this start, counted after
                                           each publish; 0 before the
                                           first; wraps past 0 to 1 */
#define CM_Q_O_PASS_AGE_MS      16UL    /* ms from that publish to this
                                           answer, from a running clock;
                                           CM_Q_UNKNOWN before the first */
#define CM_Q_O_LIVE             20UL    /* CM_LIVE_* (cm_live.h) */
#define CM_Q_O_FLAGS            24UL    /* CM_Q_F_* */
#define CM_Q_O_REC_OFFSET       28UL    /* the status record */
#define CM_Q_O_EXT_OFFSET       32UL    /* the first router extension */
#define CM_Q_O_EXT_SHAPE        36UL    /* count 15:0, entry bytes 31:16 */
#define CM_Q_O_EPOCH_LO         40UL    /* this start of this NHI: the system */
#define CM_Q_O_EPOCH_HI         44UL    /* time at the query device's creation,
                                           made unique per boot; never 0 */
#define CM_Q_O_NHI_BDF          48UL    /* the NHI's bus 15:8, device 7:3,
                                           function 2:0, as the record's
                                           port words */
#define CM_Q_O_ID_OFFSET        52UL    /* the NHI's device instance id, */
#define CM_Q_O_ID_BYTES         56UL    /* ASCII, no NUL, at most 200 */
#define CM_Q_O_CONTENT_GEN      60UL    /* bumped when anything in the answer
                                           but the pass counter, its age and
                                           the record's own generation,
                                           uptime, time and echo changes;
                                           never 0; per start *\/

#define CM_Q_F_INERT            0x01UL  /* the thread waits with no timeout */
#define CM_Q_F_TRUNCATED        0x02UL  /* more routers than the record holds */
#define CM_Q_F_STOPPING         0x04UL  /* the NHI is stopping: refuse ejects */

/* Display only: an answer older than this is shown as "not current".
   An eject never relies on age (design record 25 section 6.2): it waits
   for CM_Q_O_PASS to move at least CM_Q_NEW_PASSES past the value it
   read when it began (wrap-safe), so the snapshot was built after. */
#define CM_Q_NEW_PASSES         2UL
#define CM_Q_CURRENT_MS         3000UL
#define CM_Q_NEW_PASS_WAIT_MS   3000UL

/* One router extension, in the record's router order. */
#define CM_Q_EXT_BYTES          32UL
#define CM_Q_X_RATE             0UL     /* Gbit/s per lane: 10, 20, 40;
                                           0 unknown */
#define CM_Q_X_WIDTH            4UL     /* lanes: 1, 2; 0 unknown */
#define CM_Q_X_LINK_STATE       8UL     /* the lane adapter's link state as
                                           read, CM_Q_UNKNOWN where not */
#define CM_Q_X_GEN_RAW          12UL    /* the negotiated lane speed code as
                                           read, CM_Q_UNKNOWN where not */
#define CM_Q_X_IDS              16UL    /* vendor 15:0, device 31:16 the
                                           connection manager holds for a
                                           router the record has no ids
                                           for; 0 unknown */
#define CM_Q_X_FLAGS            20UL    /* CM_Q_XF_* */
/* 24..31 reserved, 0 */

#define CM_Q_XF_RATE_READ       0x01UL  /* rate and width read from the lane
                                           adapter by the connection manager
                                           on its own path, not assumed */

#define CM_Q_UNKNOWN            0xFFFFFFFFUL

#endif /* CM_QUERY_H */
