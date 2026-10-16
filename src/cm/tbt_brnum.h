/*
 * tbt_brnum.h - bus numbers for the bridges a tunnel brought up below a
 * tunnel port, on Windows 98 SE and ME (design record 21 section 6.1.1;
 * XhciCmBridgeNumbering).
 *
 * WHY. Windows 98's PCI bus driver (PCI.VXD) leaves a PCI-to-PCI bridge
 * that arrives after the start in the state it came up in: secondary and
 * subordinate bus 0, so nothing behind it is reachable. On the owner's
 * ThinkPad P14s Gen 1 the ASMedia ASM2464 enclosure's switch upstream port
 * then shows Code 29 (CM_PROB_HARDWARE_DISABLED, an empty allocation) and
 * its NVMe never appears; in the emulator PCI.VXD numbered one level per
 * restart prompt, left no room for the next and placed a window outside
 * the parent's (record 21 section 6.1.1). The BIOS numbers the chain at
 * POST, which is why the same enclosure works when present at power-on.
 * This file does what the BIOS did, for a chain below one tunnel port:
 * depth first, inside the port's own secondary..subordinate range.
 *
 * WHAT IT WRITES. The bus-number register (offset 18h: primary 7:0,
 * secondary 15:8, subordinate 23:16; the secondary latency timer in 31:24
 * is written back as read) of type 1 functions below the port, only when
 * every bridge on the port's secondary bus reads secondary and
 * subordinate 0 (a chain that came up in reset state), and nothing
 * outside the port's range. With XHCI_TBT_BRNUM_F_WINDOWS, then: each
 * memory BAR behind the chain sized (all-ones written, read back, the
 * value read written back and checked), and each numbered bridge's memory
 * window (20h) placed inside the port's own window, its I/O (1Ch) and
 * prefetchable (24h) windows closed and its Memory Space and Bus Master
 * Enable (04h) set. Before each write the bridge is read again and must
 * still be the function journalled (identity, a type 1 header), and the
 * caller's go callback must still allow it; each bus-number write is
 * journalled before it is issued and read back after. A refusal after the
 * first write writes everything journalled back as it was read, deepest
 * first, each only while the bridge still reads as the one journalled,
 * and counts what could not be put back; a window refusal keeps the bus
 * numbers.
 *
 * Pure: the caller supplies the configuration-space accessors (tbt_topo.h's
 * XHCI_TBT_CFG; Write is used for dword writes at 18h alone) and owns the
 * IRQL they run at. Host suite: test\test_tbt_brnum.c.
 */

#ifndef TBT_BRNUM_H
#define TBT_BRNUM_H

#include "tbt_topo.h"

/* Bounds: the depth of bridges below the port, and how many it numbers. */
#define XHCI_TBT_BRNUM_MAX_DEPTH    6UL
#define XHCI_TBT_BRNUM_MAX_BRIDGES  16UL

/* XHCI_TBT_BRNUM.Outcome. */
#define XHCI_TBT_BRNUM_NONE         0UL /* no bridge on the secondary bus     */
#define XHCI_TBT_BRNUM_DONE         1UL /* the chain numbered                 */
#define XHCI_TBT_BRNUM_NUMBERED     2UL /* a bridge already numbered: nothing
                                           written                          */
#define XHCI_TBT_BRNUM_PORT         3UL /* the port's own range unreadable or
                                           not a range below it            */
#define XHCI_TBT_BRNUM_NO_ROOM      4UL /* more bridges than buses: undone   */
#define XHCI_TBT_BRNUM_TOO_DEEP     5UL /* deeper than MAX_DEPTH: undone     */
#define XHCI_TBT_BRNUM_TOO_MANY     6UL /* more than MAX_BRIDGES: undone     */
#define XHCI_TBT_BRNUM_ACCESS       7UL /* a read or write failed: undone    */
#define XHCI_TBT_BRNUM_NO_WRITER    8UL /* io->Write NULL: nothing read      */
#define XHCI_TBT_BRNUM_INCONSISTENT 9UL /* a numbered bridge below one in
                                           reset state: undone              */
#define XHCI_TBT_BRNUM_NOT_READY    10UL /* a function answered Configuration
                                           Request Retry Status: undone, to
                                           be tried again                   */
#define XHCI_TBT_BRNUM_STALE        11UL /* the go callback refused: undone */

/* XHCI_TBT_BRNUM.WinOutcome: the memory windows of a chain just numbered
 * (XHCI_TBT_BRNUM_F_WINDOWS). A refusal puts every window written back. */
