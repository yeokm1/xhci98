/*
 * xhci_xpipe.h - stage 2 of the transfer size, its pure half (roadmap-hcd.md
 * task 36.24; design record 13 section 11.6): two TDs of one bulk URB on
 * the ring at once, cut from one mapping of the whole URB.
 *
 * An eligible URB is mapped once into a wide scatter/gather list - one
 * element per page or part of a page, in order, covering the URB - and its
 * TDs are cut from that list here, each at most XHCI_XPIPE_TD_ELEMENTS
 * elements (half the usable TRBs of every ring this driver has, so two TDs
 * of one URB always fit an empty ring) and, when it is not the URB's last,
 * ending on a packet boundary. A TD may end inside an element; the next one
 * starts there. The cursor (element index and the bytes of it already
 * planned) is the record's, so a cut never walks the list from its start.
 *
 * DDK-free: part of the pure core. C89. IRQL: any; the callers hold the
 * controller lock over a record's cursor.
 */

#ifndef XHCI_XPIPE_H
#define XHCI_XPIPE_H

#include "xhci_compat.h"
#include "xhci_usbport.h"

/* A TD's elements at most: half of a 64-TRB ring's 62 usable TRBs. */
#define XHCI_XPIPE_TD_ELEMENTS      31UL

/* Where the next TD starts in a wide list: the element and the bytes of it
 * earlier TDs took. */
typedef struct _XHCI_XPIPE_CURSOR {
    ULONG Element;
    ULONG Skip;
} XHCI_XPIPE_CURSOR, *PXHCI_XPIPE_CURSOR;

/* One TD cut from a wide list. */
typedef struct _XHCI_XPIPE_TD {
    ULONG First;                /* its first element                   */
    ULONG FirstSkip;            /* bytes of that element not its own    */
    ULONG Count;                /* elements it touches, 1 to 31         */
    ULONG Bytes;                /* its length                           */
} XHCI_XPIPE_TD, *PXHCI_XPIPE_TD;

#define XHCI_XPIPE_OK               0UL
#define XHCI_XPIPE_DONE             1UL /* nothing left to plan             */
#define XHCI_XPIPE_BAD_LIST         2UL /* an element of 0 bytes, the list
                                         * shorter than the URB, a cursor
                                         * past it                          */
#define XHCI_XPIPE_NO_PACKET        3UL /* 31 elements hold less than one
                                         * packet: cannot cut a TD that is
                                         * not the last                     */

/*
 * The next TD of a URB of `length` bytes whose wide list is `el[0..count)`,
 * `planned` bytes of it already in TDs, the cursor at `*cur`, the pipe's
 * packet size `mps` (non-zero) and at most `maxElements` (1 to 31)
 * elements a TD. Fills *td and moves *cur past it on XHCI_XPIPE_OK.
 */
ULONG XhciXpipePlan(const USBPORT_SCATTER_GATHER_ELEMENT *el, ULONG count,
                    ULONG length, ULONG planned, ULONG mps,
                    ULONG maxElements, PXHCI_XPIPE_CURSOR cur,
                    PXHCI_XPIPE_TD td);

/*
 * The TD's own list for the engine: `td->Count` elements copied from `el`
 * into `out` (room for XHCI_XPIPE_TD_ELEMENTS), the first moved on by
 * FirstSkip and the last cut to the TD's length, each SgOffset the byte's
 * offset within the TD. Returns the element count, or 0 when `td` does not
 * fit `el` or `out`.
 */
ULONG XhciXpipeBuild(const USBPORT_SCATTER_GATHER_ELEMENT *el, ULONG count,
                     const XHCI_XPIPE_TD *td,
                     PUSBPORT_SCATTER_GATHER_LIST out, ULONG outRoom);


/* ----------------------------------------------------------------------- */
/* The rest of stage 2's pure half (design record 13 section 11.6)          */
/* ----------------------------------------------------------------------- */

