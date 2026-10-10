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

#endif /* XHCI_XPIPE_H */
