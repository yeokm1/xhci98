/*
 * tbt_icm.h - the client of an Intel Thunderbolt 3 controller's firmware
 * connection manager (the ICM), its pure half (roadmap-usb4.md task 39-A.3;
 * design record 21 sections 1.4 and 5).
 *
 * What is here: the ICM frames of ICM-PROTOCOL.md sections 3 and 4 behind a
 * per-generation table of encoders, decoders and capabilities (record 21
 * section 1.4), the bind, resume, suspend and unbind sequences of record 21
 * sections 5.1 and 5.6, the device table and the events of section 5.3,
 * the approval decision of section 5.4 behind a policy value and a hook
 * (decision D4: XhciCmApprove 0 or 1, task 39-A.6 reads it), and the
 * timeouts, retries and error flag of section 5.5. What is not: the
 * registry, PnP, power, the thread, the common buffer, the re-enumeration
 * and the nvme98 statement - the driver (39-A.4, 39-A.5, 39-A.7) does those
 * and reaches this file through the TBT_HOST accessor of tbt_ring.h, an
 * action queue it drains, and one upcall.
 *
 * Every word layout is ICM-PROTOCOL.md's (usb4-emu, branch main, read at
 * a68bc74), cited "ICM n.n" with its [obs] and [inf] tags; Linux's
 * drivers/thunderbolt is not a source (record 21 section 9).
 *
 * TWO ROWS. Alpine Ridge is captured (the P14s Gen 1, NHI 8086:15BF) and
 * every layout of its row is an observed frame or the description beside
 * one. Titan Ridge is NOT captured: its row is built from ICM 4.9's
 * comparison table alone (owner, decision D9 of 2026-10-15), and each byte
 * layout that table does not fix is this file's choice, marked UNCAPTURED
 * where it is made. The client is written so a wrong choice costs an
 * approval and never the thread: a response with the error flag, a
 * response that never comes, an event too short to decode - each leaves
 * the device unapproved, is counted and logged, and every wait is bounded.
 *
 * The sequence a driver runs: TbtIcmInit, TbtIcmStart (refusing the start
 * on a negative result), then on the thread every idle period TbtIcmPoll
 * followed by TbtIcmNextAction until it returns FALSE, running each tunnel
 * up or down the action names; TbtIcmSuspend and TbtIcmResume around a
 * system sleep; TbtIcmUnbind at stop. While the driver itself waits for a
 * tunnel port (record 21 section 6.5) it calls TbtIcmService between its
 * reads and TbtIcmActionLive to learn whether the device left meanwhile.
 *
 * IRQL and locking: as tbt_ring.h - PASSIVE_LEVEL, on the connection-
 * manager thread, no lock held, for every function that takes a TBT_ICM.
 * The decoders and encoders of the profile rows and TbtIcmProfileForId are
 * pure and callable at any IRQL. The LinkDown upcall and the approval hook
 * run on that thread, possibly inside a command wait, and must not call
 * back into this file.
 *
 * DDK-free: part of the pure core. C89, no 64-bit arithmetic, no floating
 * point.
 */

#ifndef TBT_ICM_H
#define TBT_ICM_H

#include "../xhci_compat.h"
#include "tbt_regs.h"
#include "tbt_ring.h"

/* ---------------------------------------------------------------------- */
/* ICM header word (ICM 3.1)                                              */
/* ---------------------------------------------------------------------- */

#define TBT_ICM_HDR_CODE(v)         ((ULONG)(v) & 0xFFUL)           /* 7:0 */
#define TBT_ICM_HDR_FLAGS(v)        (((ULONG)(v) >> 8) & 0xFFUL)    /* 15:8 */
#define TBT_ICM_HDR_INDEX(v)        (((ULONG)(v) >> 16) & 0xFFUL)   /* 23:16 */
#define TBT_ICM_HDR_COUNT(v)        (((ULONG)(v) >> 24) & 0xFFUL)   /* 31:24 */
/* Bit 0 of the flags is the error flag of every response (ICM 3.1, 4.8 [inf]). */
#define TBT_ICM_FLAG_ERROR          0x01UL
/* A request goes out with flags, index and count 0 (ICM 3.1 [obs] T:1). */
#define TBT_ICM_REQUEST(code)       ((ULONG)(code) & 0xFFUL)

