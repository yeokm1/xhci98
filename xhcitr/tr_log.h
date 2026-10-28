/*
 * tr_log.h - the pure half of XHCITR's "Driver log" nodes (design record
 * 25 sections 3.5, 4.5 and 4.6, revision 9): xhcicm.sys's log answer
 * (src\cm\cm_query.h CM_QL_*) and xhci98.sys's log and counters as its
 * USBUSER pass-through door gives them to XHCISNAP (xhcisnap\README.md,
 * src\xhci.h's snapshot header, schema 5), read as bytes and turned into
 * text. No Windows call: test\test_trlog.c links it under /Za. C89.
 *
 * Nothing here shows an internal address: xhcicm's answer carries none
 * (the driver withholds its three address records), and xhci98's ring is
 * shown only at XhciLogVerbosity 2 and 3 - at 4 it holds kernel
 * addresses and is withheld (decision T15). Only the ring and the HCD
 * region's counter block are ever taken out of a window; the raw
 * extension image is never kept or shown.
 */

#ifndef TR_LOG_H
#define TR_LOG_H

#include "tr_core.h"

/* ---- xhcicm.sys: the log answer (CM_QL_*) ----------------------------- */

typedef struct _TR_QLOG {
    TR_U32 Bytes;               /* CM_QL_O_BYTES                            */
    TR_U32 Flags;               /* CM_QL_F_*                                */
    TR_U32 Verbosity;           /* XhciLogVerbosity applied                 */
    TR_U32 VerbosityRead;
    TR_U32 EpochLo;
    TR_U32 EpochHi;
    const unsigned char *Ctr;   /* the counters' text                       */
    TR_U32 CtrBytes;
    const unsigned char *Log;   /* the log's text, oldest first             */
    TR_U32 LogBytes;
    TR_U32 Appends;
    TR_U32 Dropped;
    TR_U32 Pass;
    TR_U32 AgeMs;
} TR_QLOG;

/* An answer of n bytes (no more than the call returned). 1 a whole,
 * consistent answer; -1 the header alone, or a size field asking for more
 * than n (l->Bytes says how much, at most CM_QL_ASK_BYTES); 0 anything
 * else: wrong magic or version, or a counter or log part outside the
 * answer or past its bound. */
int tr_qlog_parse(const unsigned char *b, size_t n, TR_QLOG *l);

/* The node's text from a parsed answer: recording on or off (and the
 * advice, with keyPath, the NHI's driver key under HKEY_LOCAL_MACHINE),
 * the nonzero counters, the log. nt: 2000 and later. */
void tr_qlog_text(TR_TEXT *t, const TR_QLOG *l, const char *keyPath, int nt);

/* How to turn xhcicm's recording on (record 25 section 3.5). */
void tr_log_advice_cm(TR_TEXT *t, const char *keyPath, int nt);

/* ---- xhci98.sys: the pass-through door (as xhcisnap.c's take_window) --- */

#define TR_SNAP_IOCTL           0x00220438UL    /* IOCTL_USB_USER_REQUEST */
#define TR_SNAP_PASS_THRU       3UL             /* USBUSER_PASS_THRU      */
#define TR_UU_REQUEST           0x00UL
#define TR_UU_STATUS            0x04UL
#define TR_UU_REQUEST_BYTES     0x08UL
#define TR_UU_ACTUAL_BYTES      0x0CUL
#define TR_UU_GUID              0x10UL
#define TR_UU_PARAM_BYTES       0x20UL
#define TR_UU_PARAMETERS        0x24UL
#define TR_SNAP_PARAM_BYTES     0xF000UL
#define TR_SNAP_BUFFER_BYTES    (TR_UU_PARAMETERS + TR_SNAP_PARAM_BYTES)
#define TR_SNAP_REQ_SIGNATURE   0x514E5358UL    /* 'XSNQ' */
#define TR_SNAP_SIGNATURE       0x504E5358UL    /* 'XSNP' */
#define TR_SNAP_SCHEMA          5UL
#define TR_SNAP_HDR_BYTES       152UL           /* 38 ULONGs, schema 5    */
#define TR_SNAP_REGION_EXT      0UL
#define TR_SNAP_REGION_HCD      3UL
#define TR_SNAP_HCD_MAX_BYTES   32768UL
#define TR_SNAP_UU_NOT_SUPPORTED 6UL            /* the channel is off      */

