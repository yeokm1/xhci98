/*
 * cm_ejectos.c - the driver half of the Windows 98 SE and ME eject
 * transaction and the 9x statement decision (cm_eject.h; roadmap-usb4.md
 * task 43.18; design record 21 section 7.2, "The 9x half", parts 0 to 2).
 *
 * THE RECORDS. Every transaction is an image-static record (CM_EJ_REC),
 * never the NHI's extension and never the call workers' block: it is the
 * member blocker of part 0 by construction. The image stays resident on
 * 98 SE and ME (no DriverUnload, R4), so a record outlives the NHI's stop
 * and its workers: CmEjectStop detaches the records of the stopping NHI;
 * one whose removal is already authorised (a CLEAN or ISOLATED_NOT_CLEAN
 * answer taken, an isolation proven, the unit vanished after its query, or
 * a unit with no request) and still owed is completed by its member's
 * worker alone - Remove_SubTree every 2 s until 30 s after its first
 * attempt, the request withdrawn on success - and every other one is kept
 * as it stands for a later start: that start adopts it before any
 * exposure or re-enumeration of the member (CmEjectHeld holds them),
 * looking its devnode up again by the instance id the record keeps
 * (cleared when it is gone; resumed where it stood when present; a query
 * refused, or never made, owes nothing). The transactions' clock is this
 * file's own, from the tick count, so a deadline survives a stop.
 *
 * THE CALLS. Each CONFIGMG call is made by the member's call worker
 * (cm_callwk.c) through an executor here, which reads nothing of the NHI:
 * only its route, the record (static) and the operation's arguments. Every
 * call that names the unit's devnode first reads that handle's instance id
 * back and answers CR_NO_SUCH_DEVNODE when it is no longer the unit's (a
 * handle is reused by CONFIGMG for a later devnode). A result is taken by
 * the thread through the handoff word, or after the stop by the record's
 * late taker on the worker (cm_call.h, "AFTER THE STOP").
 *
 * THE ASK, THE REPLY AND THE RESULT are cm_ejask.h's, read and written by
 * cm_statos.c (the only file that writes registry values).
 *
 * THE DECISION (part 2). CmNvlinkIcmDown and CmNvlinkPortDown on 98 SE and
 * ME enter it (CmEjectDecide) and answer CM_NVLINK_PENDING; the member's
 * worker reads the SHPC slot through PCI.VXD's guarded route, the DWORD
 * Select written as a dword read, replaced and read back (CmEjByteWrite),
 * the select byte restored by the reading itself; the thread takes the
 * reading only for the decision's number and before its deadline, and
 * CmNvlinkDecided publishes or withholds. While it is pending the member's
 * removal is held (CmEjectHeld). The decision has the worker first: no
 * transaction call is submitted to a member with a decision pending.
 *
 * IRQL: as cm_eject.h; executors and late takers at PASSIVE_LEVEL on a
 * member's call worker. cmEjLock guards every record and is never held
 * across a call, a wait or another module's lock.
 */

#include "cm.h"
#include "cm_cfgmg.h"
#include "cm_ports.h"
#include "cm_nvlink.h"
#include "cm_call.h"
#include "cm_eject.h"

#if defined(_WIN64) && defined(NO_LEGACY_DRIVERS)
NTHALAPI ULONG HalGetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber,
                             ULONG SlotNumber, PVOID Buffer, ULONG Length);
#endif

#define CM_EJ_RECORDS       8UL
#define CM_EJ_ID_CHARS      (CMG_MAX_DEVICE_ID_LEN + 1)
#define CM_EJ_WALK_STACK    64UL
#define CM_EJ_WALK_NODES    2048UL
#define CM_EJ_DETACH_TICK   100UL
#define CM_EJ_CLOCK_MAX_TICKS 4000000UL

/* The call kinds (the log's), and the executors' result layouts. */
#define CM_EJ_K_FIND        0x41UL
#define CM_EJ_K_QUERY       0x42UL
#define CM_EJ_K_REMOVE      0x43UL
#define CM_EJ_K_POPULATION  0x44UL
#define CM_EJ_K_ADOPT       0x45UL
#define CM_EJ_K_DETACHED    (0x46UL | CM_CALL_K_DETACH)
#define CM_EJ_K_SLOT        0x47UL

#define CM_EJ_FIND_ID_AT    12UL

typedef struct _CM_EJ_REC {
    CM_EJ_TX Tx;
    struct _CM_NHI *Nhi;    /* the start that owns it; NULL detached       */
    ULONG Member;           /* its index in that start's set               */
    ULONG OpId;             /* the call outstanding, 0 none                */
    PVOID CallBlock;        /* ... and the call workers it went to: only
                               that start's thread takes it back; after
                               its stop the late taker does               */
    ULONG Tool;             /* a tool's ask: the result slot is its        */
    ULONG LateRunning;      /* the stopped start's worker completes it     */
    ULONG DownSeen;         /* a tunnel down on its member since this
                               start took it (CmEjectDown)                */
    ULONG InStep;           /* the thread is inside this record's step (a
                               teardown waits there): a servicing point
                               reached from it steps the others alone      */
    ULONG MemberDevnode;
    char Id[CM_EJ_ID_CHARS];
} CM_EJ_REC;

static KSPIN_LOCK cmEjLock;
static CM_EJ_REC cmEj[CM_EJ_RECORDS];
static LONG cmEjDecPending;
static ULONG cmEjClockMs;
static ULONG cmEjClockLast;
static ULONG cmEjClockRest;
static ULONG cmEjTickUs;

/* ====================================================================== */
/* The clock and configuration space                                      */
/* ====================================================================== */

/* Milliseconds, from the tick count, shared by every thread that steps a
 * record (the threads, the workers' late takers). Any IRQL <= DISPATCH. */
static ULONG cmEjNow(VOID)
{
    LARGE_INTEGER ticks;
    KIRQL irql;
    ULONG delta;
    ULONG now;

    KeAcquireSpinLock(&cmEjLock, &irql);
    KeQueryTickCount(&ticks);
    delta = ticks.LowPart - cmEjClockLast;
    cmEjClockLast = ticks.LowPart;
    if (delta > CM_EJ_CLOCK_MAX_TICKS) {
        /* A gap this long (61 hours at 55 ms ticks) runs slow. */
        delta = CM_EJ_CLOCK_MAX_TICKS;
    }
    /* delta x tick (us) / 1000 in words: the whole milliseconds of a tick
     * times delta (at most 55 x 4,000,000), its microsecond part kept in
     * the rest (at most 999 x 4,000,000 + 999): no product past 32 bits. */
    cmEjClockRest += delta * (cmEjTickUs % 1000UL);
    cmEjClockMs += delta * (cmEjTickUs / 1000UL) + cmEjClockRest / 1000UL;
    cmEjClockRest %= 1000UL;
    now = cmEjClockMs;
    KeReleaseSpinLock(&cmEjLock, irql);
    return now;
}

VOID CmEjectInit(VOID)
{
    LARGE_INTEGER ticks;
    ULONG unit;

    KeInitializeSpinLock(&cmEjLock);
    unit = KeQueryTimeIncrement();
    if (unit == 0) {
        unit = 10000UL;
    }
    /* 100 ns units to microseconds; 10 to 55 ms ticks. */
    cmEjTickUs = unit / 10UL;
    KeQueryTickCount(&ticks);
    cmEjClockLast = ticks.LowPart;
    /* Never 0: a stamp of 0 reads as none. */
    cmEjClockMs = 1;
}

/* One dword of the function at `bdf` through HalGetBusData; all ones for a
 * function that does not answer, 0 when the read failed. PASSIVE_LEVEL. */
static ULONG cmEjCfg(ULONG bdf, ULONG offset)
{
    UCHAR buffer[32];
    ULONG got;

    if (offset > 28UL || (offset & 3UL) != 0) {
        return 0;
    }
    got = HalGetBusData(PCIConfiguration, XHCI_TBT_BDF_BUS(bdf),
                        XHCI_TBT_BDF_DEV(bdf) | (XHCI_TBT_BDF_FN(bdf) << 5),
                        buffer, offset + 4UL);
    if (got >= offset + 4UL) {
        return (ULONG)buffer[offset] | ((ULONG)buffer[offset + 1] << 8) |
               ((ULONG)buffer[offset + 2] << 16) |
               ((ULONG)buffer[offset + 3] << 24);
    }
    if (got == 2) {
        return 0xFFFFFFFFUL;
    }
    return 0;
}