/* Command codes, ICM 4's first table. */
#define TBT_ICM_CMD_DRIVER_READY    0x03UL
#define TBT_ICM_CMD_APPROVE         0x04UL
#define TBT_ICM_CMD_GET_ROUTE       0x0AUL
/* Event codes, ICM 4's second table. */
#define TBT_ICM_EV_CONNECTED        0x03UL
#define TBT_ICM_EV_DISCONNECTED     0x04UL

/* Driver-ready flags, shared by both generations (ICM 4.1, 4.9 [inf]). */
#define TBT_ICM_READY_RTD3          0x40UL  /* bit 6; logged, never acted on */

/* Link info, 16 bits (ICM 4.4): word 5 bits 31:16 of a device-connected
 * event, word 1 bits 31:16 of get route and device disconnected. */
#define TBT_ICM_LI_LINK(v)          ((ULONG)(v) & 0x7UL)            /* 2:0 */
#define TBT_ICM_LI_DEPTH(v)         (((ULONG)(v) >> 4) & 0xFUL)     /* 7:4 */
#define TBT_ICM_LI_APPROVED         0x0100UL                        /* 8 */
#define TBT_ICM_LI_REJECTED         0x0200UL                        /* 9 */
#define TBT_ICM_LI_BOOT             0x0400UL                        /* 10 */
#define TBT_ICM_LI_MAKE(link, depth) \
    (((ULONG)(link) & 0x7UL) | (((ULONG)(depth) & 0xFUL) << 4))

/* Device-connected event flags (header bits 15:8, ICM 4.4). */
#define TBT_ICM_EVF_DUAL_LANE       0x20UL  /* bit 5 */
#define TBT_ICM_EVF_SPEED_20G       0x80UL  /* bit 7, else 10 Gb/s */

/* The endpoint name block's entry types (ICM 4.4 [obs] T:9). */
#define TBT_ICM_NAME_VENDOR         0x01UL
#define TBT_ICM_NAME_MODEL          0x02UL
#define TBT_ICM_NAME_INTEL          0x10UL
#define TBT_ICM_NAME_UNKNOWN12      0x12UL

/* ---------------------------------------------------------------------- */
/* Sizes and timings                                                      */
/* ---------------------------------------------------------------------- */

/* The boot ACL's size, and more than one port carries at depth 1 (record
 * 21 section 5.3). */
#define TBT_ICM_MAX_DEVICES         16UL
#define TBT_ICM_EVENT_QUEUE         16UL
#define TBT_ICM_ACTION_QUEUE        48UL
#define TBT_ICM_NAME_CHARS          48UL    /* with the terminating NUL */

/* ICM 3 [inf], record 21 section 5.5 and section 8's bounds. */
#define TBT_ICM_CMD_TIMEOUT_MS      5000UL
#define TBT_ICM_APPROVE_TIMEOUT_MS  10000UL
#define TBT_ICM_TIMEOUT_MIN_MS      1000UL
#define TBT_ICM_TIMEOUT_MAX_MS      60000UL
#define TBT_ICM_RETRIES             3UL
/* The host-router poll after driver ready, ICM 4.1 [inf]: router space DW
 * 0 of route 0, 100 ms per try, up to 50 tries 50 ms apart. */
#define TBT_ICM_HOSTPOLL_TRIES      50UL
#define TBT_ICM_HOSTPOLL_TIMEOUT_MS 100UL
#define TBT_ICM_HOSTPOLL_GAP_MS     50UL
/* The reconciliation window, ICM 6.5 and 6.6 [inf]. */
#define TBT_ICM_RECONCILE_MS        500UL
/* How much one TbtIcmPoll takes before it returns to the driver. */
#define TBT_ICM_POLL_FRAMES         64UL
#define TBT_ICM_POLL_EVENTS         32UL

/* TbtIcmStart's one positive result: the firmware is in safe mode, the
 * start proceeds with no rings and nothing sent (record 21 section 5.1
 * step 5). */
#define TBT_ICM_SAFE_MODE           1L

/* ---------------------------------------------------------------------- */
/* Decoded messages                                                       */
/* ---------------------------------------------------------------------- */

