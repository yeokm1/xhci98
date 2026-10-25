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

#define CM_EJ_RECORDS       16UL
#define CM_EJ_SWEEPS        8UL
#define CM_EJ_SWEEP_IDS     64UL

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
#define CM_EJ_K_SWEEP       0x48UL

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
    ULONG Emulator;         /* the member read as the emulator's bridge: 0
                               not read yet, 1 no, 2 yes                   */
    ULONG SweepGone;        /* an accepted eject whose unit a sweep read
                               vanished (task 43.47): as a tunnel down on
                               its member                                 */
    ULONG VanishOwed;       /* a sweep read this record's devnode vanished
                               while the record held it without owning it
                               (a claim, an eject whose query was not
                               accepted): the record carries the vanished
                               child's obligation (task 43.47)            */
    char Id[CM_EJ_ID_CHARS];
} CM_EJ_REC;

/* A member's sweep for its vanished children (task 43.47), image-static as
 * the records are: detached at the NHI's stop, adopted by a later start
 * whose set names its bridge. */
typedef struct _CM_EJ_SWP {
    CM_EJ_SWEEP S;
    ULONG InUse;
    ULONG Bridge;
    struct _CM_NHI *Nhi;    /* the start that owns it; NULL detached       */
    ULONG Member;
    ULONG OpId;             /* the reading outstanding, 0 none             */
    PVOID CallBlock;
    ULONG PendNode;         /* a vanished child found with no record free:
                               kept, made a record once one is            */
    char PendId[CM_EJ_ID_CHARS];
} CM_EJ_SWP;

static KSPIN_LOCK cmEjLock;
static CM_EJ_REC cmEj[CM_EJ_RECORDS];
static CM_EJ_SWP cmEjSw[CM_EJ_SWEEPS];
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

/*
 * Who names `devnode` with instance id `id` (task 43.47), under cmEjLock,
 * `self` left out: CM_EJ_OWN_RECORD for a vanished record not retired, an
 * eject whose query was accepted, or a child a sweep keeps for its record;
 * CM_EJ_OWN_CLAIM for a CONFIGMG worker's claim or an eject whose query is
 * not accepted yet (it may end refused); CM_EJ_OWN_NONE otherwise.
 */
static ULONG cmEjOwnerAtLocked(const CM_EJ_REC *self, ULONG devnode,
                               const char *id, PULONG at)
{
    ULONG i;
    const CM_EJ_REC *rec;

    *at = CM_EJ_NONE;
    if (devnode == 0 || id == NULL || id[0] == '\0') {
        return CM_EJ_OWN_NONE;
    }
    for (i = 0; i < CM_EJ_SWEEPS; i++) {
        if (cmEjSw[i].InUse && cmEjSw[i].PendNode == devnode &&
            cmEjIdEqual(cmEjSw[i].PendId, id)) {
            return CM_EJ_OWN_RECORD;
        }
    }
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        rec = &cmEj[i];
        if (rec == self || rec->Tx.Phase == CM_EJ_P_FREE ||
            rec->Tx.Phase == CM_EJ_P_DONE || rec->Tx.Devnode != devnode ||
            !cmEjIdEqual(rec->Id, id)) {
            continue;
        }
        *at = i;
        if ((rec->Tx.Flags & CM_EJ_F_VANISH) != 0) {
            return rec->Tx.Phase == CM_EJ_P_CLAIMED ? CM_EJ_OWN_CLAIM
                                                    : CM_EJ_OWN_RECORD;
        }
        return (rec->Tx.Flags & CM_EJ_F_QUERIED) != 0 ? CM_EJ_OWN_RECORD
                                                      : CM_EJ_OWN_CLAIM;
    }
    return CM_EJ_OWN_NONE;
}

static ULONG cmEjOwnerLocked(const CM_EJ_REC *self, ULONG devnode,
                             const char *id)
{
    ULONG at;

    return cmEjOwnerAtLocked(self, devnode, id, &at);
}