static BOOLEAN cmEjIdEqual(const char *a, const char *b)
{
    ULONG i;

    for (i = 0; i < CM_EJ_ID_CHARS; i++) {
        if (a[i] != b[i]) {
            return FALSE;
        }
        if (a[i] == '\0') {
            return TRUE;
        }
    }
    return FALSE;
}

/* ====================================================================== */
/* The executors, on a member's call worker                               */
/* ====================================================================== */

/*
 * A depth-first walk of the devnode tree from `root`: the first devnode
 * whose instance id names location `bdf` (with `want`/`n`, its expected
 * path) or, with `id` given, carries exactly that id. *complete TRUE only
 * when every node was read. 0 for none.
 */
static ULONG cmEjWalk(PCM_CFGMG cfg, ULONG root, ULONG bdf,
                      const ULONG *want, ULONG n, const char *id,
                      BOOLEAN *complete)
{
    ULONG stack[CM_EJ_WALK_STACK];
    ULONG depth;
    ULONG nodes;
    ULONG node;
    ULONG next;
    ULONG cr;
    char text[CM_EJ_ID_CHARS];

    *complete = FALSE;
    depth = 0;
    nodes = 0;
    stack[depth++] = root;
    while (depth > 0) {
        if (nodes == CM_EJ_WALK_NODES) {
            return 0;
        }
        node = stack[--depth];
        nodes++;
        cr = CmCfgmgDeviceId(cfg, node, text, sizeof(text));
        if (cr != CMG_CR_SUCCESS) {
            return 0;
        }
        text[CM_EJ_ID_CHARS - 1] = '\0';
        if (id != NULL ? cmEjIdEqual(text, id)
                       : CmCfgwkIdNames(text, bdf, want, n)) {
            *complete = TRUE;
            return node;
        }
        cr = CmCfgmgChild(cfg, node, &next);
        if (cr == CMG_CR_NO_SUCH_DEVNODE) {
            continue;
        }
        if (cr != CMG_CR_SUCCESS) {
            return 0;
        }
        while (next != 0) {
            if (depth == CM_EJ_WALK_STACK) {
                return 0;
            }
            stack[depth++] = next;
            cr = CmCfgmgSibling(cfg, next, &next);
            if (cr == CMG_CR_NO_SUCH_DEVNODE) {
                break;
            }
            if (cr != CMG_CR_SUCCESS) {
                return 0;
            }
        }
    }
    *complete = TRUE;
    return 0;
}

/* The member's devnode (by its location and path, read now), and the path
 * in `path`/`*n`. 0 when not found. */
static ULONG cmEjMemberNode(PCM_CFGMG cfg, ULONG member, PULONG path,
                            PULONG n)
{
    BOOLEAN complete;

    *n = 0;
    if (!cfg->Open || !CmCfgwkPciPath(member, path, n)) {
        return 0;
    }
    return cmEjWalk(cfg, cfg->Root, member, path, *n, NULL, &complete);
}

/* FIND: op [0] the member, [1] the unit. Result: [0] the unit's devnode,
 * [1] the member's, [2] the id's length, its characters at
 * CM_EJ_FIND_ID_AT. */
static ULONG cmEjRunFind(PVOID route, PVOID context, const VOID *op,
                         VOID *res)
{
    PCM_CFGMG cfg;
    const ULONG *in;
    PULONG out;
    ULONG path[CM_PORTS_PATH_MAX];
    ULONG want[CM_PORTS_PATH_MAX];
    ULONG n;
    ULONG k;
    ULONG member;
    ULONG node;
    ULONG len;
    char text[CM_EJ_ID_CHARS];

    UNREFERENCED_PARAMETER(context);
    cfg = (PCM_CFGMG)route;
    in = (const ULONG *)op;
    out = (PULONG)res;
    member = cmEjMemberNode(cfg, in[0], path, &n);
    out[1] = member;
    if (member == 0 || n + 1 > CM_PORTS_PATH_MAX) {
        return 12;
    }
    want[0] = in[1] & 0xFFUL;
    for (k = 0; k < n; k++) {
        want[k + 1] = path[k];
    }
    if (CmCfgmgChild(cfg, member, &node) != CMG_CR_SUCCESS) {
        return 12;
    }
    while (node != 0) {
        if (CmCfgmgDeviceId(cfg, node, text, sizeof(text)) ==
            CMG_CR_SUCCESS) {
            text[CM_EJ_ID_CHARS - 1] = '\0';
            if (CmCfgwkIdNames(text, in[1], want, n + 1)) {
                for (len = 0; len < CM_EJ_ID_CHARS - 1 && text[len] != '\0';
                     len++) {
                    ((char *)res)[CM_EJ_FIND_ID_AT + len] = text[len];
                }
                out[0] = node;
                out[2] = len;
                return CM_EJ_FIND_ID_AT + len;
            }
        }
        if (CmCfgmgSibling(cfg, node, &node) != CMG_CR_SUCCESS) {
            break;
        }
    }
    return 12;
}

/* Whether `devnode` still carries the record's instance id. */
/* Whether `devnode` still carries the record's instance id, as the
 * CONFIGRET the call naming it answers with: CR_SUCCESS (it does: the call
 * is made), CR_NO_SUCH_DEVNODE (verified gone: CONFIGMG has no such
 * devnode, or a successful read finds another id), or CR_FAILURE (the
 * read was inconclusive: nothing is concluded, the call is not made). */
static ULONG cmEjStill(PCM_CFGMG cfg, const CM_EJ_REC *rec, ULONG devnode)
{
    char text[CM_EJ_ID_CHARS];
    ULONG cr;

    if (devnode == 0 || rec->Id[0] == '\0') {
        return CMG_CR_FAILURE;
    }
    cr = CmCfgmgDeviceId(cfg, devnode, text, sizeof(text));
    if (cr == CMG_CR_NO_SUCH_DEVNODE || cr == CMG_CR_INVALID_DEVNODE) {
        return CMG_CR_NO_SUCH_DEVNODE;
    }
    if (cr != CMG_CR_SUCCESS) {
        return CMG_CR_FAILURE;
    }
    text[CM_EJ_ID_CHARS - 1] = '\0';
    return cmEjIdEqual(text, rec->Id) ? CMG_CR_SUCCESS
                                      : CMG_CR_NO_SUCH_DEVNODE;
}

/* QUERY and REMOVE: op [0] the devnode. Result [0] the CONFIGRET. */
static ULONG cmEjRunQuery(PVOID route, PVOID context, const VOID *op,
                          VOID *res)
{
    PCM_CFGMG cfg;
    ULONG devnode;

    cfg = (PCM_CFGMG)route;
    devnode = ((const ULONG *)op)[0];
    ((PULONG)res)[0] = cmEjStill(cfg, (CM_EJ_REC *)context, devnode);
    if (((PULONG)res)[0] == CMG_CR_SUCCESS) {
        ((PULONG)res)[0] = CmCfgmgQueryRemove(cfg, devnode);
    }
    return 4;
}

static ULONG cmEjRunRemove(PVOID route, PVOID context, const VOID *op,
                           VOID *res)
{
    PCM_CFGMG cfg;
    ULONG devnode;

    cfg = (PCM_CFGMG)route;
    devnode = ((const ULONG *)op)[0];
    ((PULONG)res)[0] = cmEjStill(cfg, (CM_EJ_REC *)context, devnode);
    if (((PULONG)res)[0] == CMG_CR_SUCCESS) {
        ((PULONG)res)[0] = CmCfgmgRemove(cfg, devnode);
    }
    return 4;
}

/* POPULATION: op [0] the member, [1] the unit's devnode. Every other child
 * of the member read not started, the list complete (record 24 section
 * 5.4). Result [0] CM_EJ_POP_*. */