typedef struct _TBT_ICM_READY {
    ULONG Words;            /* DWs the response carried */
    ULONG Flags;            /* header bits 15:8 */
    ULONG SecurityLevel;    /* ICM 4.1: 0 none, 1 user, 2 secure, ... */
    ULONG BootAclCount;
    BOOLEAN BootAclSupported;
    ULONG RomVersion;       /* Alpine Ridge */
    ULONG RamVersion;       /* Alpine Ridge */
    ULONG ProtocolVersion;  /* Titan Ridge, UNCAPTURED */
    ULONG NvmVersion;       /* Titan Ridge, UNCAPTURED */
    ULONG DeviceId;         /* Titan Ridge, UNCAPTURED */
} TBT_ICM_READY, *PTBT_ICM_READY;

#define TBT_ICM_NAMES_VENDOR        0x01UL
#define TBT_ICM_NAMES_MODEL         0x02UL
#define TBT_ICM_NAMES_INTEL         0x04UL
#define TBT_ICM_NAMES_MALFORMED     0x80UL

typedef struct _TBT_ICM_NAMES {
    ULONG Present;          /* TBT_ICM_NAMES_* */
    ULONG Entries;          /* entries walked, the terminator excluded */
    char Vendor[TBT_ICM_NAME_CHARS];
    char Model[TBT_ICM_NAME_CHARS];
    ULONG TbVendorId;       /* the Intel entry's vendor ID */
    ULONG TbModelId;        /* its model ID */
    ULONG IntelFlags;       /* its flags byte; bit 0 the device supports RTD3 */
} TBT_ICM_NAMES, *PTBT_ICM_NAMES;

/* A decoded device-connected event. */
typedef struct _TBT_ICM_ANNOUNCE {
    ULONG Uuid[4];
    ULONG EventFlags;
    ULONG Key;              /* Alpine Ridge only (ICM 4.9: none on Titan Ridge) */
    ULONG ConnectionId;
    ULONG LinkInfo;
    ULONG Link;
    ULONG Depth;
    BOOLEAN Approved;
    BOOLEAN Rejected;
    BOOLEAN Boot;
    BOOLEAN RouteValid;     /* carried by the event (Titan Ridge) */
    TBT_ROUTE Route;
    TBT_ICM_NAMES Names;
} TBT_ICM_ANNOUNCE, *PTBT_ICM_ANNOUNCE;

/* A decoded device-disconnected event. */
typedef struct _TBT_ICM_GONE {
    ULONG Link;
    ULONG Depth;
    BOOLEAN RouteValid;
    TBT_ROUTE Route;
} TBT_ICM_GONE, *PTBT_ICM_GONE;

/* One entry of the device table (record 21 section 5.3). */
typedef struct _TBT_ICM_DEVICE {
    BOOLEAN InUse;
    BOOLEAN Approved;       /* the tunnel exists, by announcement or approve */
    BOOLEAN Boot;
    BOOLEAN Stale;          /* not yet re-announced since the last driver ready */
    BOOLEAN RouteValid;
    ULONG Serial;           /* this occupancy's stamp, never 0 while InUse */
    ULONG Uuid[4];
    ULONG Key;
    ULONG ConnectionId;
    ULONG Link;
    ULONG Depth;
    ULONG EventFlags;
    TBT_ROUTE Route;
    TBT_ICM_NAMES Names;
} TBT_ICM_DEVICE, *PTBT_ICM_DEVICE;

/* ---------------------------------------------------------------------- */
/* The per-generation table (record 21 section 1.4)                       */
/* ---------------------------------------------------------------------- */

#define TBT_ICM_GEN_ALPINE_RIDGE    1UL
#define TBT_ICM_GEN_TITAN_RIDGE     2UL

#define TBT_ICM_CAP_GET_ROUTE       0x01UL  /* the route must be asked for (Alpine Ridge) */
#define TBT_ICM_CAP_KEY             0x02UL  /* events and approve carry a connection key */
#define TBT_ICM_CAP_ROUTE_IN_EVENTS 0x04UL  /* devices are addressed by route (Titan Ridge) */
#define TBT_ICM_CAP_CAPTURED        0x08UL  /* the row's layouts were read off a capture */