/* XHCI_SNAPSHOT_S_* */
#define TR_SNAP_S_TRUNCATED     0x01UL
#define TR_SNAP_S_BAD_REQUEST   0x02UL
#define TR_SNAP_S_BAD_EXTENSION 0x04UL
#define TR_SNAP_S_BAD_REGION    0x08UL
#define TR_SNAP_S_PAST_END      0x10UL

/* The whole request buffer (TR_SNAP_BUFFER_BYTES) for one window of
 * `region` at `offset`, as take_window fills it. */
void tr_snap_request(unsigned char *buf, TR_U32 region, TR_U32 offset);

typedef struct _TR_SNAPW {
    TR_U32 UuStatus;            /* the door's own status; 0 answered        */
    TR_U32 Status;              /* TR_SNAP_S_*                               */
    TR_U32 Region;
    TR_U32 Offset;
    TR_U32 RegionBytes;
    TR_U32 PayloadBytes;
    TR_U32 Flavour;             /* XHCI_SNAPSHOT_FLAVOUR_*                   */
    TR_U32 VerbosityRead;
    TR_U32 VerbosityApplied;
    TR_U32 RingOffset;
    TR_U32 RingBytes;
    TR_U32 RingHead;
    TR_U32 RingUsed;
    const unsigned char *Payload;
} TR_SNAPW;

#define TR_SW_OK                0   /* a window, w filled                   */
#define TR_SW_OFF               1   /* the door answered 6: channel off     */
#define TR_SW_REFUSED           2   /* another door status (w->UuStatus)    */
#define TR_SW_BAD               3   /* no signature, another schema, short  */

/* One window's answer, `returned` bytes of the request buffer. */
int tr_snap_window(const unsigned char *buf, size_t returned, TR_SNAPW *w);

/* xhci98's state lines: the flavour, recording on or off and why, the
 * advice when off. From the first window's header. */
void tr_hcd_head_text(TR_TEXT *t, const TR_SNAPW *w, int nt);

/* The door answered that its channel is off (level 0). */
void tr_hcd_off_text(TR_TEXT *t, int nt);

/* How to turn xhci98's recording on (SNAPON.BAT). */
void tr_log_advice_hcd(TR_TEXT *t, int nt);

/* The ring from the window w taken at `asked` (the ring's offset, from
 * the first window): checked and decoded with w's own header. Level 4
 * withheld. Returns TR_RING_*. */
#define TR_RING_SHOWN           0
#define TR_RING_EMPTY           1
#define TR_RING_WITHHELD        2   /* level 4: kernel addresses            */
#define TR_RING_OFF             3   /* level below 2: not recording         */
#define TR_RING_BAD             4   /* the window does not hold the ring    */
int tr_hcd_ring_text(TR_TEXT *t, const TR_SNAPW *w, TR_U32 asked);

/* The HCD region's counter block (version 1), its nonzero words with
 * XHCIHC_COUNTERS' names: 1 decoded, 0 a shape this tool does not read
 * (said in t). */
int tr_hcd_counters_text(TR_TEXT *t, const unsigned char *img, size_t n);

/* Word i's name in XHCIHC_COUNTERS (tr_hcctr.h), NULL past it. */
const char *tr_hc_name(TR_U32 i);
TR_U32 tr_hc_count(void);

/* ---- xhci98.sys: its query request (src\xhci_qry.h XHQ_*) ---------------
 *
 * Since revision 12 (the owner's logging-consistency decision of
 * 2026-10-16) the "Driver log (xhci98)" node and the PCI configuration node
 * read xhci98 through IOCTL_XHCI98_QUERY, as xhcicm is read; a driver that
 * refuses it is read through the door as before (the log) or said to be
 * older (PCI). */

typedef struct _TR_XHQ {
    TR_U32 Bytes;
    TR_U32 Flags;               /* XHQ_F_*                                  */
    TR_U32 Verbosity;
    TR_U32 VerbosityRead;
    const unsigned char *Ctr;   /* the counter block's words                */
    TR_U32 CtrBytes;
    const unsigned char *Log;   /* the log's text, oldest first             */
    TR_U32 LogBytes;
    const unsigned char *Pci;   /* 256 bytes, or NULL                       */
    TR_U32 PciBytes;
    TR_U32 PciState;            /* XHQ_PCI_*                                */
    TR_U32 Appends;
    TR_U32 Dropped;
    TR_U32 Flavour;
    /* Version 2 (3.3.0.0; record 25 sections 18 and 19); 0 and NULL in a
     * version 1 answer. */
    TR_U32 Version;             /* XHQ_VERSION_1 to XHQ_VERSION             */
    TR_U32 DriverVersion;       /* XHQ_O_DRIVER_VERSION                     */
    const unsigned char *Xport; /* the transport part's records             */
    TR_U32 XportCount;
    TR_U32 XportRecord;         /* one record's bytes, at least 40          */
    /* Version 3 (roadmap-usb4 44.80): 0 and NULL in an older answer. */
    const unsigned char *Ports; /* root port n's XHQ_PT_* at n - 1          */
    TR_U32 PortCount;
} TR_XHQ;