/* A wide list's elements at most: a 2 MiB URB at any alignment. */
#define XHCI_XPIPE_WIDE_ELEMENTS    513UL
/* The page the mapper cuts at, on both architectures. */
#define XHCI_XPIPE_PAGE             4096UL

/*
 * Eligibility (point 1), judged once when a record is filled: a bulk pipe,
 * more than one chunk at today's plan, OUT on any bulk pipe and IN only on
 * a stream, the URB's page span at most a wide list's and the map grant,
 * and a free entry. `span` is ADDRESS_AND_SIZE_TO_SPAN_PAGES of the URB.
 */
#define XHCI_XPIPE_ELIG_OK          0UL
#define XHCI_XPIPE_ELIG_TYPE        1UL /* control, interrupt or isoch   */
#define XHCI_XPIPE_ELIG_ONE_CHUNK   2UL /* fits one TD today             */
#define XHCI_XPIPE_ELIG_DIRECTION   3UL /* IN without streams            */
#define XHCI_XPIPE_ELIG_SPAN        4UL /* over the entry or the grant   */
#define XHCI_XPIPE_ELIG_NO_ENTRY    5UL /* the pool empty or all taken   */

ULONG XhciXpipeEligible(ULONG bulk, ULONG in, ULONG stream, ULONG chunk,
                        ULONG length, ULONG span, ULONG grant,
                        ULONG entryFree);

/*
 * Promotion (point 2), inside the first channel request's routine: a NULL
 * base, the adapter's grant at this start exactly XHCI_XPIPE_FREE_GRANT -
 * the register-free adapter's signature on every HAL read (11.6.1:
 * BYTES_TO_PAGES(0x7FFFFFFF) + 1) - and promotion enabled.
 */
#define XHCI_XPIPE_FREE_GRANT       0x80001UL
#define XHCI_XPIPE_PROMOTE          0UL
#define XHCI_XPIPE_PROMOTE_BASE     1UL /* the HAL handed registers      */
#define XHCI_XPIPE_PROMOTE_OFF      2UL /* not enabled at this start     */
#define XHCI_XPIPE_PROMOTE_GRANT    3UL /* another grant: not register-free */

ULONG XhciXpipePromote(ULONG baseNull, ULONG grant, ULONG enabled);

/*
 * One physically contiguous run the HAL mapped, appended to a list cut at
 * page bounds - one element per page or part of one - at URB offset
 * `offset`. Returns 0, the list untouched past *count, when it would hold
 * more than `cap` elements or the run is empty or crosses 4 GiB.
 */
ULONG XhciXpipeAppendRun(PUSBPORT_SCATTER_GATHER_ELEMENT el, PULONG count,
                         ULONG cap, ULONG pa, ULONG run, ULONG offset);

/*
 * The register-free check's verdict (point 2): `a` mapped by a 1-register
 * request, `b` by an ordinary one, each over `bytes`; both bases NULL, both
 * lists covering the bytes, and element for element the same.
 */
#define XHCI_XPIPE_CHECK_NOT_RUN    0UL /* the switch off                */
#define XHCI_XPIPE_CHECK_PASS       1UL
#define XHCI_XPIPE_CHECK_BASE       2UL /* a base was not NULL           */
#define XHCI_XPIPE_CHECK_DIFFER     3UL /* the lists name other bytes    */
#define XHCI_XPIPE_CHECK_SHORT      4UL /* a list fell short of `bytes`  */
#define XHCI_XPIPE_CHECK_NO_MEMORY  5UL /* the buffer, its MDL, the pool */
#define XHCI_XPIPE_CHECK_CHANNEL    6UL /* a request refused or late     */
#define XHCI_XPIPE_CHECK_NOT_NT     7UL /* Windows 98 or ME: never runs  */
#define XHCI_XPIPE_CHECK_GRANT      8UL /* the grant is not 0x80001: no
                                         * mapping tried                 */