#define XHCI_TBT_BRWIN_NONE         0UL /* not asked, or nothing numbered    */
#define XHCI_TBT_BRWIN_DONE         1UL /* every bridge's window written     */
#define XHCI_TBT_BRWIN_PORT_CLOSED  2UL /* the port has no memory window     */
#define XHCI_TBT_BRWIN_NO_ROOM      3UL /* the chain needs more than it      */
#define XHCI_TBT_BRWIN_ACTIVE       4UL /* a function below already decodes  */
#define XHCI_TBT_BRWIN_ACCESS       5UL /* a read or write failed            */
#define XHCI_TBT_BRWIN_STALE        6UL /* the go callback refused           */

/* Flags. */
#define XHCI_TBT_BRNUM_F_WINDOWS    0x1UL   /* also give the chain windows   */

/* Configuration registers this file writes (a type 1 header but 04h). */
#define XHCI_TBT_CFG_COMMAND        0x04UL  /* Command 15:0, Status 31:16   */
#define XHCI_TBT_CFG_IOWIN          0x1CUL  /* I/O base, limit; Sec. Status */
#define XHCI_TBT_CFG_MEMWIN         0x20UL  /* memory base 15:0, limit 31:16 */
#define XHCI_TBT_CFG_PFWIN          0x24UL  /* prefetchable base, limit     */

/* Asked before each numbering write: nonzero while the operation may go
 * on. NULL allows every write. */
typedef ULONG (*XHCI_TBT_BRNUM_GO)(PVOID context);

typedef struct _XHCI_TBT_BRNUM {
    ULONG Outcome;          /* XHCI_TBT_BRNUM_*                           */
    ULONG PortBuses;        /* the port's 18h dword as read               */
    ULONG Functions;        /* functions on the port's secondary bus      */
    ULONG Bridges;          /* type 1 functions met below the port        */
    ULONG Numbered;         /* bridges left numbered (DONE only)          */
    ULONG Last;             /* the last bus given (DONE only), else 0     */
    ULONG Where;            /* the function a refusal stopped at, or
                               XHCI_TBT_NO_BDF                            */
    ULONG Writes;           /* writes made, the undo's included           */
    ULONG Undone;           /* bridges written back as read               */
    ULONG UndoFailed;       /* ... that could not be (changed, or the
                               write did not hold)                       */
    ULONG Bdf[XHCI_TBT_BRNUM_MAX_BRIDGES];   /* each journalled bridge    */
    ULONG Id[XHCI_TBT_BRNUM_MAX_BRIDGES];    /* its identity dword        */
    /* The windows (XHCI_TBT_BRNUM_F_WINDOWS). */
    ULONG WinOutcome;       /* XHCI_TBT_BRWIN_*                           */
    ULONG WinWhere;         /* the function a refusal stopped at          */
    ULONG PortMem;          /* the port's 20h dword as read               */
    ULONG WinNeed;          /* what the port's secondary bus needs        */
    ULONG Need[XHCI_TBT_BRNUM_MAX_BRIDGES];  /* each bridge's window size */
    ULONG Align[XHCI_TBT_BRNUM_MAX_BRIDGES]; /* ... and its alignment     */
    ULONG Win[XHCI_TBT_BRNUM_MAX_BRIDGES];   /* its 20h dword as written  */
    ULONG WinDone[XHCI_TBT_BRNUM_MAX_BRIDGES]; /* window registers written */
    ULONG WasCmd[XHCI_TBT_BRNUM_MAX_BRIDGES];  /* 04h, 1Ch, 20h and 24h   */
    ULONG WasIo[XHCI_TBT_BRNUM_MAX_BRIDGES];   /* as read before          */
    ULONG WasMem[XHCI_TBT_BRNUM_MAX_BRIDGES];
    ULONG WasPf[XHCI_TBT_BRNUM_MAX_BRIDGES];
    ULONG Was[XHCI_TBT_BRNUM_MAX_BRIDGES];   /* its 18h dword as read     */
    ULONG Now[XHCI_TBT_BRNUM_MAX_BRIDGES];   /* ... and as left           */
} XHCI_TBT_BRNUM, *PXHCI_TBT_BRNUM;

/*
 * Number the bridges below the tunnel port at `port`. The port's own
 * bus-number register must read a secondary above its own bus and a
 * subordinate at or above that; bridges are then given secondary buses
 * from the port's secondary + 1 up to its subordinate, depth first, each
 * bridge's subordinate the last bus of its own subtree (the BIOS's
 * layout: on the P14s the ASM2464's upstream port 06/07/08 and its
 * downstream port 07/08/08 below the tunnel port's secondary bus 06).
 * `go` (may be NULL) is asked before each numbering write. Returns
 * r->Outcome.
 */
ULONG XhciTbtNumberBridges(PXHCI_TBT_CFG io, ULONG port, ULONG flags,
                           XHCI_TBT_BRNUM_GO go, PVOID goContext,
                           PXHCI_TBT_BRNUM r);

#endif /* TBT_BRNUM_H */