static ULONG cmEjRunPopulation(PVOID route, PVOID context, const VOID *op,
                               VOID *res)
{
    PCM_CFGMG cfg;
    const ULONG *in;
    ULONG path[CM_PORTS_PATH_MAX];
    ULONG n;
    ULONG member;
    ULONG node;
    ULONG status;
    ULONG problem;
    ULONG cr;
    ULONG count;

    UNREFERENCED_PARAMETER(context);
    cfg = (PCM_CFGMG)route;
    in = (const ULONG *)op;
    ((PULONG)res)[0] = CM_EJ_POP_INCOMPLETE;
    member = cmEjMemberNode(cfg, in[0], path, &n);
    if (member == 0) {
        return 4;
    }
    cr = CmCfgmgChild(cfg, member, &node);
    if (cr == CMG_CR_NO_SUCH_DEVNODE) {
        ((PULONG)res)[0] = CM_EJ_POP_PASSED;
        return 4;
    }
    if (cr != CMG_CR_SUCCESS) {
        return 4;
    }
    for (count = 0; node != 0; count++) {
        if (count == CM_EJ_WALK_STACK) {
            return 4;
        }
        if (node != in[1]) {
            if (CmCfgmgStatus(cfg, node, &status, &problem) !=
                CMG_CR_SUCCESS) {
                return 4;
            }
            if ((status & CMG_DN_STARTED) != 0) {
                ((PULONG)res)[0] = CM_EJ_POP_SIBLING;
                return 4;
            }
        }
        cr = CmCfgmgSibling(cfg, node, &node);
        if (cr == CMG_CR_NO_SUCH_DEVNODE) {
            break;
        }
        if (cr != CMG_CR_SUCCESS) {
            return 4;
        }
    }
    ((PULONG)res)[0] = CM_EJ_POP_PASSED;
    return 4;
}

/* ADOPT: the devnode carrying the record's instance id, the whole tree
 * walked. Result [0] the devnode or 0, [1] 1 when the walk was
 * complete. */
static ULONG cmEjRunAdopt(PVOID route, PVOID context, const VOID *op,
                          VOID *res)
{
    PCM_CFGMG cfg;
    CM_EJ_REC *rec;
    BOOLEAN complete;

    UNREFERENCED_PARAMETER(op);
    cfg = (PCM_CFGMG)route;
    rec = (CM_EJ_REC *)context;
    ((PULONG)res)[0] = 0;
    ((PULONG)res)[1] = 0;
    if (!cfg->Open || rec->Id[0] == '\0') {
        return 8;
    }
    ((PULONG)res)[0] = cmEjWalk(cfg, cfg->Root, 0, NULL, 0, rec->Id,
                                &complete);
    ((PULONG)res)[1] = complete ? 1UL : 0UL;
    return 8;
}

/* PCI.VXD's dword route for the slot reading (part 2). */
static ULONG cmEjVxdRead(PVOID context, ULONG bdf, ULONG offset,
                         PULONG value)
{
    return CmCfgmgPciRead((PCM_CFGMG)context, bdf, offset, value) ? 1UL
                                                                   : 0UL;
}

static ULONG cmEjVxdWrite32(PVOID context, ULONG bdf, ULONG offset,
                            ULONG value)
{
    return CmCfgmgPciWrite((PCM_CFGMG)context, bdf, offset, value) ? 1UL
                                                                    : 0UL;
}

static ULONG cmEjVxdWrite(PVOID context, ULONG bdf, ULONG offset,
                          ULONG width, ULONG value)
{
    return CmEjByteWrite(cmEjVxdRead, cmEjVxdWrite32, context, bdf, offset,
                         width, value)
               ? 1UL
               : 0UL;
}

/* SLOT: op [0] the member, [1] its id as HalGetBusData read it (PCI.VXD's
 * guard), [2] the decision's number. Result [0] the number, [1] the
 * reading (XHCI_TBT_PORT_*), [2] 1 when the bridge reads as the
 * emulator's (an SHPC capability, no PCI Express capability), [3] the
 * clock at the read. */
static ULONG cmEjRunSlot(PVOID route, PVOID context, const VOID *op,
                         VOID *res)
{
    PCM_CFGMG cfg;
    const ULONG *in;
    PULONG out;
    XHCI_TBT_CFG io;
    XHCI_TBT_PORT port;
    ULONG verdict;

    UNREFERENCED_PARAMETER(context);
    cfg = (PCM_CFGMG)route;
    in = (const ULONG *)op;
    out = (PULONG)res;
    out[0] = in[2];
    out[1] = XHCI_TBT_PORT_ERROR;
    out[2] = 0;
    if (cfg->Open && (cfg->PciOpen || CmCfgmgPciOpen(cfg, in[0], in[1]))) {
        io.Read = cmEjVxdRead;
        io.Write = cmEjVxdWrite;
        io.Context = cfg;
        io.Reads = 0;
        io.Writes = 0;
        CmZero(&port, sizeof(port));
        verdict = XhciTbtPortRead(&io, in[0], &port);
        out[1] = verdict;
        out[2] = (port.ShpcCap != 0 && port.PcieCap == 0) ? 1UL : 0UL;
    }
    out[3] = cmEjNow();
    return 16;
}

/* ====================================================================== */
/* Taking results                                                         */
/* ====================================================================== */

/* A call's result into its record, under cmEjLock. */
static VOID cmEjTakeLocked(CM_EJ_REC *rec, ULONG kind, const ULONG *res,
                           ULONG bytes, BOOLEAN u4, ULONG now)
{
    ULONG len;
    ULONG k;

    rec->OpId = 0;
    switch (kind) {
    case CM_EJ_K_FIND:
        len = bytes >= CM_EJ_FIND_ID_AT ? res[2] : 0;
        if (len > CM_EJ_ID_CHARS - 1 || bytes < CM_EJ_FIND_ID_AT + len) {
            len = 0;
        }
        for (k = 0; k < len; k++) {
            rec->Id[k] = ((const char *)res)[CM_EJ_FIND_ID_AT + k];
        }
        rec->Id[len] = '\0';
        rec->MemberDevnode = bytes >= 8 ? res[1] : 0;
        CmEjFeed(&rec->Tx, CM_EJ_FED_FIND, len != 0 ? res[0] : 0, 0, now);
        return;
    case CM_EJ_K_QUERY:
        CmEjFeed(&rec->Tx, CM_EJ_FED_QUERY,
                 bytes >= 4 ? res[0] : CMG_CR_FAILURE, 0, now);
        return;
    case CM_EJ_K_REMOVE:
        CmEjFeed(&rec->Tx, CM_EJ_FED_REMOVE,
                 bytes >= 4 ? res[0] : CMG_CR_FAILURE, u4 ? 1UL : 0UL, now);
        return;
    case CM_EJ_K_POPULATION:
        CmEjFeed(&rec->Tx, CM_EJ_FED_POPULATION,
                 bytes >= 4 ? res[0] : CM_EJ_POP_INCOMPLETE, 0, now);
        return;
    case CM_EJ_K_ADOPT:
        CmEjFeed(&rec->Tx, CM_EJ_FED_ADOPT, bytes >= 8 ? res[0] : 0,
                 bytes >= 8 ? res[1] : 0, now);
        return;
    default:
        return;
    }
}

/* What a withdrawn call leaves: nothing was called. */
static VOID cmEjWithdrawnLocked(CM_EJ_REC *rec)
{
    rec->OpId = 0;
    if (rec->Tx.Pending == CM_EJ_DO_REMOVE && rec->Tx.Attempts != 0) {
        rec->Tx.Attempts--;
    }
    rec->Tx.Pending = 0;
}

/*
 * After the stop (the detached block): the removal an authorised
 * transaction owes, made on this worker alone - every 2 s until 30 s after
 * its first attempt (CmEjNext's own clock), the request withdrawn on
 * success. Nothing else: a transaction that needs the thread (an answer
 * awaited, a teardown) is left for a later start. PASSIVE_LEVEL, on the
 * member's worker.
 */