ULONG XhciXpipeCheckCompare(const USBPORT_SCATTER_GATHER_ELEMENT *a,
                            ULONG na, ULONG baseANull,
                            const USBPORT_SCATTER_GATHER_ELEMENT *b,
                            ULONG nb, ULONG baseBNull, ULONG bytes);

/*
 * A pipelined record's life (point 6) as a pure machine; the driver keeps
 * one per record holding a promoted mapping and calls these under the
 * controller lock. TDs are numbered from 0 in publication order, which is
 * ring order; TD n lives in slot n % 2. `Limit` is the byte boundary: TDs
 * numbered below it count, a TD that moved less than it asked for or ended
 * in error setting it to its own number + 1, a failure before the ring to
 * the TDs already published. The status (`Outcome`) is decided apart: the
 * first latched wins, except that an early end on the ring at a TD before a
 * latched publication failure's position replaces that failure.
 */
#define XHCI_XPIPE_NO_LIMIT         0xFFFFFFFFUL

#define XHCI_XPIPE_MAP_NOT_QUEUED   0UL /* held before its mapping       */
#define XHCI_XPIPE_MAP_QUEUED       1UL /* on the pump's list            */
#define XHCI_XPIPE_MAP_IN_ROUTINE   2UL /* the HAL called the routine    */
#define XHCI_XPIPE_MAP_ALLOCATED    3UL /* the routine returned, keeping */
#define XHCI_XPIPE_MAP_REFUSED      4UL /* nothing was ever acquired     */

#define XHCI_XPIPE_OUT_NONE         0UL
#define XHCI_XPIPE_OUT_STOP         1UL /* cancel, abort, reset, removal */
#define XHCI_XPIPE_OUT_END          2UL /* a short, an error, a verdict  */
#define XHCI_XPIPE_OUT_PREFAIL      3UL /* a mapping or publication      */

typedef struct _XHCI_XPIPE_REC {
    ULONG Length;               /* the URB's bytes                      */
    ULONG Planned;              /* bytes put in TDs so far              */
    ULONG Committed;            /* bytes of TDs retired before Limit    */
    ULONG Published;            /* TDs published                        */
    ULONG Retired;              /* TDs retired, in ring order           */
    ULONG Limit;                /* TDs numbered below it count          */
    ULONG NoMore;               /* no further TD is published           */
    ULONG Truncating;           /* a successor must be taken off        */
    ULONG Finalized;            /* the finalizer fired                  */
    ULONG MapState;             /* XHCI_XPIPE_MAP_*                     */
    ULONG OutcomeKind;          /* XHCI_XPIPE_OUT_*                     */
    ULONG OutcomeAt;            /* the TD, or the failure's position    */
    LONG Outcome;               /* the status latched                   */
    ULONG TdBytes[2];           /* each slot's TD length                */
} XHCI_XPIPE_REC, *PXHCI_XPIPE_REC;

#define XhciXpipeOnRing(r)          ((r)->Published - (r)->Retired)

/* What a step asks of the driver. */
#define XHCI_XPIPE_F_REFILL         0x01UL /* plan and publish the next  */
#define XHCI_XPIPE_F_TRUNCATE       0x02UL /* hand the successor to the
                                            * thread: an early end with
                                            * one still queued           */
#define XHCI_XPIPE_F_FINAL          0x04UL /* complete it, once          */
#define XHCI_XPIPE_F_OVERLAP        0x08UL /* the next TD was on the ring
                                            * as this one retired        */
#define XHCI_XPIPE_F_TRUNC_END      0x10UL /* the successor has left     */
#define XHCI_XPIPE_F_SEALED         0x20UL /* this step set the boundary */
#define XHCI_XPIPE_F_ORDER          0x40UL /* a retirement out of ring
                                            * order, or none on the ring */
#define XHCI_XPIPE_NO_SLOT          0xFFFFFFFFUL

