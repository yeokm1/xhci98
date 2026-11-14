/*
 * tr_log.h - the pure half of XHCITR's "Driver log" nodes (design record
 * 25 sections 3.5, 4.5 and 4.6, revision 9): xhcicm.sys's log answer
 * (src\cm\cm_query.h CM_QL_*) and xhci98.sys's log and counters as its
 * USBUSER pass-through door gives them to XHCISNAP (xhcisnap\README.md,
 * src\xhci.h's snapshot header, schema 5), read as bytes and turned into
 * text. No Windows call: test\test_trlog.c links it under /Za. C89.
 *
 * Below level 4 nothing here shows an internal address: xhcicm's answer
 * withholds its address records' values and xhci98's ring holds none. At
 * level 4 both logs are shown as the drivers serve them, kernel addresses
 * included (from 3.5.0.0, the owner's decision of 2026-11-13; record 25
 * decision T15 as revised); an xhci98.sys before 3.5.0.0 serves no log at
 * 4 and the node says so. Only the ring and the HCD region's counter
 * block are ever taken out of a window; the raw extension image is never
 * kept or shown.
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
/* UsbUserMiniportError: the driver's answer when the channel is off. */
#define TR_SNAP_UU_MINIPORT_ERROR 6UL

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
 * the first window): checked and decoded with w's own header, at levels 2
 * to 4 (at 4 with the kernel addresses it holds). Returns TR_RING_*. (2,
 * level 4 withheld, is no longer returned, from 3.5.0.0.) */
#define TR_RING_SHOWN           0
#define TR_RING_EMPTY           1
#define TR_RING_OFF             3   /* level below 2 or above 4             */
#define TR_RING_BAD             4   /* the window does not hold the ring    */
int tr_hcd_ring_text(TR_TEXT *t, const TR_SNAPW *w, TR_U32 asked);

/* The HCD region's counter block (version 1), its nonzero words with
 * XHCIHC_COUNTERS' names: 1 decoded, 0 a shape this tool does not read
 * (said in t). */
int tr_hcd_counters_text(TR_TEXT *t, const unsigned char *img, size_t n);

/* Word i's name in XHCIHC_COUNTERS (tr_hcctr.h), NULL past it. */
const char *tr_hc_name(TR_U32 i);
TR_U32 tr_hc_count(void);

/* ---- xhci98.sys: a held port (roadmap-usb4 task 49.43) -----------------
 *
 * The driver's tolerance holds a root or hub port after repeated failures
 * (design record 17; src\xhci_tol.h XHCI_TOL_HOLD_*). The tool reads the
 * hold byte of each location through the same door windows the snapshot
 * engine reads (tr_snap.c print_tol): the HCD region's header for where
 * the tolerance block and its locations lie in the extension, then the
 * extension's window holding them. Read only; no new request. */

#define TR_HOLD_NONE            0
#define TR_HOLD_REENUMS         1   /* powered: a replug or a restart ends it */
#define TR_HOLD_REPOWERS        2   /* unpowered: only a controller start     */
#define TR_HOLD_OC_WAIT         3   /* unpowered, over-current never cleared  */

#define TR_TOL_HOLD_AT          26UL    /* the hold byte in a location      */
#define TR_TOL_MAX_LOCS         1024UL

typedef struct _TR_TOLMAP {
    TR_U32 TolAt;               /* the tolerance block in the extension     */
    TR_U32 TolBytes;
    TR_U32 LocBytes;            /* one location record                      */
    TR_U32 RootAt;              /* within the block                         */
    TR_U32 RootLocs;            /* root port i + 1 at location i            */
    TR_U32 HubAt;
    TR_U32 HubLocs;             /* hub i / HubPorts, port i % HubPorts + 1  */
    TR_U32 HubPorts;
} TR_TOLMAP;

/* The map from the HCD region's header (its first window's payload, n
 * bytes) and the extension's size (an extension window's RegionBytes):
 * 1 and *m when it is a shape the snapshot engine reads; 0 otherwise (a
 * driver before 2.2.0.0's tolerance, or a header that does not fit). */
int tr_tol_map(const unsigned char *hcd, size_t n, TR_U32 extBytes,
               TR_TOLMAP *m);

/* The extension offset of location loc's hold byte (root locations first,
 * then the hub ones), loc below RootLocs + HubLocs. */
TR_U32 tr_tol_hold_offset(const TR_TOLMAP *m, TR_U32 loc);

/* Location loc's hold (TR_HOLD_*) from an extension window w; -1 when w
 * is not the extension's or does not hold that byte. */