static VOID cmEjDetachedLoop(PCM_CFGMG cfg, CM_EJ_REC *rec)
{
    ULONG t;
    CM_EJ_IN in;
    LARGE_INTEGER pause;
    KIRQL irql;
    ULONG what;
    ULONG cr;
    ULONG devnode;
    ULONG entry;
    ULONG seq;

    for (;;) {
        CmZero(&in, sizeof(in));
        in.NowMs = cmEjNow();
        in.SlotIdle = TRUE;
        KeAcquireSpinLock(&cmEjLock, &irql);
        what = CM_EJ_DO_NONE;
        if (rec->Nhi == NULL && CmEjOwesRemoval(&rec->Tx) &&
            (rec->Tx.Flags & CM_EJ_F_BLOCKED) == 0) {
            what = CmEjNext(&rec->Tx, &in);
        } else if (rec->Nhi == NULL && rec->Tx.Phase == CM_EJ_P_WITHDRAW) {
            what = CmEjNext(&rec->Tx, &in);
        }
        if (what == CM_EJ_DO_REMOVE) {
            CmEjSubmitted(&rec->Tx, what, TRUE, in.NowMs);
        }
        devnode = rec->Tx.Devnode;
        entry = rec->Tx.Entry;
        seq = rec->Tx.Sequence;
        KeReleaseSpinLock(&cmEjLock, irql);
        if (what == CM_EJ_DO_REMOVE) {
            cr = cmEjStill(cfg, rec, devnode);
            if (cr == CMG_CR_SUCCESS) {
                cr = CmCfgmgRemove(cfg, devnode);
            }
            t = cmEjNow();
            KeAcquireSpinLock(&cmEjLock, &irql);
            CmEjFeed(&rec->Tx, CM_EJ_FED_REMOVE, cr, 0, t);
            KeReleaseSpinLock(&cmEjLock, irql);
            continue;
        }
        if (what == CM_EJ_DO_WITHDRAW) {
            CmNvlinkWkWithdraw(entry, seq);
            t = cmEjNow();
            KeAcquireSpinLock(&cmEjLock, &irql);
            CmEjFeed(&rec->Tx, CM_EJ_FED_WITHDRAW, 0, 0, t);
            KeReleaseSpinLock(&cmEjLock, irql);
            continue;
        }
        if (what != CM_EJ_DO_NONE) {
            /* Anything else is the thread's: left for a later start. */
            break;
        }
        KeAcquireSpinLock(&cmEjLock, &irql);
        what = (rec->Nhi == NULL && CmEjOwesRemoval(&rec->Tx) &&
                (rec->Tx.Flags & CM_EJ_F_BLOCKED) == 0)
                   ? 1UL
                   : 0UL;
        KeReleaseSpinLock(&cmEjLock, irql);
        if (what == 0) {
            break;
        }
        pause.LowPart = (ULONG)(0UL - CM_EJ_DETACH_TICK * 10000UL);
        pause.HighPart = -1;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &pause);
    }
    KeAcquireSpinLock(&cmEjLock, &irql);
    rec->LateRunning = 0;
    KeReleaseSpinLock(&cmEjLock, irql);
}

/* DETACHED: submitted by the stop for an authorised record with no call
 * out; runs the loop above. */
static ULONG cmEjRunDetached(PVOID route, PVOID context, const VOID *op,
                             VOID *res)
{
    UNREFERENCED_PARAMETER(op);
    UNREFERENCED_PARAMETER(res);
    cmEjDetachedLoop((PCM_CFGMG)route, (CM_EJ_REC *)context);
    return 0;
}

/* The late taker (cm_call.h): an outcome the thread did not take before
 * the stop, kept in the record (an accepted query's CONFIGRET, a removal's
 * success); then, the record detached and its removal authorised, the
 * loop above. */
static VOID cmEjLate(PVOID route, PVOID context, ULONG kind, ULONG id,
                     ULONG outcome, const VOID *res, ULONG bytes)
{
    ULONG t;
    CM_EJ_REC *rec;
    KIRQL irql;
    BOOLEAN loop;

    rec = (CM_EJ_REC *)context;
    if (rec == NULL || kind == CM_EJ_K_DETACHED) {
        return;
    }
    t = cmEjNow();
    KeAcquireSpinLock(&cmEjLock, &irql);
    if (rec->OpId == id && id != 0) {
        if (outcome == CM_CALL_L_RESULT) {
            cmEjTakeLocked(rec, kind, (const ULONG *)res, bytes, FALSE,
                           t);
        } else {
            cmEjWithdrawnLocked(rec);
        }
    }
    loop = (BOOLEAN)(rec->Nhi == NULL && rec->LateRunning == 0 &&
                     (CmEjOwesRemoval(&rec->Tx) ||
                      rec->Tx.Phase == CM_EJ_P_WITHDRAW));
    if (loop) {
        rec->LateRunning = 1;
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    if (loop) {
        cmEjDetachedLoop((PCM_CFGMG)route, rec);
    }
}

/* ====================================================================== */
/* The thread                                                             */
/* ====================================================================== */

static BOOLEAN cmEj9x(PCM_NHI nhi)
{
    return (BOOLEAN)(nhi->Reenum.Route == CM_ROUTE_CONFIGMG &&
                     nhi->Reenum.Callwk != NULL);
}

/* Until the two systems are told apart here, a 9x statement needs both
 * admitted (decision V4 admits them one at a time: the version read that
 * separates them comes with the first admission). */
static BOOLEAN cmEjQualified(VOID)
{
    return (BOOLEAN)(CmEjQualified9x(FALSE) && CmEjQualified9x(TRUE));
}

VOID CmEjectReset(PCM_NHI nhi)
{
    KIRQL irql;
    ULONG i;

    CmZero(&nhi->Ej, sizeof(nhi->Ej));
    nhi->Ej.Owner = CM_EJ_NONE;
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        if (cmEj[i].Nhi == nhi) {
            cmEj[i].Nhi = NULL;
        }
    }
    KeReleaseSpinLock(&cmEjLock, irql);
}

/* The member index of `bridge` in this start's set, owned exclusively and
 * active; XHCI_TBT_NO_MEMBER otherwise. */
static ULONG cmEjMemberOf(PCM_NHI nhi, ULONG bridge, BOOLEAN active)
{
    ULONG flags;
    ULONG i;

    i = CmReenumMemberFlags(nhi, bridge, &flags);
    if (i == XHCI_TBT_NO_MEMBER ||
        (flags & CM_PORT_EXCLUSIVE) == 0 ||
        (active && (flags & CM_PORT_ACTIVE) == 0)) {
        return XHCI_TBT_NO_MEMBER;
    }
    return i;
}

static BOOLEAN cmEjDecPosted(PCM_NHI nhi, ULONG member)
{
    return (BOOLEAN)(member < XHCI_TBT_MAX_TUNNELS &&
                     nhi->Ej.Dec[member].State == CM_EJ_D_POSTED);
}

static VOID cmEjWrite(PCM_NHI nhi, CM_EJ_REC *rec, ULONG now)
{
    UCHAR buf[CM_EJ_RESULT_BYTES];
    KIRQL irql;
    ULONG step;

    KeAcquireSpinLock(&cmEjLock, &irql);
    if (!CmEjResultDue(&rec->Tx, now)) {
        KeReleaseSpinLock(&cmEjLock, irql);
        return;
    }
    CmEjResultEncode(&rec->Tx, buf, now);
    step = rec->Tx.Step;
    KeReleaseSpinLock(&cmEjLock, irql);
    if (CmStatusEjectWrite(nhi, TRUE, buf, CM_EJ_RESULT_BYTES)) {
        nhi->Ej.Writes++;
    } else {
        /* Tried again at the next pass. */
        nhi->Ej.WriteFailures++;
        KeAcquireSpinLock(&cmEjLock, &irql);
        rec->Tx.Flags |= CM_EJ_F_DIRTY;
        KeReleaseSpinLock(&cmEjLock, irql);
    }
    CmLogNote(nhi, "cm.eject.result",
              (step << 24) | ((rec->Tx.Outcome & 0xFFUL) << 16) |
                  (rec->Tx.Nonce & 0xFFFFUL));
}