typedef struct _TBT_ICM_PROFILE {
    const char *Name;
    ULONG Generation;
    ULONG Caps;
    /* Each decoder returns FALSE for a frame it cannot read; the client
     * then counts and ignores it. */
    BOOLEAN (*DecodeReady)(const ULONG *dw, ULONG words, PTBT_ICM_READY out);
    BOOLEAN (*DecodeConnected)(const ULONG *dw, ULONG words, PTBT_ICM_ANNOUNCE out);
    BOOLEAN (*DecodeDisconnected)(const ULONG *dw, ULONG words, PTBT_ICM_GONE out);
    /* Fills `dw` (room for TBT_ICM_APPROVE_MAX_WORDS) and returns its
     * length; the response echoes words 1 to 4, the UUID. */
    ULONG (*EncodeApprove)(const TBT_ICM_DEVICE *dev, PULONG dw);
} TBT_ICM_PROFILE, *PTBT_ICM_PROFILE;

#define TBT_ICM_APPROVE_MAX_WORDS   8UL

extern const TBT_ICM_PROFILE TbtIcmProfileAlpineRidge;
extern const TBT_ICM_PROFILE TbtIcmProfileTitanRidge;

/*
 * The row for an NHI's PCI ids, or NULL. The ids are pci-ids.ucw.cz's
 * Intel "Thunderbolt 3 NHI" entries (pci.ids version 2026.10.14), Alpine
 * Ridge and Titan Ridge, never Linux's (owner, decision D9); `*captured` is
 * set TRUE for the one id a capture exists for, 15BF. `forceIntel` is
 * XhciCmMode 2 (record 21 section 8): an Intel id the table does not list
 * takes the Alpine Ridge row. Any IRQL.
 */
const TBT_ICM_PROFILE *TbtIcmProfileForId(ULONG vendorId,
                                          ULONG deviceId,
                                          BOOLEAN forceIntel,
                                          BOOLEAN *captured);

/* The name-block walk both rows share, exposed for the host suite. `dw`
 * is the block's first DW, `words` its length. Any IRQL. */
VOID TbtIcmDecodeNames(const ULONG *dw, ULONG words, PTBT_ICM_NAMES out);

/* ---------------------------------------------------------------------- */
/* Actions and the upcall                                                 */
/* ---------------------------------------------------------------------- */

#define TBT_ICM_ACT_TUNNEL_UP       1UL
#define TBT_ICM_ACT_TUNNEL_DOWN     2UL

#define TBT_ICM_WHY_APPROVED        1UL     /* approve succeeded */
#define TBT_ICM_WHY_ANNOUNCED       2UL     /* announced already approved, new to the table */
#define TBT_ICM_WHY_DISCONNECTED    3UL     /* a device-disconnected event */
#define TBT_ICM_WHY_RECONCILED      4UL     /* not re-announced after driver ready */
#define TBT_ICM_WHY_REPLACED        5UL     /* another announcement took its place */

/*
 * One tunnel change for the driver's thread to act on (record 21 section
 * 6.5), a snapshot of the entry when it was queued. Every entry that
 * leaves the table queues exactly one TUNNEL_DOWN, approved or not
 * (WasApproved says which); a TUNNEL_UP is queued only for an approved one.
 */
typedef struct _TBT_ICM_ACTION {
    ULONG Kind;             /* TBT_ICM_ACT_* */
    ULONG Why;              /* TBT_ICM_WHY_* */
    ULONG Slot;             /* the table entry */
    ULONG Serial;           /* its stamp, for TbtIcmActionLive */
    BOOLEAN WasApproved;
    BOOLEAN Boot;
    BOOLEAN LinkDownSent;   /* the LinkDown upcall already ran for it */
    BOOLEAN RouteValid;
    ULONG Uuid[4];
    ULONG Link;
    ULONG Depth;
    TBT_ROUTE Route;
} TBT_ICM_ACTION, *PTBT_ICM_ACTION;

/*
 * Called the moment a device-disconnected event is decoded, wherever that
 * is - inside a command wait (`inWait` TRUE) or in the idle poll - before
 * the entry is dropped and its TUNNEL_DOWN queued: record 21 section 4.4's
 * link-down notification, which task 39-A.7 wires to nvme98. May be NULL.
 */