int tr_tol_hold(const TR_TOLMAP *m, const TR_SNAPW *w, TR_U32 loc);

/* The details note at a held port (owner, 2026-11-13), without its label;
 * NULL for TR_HOLD_NONE or an unknown hold:
 * 1: "This port is on hold after repeated failures. Unplug the device and
 * plug it in again. The hold also ends when the controller restarts."
 * 2 and 3: "This port is switched off after repeated failures. It stays
 * off until the controller restarts: restart Windows, or disable and
 * enable the controller in Device Manager." */
const char *tr_hold_note(int hold);

/* A hub port's hold, said at the controller (the tool cannot tell which
 * of its hubs the driver's hub number is): "A port of a hub on this
 * controller ..." with the same advice. NULL as tr_hold_note. */
const char *tr_hold_hub_note(int hold);

/* At a port the tool could not read holds for (the door closed at level
 * 0, or a read failed) whose connection is "the device failed
 * enumeration": "If this keeps failing, unplug the device and plug it in
 * again." */
extern const char tr_hold_fallback[];

/* The tree line's suffix at a held port: " (on hold)". */
extern const char tr_hold_tag[];

/* The balloon for a newly held location (owner, 2026-11-13), into out:
 * hub 0 and port the root port; hub 1 for a hub's port (port not named).
 * ctrl the controller's name, cut to fit. 1: "Port 3 of <ctrl> is on hold
 * after repeated failures. Unplug the device and plug it in again."; 2 and
 * 3: "Port 3 of <ctrl> is switched off after repeated failures. Restart
 * Windows to turn it on again."; a hub's: "A port of a hub on <ctrl> ...".
 * At most 255 characters (a balloon's szInfo). */
void tr_hold_balloon(int hold, int hub, TR_U32 port, const char *ctrl,
                     char *out, size_t cap);
#define TR_HOLD_TITLE           "USB port on hold"

/* Whether a new hold is announced (owner, 2026-11-14): on Windows 2000 and
 * later as the removal warning's option says (warn, "Warn when a device
 * is removed unexpectedly", on by default); on 98 SE and ME, where that
 * option is not offered, always. */
int tr_hold_notice_on(int nt, int warn);

/* Announce once per hold (owner, 2026-11-13): each location, keyed by its
 * controller and location number, remembers the hold last seen; a hold
 * seen where none was, or a different one, is queued once; a location seen
 * clear is cleared, so a later hold announces again; a location not read
 * keeps what it had. The queue gives one balloon at a time, oldest first,
 * and drops the newest past TR_HQ_MAX (counted). */
#define TR_HQ_SEEN              256
#define TR_HQ_MAX               16
/* Failed sends an announcement survives (tr_holdq_sent): one is tried at
 * most once a watch (10 s), so 30 is about five minutes of a shell that
 * refuses it before it is dropped (counted). */
#define TR_HQ_TRIES             30

typedef struct _TR_HQENT {
    TR_U32 Key;                 /* the controller's (tr_key's hash)        */
    TR_U32 Loc;
    int Hold;
    int Tries;                  /* failed sends so far (queue entries)     */
} TR_HQENT;

typedef struct _TR_HOLDQ {
    TR_HQENT Seen[TR_HQ_SEEN];
    int SeenN;
    TR_HQENT Q[TR_HQ_MAX];
    int QN;
    TR_U32 Dropped;
} TR_HOLDQ;

void tr_holdq_init(TR_HOLDQ *q);
/* One location read: hold its TR_HOLD_*. 1 when it was queued. */
int tr_holdq_observe(TR_HOLDQ *q, TR_U32 key, TR_U32 loc, int hold);
/* The oldest queued announcement still true (its location's last seen
 * hold the one queued; an untrue one is dropped): 1 and *e, kept queued;
 * 0 none. tr_holdq_pop removes it; tr_holdq_next is the two. */
int tr_holdq_peek(TR_HOLDQ *q, TR_HQENT *e);
void tr_holdq_pop(TR_HOLDQ *q);
int tr_holdq_next(TR_HOLDQ *q, TR_HQENT *e);
/* Every remembered and queued entry of a key not among keys[0..n) (a
 * controller no longer listed) forgotten. */
void tr_holdq_keep_keys(TR_HOLDQ *q, const TR_U32 *keys, int n);
/* The oldest queued announcement still true whose key is among
 * keys[0..n) (the controllers just read): 1, *e, removed; untrue ones
 * dropped on the way, others' left queued, so an unreadable controller
 * never holds up the rest. 0 none. */