/* The REQUEST step: the entry this unit's function was published UP for
 * by this domain, behind this member, for these ids (CmNvlinkWkCandidate
 * names the bus and slot; the unit's location must be that one), then
 * REMOVE_PENDING with a fresh sequence while all of it still holds. */
static VOID cmEjRequest(PCM_NHI nhi, CM_EJ_REC *rec)
{
    ULONG bus;
    ULONG slot;
    ULONG gen;
    ULONG k;
    ULONG entry;
    ULONG egen;
    ULONG seq;
    ULONG now;
    KIRQL irql;

    entry = CM_EJ_NONE;
    egen = 0;
    seq = 0;
    if (rec->Tx.Nvme) {
        for (k = 0; k < STOR98LS_ENTRIES; k++) {
            if (CmNvlinkWkCandidate(nhi, rec->Tx.Bridge, rec->Tx.UnitId, k,
                                    &bus, &slot, &gen) &&
                bus == XHCI_TBT_BDF_BUS(rec->Tx.Unit) &&
                slot == (XHCI_TBT_BDF_DEV(rec->Tx.Unit) |
                         (XHCI_TBT_BDF_FN(rec->Tx.Unit) << 5))) {
                entry = k;
                egen = gen;
                break;
            }
        }
    }
    /* T_ack from before the request is published (R11). */
    now = cmEjNow();
    if (entry != CM_EJ_NONE) {
        entry = CmNvlinkWkRequest(nhi, entry, egen, rec->Tx.UnitId, &seq);
        if (entry == CM_NVLINK_NONE) {
            /* An UP entry names the unit, and its request was not
             * published: never a plain removal (R11 O5). */
            entry = CM_EJ_REQ_FAILED;
        }
    }
    KeAcquireSpinLock(&cmEjLock, &irql);
    rec->Tx.Generation = egen;
    CmEjFeed(&rec->Tx, CM_EJ_FED_REQUEST, entry, seq, now);
    KeReleaseSpinLock(&cmEjLock, irql);
    CmLogNote(nhi, "cm.eject.request",
              (entry == CM_EJ_NONE || entry == CM_EJ_REQ_FAILED)
                  ? entry
                  : ((seq << 8) | entry));
}

/* A call for record `rec` on its member's worker; FALSE when it was not
 * taken (the worker busy or gone: tried again later). */
static BOOLEAN cmEjSubmit(PCM_NHI nhi, CM_EJ_REC *rec, ULONG what)
{
    ULONG t;
    ULONG op[2];
    ULONG kind;
    ULONG id;
    CM_CALL_RUN run;
    KIRQL irql;

    op[0] = rec->Tx.Devnode;
    op[1] = 0;
    switch (what) {
    case CM_EJ_DO_FIND:
        op[0] = rec->Tx.Bridge;
        op[1] = rec->Tx.Unit;
        kind = CM_EJ_K_FIND;
        run = cmEjRunFind;
        break;
    case CM_EJ_DO_QUERY:
        kind = CM_EJ_K_QUERY;
        run = cmEjRunQuery;
        break;
    case CM_EJ_DO_REMOVE:
        kind = CM_EJ_K_REMOVE;
        run = cmEjRunRemove;
        break;
    case CM_EJ_DO_POPULATION:
        op[0] = rec->Tx.Bridge;
        op[1] = rec->Tx.Devnode;
        kind = CM_EJ_K_POPULATION;
        run = cmEjRunPopulation;
        break;
    case CM_EJ_DO_ADOPT:
        kind = CM_EJ_K_ADOPT;
        run = cmEjRunAdopt;
        break;
    default:
        return FALSE;
    }
    id = CmCallwkSubmit(nhi->Reenum.Callwk, rec->Member, kind, run, cmEjLate,
                        rec, op, sizeof(op), nhi->ClockMs);
    t = cmEjNow();
    KeAcquireSpinLock(&cmEjLock, &irql);
    if (id != 0) {
        rec->OpId = id;
        rec->CallBlock = (PVOID)nhi->Reenum.Callwk;
    }
    CmEjSubmitted(&rec->Tx, what, (BOOLEAN)(id != 0), t);
    KeReleaseSpinLock(&cmEjLock, irql);
    if (id != 0) {
        CmLogNote(nhi, "cm.eject.call", (kind << 24) | (rec->Member << 16) |
                                            (rec->Tx.Phase & 0xFFUL));
    }
    return (BOOLEAN)(id != 0);
}

/* One record's step on the thread. */
static VOID cmEjStepRec(PCM_NHI nhi, CM_EJ_REC *rec, BOOLEAN nested)
{
    ULONG t;
    CM_EJ_IN in;
    ULONG res[CM_CALL_RES_BYTES / 4];
    ULONG got;
    ULONG r;
    ULONG what;
    ULONG id;
    ULONG port;
    ULONG n;
    BOOLEAN busy;
    BOOLEAN closed;
    BOOLEAN marked;
    BOOLEAN isolate;
    KIRQL irql;

    port = CmNhiMemberPort(nhi, rec->Member);
    /* The call outstanding, taken by its id. */
    KeAcquireSpinLock(&cmEjLock, &irql);
    id = rec->CallBlock == (PVOID)nhi->Reenum.Callwk ? rec->OpId : 0;
    KeReleaseSpinLock(&cmEjLock, irql);
    if (id != 0) {
        r = CmCallwkTake(nhi->Reenum.Callwk, rec->Member, id, res,
                         sizeof(res), &got);
        if (r == CM_CALL_T_TAKEN || r == CM_CALL_T_STALE ||
            r == CM_CALL_T_NONE) {
            t = cmEjNow();
            KeAcquireSpinLock(&cmEjLock, &irql);
            if (r == CM_CALL_T_TAKEN && rec->Tx.Pending != 0) {
                cmEjTakeLocked(rec, rec->Tx.Pending == CM_EJ_DO_FIND
                                        ? CM_EJ_K_FIND
                                    : rec->Tx.Pending == CM_EJ_DO_QUERY
                                        ? CM_EJ_K_QUERY
                                    : rec->Tx.Pending == CM_EJ_DO_REMOVE
                                        ? CM_EJ_K_REMOVE
                                    : rec->Tx.Pending == CM_EJ_DO_POPULATION
                                        ? CM_EJ_K_POPULATION
                                        : CM_EJ_K_ADOPT,
                               res, got, (BOOLEAN)(port != 0), t);
            } else {
                cmEjWithdrawnLocked(rec);
            }
            KeReleaseSpinLock(&cmEjLock, irql);
        }
    }
    for (n = 0; n < 12; n++) {
        CmZero(&in, sizeof(in));
        in.NowMs = cmEjNow();
        in.Nested = nested;
        in.U4Known = (BOOLEAN)(port != 0);
        in.Qualified = cmEjQualified();
        busy = CmCallwkBusy(nhi->Reenum.Callwk, rec->Member);
        in.SlotIdle = (BOOLEAN)(!busy && !cmEjDecPosted(nhi, rec->Member));
        in.AckHeld = CmNvlinkAckHeld(nhi, rec->Tx.Bridge);
        KeAcquireSpinLock(&cmEjLock, &irql);
        /* The unit gone counts only after a tunnel down on its member, its
         * statement decided and any acknowledgement wait ended: an
         * all-ones read alone is a presence signal, not isolation (section
         * 7.3). */
        if (rec->DownSeen && !in.AckHeld &&
            !cmEjDecPosted(nhi, rec->Member) &&
            (rec->Tx.Flags & CM_EJ_F_QUERIED) != 0 &&
            (rec->Tx.Flags & (CM_EJ_F_REMOVED | CM_EJ_F_VANISHED)) == 0) {
            KeReleaseSpinLock(&cmEjLock, irql);
            id = cmEjCfg(rec->Tx.Unit, 0);
            in.Vanished = (BOOLEAN)(id == 0xFFFFFFFFUL ||
                                    (id != 0 && id != rec->Tx.UnitId));
            KeAcquireSpinLock(&cmEjLock, &irql);
        }
        if (rec->Tx.Phase == CM_EJ_P_ANSWER) {
            in.Answer = CmNvlinkWkAnswer(rec->Tx.Entry, rec->Tx.Sequence,
                                         rec->Tx.Generation,
                                         in.NowMs - rec->Tx.ReqAt);
        }
        what = CmEjNext(&rec->Tx, &in);
        isolate = (BOOLEAN)((rec->Tx.Flags & CM_EJ_F_ISOLATE) != 0);
        KeReleaseSpinLock(&cmEjLock, irql);
        switch (what) {
        case CM_EJ_DO_NONE:
            return;
        case CM_EJ_DO_FIND:
        case CM_EJ_DO_QUERY:
        case CM_EJ_DO_REMOVE:
        case CM_EJ_DO_POPULATION:
        case CM_EJ_DO_ADOPT:
            if (!cmEjSubmit(nhi, rec, what)) {
                return;
            }
            continue;
        case CM_EJ_DO_REQUEST:
            cmEjRequest(nhi, rec);
            continue;
        case CM_EJ_DO_WITHDRAW:
            CmNvlinkWkWithdraw(rec->Tx.Entry, rec->Tx.Sequence);
            t = cmEjNow();
            KeAcquireSpinLock(&cmEjLock, &irql);
            CmEjFeed(&rec->Tx, CM_EJ_FED_WITHDRAW, 0, port != 0 ? 1UL : 0UL,
                     t);
            KeReleaseSpinLock(&cmEjLock, irql);
            continue;
        case CM_EJ_DO_TEARDOWN:
            closed = CmNhiU4EjectMember(nhi, rec->Tx.Bridge);
            CmLogNote(nhi, "cm.eject.teardown",
                      (rec->Tx.Bridge << 8) | (closed ? 1UL : 0UL));
            t = cmEjNow();
            KeAcquireSpinLock(&cmEjLock, &irql);
            CmEjFeed(&rec->Tx, CM_EJ_FED_TEARDOWN, closed ? 1UL : 0UL,
                     in.Qualified ? 1UL : 0UL, t);
            KeReleaseSpinLock(&cmEjLock, irql);
            continue;
        case CM_EJ_DO_MARK:
            /* The isolation's statement waits for its acknowledgement
             * (R12 S4); after a removal or O6 none is waited for. */
            marked = CmNvlinkMarkEntry(nhi, rec->Tx.Bridge, rec->Tx.Entry,
                                       isolate);
            CmLogNote(nhi, "cm.eject.mark",
                      (rec->Tx.Entry << 8) | (marked ? 1UL : 0UL));
            t = cmEjNow();
            KeAcquireSpinLock(&cmEjLock, &irql);
            CmEjFeed(&rec->Tx, CM_EJ_FED_MARK, marked ? 1UL : 0UL, 0,
                     t);
            KeReleaseSpinLock(&cmEjLock, irql);
            continue;
        case CM_EJ_DO_REENUM:
            /* O5's ISOLATED_NOT_CLEAN: the member's ordinary
             * re-enumeration, exposure first, once this gate falls. */
            CmReenumOwe(nhi, rec->Member);
            t = cmEjNow();
            KeAcquireSpinLock(&cmEjLock, &irql);
            CmEjFeed(&rec->Tx, CM_EJ_FED_REENUM, 0, 0, t);
            KeReleaseSpinLock(&cmEjLock, irql);
            continue;
        default:
            return;
        }
    }
}