typedef VOID (*TBT_ICM_LINKDOWN)(PVOID context,
                                 const TBT_ICM_DEVICE *dev,
                                 BOOLEAN inWait);

/*
 * The approval policy's hook (record 21 section 5.4): called for a device
 * announced unapproved and not rejected, before any approve is sent;
 * returns TRUE to approve. NULL means the policy value alone decides
 * (TbtIcmSetApprovePolicy). Task 39-A.6 is the layer that sets either.
 */
typedef BOOLEAN (*TBT_ICM_APPROVE_HOOK)(PVOID context,
                                        const TBT_ICM_DEVICE *dev);

/* ---------------------------------------------------------------------- */
/* The client                                                             */
/* ---------------------------------------------------------------------- */

typedef struct _TBT_ICM_COUNTERS {
    ULONG CommandsSent;
    ULONG Responses;
    ULONG Timeouts;
    ULONG Retries;
    ULONG ErrorFlags;
    ULONG Strays;           /* responses nobody waited for, other frames */
    ULONG EventsConnected;
    ULONG EventsDisconnected;
    ULONG EventsUnknown;
    ULONG EventsMalformed;
    ULONG EventsQueued;     /* decoded inside a wait, run after it */
    ULONG QueueOverflows;
    ULONG ActionOverflows;
    ULONG TableFull;
    ULONG Rejected;
    ULONG Refreshes;
    ULONG DisconnectUnknown;
    ULONG RouteFailures;
    ULONG ApprovalsSent;
    ULONG ApprovalsOk;
    ULONG ApprovalsFailed;
    ULONG ApprovalsAbandoned;   /* the device left during the wait */
    ULONG ApprovalsWithheld;    /* the policy said no */
    ULONG TunnelUps;
    ULONG TunnelDowns;
    ULONG Reconciled;
    ULONG MailboxSent;
    ULONG MailboxOk;
    ULONG MailboxErrors;
    ULONG MailboxTimeouts;
    ULONG HostPollTries;
    ULONG HostPollAnswered;
    ULONG IdlePolls;
} TBT_ICM_COUNTERS;

typedef struct _TBT_ICM {
    const TBT_ICM_PROFILE *Profile;
    PTBT_RING Ring;
    const TBT_HOST *Host;

    TBT_ICM_LINKDOWN LinkDown;
    PVOID LinkDownContext;
    TBT_ICM_APPROVE_HOOK ApproveHook;
    PVOID ApproveHookContext;
    BOOLEAN ApproveAll;         /* XhciCmApprove: 1 TRUE (default), 0 FALSE */
    ULONG CmdTimeoutMs;
    ULONG ApproveTimeoutMs;
    ULONG Retries;

    BOOLEAN Bound;              /* rings running and driver ready answered */
    BOOLEAN SafeMode;
    BOOLEAN InWait;             /* a command or a poll is outstanding */
    ULONG WaitSlot;             /* the entry the outstanding command is for */
    ULONG WaitSerial;           /* 0 when it is for none */
    BOOLEAN WaitGone;           /* that entry was dropped during the wait */

    TBT_ICM_READY Ready;
    ULONG NextSerial;
    TBT_ICM_DEVICE Devices[TBT_ICM_MAX_DEVICES];

    TBT_ICM_ANNOUNCE Queue[TBT_ICM_EVENT_QUEUE];
    ULONG QueueHead;
    ULONG QueueCount;

    TBT_ICM_ACTION Actions[TBT_ICM_ACTION_QUEUE];
    ULONG ActionHead;
    ULONG ActionCount;

    TBT_ICM_COUNTERS Counters;
} TBT_ICM, *PTBT_ICM;

/* Bind `icm` to its row, its ring and the accessor, with every policy and
 * timing at its default. Installs the ring's unsolicited receiver. */
VOID TbtIcmInit(PTBT_ICM icm,
                const TBT_ICM_PROFILE *profile,
                PTBT_RING ring,
                const TBT_HOST *host);