VOID XhciXpipeRecInit(PXHCI_XPIPE_REC r, ULONG length, ULONG mapState);
/* A TD of `bytes` published: its slot, or XHCI_XPIPE_NO_SLOT (nothing
 * changed) when two are on the ring, NoMore is set or the bytes do not
 * fit what is left. */
ULONG XhciXpipeRecPublish(PXHCI_XPIPE_REC r, ULONG bytes);
/* The oldest TD retired with `moved` bytes and `status` (0 success).
 * `removed` is 1 when software took it off (the thread's cancel walk, a
 * drain) rather than the engine ending it: its progress counts and a TD
 * that did not finish still seals the boundary, but its status is offered
 * as a stop's - it never replaces a publication failure as a device's end
 * would - and it hands no successor over: the remover takes the rest. */
ULONG XhciXpipeRecRetire(PXHCI_XPIPE_REC r, ULONG slot, ULONG moved,
                         LONG status, ULONG removed);
/* A cancel, abort, reset or removal (XHCI_XPIPE_OUT_STOP), or a failure
 * before the ring (XHCI_XPIPE_OUT_PREFAIL), with its status. */
ULONG XhciXpipeRecStop(PXHCI_XPIPE_REC r, ULONG kind, LONG status);
/* The mapping moved to `mapState`; XHCI_XPIPE_F_FINAL when that makes it
 * final. */
ULONG XhciXpipeRecMapState(PXHCI_XPIPE_REC r, ULONG mapState);
/* The status the URB reports: the latched outcome - a cancel (a stop
 * whose status is `cancelStatus`) that raced every byte's move is
 * success, as today's path reports it; any other stop keeps its status
 * (a removal at a device's going, a fault's abort) - and a success short
 * of the length the client did not allow is `shortStatus`. */
LONG XhciXpipeRecStatus(const XHCI_XPIPE_REC *r, ULONG shortOk,
                        LONG shortStatus, LONG cancelStatus);

/*
 * Stage 2's counters (point 7), in the snapshot's HCD header after the
 * words 36.21 and 36.9 took (XHCI_SNAPSHOT_HCD_XPIPE_AT), whole and in this
 * order; all ULONGs, under the controller lock.
 */
typedef struct _XHCI_XPIPE_STATS {
    ULONG Switch;               /* XhciWideMap as read, ~0 absent       */
    ULONG Enabled;              /* promotion enabled at this start      */
    ULONG Check;                /* XHCI_XPIPE_CHECK_*                   */
    ULONG Pool;                 /* wide entries allocated               */
    ULONG InUse;                /* entries held now                     */
    ULONG Claimed;              /* entries claimed by eligible URBs     */
    ULONG AwaySpan;             /* eligible but over the span limits    */
    ULONG AwayPool;             /* eligible but no free entry           */
    ULONG Promoted;             /* whole-URB mappings                   */
    ULONG NoPromoteBase;        /* first mapping handed registers       */
    ULONG NoPromoteOff;         /* promotion not enabled, or the grant
                                 * not the register-free one           */
    ULONG NoPromoteRun;         /* a run of the whole URB did not map   */
    ULONG Pipelined;            /* URBs with two TDs outstanding once   */
    ULONG Tds;                  /* TDs cut from wide lists              */
    ULONG Refills;              /* published in a retirement            */
    ULONG HeldPause;            /* refills held by a pause              */
    ULONG HeldBusy;             /* refills held by a busy ring          */
    ULONG EarlyShort;           /* early ends with a successor queued   */
    ULONG EarlyError;
    ULONG EarlyVerdict;
    ULONG Taken;                /* successors taken off by the thread   */
    ULONG Overlap;              /* the next TD ready as one retired     */
    ULONG PreRing;              /* failures before a TD reached it      */
    ULONG Order;                /* retirements out of ring order        */
} XHCI_XPIPE_STATS, *PXHCI_XPIPE_STATS;

#define XHCI_XPIPE_STATS_WORDS      24UL

#endif /* XHCI_XPIPE_H */