/* Part 2: each pending decision's reading submitted, taken, or expired;
 * each decided one published or withheld. */
static VOID cmEjDecisions(PCM_NHI nhi)
{
    CM_EJ_DOM *d;
    ULONG res[4];
    ULONG op[3];
    ULONG got;
    ULONG r;
    ULONG i;
    ULONG now;
    BOOLEAN decided;

    d = &nhi->Ej;
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (d->Dec[i].State == CM_EJ_D_IDLE && d->DecOp[i] == 0) {
            continue;
        }
        if (d->DecOp[i] != 0) {
            r = CmCallwkTake(nhi->Reenum.Callwk, i, d->DecOp[i], res,
                             sizeof(res), &got);
            if (r != CM_CALL_T_BUSY) {
                d->DecOp[i] = 0;
                if (r == CM_CALL_T_TAKEN && got >= 16 &&
                    CmEjDecideTake(&d->Dec[i], res[0], res[1],
                                   (BOOLEAN)(res[2] != 0), res[3],
                                   cmEjNow())) {
                    if (d->Dec[i].Reading == XHCI_TBT_PORT_ERROR &&
                        res[1] != XHCI_TBT_PORT_ERROR) {
                        d->DecidedLate++;
                        CmLogNote(nhi, "cm.nvlink.9x.reading-late",
                                  res[3] - d->Dec[i].BeganAt);
                    }
                }
            }
        }
        now = cmEjNow();
        decided = CmEjDecideExpire(&d->Dec[i], now);
        if (decided) {
            CmLogNote(nhi, "cm.nvlink.9x.reading-none", i);
        }
        if (d->Dec[i].State == CM_EJ_D_POSTED && d->DecOp[i] == 0 &&
            !CmCallwkBusy(nhi->Reenum.Callwk, i)) {
            op[0] = d->Dec[i].Bridge;
            op[1] = cmEjCfg(d->Dec[i].Bridge, 0);
            op[2] = d->Dec[i].Serial;
            d->DecOp[i] = CmCallwkSubmit(nhi->Reenum.Callwk, i, CM_EJ_K_SLOT,
                                         cmEjRunSlot, NULL, NULL, op,
                                         sizeof(op), nhi->ClockMs);
        }
        if (d->Dec[i].State == CM_EJ_D_DECIDED) {
            d->Decided++;
            CmNvlinkDecided(nhi, &d->Dec[i]);
            d->Dec[i].State = CM_EJ_D_IDLE;
            (VOID)InterlockedDecrement(&cmEjDecPending);
        }
    }
}

VOID CmEjectService(PCM_NHI nhi, BOOLEAN nested)
{
    CM_EJ_REC *rec;
    KIRQL irql;
    ULONG i;
    ULONG member;
    ULONG now;
    BOOLEAN owner;
    BOOLEAN present;
    BOOLEAN consumed;
    UCHAR zero[CM_EJ_RESULT_BYTES];

    if (!cmEj9x(nhi)) {
        return;
    }
    if (!nhi->Ej.Stepping) {
        /* The decisions once at a time; a servicing point reached from
         * inside them (none waits) skips them. */
        nhi->Ej.Stepping = 1;
        cmEjDecisions(nhi);
        nhi->Ej.Stepping = 0;
    }
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        rec = &cmEj[i];
        /* A stopped start's record on one of this start's members is
         * adopted before anything else of that member (CmEjectHeld has
         * held it meanwhile). */
        KeAcquireSpinLock(&cmEjLock, &irql);
        if (rec->Tx.Phase != CM_EJ_P_FREE && rec->Nhi == NULL &&
            rec->LateRunning == 0) {
            KeReleaseSpinLock(&cmEjLock, irql);
            member = cmEjMemberOf(nhi, rec->Tx.Bridge, FALSE);
            KeAcquireSpinLock(&cmEjLock, &irql);
            if (member != XHCI_TBT_NO_MEMBER && rec->Nhi == NULL &&
                rec->LateRunning == 0 && rec->Tx.Phase != CM_EJ_P_FREE) {
                rec->Nhi = nhi;
                rec->Member = member;
                rec->Tool = 0;
                /* A call of the stopped start still out stays its late
                 * taker's (OpId kept): the adoption waits for it. */
                rec->DownSeen = 0;
                CmEjAdopt(&rec->Tx, cmEjClockMs);
                KeReleaseSpinLock(&cmEjLock, irql);
                CmLogNote(nhi, "cm.eject.adopt",
                          (rec->Tx.Bridge << 8) | rec->Tx.Phase);
                KeAcquireSpinLock(&cmEjLock, &irql);
            }
        }
        if (rec->Nhi != nhi || rec->Tx.Phase == CM_EJ_P_FREE ||
            rec->InStep) {
            /* A record whose own step is waiting further up this thread
             * (a teardown's safe points) is left to that step; every
             * other record is stepped (R6e: no member's wait delays
             * another's work). */
            KeReleaseSpinLock(&cmEjLock, irql);
            continue;
        }
        rec->InStep = 1;
        KeReleaseSpinLock(&cmEjLock, irql);
        cmEjStepRec(nhi, rec, nested);
        KeAcquireSpinLock(&cmEjLock, &irql);
        rec->InStep = 0;
        KeReleaseSpinLock(&cmEjLock, irql);
        if (nested) {
            continue;
        }
        now = cmEjNow();
        owner = (BOOLEAN)(nhi->Ej.Owner == i && rec->Tool);
        if (owner) {
            cmEjWrite(nhi, rec, now);
        }
        present = (BOOLEAN)(nhi->Ej.AskPresent &&
                            nhi->Ej.AskNonce == rec->Tx.Nonce);
        KeAcquireSpinLock(&cmEjLock, &irql);
        consumed = owner ? CmEjConsume(&rec->Tx, present, now) : FALSE;
        if (!owner && (rec->Tx.Flags & CM_EJ_F_CONSUMED) == 0) {
            rec->Tx.Flags |= CM_EJ_F_CONSUMED;
        }
        if (CmEjSpent(&rec->Tx) && rec->OpId == 0) {
            rec->Tx.Phase = CM_EJ_P_FREE;
            rec->Nhi = NULL;
        }
        KeReleaseSpinLock(&cmEjLock, irql);
        if (consumed) {
            nhi->Ej.Owner = CM_EJ_NONE;
            if (!present) {
                /* The ask gone after a terminal step: the slot's nonce
                 * cleared. */
                CmZero(zero, sizeof(zero));
                (VOID)CmStatusEjectWrite(nhi, TRUE, zero, sizeof(zero));
            }
            CmLogNote(nhi, "cm.eject.consumed", present ? 1UL : 0UL);
        }
    }
}