VOID TbtIcmSetLinkDown(PTBT_ICM icm, TBT_ICM_LINKDOWN fn, PVOID context);
VOID TbtIcmSetApprovePolicy(PTBT_ICM icm, BOOLEAN approveAll);
VOID TbtIcmSetApproveHook(PTBT_ICM icm, TBT_ICM_APPROVE_HOOK fn, PVOID context);
/* Each held to TBT_ICM_TIMEOUT_MIN_MS..MAX_MS; 0 means the default. */
VOID TbtIcmSetTimeouts(PTBT_ICM icm, ULONG cmdMs, ULONG approveMs);

/*
 * Record 21 section 5.1 steps 2 to 5: the host interface capabilities
 * (paths at least 1, version 0), firmware status (ICM enabled), the NVM
 * authentication poll, the operating mode. TBT_OK for connection-manager
 * mode, TBT_ICM_SAFE_MODE for mode 0, TBT_E_REFUSED or TBT_E_HW otherwise.
 * Touches no ring. `mode` may be NULL.
 */
LONG TbtIcmCheckFirmware(PTBT_ICM icm, PULONG mode);

/* One NHI mailbox command (ICM 2.3): TBT_OK, TBT_E_ERROR_FLAG or
 * TBT_E_TIMEOUT. */
LONG TbtIcmMailbox(PTBT_ICM icm, ULONG code, ULONG data);

/*
 * Record 21 section 5.1 steps 2 to 10 over the common buffer the driver
 * allocated (`va`, `phys`, `bytes`, as TbtRingStart): the checks, then ring
 * 0, mailbox 0x23, driver ready, the host-router poll and the events it
 * collected. TBT_OK, TBT_ICM_SAFE_MODE (nothing started), or a negative
 * result on which the start is refused - the rings already stopped and
 * mailbox 0x07 sent if they had been started.
 */
LONG TbtIcmStart(PTBT_ICM icm, PVOID va, ULONG phys, ULONG bytes);

/*
 * The idle poll: every frame the RX ring holds is decoded and acted on -
 * a new device gets its route and its approval here, so this may send
 * commands and wait - then the events decoded inside those waits. Bounded
 * by TBT_ICM_POLL_FRAMES and TBT_ICM_POLL_EVENTS; what is left waits for
 * the next call. Then drain TbtIcmNextAction.
 */
VOID TbtIcmPoll(PTBT_ICM icm);

/* Drain the RX ring as a wait does: device-connected events are queued
 * for the next TbtIcmPoll, disconnects act at once. Sends nothing. For the
 * driver's own waits (the tunnel-up poll of record 21 section 6.5). */
VOID TbtIcmService(PTBT_ICM icm);

/* The next queued tunnel change, oldest first. */
BOOLEAN TbtIcmNextAction(PTBT_ICM icm, PTBT_ICM_ACTION out);

/* Whether the entry an action names is still the one it named. */
BOOLEAN TbtIcmActionLive(const TBT_ICM *icm, const TBT_ICM_ACTION *action);

/* The table, for the log and the snapshot. NULL for a free slot. */
const TBT_ICM_DEVICE *TbtIcmDevice(const TBT_ICM *icm, ULONG slot);
ULONG TbtIcmDeviceCount(const TBT_ICM *icm);

/* Record 21 section 5.6: system suspend - rings stopped, mailbox 0x05,
 * then 0x07, each failure logged and tolerated. The table is kept. */
VOID TbtIcmSuspend(PTBT_ICM icm);

/*
 * Resume to D0: section 5.1 from step 6 without mailbox 0x23, on the
 * block TbtIcmStart was given, then the reconciliation of section 5.3 - a
 * table entry not re-announced within TBT_ICM_RECONCILE_MS of the
 * host-router poll is dropped with a TUNNEL_DOWN. TBT_OK, or a negative
 * result after which the client is unbound (rings stopped, 0x07 sent).
 */
LONG TbtIcmResume(PTBT_ICM icm);

/* Unbind (stop, remove, disable, shutdown): rings stopped, then mailbox
 * 0x07 (ICM 6.7 [obs]). Tunnels and approvals survive it; nothing is
 * queued. Idempotent; sends nothing in safe mode. */
VOID TbtIcmUnbind(PTBT_ICM icm);

#endif /* TBT_ICM_H */