static VOID cmEjIdCopy(char *to, const char *from)
{
    ULONG k;

    for (k = 0; k < CM_EJ_ID_CHARS - 1 && from[k] != '\0'; k++) {
        to[k] = from[k];
    }
    to[k] = '\0';
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

/* A vanished child's record, under cmEjLock, for the sweep `sw`: TRUE when
 * made (its start the sweep's, NULL while detached: adopted then). */
static BOOLEAN cmEjRecordLocked(CM_EJ_SWP *sw, ULONG node, const char *id,
                                ULONG now)
{
    CM_EJ_REC *rec;
    ULONG i;

    for (i = 0; i < CM_EJ_RECORDS; i++) {
        rec = &cmEj[i];
        if (rec->Tx.Phase != CM_EJ_P_FREE) {
            continue;
        }
        CmZero(rec, sizeof(*rec));
        CmEjBeginVanished(&rec->Tx, sw->Bridge, node, now);
        rec->Nhi = sw->Nhi;
        rec->Member = sw->Member;
        cmEjIdCopy(rec->Id, id);
        return TRUE;
    }
    return FALSE;
}

/*
 * A vanished child observed by the sweep `sw`, under cmEjLock, recorded at
 * once in image-static state, so no observation lives only in a call's
 * result: nobody's - a vanished record made (no record free: kept in the
 * sweep, PendNode, one; *full); held by a claim or an eject not accepted -
 * that record carries the obligation (VanishOwed); owned by an accepted
 * eject - given its disappearance (SweepGone).
 */
static VOID cmEjObserveLocked(CM_EJ_SWP *sw, ULONG node, const char *id,
                              ULONG now, PULONG made, BOOLEAN *full)
{
    ULONG at;
    ULONG owner;

    owner = cmEjOwnerAtLocked(NULL, node, id, &at);
    if (owner == CM_EJ_OWN_NONE) {
        if (cmEjRecordLocked(sw, node, id, now)) {
            (*made)++;
        } else {
            *full = TRUE;
            if (sw->PendNode == 0) {
                sw->PendNode = node;
                cmEjIdCopy(sw->PendId, id);
            }
        }
        return;
    }
    if (at == CM_EJ_NONE) {
        /* The sweep's own kept child. */
        return;
    }
    if (owner == CM_EJ_OWN_CLAIM) {
        cmEj[at].VanishOwed = 1;
    } else if ((cmEj[at].Tx.Flags & CM_EJ_F_VANISH) == 0) {
        cmEj[at].SweepGone = 1;
    }
}

/*
 * SWEEP (task 43.47): op [0] the member; context the sweep. The identities
 * answering on its secondary bus (the CONFIGMG worker's reading), its
 * devnode by path, its children walked; every vanished child recorded as
 * it is read (cmEjObserveLocked). Result [0] 1 when the bus and every
 * child were read, [1] records made, [2] 1 when a child found no free
 * record.
 */
static ULONG cmEjRunSweep(PVOID route, PVOID context, const VOID *op,
                          VOID *res)
{
    PCM_CFGMG cfg;
    CM_EJ_SWP *sw;
    PULONG out;
    ULONG member;
    ULONG ids[CM_EJ_SWEEP_IDS];
    ULONG path[CM_PORTS_PATH_MAX];
    ULONG nIds;
    ULONG n;
    ULONG node;
    ULONG memberNode;
    ULONG count;
    ULONG cr;
    ULONG id;
    ULONG none;
    ULONG made;
    ULONG now;
    BOOLEAN full;
    KIRQL irql;
    char text[CM_EJ_ID_CHARS];

    cfg = (PCM_CFGMG)route;
    sw = (CM_EJ_SWP *)context;
    member = ((const ULONG *)op)[0];
    out = (PULONG)res;
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    made = 0;
    full = FALSE;
    nIds = CmCfgwkBusIds(member, ids, CM_EJ_SWEEP_IDS);
    if (nIds > CM_EJ_SWEEP_IDS) {
        /* Not read completely: nothing is taken for vanished. */
        return 12;
    }
    memberNode = cmEjMemberNode(cfg, member, path, &n);
    if (memberNode == 0) {
        return 12;
    }
    cr = CmCfgmgChild(cfg, memberNode, &node);
    if (cr == CMG_CR_NO_SUCH_DEVNODE) {
        node = 0;
    } else if (cr != CMG_CR_SUCCESS) {
        return 12;
    }
    for (count = 0; node != 0; count++) {
        if (count == CM_EJ_WALK_NODES ||
            CmCfgmgDeviceId(cfg, node, text, sizeof(text)) !=
                CMG_CR_SUCCESS) {
            goto incomplete;
        }
        text[CM_EJ_ID_CHARS - 1] = '\0';
        if (!CmPortsIdVenDev(text, sizeof(text), &id)) {
            /* Its ids not read from the instance id: never taken for
             * vanished (the worker's removal's rule). */
            id = 0;
        }
        none = CM_EJ_OWN_NONE;
        if (CmEjSweepPick(ids, nIds, &id, &none, 1, &n) == 0) {
            now = cmEjNow();
            KeAcquireSpinLock(&cmEjLock, &irql);
            cmEjObserveLocked(sw, node, text, now, &made, &full);
            KeReleaseSpinLock(&cmEjLock, irql);
        }
        cr = CmCfgmgSibling(cfg, node, &node);
        if (cr == CMG_CR_NO_SUCH_DEVNODE) {
            break;
        }
        if (cr != CMG_CR_SUCCESS) {
            goto incomplete;
        }
    }
    out[0] = 1;

incomplete:
    out[1] = made;
    out[2] = full ? 1UL : 0UL;
    return 12;
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
        if (len != 0 && cmEjOwnerLocked(rec, res[0], rec->Id) !=
                            CM_EJ_OWN_NONE) {
            /* A vanished child's record, a sweep or a CONFIGMG worker's
             * claim names the devnode (task 43.47): not found, nothing
             * called. */
            len = 0;
        }
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

/* The eject's statements: the systems' admission and the member the
 * emulator's bridge (CmEjStatementQualified). Decision V4 admits the
 * systems one at a time; both are, each on its own reading, so no version
 * read has to tell them apart here. The member is read once per record,
 * only once the systems are admitted (on the thread, configuration reads
 * alone). */
static BOOLEAN cmEjQualified(PCM_NHI nhi, CM_EJ_REC *rec)
{
    if (!CmEjStatementQualified(TRUE)) {
        return FALSE;
    }
    if (rec->Emulator == 0) {
        rec->Emulator = CmNvlinkEmulatorMember(nhi, rec->Tx.Bridge) ? 2UL
                                                                    : 1UL;
        CmLogNote(nhi, "cm.eject.emulator",
                  (rec->Tx.Bridge << 8) | rec->Emulator);
    }
    return CmEjStatementQualified((BOOLEAN)(rec->Emulator == 2));
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
    for (i = 0; i < CM_EJ_SWEEPS; i++) {
        if (cmEjSw[i].Nhi == nhi) {
            cmEjSw[i].Nhi = NULL;
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
    BOOLEAN vanish;
    KIRQL irql;

    port = CmNhiMemberPort(nhi, rec->Member);
    vanish = (BOOLEAN)((rec->Tx.Flags & CM_EJ_F_VANISH) != 0);
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
            if (vanish && r == CM_CALL_T_TAKEN && got >= 4) {
                /* A vanished child's call's CONFIGRET (the legs read the
                 * veto's retries from it). */
                CmLogNote(nhi, "cm.vanish.cr",
                          (rec->Tx.Bridge << 16) |
                              ((rec->Tx.Phase & 0xFFUL) << 8) |
                              (res[0] & 0xFFUL));
            }
        }
    }
    for (n = 0; n < 12; n++) {
        CmZero(&in, sizeof(in));
        in.NowMs = cmEjNow();
        in.Nested = nested;
        in.U4Known = (BOOLEAN)(port != 0);
        in.Qualified = vanish ? FALSE : cmEjQualified(nhi, rec);
        busy = CmCallwkBusy(nhi->Reenum.Callwk, rec->Member);
        in.SlotIdle = (BOOLEAN)(!busy && !cmEjDecPosted(nhi, rec->Member));
        in.AckHeld = CmNvlinkAckHeld(nhi, rec->Tx.Bridge);
        if (vanish && in.AckHeld) {
            /* A vanished child's removal calls wait for the removal gate:
             * the statement's acknowledgement before the removal (task
             * 43.47; obligation 3). */
            in.SlotIdle = FALSE;
        }
        KeAcquireSpinLock(&cmEjLock, &irql);
        /* The unit gone counts only after a tunnel down on its member, its
         * statement decided and any acknowledgement wait ended: an
         * all-ones read alone is a presence signal, not isolation (section
         * 7.3). */
        if (!vanish &&
            (rec->DownSeen || rec->SweepGone || rec->VanishOwed) &&
            !in.AckHeld &&
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
        case CM_EJ_DO_RETIRE:
            marked = CmNvlinkRetireEjected(nhi, rec->Tx.Bridge,
                                           rec->Tx.Entry);
            CmLogNote(nhi, "cm.eject.retire",
                      (rec->Tx.Entry << 8) | (marked ? 1UL : 0UL));
            t = cmEjNow();
            KeAcquireSpinLock(&cmEjLock, &irql);
            CmEjFeed(&rec->Tx, CM_EJ_FED_RETIRE, marked ? 1UL : 0UL, 0, t);
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

/* ====================================================================== */
/* The vanished children (task 43.47)                                     */
/* ====================================================================== */

/* The child a full reading kept, made a record once one is free, under
 * cmEjLock: TRUE when it was (or another names it now). */
static BOOLEAN cmEjPendLocked(CM_EJ_SWP *sw, ULONG now)
{
    ULONG made;
    ULONG node;
    BOOLEAN full;
    char text[CM_EJ_ID_CHARS];

    node = sw->PendNode;
    cmEjIdCopy(text, sw->PendId);
    sw->PendNode = 0;
    made = 0;
    full = FALSE;
    cmEjObserveLocked(sw, node, text, now, &made, &full);
    return (BOOLEAN)!full;
}

/* The sweep's reading after the stop: read again by the adopting start. */
static VOID cmEjSwLate(PVOID route, PVOID context, ULONG kind, ULONG id,
                       ULONG outcome, const VOID *res, ULONG bytes)
{
    CM_EJ_SWP *sw;
    KIRQL irql;

    UNREFERENCED_PARAMETER(route);
    UNREFERENCED_PARAMETER(kind);
    UNREFERENCED_PARAMETER(outcome);
    UNREFERENCED_PARAMETER(res);
    UNREFERENCED_PARAMETER(bytes);
    sw = (CM_EJ_SWP *)context;
    if (sw == NULL) {
        return;
    }
    KeAcquireSpinLock(&cmEjLock, &irql);
    if (sw->OpId == id && id != 0) {
        sw->OpId = 0;
        CmEjSweepLost(&sw->S);
    }
    KeReleaseSpinLock(&cmEjLock, irql);
}

/* Each sweep of this start: a stopped start's adopted, its reading taken,
 * a kept child made a record, the next reading submitted. On the thread;
 * no call, no wait. */
static VOID cmEjSweeps(PCM_NHI nhi)
{
    CM_EJ_SWP *sw;
    ULONG res[CM_CALL_RES_BYTES / 4];
    ULONG op[1];
    ULONG got;
    ULONG r;
    ULONG i;
    ULONG id;
    ULONG end;
    ULONG member;
    ULONG bridge;
    ULONG now;
    ULONG note;
    BOOLEAN complete;
    BOOLEAN full;
    BOOLEAN made;
    BOOLEAN idle;
    BOOLEAN live;
    BOOLEAN adopted;
    BOOLEAN submit;
    KIRQL irql;

    /* No reading while the host interface is lost: the bridges may read
     * reset (record 21 section 5.5). */
    live = (BOOLEAN)(CmLiveTransport(&nhi->Live) && !nhi->Reenum.LossDrain);
    for (i = 0; i < CM_EJ_SWEEPS; i++) {
        sw = &cmEjSw[i];
        adopted = FALSE;
        KeAcquireSpinLock(&cmEjLock, &irql);
        if (sw->InUse && sw->Nhi == NULL && sw->OpId == 0) {
            bridge = sw->Bridge;
            KeReleaseSpinLock(&cmEjLock, irql);
            member = cmEjMemberOf(nhi, bridge, FALSE);
            KeAcquireSpinLock(&cmEjLock, &irql);
            if (member != XHCI_TBT_NO_MEMBER &&
                member < XHCI_TBT_MAX_TUNNELS && sw->InUse &&
                sw->Nhi == NULL && sw->OpId == 0 && sw->Bridge == bridge) {
                sw->Nhi = nhi;
                sw->Member = member;
                sw->CallBlock = NULL;
                CmEjSweepLost(&sw->S);
                adopted = TRUE;
            }
        }
        if (!sw->InUse || sw->Nhi != nhi) {
            KeReleaseSpinLock(&cmEjLock, irql);
            continue;
        }
        member = sw->Member;
        bridge = sw->Bridge;
        id = sw->CallBlock == (PVOID)nhi->Reenum.Callwk ? sw->OpId : 0;
        KeReleaseSpinLock(&cmEjLock, irql);
        if (adopted) {
            CmLogNote(nhi, "cm.vanish.sweep.adopt", (bridge << 8) | member);
        }
        if (id != 0) {
            r = CmCallwkTake(nhi->Reenum.Callwk, member, id, res,
                             sizeof(res), &got);
            if (r == CM_CALL_T_TAKEN || r == CM_CALL_T_STALE ||
                r == CM_CALL_T_NONE) {
                complete = (BOOLEAN)(r == CM_CALL_T_TAKEN && got >= 12 &&
                                     res[0] != 0);
                full = (BOOLEAN)(r == CM_CALL_T_TAKEN && got >= 12 &&
                                 res[2] != 0);
                made = (BOOLEAN)(r == CM_CALL_T_TAKEN && got >= 12 &&
                                 res[1] != 0);
                now = cmEjNow();
                end = CM_EJ_SW_END_NONE;
                KeAcquireSpinLock(&cmEjLock, &irql);
                sw->OpId = 0;
                if (r != CM_CALL_T_TAKEN) {
                    CmEjSweepLost(&sw->S);
                } else {
                    /* The reading recorded its children itself; a full
                     * one is read again once its kept child has a
                     * record (CmEjSweepMade). */
                    end = CmEjSweepTake(&sw->S, complete,
                                        (BOOLEAN)(made || full),
                                        (BOOLEAN)!full, 0, now);
                    if (made && !full) {
                        sw->S.Records += res[1] - 1;
                    }
                    if (end != CM_EJ_SW_END_NONE) {
                        sw->InUse = 0;
                    }
                }
                note = (sw->S.Readings << 16) | (sw->S.Records << 8);
                KeReleaseSpinLock(&cmEjLock, irql);
                CmLogNote(nhi, "cm.vanish.reading",
                          (bridge << 16) | (complete ? 0x8000UL : 0UL) |
                              (full ? 0x4000UL : 0UL) |
                              ((r == CM_CALL_T_TAKEN && got >= 12)
                                   ? (res[1] & 0xFFUL)
                                   : 0UL));
                if (end != CM_EJ_SW_END_NONE) {
                    CmLogNote(nhi, "cm.vanish.sweep.end", note | member);
                }
            }
        }
        /* A child a full reading kept, made a record once one is free:
         * never read again for it. */
        now = cmEjNow();
        KeAcquireSpinLock(&cmEjLock, &irql);
        if (sw->InUse && sw->Nhi == nhi && sw->PendNode != 0 &&
            cmEjPendLocked(sw, now)) {
            CmEjSweepMade(&sw->S);
        }
        KeReleaseSpinLock(&cmEjLock, irql);
        /* The next reading: the decision's slot reading first, and never
         * beside a re-enumeration of the member the CONFIGMG worker has
         * admitted and not finished. */
        idle = (BOOLEAN)(live && !CmCallwkBusy(nhi->Reenum.Callwk, member) &&
                         !cmEjDecPosted(nhi, member) &&
                         (nhi->Reenum.Wk == NULL ||
                          (CmCfgwkEnumBusy(nhi->Reenum.Wk) &
                           (1UL << member)) == 0));
        now = cmEjNow();
        KeAcquireSpinLock(&cmEjLock, &irql);
        submit = (BOOLEAN)(sw->InUse && sw->Nhi == nhi && sw->PendNode == 0 &&
                           sw->OpId == 0 &&
                           CmEjSweepNext(&sw->S, idle, now));
        KeReleaseSpinLock(&cmEjLock, irql);
        if (!submit) {
            continue;
        }
        op[0] = bridge;
        id = CmCallwkSubmit(nhi->Reenum.Callwk, member, CM_EJ_K_SWEEP,
                            cmEjRunSweep, cmEjSwLate, sw, op, sizeof(op),
                            nhi->ClockMs);
        KeAcquireSpinLock(&cmEjLock, &irql);
        if (id != 0) {
            sw->OpId = id;
            sw->CallBlock = (PVOID)nhi->Reenum.Callwk;
        }
        CmEjSweepSubmitted(&sw->S, (BOOLEAN)(id != 0));
        KeReleaseSpinLock(&cmEjLock, irql);
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
    BOOLEAN retired;
    ULONG bridge;
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
    cmEjSweeps(nhi);
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        rec = &cmEj[i];
        /* A stopped start's record on one of this start's members is
         * adopted before anything else of that member (CmEjectHeld has
         * held it meanwhile). */
        KeAcquireSpinLock(&cmEjLock, &irql);
        if (rec->Tx.Phase == CM_EJ_P_CLAIMED &&
            (rec->Tx.Flags & CM_EJ_F_PARKED) != 0) {
            /* A parked claim (task 43.47): taken over where this start
             * has the member's call worker - the same claim, its bridge
             * read again after the lookup (the slot may have been reused
             * meanwhile). */
            bridge = rec->Tx.Bridge;
            KeReleaseSpinLock(&cmEjLock, irql);
            member = cmEjMemberOf(nhi, bridge, FALSE);
            KeAcquireSpinLock(&cmEjLock, &irql);
            if (member != XHCI_TBT_NO_MEMBER &&
                member < XHCI_TBT_MAX_TUNNELS &&
                rec->Tx.Bridge == bridge &&
                CmCallwkUsable(nhi->Reenum.Callwk, member) &&
                CmEjClaimAdopt(&rec->Tx, cmEjClockMs)) {
                rec->Nhi = NULL;
                rec->LateRunning = 0;
            }
        }
        if (rec->Tx.Phase != CM_EJ_P_FREE &&
            rec->Tx.Phase != CM_EJ_P_CLAIMED && rec->Nhi == NULL &&
            rec->LateRunning == 0) {
            bridge = rec->Tx.Bridge;
            KeReleaseSpinLock(&cmEjLock, irql);
            member = cmEjMemberOf(nhi, bridge, FALSE);
            KeAcquireSpinLock(&cmEjLock, &irql);
            if (member != XHCI_TBT_NO_MEMBER && rec->Nhi == NULL &&
                rec->LateRunning == 0 && rec->Tx.Phase != CM_EJ_P_FREE &&
                rec->Tx.Phase != CM_EJ_P_CLAIMED &&
                rec->Tx.Bridge == bridge) {
                rec->Nhi = nhi;
                rec->Member = member;
                rec->Tool = 0;
                /* A call of the stopped start still out stays its late
                 * taker's (OpId kept): the adoption waits for it. */
                rec->DownSeen = 0;
                /* A sweep's observation (SweepGone, VanishOwed) is kept:
                 * the vanished devnode's evidence stands for any start
                 * (task 43.47). */
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
        retired = (BOOLEAN)((rec->Tx.Flags & CM_EJ_F_VANISH) != 0 &&
                            rec->Tx.Phase == CM_EJ_P_DONE &&
                            rec->Tx.Pending == 0 && rec->OpId == 0 &&
                            rec->LateRunning == 0);
        KeReleaseSpinLock(&cmEjLock, irql);
        if (retired) {
            /* A vanished child retired (task 43.47), by whichever thread:
             * the CONFIGMG worker forgets its handle (a prune walk, paid
             * before any re-enumeration it makes) before the record lets
             * the gate fall. */
            if (nhi->Reenum.Wk != NULL) {
                CmCfgwkPrune(nhi->Reenum.Wk);
            }
            CmLogNote(nhi, "cm.vanish.retired",
                      (rec->Tx.Bridge << 16) | ((rec->Tx.Flags &
                                                 CM_EJ_F_BLOCKED) != 0
                                                    ? 0x100UL
                                                    : 0UL) |
                          (rec->Tx.RemoveCr & 0xFFUL));
            KeAcquireSpinLock(&cmEjLock, &irql);
            rec->Tx.Phase = CM_EJ_P_FREE;
            rec->Nhi = NULL;
            KeReleaseSpinLock(&cmEjLock, irql);
            continue;
        }
        KeAcquireSpinLock(&cmEjLock, &irql);
        retired = (BOOLEAN)((rec->Tx.Flags & CM_EJ_F_VANISH) == 0 &&
                            (rec->SweepGone || rec->VanishOwed) &&
                            rec->Tx.Phase == CM_EJ_P_DONE &&
                            (rec->Tx.Flags & CM_EJ_F_REMOVED) != 0);
        KeReleaseSpinLock(&cmEjLock, irql);
        if (retired) {
            /* An eject that removed a vanished child (task 43.47): the
             * worker's sets forget the handle (the prune posted) before
             * the flags that hold the gate and the barrier are cleared. */
            if (nhi->Reenum.Wk != NULL) {
                CmCfgwkPrune(nhi->Reenum.Wk);
            }
            KeAcquireSpinLock(&cmEjLock, &irql);
            rec->SweepGone = 0;
            rec->VanishOwed = 0;
            KeReleaseSpinLock(&cmEjLock, irql);
        }
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
            if (rec->VanishOwed &&
                (rec->Tx.Flags & CM_EJ_F_REMOVED) == 0) {
                /* An eject whose unit a sweep read vanished, ended without
                 * removing it (refused, not found): the vanished child's
                 * transaction from here, on the same devnode and id
                 * (task 43.47), its result already the tool's. */
                CmEjBeginVanished(&rec->Tx, rec->Tx.Bridge, rec->Tx.Devnode,
                                  now);
                rec->Tool = 0;
                rec->VanishOwed = 0;
                rec->SweepGone = 0;
            } else {
                rec->Tx.Phase = CM_EJ_P_FREE;
                rec->Nhi = NULL;
            }
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
    if (check.Member && CmEjectHeld(nhi, ask.Member & 0xFFFFUL)) {
        /* A sweep or a vanished child's record on the member (task
         * 43.47). */
        check.Busy = TRUE;
    }
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
        /* A vanished child's record holds it until its start has asked
         * for the prune walk and freed it, even once retired (task
         * 43.47). */
        if (cmEj[i].Tx.Phase != CM_EJ_P_FREE &&
            cmEj[i].Tx.Bridge == bridge &&
            (cmEj[i].Nhi == nhi || cmEj[i].Nhi == NULL) &&
            (CmEjGate(&cmEj[i].Tx) ||
             (cmEj[i].Tx.Flags & CM_EJ_F_VANISH) != 0 ||
             cmEj[i].SweepGone || cmEj[i].VanishOwed)) {
            held = TRUE;
        }
    }
    for (i = 0; i < CM_EJ_SWEEPS && !held; i++) {
        if (cmEjSw[i].InUse && cmEjSw[i].Bridge == bridge &&
            (cmEjSw[i].Nhi == nhi || cmEjSw[i].Nhi == NULL)) {
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
        if (cmEj[i].Nhi != NULL &&
            (CmEjBusy(&cmEj[i].Tx) ||
             ((cmEj[i].Tx.Flags & CM_EJ_F_VANISH) != 0 &&
              cmEj[i].Tx.Phase == CM_EJ_P_DONE))) {
            return TRUE;
        }
    }
    for (i = 0; i < CM_EJ_SWEEPS; i++) {
        if (cmEjSw[i].InUse && cmEjSw[i].Nhi != NULL) {
            return TRUE;
        }
    }
    return FALSE;
}

ULONG CmEjectBlockedMask(PCM_NHI nhi)
{
    KIRQL irql;
    ULONG i;
    ULONG k;
    ULONG n;
    ULONG m;
    ULONG now;
    ULONG mask;
    ULONG claim[CM_EJ_RECORDS];
    BOOLEAN claimBlocked[CM_EJ_RECORDS];

    mask = 0;
    n = 0;
    now = cmEjNow();
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        /* A CONFIGMG worker's claim belongs to no start: its member's bit
         * is read by location below, blocked once its removal has run
         * past O6's 30 s (task 43.47). */
        if (cmEj[i].Tx.Phase == CM_EJ_P_CLAIMED) {
            claim[n] = cmEj[i].Tx.Bridge;
            claimBlocked[n] = CmEjClaimBlocked(&cmEj[i].Tx, now);
            n++;
        }
    }
    for (i = 0; i < CM_EJ_SWEEPS; i++) {
        if (cmEjSw[i].InUse && cmEjSw[i].Nhi == nhi &&
            cmEjSw[i].Member < XHCI_TBT_MAX_TUNNELS) {
            mask |= 0x10UL << cmEjSw[i].Member;
        }
    }
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
    for (k = 0; k < n; k++) {
        m = cmEjMemberOf(nhi, claim[k], FALSE);
        if (m < XHCI_TBT_MAX_TUNNELS) {
            mask |= (claimBlocked[k] ? 1UL : 0x10UL) << m;
        }
    }
    /* A member both blocked and in progress reads blocked. */
    mask &= ~((mask & 0xFUL) << 4);
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
    /* The sweeps first (task 43.47): a reading still running records into
     * detached records from here, which a later start adopts, never into
     * one owned by this stopping start after its scan below. */
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_SWEEPS; i++) {
        if (cmEjSw[i].Nhi == nhi) {
            cmEjSw[i].Nhi = NULL;
        }
    }
    KeReleaseSpinLock(&cmEjLock, irql);
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

/* ====================================================================== */
/* Task 43.47: the sweep's entry and the CONFIGMG worker's claims         */
/* ====================================================================== */

BOOLEAN CmEjectSweep(PCM_NHI nhi, ULONG member)
{
    CM_EJ_SWP *sw;
    CM_EJ_SWP *spare;
    KIRQL irql;
    ULONG bridge;
    ULONG now;
    ULONG i;

    if (!cmEj9x(nhi) || member >= nhi->Reenum.Members ||
        member >= XHCI_TBT_MAX_TUNNELS ||
        !CmCallwkUsable(nhi->Reenum.Callwk, member)) {
        /* No call worker with an open route: the CONFIGMG worker's own
         * removal, as before. */
        return FALSE;
    }
    bridge = nhi->Reenum.Port[member].Bdf;
    now = cmEjNow();
    sw = NULL;
    spare = NULL;
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_SWEEPS; i++) {
        if (cmEjSw[i].InUse && cmEjSw[i].Bridge == bridge) {
            sw = &cmEjSw[i];
            break;
        }
        if (!cmEjSw[i].InUse && spare == NULL) {
            spare = &cmEjSw[i];
        }
    }
    if (sw == NULL && spare != NULL) {
        sw = spare;
        CmZero(sw, sizeof(*sw));
        sw->InUse = 1;
        sw->Bridge = bridge;
        sw->Nhi = nhi;
        sw->Member = member;
    }
    if (sw != NULL) {
        if (sw->Nhi == NULL && sw->OpId == 0) {
            /* A stopped start's: this one's from now. */
            sw->Nhi = nhi;
            sw->Member = member;
            sw->CallBlock = NULL;
            CmEjSweepLost(&sw->S);
        }
        CmEjSweepBegin(&sw->S, now);
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    if (sw == NULL) {
        /* Every sweep in use: the CONFIGMG worker's removal, as before. */
        CmLogNote(nhi, "cm.vanish.sweep.none", bridge);
        return FALSE;
    }
    CmLogNote(nhi, "cm.vanish.sweep", (bridge << 8) | member);
    return TRUE;
}

ULONG CmEjectNow(VOID)
{
    return cmEjNow();
}

VOID CmEjectNudge(PCM_NHI nhi, ULONG member)
{
    KIRQL irql;
    ULONG now;
    ULONG i;
    ULONG n;

    if (!cmEj9x(nhi)) {
        return;
    }
    now = cmEjNow();
    n = 0;
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_RECORDS; i++) {
        if (cmEj[i].Nhi == nhi &&
            (member == XHCI_TBT_NO_MEMBER || cmEj[i].Member == member) &&
            CmEjNudge(&cmEj[i].Tx, now)) {
            n++;
        }
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    if (n != 0) {
        CmLogNote(nhi, "cm.vanish.nudge", (member << 16) | n);
    }
}

ULONG CmEjectClaim(ULONG bridge, ULONG devnode, const char *id,
                   BOOLEAN *owned, BOOLEAN *accepted, PULONG entry,
                   PULONG sequence, PULONG generation)
{
    KIRQL irql;
    ULONG now;
    ULONG i;
    ULONG claim;

    *owned = FALSE;
    *accepted = FALSE;
    *entry = CM_EJ_NONE;
    *sequence = 0;
    *generation = 0;
    if (devnode == 0 || id == NULL || id[0] == '\0') {
        /* Without its instance id no record could follow the devnode. */
        return 0;
    }
    now = cmEjNow();
    claim = 0;
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_RECORDS && claim == 0; i++) {
        if (cmEj[i].Tx.Devnode == devnode && cmEjIdEqual(cmEj[i].Id, id) &&
            CmEjClaimResume(&cmEj[i].Tx)) {
            /* Its own parked claim: the query was accepted, the removal
             * alone is owed. */
            *accepted = TRUE;
            *entry = cmEj[i].Tx.Entry;
            *sequence = cmEj[i].Tx.Sequence;
            *generation = cmEj[i].Tx.Generation;
            cmEj[i].LateRunning = 1;
            claim = i + 1;
        }
    }
    if (claim != 0) {
        KeReleaseSpinLock(&cmEjLock, irql);
        return claim;
    }
    if (cmEjOwnerLocked(NULL, devnode, id) != CM_EJ_OWN_NONE) {
        *owned = TRUE;
    } else {
        for (i = 0; i < CM_EJ_RECORDS; i++) {
            if (cmEj[i].Tx.Phase != CM_EJ_P_FREE) {
                continue;
            }
            CmZero(&cmEj[i], sizeof(cmEj[i]));
            CmEjClaimBegin(&cmEj[i].Tx, bridge, devnode, now);
            /* The worker's while it lasts: never adopted, never stepped. */
            cmEj[i].LateRunning = 1;
            cmEjIdCopy(cmEj[i].Id, id);
            claim = i + 1;
            break;
        }
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    return claim;
}

VOID CmEjectClaimAccepted(ULONG claim, ULONG firstAt)
{
    KIRQL irql;

    if (claim == 0 || claim > CM_EJ_RECORDS) {
        return;
    }
    KeAcquireSpinLock(&cmEjLock, &irql);
    CmEjClaimProgress(&cmEj[claim - 1].Tx, firstAt);
    KeReleaseSpinLock(&cmEjLock, irql);
}

BOOLEAN CmEjectClaimEnd(ULONG claim, ULONG queryCr, ULONG removeCr,
                        ULONG firstAt, ULONG lastAt, ULONG attempts,
                        ULONG entry, ULONG sequence, ULONG generation,
                        BOOLEAN vanished, BOOLEAN takeover)
{
    CM_EJ_REC *rec;
    KIRQL irql;
    ULONG now;
    BOOLEAN took;

    if (claim == 0 || claim > CM_EJ_RECORDS) {
        return FALSE;
    }
    rec = &cmEj[claim - 1];
    now = cmEjNow();
    took = FALSE;
    KeAcquireSpinLock(&cmEjLock, &irql);
    if (rec->Tx.Phase == CM_EJ_P_CLAIMED) {
        /* Without a call worker an accepted query whose removal failed is
         * parked for this worker (removal only, CmEjectClaim) or a start
         * that can take it over; with one it becomes the transaction. */
        took = CmEjClaimEnd(&rec->Tx, queryCr, removeCr, firstAt, lastAt,
                            attempts, entry, sequence, generation,
                            (BOOLEAN)(vanished || rec->VanishOwed), takeover,
                            now);
        if (rec->Tx.Phase == CM_EJ_P_FREE) {
            rec->VanishOwed = 0;
        }
        /* A transaction now, or parked: adopted by the running start as a
         * stopped start's record is (no start owns it yet). */
        rec->Nhi = NULL;
        rec->LateRunning = 0;
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    return took;
}

BOOLEAN CmEjectVanishHeld(ULONG bridge)
{
    KIRQL irql;
    ULONG i;
    BOOLEAN held;

    held = FALSE;
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_RECORDS && !held; i++) {
        if (cmEj[i].Tx.Phase != CM_EJ_P_FREE &&
            ((cmEj[i].Tx.Flags & CM_EJ_F_VANISH) != 0 ||
             cmEj[i].SweepGone || cmEj[i].VanishOwed) &&
            cmEj[i].Tx.Bridge == bridge) {
            held = TRUE;
        }
    }
    for (i = 0; i < CM_EJ_SWEEPS && !held; i++) {
        if (cmEjSw[i].InUse && cmEjSw[i].Bridge == bridge) {
            held = TRUE;
        }
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    return held;
}

ULONG CmEjectParkedNext(ULONG bridge, PULONG devnode, char *id, ULONG idChars,
                        PULONG what, PULONG entry, PULONG sequence,
                        PULONG generation)
{
    KIRQL irql;
    ULONG now;
    ULONG i;
    ULONG claim;
    ULONG k;

    now = cmEjNow();
    claim = 0;
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_RECORDS && claim == 0; i++) {
        if (cmEj[i].Tx.Bridge != bridge) {
            continue;
        }
        *what = CmEjClaimRetry(&cmEj[i].Tx, now);
        if (*what == CM_EJ_DO_NONE) {
            continue;
        }
        cmEj[i].LateRunning = 1;
        *devnode = cmEj[i].Tx.Devnode;
        for (k = 0; k + 1 < idChars && cmEj[i].Id[k] != '\0'; k++) {
            id[k] = cmEj[i].Id[k];
        }
        id[k] = '\0';
        *entry = cmEj[i].Tx.Entry;
        *sequence = cmEj[i].Tx.Sequence;
        *generation = cmEj[i].Tx.Generation;
        claim = i + 1;
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    return claim;
}

BOOLEAN CmEjectParkedAny(ULONG bridge)
{
    KIRQL irql;
    ULONG i;
    BOOLEAN any;

    any = FALSE;
    KeAcquireSpinLock(&cmEjLock, &irql);
    for (i = 0; i < CM_EJ_RECORDS && !any; i++) {
        if (cmEj[i].Tx.Phase == CM_EJ_P_CLAIMED &&
            (cmEj[i].Tx.Flags & CM_EJ_F_PARKED) != 0 &&
            cmEj[i].Tx.Bridge == bridge) {
            any = TRUE;
        }
    }
    KeReleaseSpinLock(&cmEjLock, irql);
    return any;
}