VOID CmEjectAsk(PCM_NHI nhi, BOOLEAN present, const UCHAR *bytes,
                ULONG length)
{
    CM_EJ_ASK ask;
    CM_EJ_CHECK check;
    CM_EJ_REC *rec;
    UCHAR reply[CM_EJ_REPLY_BYTES];
    UCHAR zero[CM_EJ_RESULT_BYTES];
    KIRQL irql;
    ULONG why;
    ULONG member;
    ULONG secondary;
    ULONG cls;
    ULONG free;
    ULONG i;
    ULONG cbus;
    ULONG cslot;
    ULONG cgen;
    BOOLEAN decoded;

    decoded = (BOOLEAN)(present && CmEjAskDecode(bytes, length, &ask));
    if (!nhi->Ej.AskRead) {
        /* The start's first read: an ask found now was written before this
         * start and is never taken; the slot's old result is cleared. */
        nhi->Ej.AskRead = 1;
        nhi->Ej.AskNonce = decoded ? ask.Nonce : 0;
        nhi->Ej.AskPresent = present ? 1UL : 0UL;
        CmZero(zero, sizeof(zero));
        (VOID)CmStatusEjectWrite(nhi, TRUE, zero, sizeof(zero));
        return;
    }
    nhi->Ej.AskPresent = decoded ? 1UL : 0UL;
    if (!decoded || ask.Nonce == nhi->Ej.AskNonce) {
        return;
    }
    nhi->Ej.AskNonce = ask.Nonce;

    /* The checks, read now; this read is the one immediately before
     * acceptance, so an ask the tool deleted is never accepted. */
    CmZero(&check, sizeof(check));
    check.Available = (BOOLEAN)(cmEj9x(nhi) && CmNhiRunning(nhi) &&
                                CmLiveTransport(&nhi->Live));
    check.EpochLo = nhi->QueryEpochLo;
    check.EpochHi = nhi->QueryEpochHi;
    if (nhi->QueryLastBytes >= CM_Q_HDR_BYTES) {
        check.ContentGen =
            (ULONG)nhi->QueryLast[CM_Q_O_CONTENT_GEN] |
            ((ULONG)nhi->QueryLast[CM_Q_O_CONTENT_GEN + 1] << 8) |
            ((ULONG)nhi->QueryLast[CM_Q_O_CONTENT_GEN + 2] << 16) |
            ((ULONG)nhi->QueryLast[CM_Q_O_CONTENT_GEN + 3] << 24);
    }
    member = cmEjMemberOf(nhi, ask.Member & 0xFFFFUL, TRUE);
    check.Member = (BOOLEAN)(member != XHCI_TBT_NO_MEMBER &&
                             member < XHCI_TBT_MAX_TUNNELS);
    if (check.Member && !CmCallwkLive(nhi->Reenum.Callwk, member)) {
        /* That member's call worker was never made: nothing could make
         * the transaction's calls. */
        check.Available = FALSE;
    }
    cls = 0;
    if (check.Member) {
        secondary = (cmEjCfg(ask.Member, 0x18) >> 8) & 0xFFUL;
        if (secondary == 0xFFUL) {
            secondary = 0;
        }
        if ((ask.Unit >> 8) == 0 && (ask.Unit & 0xFFFF0000UL) == 0) {
            /* The tool named the devfn below the member alone. */
            ask.Unit |= secondary << 8;
        }
        if ((ask.Unit >> 8) != 0) {
            check.UnitId = cmEjCfg(ask.Unit, 0);
            cls = cmEjCfg(ask.Unit, 8);
            if (cls == 0 || cls == 0xFFFFFFFFUL) {
                /* The class not established: never read as "not an NVMe
                 * function"; nothing taken for answering. */
                check.UnitId = 0;
            }
        }
        check.Below = (BOOLEAN)(secondary != 0 &&
                                secondary == XHCI_TBT_BDF_BUS(ask.Unit));
    }
    free = CM_EJ_NONE;
    KeAcquireSpinLock(&cmEjLock, &irql);
    check.Busy = (BOOLEAN)(nhi->Ej.Owner != CM_EJ_NONE);
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        if (cmEj[i].Tx.Phase == CM_EJ_P_FREE) {
            if (free == CM_EJ_NONE) {
                free = i;
            }
            continue;
        }
        if (cmEj[i].Tx.Bridge == ask.Member &&
            (cmEj[i].Nhi == nhi || cmEj[i].Nhi == NULL) &&
            CmEjGate(&cmEj[i].Tx)) {
            check.Busy = TRUE;
        }
    }
    if (check.Member && cmEjDecPosted(nhi, member)) {
        check.Busy = TRUE;
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    if (check.Member && (ask.Unit >> 8) != 0 &&
        ((cls >> 16) & 0xFFFFUL) == CM_NVLINK_CLASS_NVM) {
        /* An NVMe function: the channel must name it now, or no clean
         * stop can be asked and the ask is refused before the query. */
        check.NoChannel = TRUE;
        for (i = 0; i < STOR98LS_ENTRIES; i++) {
            if (CmNvlinkWkCandidate(nhi, ask.Member, check.UnitId, i, &cbus,
                                    &cslot, &cgen) &&
                cbus == XHCI_TBT_BDF_BUS(ask.Unit) &&
                cslot == (XHCI_TBT_BDF_DEV(ask.Unit) |
                          (XHCI_TBT_BDF_FN(ask.Unit) << 5))) {
                check.NoChannel = FALSE;
            }
        }
    }
    KeAcquireSpinLock(&cmEjLock, &irql);
    why = CmEjAccept(&ask, &check);
    if (why == 0 &&
        (free == CM_EJ_NONE || cmEj[free].Tx.Phase != CM_EJ_P_FREE)) {
        why = CM_EJ_WHY_BUSY;
    }
    rec = NULL;
    if (why == 0) {
        rec = &cmEj[free];
        CmZero(rec, sizeof(*rec));
        CmEjBegin(&rec->Tx, &ask, check.UnitId,
                  (BOOLEAN)(((cls >> 16) & 0xFFFFUL) == CM_NVLINK_CLASS_NVM),
                  cmEjClockMs);
        rec->Nhi = nhi;
        rec->Member = member;
        rec->Tool = 1;
        nhi->Ej.Owner = free;
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    if (why != 0) {
        nhi->Ej.Rejected++;
        CmEjReplyEncode(reply, ask.Nonce, why);
        (VOID)CmStatusEjectWrite(nhi, FALSE, reply, sizeof(reply));
        CmLogNote(nhi, "cm.eject.rejected",
                  (why << 24) | (ask.Unit & 0xFFFFUL));
        return;
    }
    nhi->Ej.Accepted++;
    CmLogNote(nhi, "cm.eject.accepted",
              ((ask.Member & 0xFFFFUL) << 16) | (ask.Unit & 0xFFFFUL));
    /* "accepted" before any call is submitted. */
    cmEjWrite(nhi, rec, cmEjNow());
}

BOOLEAN CmEjectHeld(PCM_NHI nhi, ULONG bridge)
{
    KIRQL irql;
    ULONG i;
    BOOLEAN held;

    held = FALSE;
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (nhi->Ej.Dec[i].State == CM_EJ_D_POSTED &&
            nhi->Ej.Dec[i].Bridge == bridge) {
            held = TRUE;
        }
    }
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_RECORDS && !held; i++) {
        if (cmEj[i].Tx.Phase != CM_EJ_P_FREE &&
            cmEj[i].Tx.Bridge == bridge &&
            (cmEj[i].Nhi == nhi || cmEj[i].Nhi == NULL) &&
            CmEjGate(&cmEj[i].Tx)) {
            held = TRUE;
        }
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    return held;
}