/* An answer of n bytes, version 1, 2 or 3. 1 whole and consistent; -1 the
 * header alone or a size above n (x->Bytes the size, at most
 * XHQ_MAX_BYTES); 0 anything else: magic, version, a part outside the
 * answer or past its bound, a counter part not whole words, a PCI part
 * not 0 or 256 bytes, a transport part not whole records or above
 * XHQ_XPORT_MAX of them, a ports part above XHQ_PORTS_MAX bytes. */
int tr_xhq_parse(const unsigned char *b, size_t n, TR_XHQ *x);

/* The running driver's version from a header (at least XHQ_HDR_BYTES_V1
 * bytes, n of them): 1 and *packed for a version 2 header of n >=
 * XHQ_HDR_BYTES_V2 or a version 3 one of n >= XHQ_HDR_BYTES; 0 for a
 * version 1 header (a driver before 3.3.0.0); -1 for anything else (not an
 * answer of this driver). */
int tr_xhq_driver(const unsigned char *b, size_t n, TR_U32 *packed);

/* Root port `port`'s XHQ_PT_* from a parsed answer (roadmap-usb4 44.80);
 * -1 when the answer has no ports part (a driver before it, or x NULL) or
 * the port is past it. */
int tr_xhq_port_top(const TR_XHQ *x, TR_U32 port);

/* One storage transport record (record 25 section 19). */
typedef struct _TR_XPORT {
    TR_U32 Slot;
    TR_U32 Port;
    TR_U32 Route;
    TR_U32 Iface;               /* 0xFF a device PDO                        */
    TR_U32 Transport;           /* XHCI_XPORT_NONE .. _REFUSED              */
    TR_U32 Why;                 /* XHCI_XPORT_WHY_*                         */
    TR_U32 Alternate;
    TR_U32 Inputs;              /* XHCI_XPORT_F_*                           */
    TR_U32 Triple;              /* class 7:0, subclass 15:8, protocol 23:16 */
    TR_U32 Count;
} TR_XPORT;

/* The record of root port `port` and route `route`: 1 and *r; 0 none. */
int tr_xhq_xport_find(const TR_XHQ *x, TR_U32 port, TR_U32 route,
                      TR_XPORT *r);

/* xhci_xport.h's values, which that header cannot give a tool; test_trlog
 * holds them equal. */
#define TR_XPORT_NONE               0UL
#define TR_XPORT_BOT                1UL
#define TR_XPORT_UAS                2UL
#define TR_XPORT_REFUSED            3UL
#define TR_XPORT_WHY_NOT_UAS        0UL
#define TR_XPORT_WHY_UAS            1UL
#define TR_XPORT_WHY_UAS_ONLY       2UL
#define TR_XPORT_WHY_UAS_ONLY_FORCED 3UL
#define TR_XPORT_WHY_BOT_FORCED     4UL
#define TR_XPORT_WHY_BOT_NO_STREAMS 5UL
#define TR_XPORT_WHY_NO_STREAMS     6UL

/* The "Transport:" line's words (section 19 item 3), without the label:
 * "UAS (the device offers Bulk-Only and UAS; UAS chosen)", with
 * "(interface n)" for a function and "; and N more storage interfaces"
 * where the count says so. */
void tr_xport_text(const TR_XPORT *r, char *out, size_t cap);

/* Section 19 item 4: 1 when the record says UAS and `service` (the
 * devnode's driver: a service name, or a 9x file name) is Windows'
 * Bulk-Only usbstor, compared without case or a ".sys". */
int tr_xport_stale(const TR_XPORT *r, const char *service);

/* The "Driver log (xhci98)" node's text from a parsed answer: the build,
 * recording on or off with the advice, the nonzero counters by name, the
 * log (level 4 withheld). nt: 2000 and later. */
void tr_xhq_log_text(TR_TEXT *t, const TR_XHQ *x, int nt);

#endif /* TR_LOG_H */