int tr_holdq_take(TR_HOLDQ *q, const TR_U32 *keys, int n, TR_HQENT *e);
/* tr_holdq_take's choice left queued: the announcement leaves the queue
 * only when its send was accepted (tr_holdq_sent), so one that could not
 * be shown - no icon, a refused shell call - is tried again at a later
 * watch while its hold stays as it was. */
int tr_holdq_find(TR_HOLDQ *q, const TR_U32 *keys, int n, TR_HQENT *e);
/* The send of e's announcement: ok 1 removes it; ok 0 counts a failed try
 * and removes it at the TR_HQ_TRIES-th (counted in Dropped). 1 when it
 * left the queue. */
int tr_holdq_sent(TR_HOLDQ *q, const TR_HQENT *e, int ok);

/* A controller's reading taken by the job thread at readAt (GetTickCount)
 * is used at now only within maxMs of it (wrap-safe), and only when no
 * suspend, resume or device change came since that reading began (epoch,
 * counted on the tray's thread, loaded as the read began and the same at
 * the results): 1 use it, 0 drop it, the next watch reading again (task
 * 49.43). */
#define TR_HOLD_FRESH_MS        3000UL
int tr_hold_reading_usable(TR_U32 readAt, TR_U32 now, TR_U32 maxMs,
                           TR_U32 epochAtStart, TR_U32 epochNow);
/* The read order's rotation: the i-th controller (0..n-1) a job reads is
 * the (start + i) % n-th of the n listed, start counting the jobs, so each
 * controller is in turn the last one read, whose reading is fresh however
 * long the others' doors took. n 0 gives 0. The tray keeps start below
 * n (one on from the last), so it never wraps; a raw counter that did
 * would give one controller a second turn at the wrap, no worse. */
int tr_hold_rot_index(TR_U32 start, int n, int i);

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
    TR_U32 Driver;              /* the answer's DriverVersion (UASBOT.1)    */
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
#define TR_XPORT_F_SUPERSPEED       0x1UL
#define TR_XPORT_F_HC_STREAMS       0x2UL
#define TR_XPORT_F_BOT_OWN_ID       0x8UL

/* The first xhci98.sys that reads XhciBulkOnlyOwnId (UASBOT.1), in the
 * answer's DriverVersion form (major 31:24, minor 23:16, build 15:8). */
#define TR_XPORT_OWN_ID_DRIVER      0x03040200UL

/* tr_xport_unrunnable's answers. */
#define TR_XPORT_UNRUN_NO           0
#define TR_XPORT_UNRUN_OLD          1   /* a driver that has no own id    */
#define TR_XPORT_UNRUN_GATE         2   /* XhciBulkOnlyOwnId 0            */

/* The "Transport:" line's words (section 19 item 3), without the label:
 * "UAS (the device offers Bulk-Only and UAS; UAS chosen)", with
 * "(interface n)" for a function and "; and N more storage interfaces"
 * where the count says so. */
void tr_xport_text(const TR_XPORT *r, char *out, size_t cap);

/* Section 19 item 4: 1 when the record says UAS and `service` (the
 * devnode's driver: a service name, or a 9x file name) is Windows'
 * Bulk-Only usbstor, compared without case or a ".sys". */
int tr_xport_stale(const TR_XPORT *r, const char *service);

/* Section 19 item 4, the other way round: nonzero when the record says
 * Bulk-Only at SuperSpeed on a controller that cannot stream (its inputs;
 * why 5, or why 4 where XhciForceBulkOnly is set there too), offered
 * without the Bulk-Only form's own instance id (the inputs lack
 * TR_XPORT_F_BOT_OWN_ID), and `service` is the UAS driver xhciuas, which
 * cannot start the drive there; compared as tr_xport_stale compares.
 * TR_XPORT_UNRUN_GATE when the record's driver reads XhciBulkOnlyOwnId
 * (TR_XPORT_OWN_ID_DRIVER or later), so the value was 0;
 * TR_XPORT_UNRUN_OLD for an older driver, which has no such value;
 * TR_XPORT_UNRUN_NO otherwise. */
int tr_xport_unrunnable(const TR_XPORT *r, const char *service);

/* The "Driver log (xhci98)" node's text from a parsed answer: the build,
 * recording on or off with the advice, the nonzero counters by name, the
 * log (at level 4 as served, addresses included; a driver before 3.5.0.0
 * serves none there, XHQ_F_WITHHELD alone, and the text says so). nt:
 * 2000 and later. */
void tr_xhq_log_text(TR_TEXT *t, const TR_XHQ *x, int nt);

#endif /* TR_LOG_H */