BOOLEAN CmEjectPending(VOID)
{
    ULONG i;

    /* Read without the lock (any IRQL): a state that changes just after is
     * seen at the next pass. */
    if (cmEjDecPending > 0) {
        return TRUE;
    }
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        if (cmEj[i].Nhi != NULL && CmEjBusy(&cmEj[i].Tx)) {
            return TRUE;
        }
    }
    return FALSE;
}

ULONG CmEjectBlockedMask(PCM_NHI nhi)
{
    KIRQL irql;
    ULONG i;
    ULONG mask;

    mask = 0;
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        if (cmEj[i].Nhi != nhi || !CmEjGate(&cmEj[i].Tx) ||
            cmEj[i].Member >= XHCI_TBT_MAX_TUNNELS) {
            continue;
        }
        if ((cmEj[i].Tx.Flags & CM_EJ_F_BLOCKED) != 0) {
            mask |= 1UL << cmEj[i].Member;
        } else {
            mask |= 0x10UL << cmEj[i].Member;
        }
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    return mask;
}

VOID CmEjectDown(PCM_NHI nhi, ULONG member)
{
    KIRQL irql;
    ULONG i;

    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        if (cmEj[i].Nhi == nhi && cmEj[i].Tx.Phase != CM_EJ_P_FREE &&
            (member == XHCI_TBT_NO_MEMBER || cmEj[i].Member == member)) {
            cmEj[i].DownSeen = 1;
        }
    }
    KeReleaseSpinLock(&cmEjLock, irql);
}

VOID CmEjectDownBegin(PCM_NHI nhi)
{
    nhi->Ej.DownAt = cmEjNow();
    nhi->Ej.DownAtValid = 1;
}

ULONG CmEjectDownLeft(PCM_NHI nhi, ULONG budgetMs)
{
    ULONG used;

    if (!nhi->Ej.DownAtValid) {
        return budgetMs;
    }
    used = cmEjNow() - nhi->Ej.DownAt;
    return used >= budgetMs ? 0 : budgetMs - used;
}

BOOLEAN CmEjectDecide(PCM_NHI nhi, ULONG member, ULONG kind,
                      BOOLEAN pathClosed, ULONG budgetMs)
{
    CM_EJ_DOM *d;
    ULONG op[3];

    if (!cmEj9x(nhi) || member >= nhi->Reenum.Members ||
        member >= XHCI_TBT_MAX_TUNNELS) {
        return FALSE;
    }
    d = &nhi->Ej;
    if (d->Dec[member].State == CM_EJ_D_POSTED) {
        /* One decision at a time per member: a second down before the
         * first is decided joins it. */
        return TRUE;
    }
    d->DecSerial++;
    if (d->DecSerial == 0) {
        d->DecSerial = 1;
    }
    /* Entered before the read is posted: the identity and the absolute
     * deadline first. */
    CmEjDecideBegin(&d->Dec[member], kind, member,
                    nhi->Reenum.Port[member].Bdf, d->DecSerial,
                    (kind == 1 && d->DownAtValid) ? d->DownAt : cmEjNow(),
                    budgetMs);
    d->Dec[member].PathClosed = pathClosed;
    (VOID)InterlockedIncrement(&cmEjDecPending);
    if (d->DecOp[member] == 0 &&
        !CmCallwkBusy(nhi->Reenum.Callwk, member)) {
        op[0] = d->Dec[member].Bridge;
        op[1] = cmEjCfg(op[0], 0);
        op[2] = d->DecSerial;
        d->DecOp[member] = CmCallwkSubmit(nhi->Reenum.Callwk, member,
                                          CM_EJ_K_SLOT, cmEjRunSlot, NULL,
                                          NULL, op, sizeof(op),
                                          nhi->ClockMs);
    }
    CmLogNote(nhi, "cm.nvlink.9x.decision",
              (kind << 24) | (member << 16) | (budgetMs & 0xFFFFUL));
    return TRUE;
}

VOID CmEjectStop(PCM_NHI nhi)
{
    CM_EJ_REC *rec;
    KIRQL irql;
    ULONG i;
    ULONG id;
    BOOLEAN owes;

    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (nhi->Ej.Dec[i].State != CM_EJ_D_IDLE) {
            /* No statement after the stop: withheld with it. */
            nhi->Ej.Dec[i].State = CM_EJ_D_IDLE;
            (VOID)InterlockedDecrement(&cmEjDecPending);
        }
    }
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        rec = &cmEj[i];
        KeAcquireSpinLock(&cmEjLock, &irql);
        if (rec->Nhi != nhi || rec->Tx.Phase == CM_EJ_P_FREE) {
            KeReleaseSpinLock(&cmEjLock, irql);
            continue;
        }
        owes = (BOOLEAN)((CmEjOwesRemoval(&rec->Tx) ||
                          rec->Tx.Phase == CM_EJ_P_WITHDRAW) &&
                         rec->OpId == 0 && rec->LateRunning == 0);
        /* Detached, and (when owed) held for the detached operation, before
         * the worker can run it: no adoption meanwhile. */
        rec->Nhi = NULL;
        if (owes) {
            rec->LateRunning = 1;
        }
        KeReleaseSpinLock(&cmEjLock, irql);
        id = 0;
        if (owes) {
            id = CmCallwkSubmit(nhi->Reenum.Callwk, rec->Member,
                                CM_EJ_K_DETACHED, cmEjRunDetached, cmEjLate,
                                rec, NULL, 0, nhi->ClockMs);
        }
        if (owes && id == 0) {
            /* No worker to take it: a later start adopts it. */
            KeAcquireSpinLock(&cmEjLock, &irql);
            rec->LateRunning = 0;
            KeReleaseSpinLock(&cmEjLock, irql);
        }
        CmLogNote(nhi, "cm.eject.detached",
                  (rec->Tx.Bridge << 8) | (rec->Tx.Phase & 0xFFUL));
    }
    nhi->Ej.Owner = CM_EJ_NONE;
    CmLogNote(nhi, "cm.eject.accepted.count", nhi->Ej.Accepted);
    CmLogNote(nhi, "cm.eject.rejected.count", nhi->Ej.Rejected);
    CmLogNote(nhi, "cm.eject.writes", (nhi->Ej.WriteFailures << 16) |
                                          (nhi->Ej.Writes & 0xFFFFUL));
    CmLogNote(nhi, "cm.nvlink.9x.decided", (nhi->Ej.DecidedLate << 16) |
                                               (nhi->Ej.Decided & 0xFFFFUL));
}
