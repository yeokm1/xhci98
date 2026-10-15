/*
 * tbt_icm.h - the client of an Intel Thunderbolt controller's firmware
 * connection manager (the ICM), its pure half (roadmap-usb4.md task 39-A.3;
 * design record 21 sections 1.4 and 5).
 *
 * What is here: the ICM frames behind a per-generation table of encoders,
 * decoders and capabilities (record 21 section 1.4); the profile choice by
 * PCI id and its confirmation from the firmware status after start; the
 * bind, resume, suspend and unbind sequences of record 21 sections 5.1 and
 * 5.6; the device table and the events of section 5.3; the approval
 * decision of section 5.4 behind a policy value and a hook (decision D4:
 * XhciCmApprove 0 or 1, task 39-A.6 reads it); the timeouts, retries and
 * error flag of section 5.5. What is not: the registry, PnP, power, the
 * thread, the common buffer, the configuration-space writes, the
 * re-enumeration and the nvme98 statement - the driver (39-A.4, 39-A.5,
 * 39-A.7) does those and reaches this file through the TBT_HOST accessor of
 * tbt_ring.h, an action queue it drains, and one upcall.
 *
 * SOURCES. "ICM n.n" is usb4-emu's docs/ICM-PROTOCOL.md (branch main, read
 * at a68bc74), the clean-room contract written from the P14s Gen 1 capture,
 * with its [obs] and [inf] tags. "Linux <file> <symbol>" is the Linux
 * drivers/thunderbolt mirror pinned at c6859eed, read under design record
 * 20's rule since the owner's decision of 2026-10-15 (layouts, constants,
 * ids and sequences taken and cited; no function body copied). Where the
 * two describe the same frame they agree; the Alpine Ridge layouts are the
 * capture's. A behaviour cited to Linux alone is unconfirmed until a
 * capture shows it, as every row but Alpine Ridge is.
 *
 * FOUR ROWS (record 21 section 1.4, the orchestrator's coverage plan of
 * 2026-10-15, owner to confirm):
 *   Falcon Ridge   Thunderbolt 2 (Linux icm_probe's FALCON_RIDGE cases)
 *   Alpine Ridge   CAPTURED on 8086:15BF; the others of its ids are not
 *   Titan Ridge    and Maple Ridge, Titan Ridge's messages
 *   Ice Lake       the integrated controllers, Ice Lake to Meteor Lake,
 *                  when their firmware connection manager runs
 * Only Alpine Ridge's frames have been seen on a wire; every other row is
 * UNCAPTURED, built from Linux's structure layouts, and its host vectors
 * test this file against those layouts, not against hardware. The client
 * is written so a wrong layout costs an approval and never the thread: a
 * response with the error flag, a response that never comes, an event too
 * short to decode - each leaves the device unapproved, is counted and
 * logged, and every wait is bounded.
 *
 * WHAT IS LEFT OUT, from what Linux's client does with the same firmware:
 * boot ACL reads and writes, keys and challenges (security level
 * "secure"), host-to-host (XDomain) paths, the USB4 router-operation proxy,
 * NVM upgrade, the firmware start path and CIO reset for a stopped ICM,
 * and runtime suspend. Each row's lifecycle follows Linux icm_probe's hooks
 * for it: Falcon Ridge asks the route through the multi-packet topology
 * query (icm_fr_get_route), Alpine Ridge through get route; Titan Ridge and
 * Ice Lake carry it in their events; only Falcon and Alpine Ridge send
 * mailbox 0x05 at suspend; Ice Lake has no approve and sends nothing at
 * suspend. Ice Lake's RTD3 veto is recorded (VetoActive) and needs nothing
 * more: it holds off a runtime suspend this driver never makes. Ice Lake's
 * fused host UUID and its force power are PCI configuration, tbt_hw.h's.
 *
 * Which row, and whether this client runs at all, is tbt_hw.h's
 * TbtHwSelect: TbtIcmStart is called only after it returned TBT_SEL_ICM.
 * Then on the thread every idle period TbtIcmPoll followed by
 * TbtIcmNextAction until it returns FALSE, running each tunnel up or down
 * the action names; TbtIcmSuspend and TbtIcmResume around a system sleep;
 * TbtIcmUnbind at stop. While the driver itself waits for a tunnel port
 * (record 21 section 6.5) it calls TbtIcmService between its reads and
 * TbtIcmActionLive to learn whether the device left meanwhile.
 *
 * IRQL and locking: as tbt_ring.h - PASSIVE_LEVEL, on the connection-
 * manager thread, no lock held, for every function that takes a TBT_ICM.
 * The rows' decoders and encoders and TbtIcmDecodeNames are pure and
 * callable at any IRQL. The LinkDown
 * upcall and the approval hook run on that thread, possibly inside a
 * command wait, and must not call back into this file.
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
/* ICM header word (ICM 3.1; Linux tb_msgs.h struct icm_pkg_header)       */
/* ---------------------------------------------------------------------- */

/*
 * Linux lays every message out as a little-endian structure whose first
 * byte is the code; ICM-PROTOCOL.md gives the same frames as numeric DWs
 * with the code in bits 7:0. They are the same bytes: a structure's byte k
 * is bits 8*(k%4)+7..8*(k%4) of DW k/4 (ICM 0). A u16 at byte offset 2 of a
 * DW is therefore its bits 31:16, and a uuid_t is four DWs.
 */
#define TBT_ICM_HDR_CODE(v)         ((ULONG)(v) & 0xFFUL)           /* code */
#define TBT_ICM_HDR_FLAGS(v)        (((ULONG)(v) >> 8) & 0xFFUL)    /* flags */
#define TBT_ICM_HDR_INDEX(v)        (((ULONG)(v) >> 16) & 0xFFUL)   /* packet_id */
#define TBT_ICM_HDR_COUNT(v)        (((ULONG)(v) >> 24) & 0xFFUL)   /* total_packets */
#define TBT_ICM_FLAG_ERROR          0x01UL  /* ICM_FLAGS_ERROR; ICM 4.8 */
/* A request goes out with flags, index and count 0 (ICM 3.1 [obs] T:1). */
#define TBT_ICM_REQUEST(code)       ((ULONG)(code) & 0xFFUL)

/* Command codes (ICM 4; Linux tb_msgs.h enum icm_pkg_code). */
#define TBT_ICM_CMD_GET_TOPOLOGY    0x01UL  /* ICM_GET_TOPOLOGY, Falcon Ridge */
#define TBT_ICM_CMD_DRIVER_READY    0x03UL  /* ICM_DRIVER_READY */
#define TBT_ICM_CMD_APPROVE         0x04UL  /* ICM_APPROVE_DEVICE */
#define TBT_ICM_CMD_GET_ROUTE       0x0AUL  /* ICM_GET_ROUTE */
/* Event codes (ICM 4; enum icm_event_code). */
#define TBT_ICM_EV_CONNECTED        0x03UL  /* ICM_EVENT_DEVICE_CONNECTED */
#define TBT_ICM_EV_DISCONNECTED     0x04UL  /* ICM_EVENT_DEVICE_DISCONNECTED */
#define TBT_ICM_EV_XD_CONNECTED     0x06UL  /* host-to-host; ignored */
#define TBT_ICM_EV_XD_DISCONNECTED  0x07UL  /* host-to-host; ignored */
#define TBT_ICM_EV_DP_CHANGED       0x08UL  /* DisplayPort; ignored */
#define TBT_ICM_EV_RTD3_VETO        0x0AUL  /* Ice Lake family; logged, ignored */

/* Driver-ready flags: RTD3 is bit 6 on every generation that reports it
 * (ICM 4.1; ICM_AR_FLAGS_RTD3, ICM_TR_FLAGS_RTD3). Logged, never acted on. */
#define TBT_ICM_READY_RTD3          0x40UL

/* Link info, 16 bits (ICM 4.4; ICM_LINK_INFO_*). */
#define TBT_ICM_LI_LINK(v)          ((ULONG)(v) & 0x7UL)            /* 2:0 */
#define TBT_ICM_LI_DEPTH(v)         (((ULONG)(v) >> 4) & 0xFUL)     /* 7:4 */
#define TBT_ICM_LI_APPROVED         0x0100UL                        /* 8 */
#define TBT_ICM_LI_REJECTED         0x0200UL                        /* 9 */
#define TBT_ICM_LI_BOOT             0x0400UL                        /* 10 */
#define TBT_ICM_LI_MAKE(link, depth) \
    (((ULONG)(link) & 0x7UL) | (((ULONG)(depth) & 0xFUL) << 4))
/* The bounds Linux's icm_fr_device_disconnected holds a link-and-depth
 * event to (ICM_MAX_LINK, TB_SWITCH_MAX_DEPTH in tb.h). */
#define TBT_ICM_MAX_LINK            4UL
#define TBT_ICM_MAX_DEPTH           6UL

/* Device-connected event flags, header bits 15:8 (ICM 4.4; ICM_FLAGS_*). */
#define TBT_ICM_EVF_SLEVEL(v)       (((ULONG)(v) >> 3) & 0x3UL)     /* 4:3 */
#define TBT_ICM_EVF_DUAL_LANE       0x20UL  /* bit 5 */
#define TBT_ICM_EVF_SPEED_20G       0x80UL  /* bit 7, else 10 Gb/s */

/* The endpoint name block's entry types (ICM 4.4 [obs] T:9; Linux icm.c
 * struct ep_name_entry, EP_NAME_INTEL_VSS, struct intel_vss). */
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

/* ICM 3 [inf]; Linux icm.c ICM_TIMEOUT, ICM_RETRIES, ICM_APPROVE_TIMEOUT;
 * record 21 section 8's bounds. */
#define TBT_ICM_CMD_TIMEOUT_MS      5000UL
#define TBT_ICM_APPROVE_TIMEOUT_MS  10000UL
#define TBT_ICM_TIMEOUT_MIN_MS      1000UL
#define TBT_ICM_TIMEOUT_MAX_MS      60000UL
#define TBT_ICM_RETRIES             3UL
/* The host-router poll after driver ready (ICM 4.1 [inf]; Linux icm.c
 * __icm_driver_ready): router space DW 0 of route 0, 100 ms per try, up
 * to 50 tries 50 ms apart. */
#define TBT_ICM_HOSTPOLL_TRIES      50UL
#define TBT_ICM_HOSTPOLL_TIMEOUT_MS 100UL
#define TBT_ICM_HOSTPOLL_GAP_MS     50UL
/* The reconciliation window (ICM 6.5, 6.6 [inf]; Linux icm.c icm_complete
 * queues the rescan 500 ms out and icm_postpone_rescan pushes it another
 * 500 ms on every device-connected event). The pushes are capped here. */
#define TBT_ICM_RECONCILE_MS        500UL
#define TBT_ICM_RECONCILE_MAX_MS    5000UL

/* The whole resume's budget (record 21 section 6.3, owner's decision
 * 2026-10-15): bring-up, every command and its retries, the host-router
 * poll, the reconciliation and the announcements drained, measured from
 * TbtIcmResume's entry. Every wait is clipped to what is left of it, and
 * an announcement does not extend it. 30 s below the bridge filters'
 * fixed 5-minute hold (cm_ports.h CM_PORTS_HOLD_MAX_MS). */
#define TBT_ICM_RESUME_BUDGET_MS    270000UL
/* The NVM-authentication wait a Tiger Lake class row confirms with (Linux
 * icm.c icm_tgl_is_supported: 10 ms); tbt_hw.c's. */
#define TBT_ICM_AUTH_CONFIRM_MS     10UL
/* Falcon Ridge's topology query (Linux tb_msgs.h ICM_GET_TOPOLOGY_PACKETS,
 * struct icm_fr_pkg_get_topology_response: 38 DWs a packet). */
#define TBT_ICM_TOPO_PACKETS        14UL
#define TBT_ICM_TOPO_PORTS          16UL
#define TBT_ICM_TOPO_WORDS          38UL
#define TBT_ICM_TOPO_ALL            0x3FFFUL    /* a bit per packet, all 14 */
/* The device-connected structures' lengths (ICM 4.4 [obs] 61 DWs; Linux
 * struct icm_fr_event_device_connected 61, icm_tr_event_device_connected
 * 63). A shorter frame is malformed. */
#define TBT_ICM_FR_CONNECTED_WORDS  61UL
#define TBT_ICM_TR_CONNECTED_WORDS  63UL
/* How much one TbtIcmPoll takes before it returns to the driver. */
#define TBT_ICM_POLL_FRAMES         64UL
#define TBT_ICM_POLL_EVENTS         32UL
/* Driver readys in a row to recover lost announcements, then a RESYNC. */
#define TBT_ICM_REDISCOVER_MAX      3UL


/* ---------------------------------------------------------------------- */
/* Decoded messages                                                       */
/* ---------------------------------------------------------------------- */

typedef struct _TBT_ICM_READY {
    ULONG Words;            /* DWs the response carried */
    ULONG Flags;            /* header bits 15:8 */
    ULONG SecurityLevel;    /* 0 none, 1 user, 2 secure, ... (ICM 4.1) */
    ULONG BootAclCount;
    BOOLEAN BootAclSupported;
    ULONG RomVersion;       /* Falcon and Alpine Ridge */
    ULONG RamVersion;       /* Falcon and Alpine Ridge */
    ULONG ProtocolVersion;  /* Titan Ridge and Ice Lake */
    ULONG NvmVersion;       /* Titan Ridge and Ice Lake */
    ULONG DeviceId;         /* Titan Ridge and Ice Lake */
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
    ULONG Key;              /* Falcon and Alpine Ridge only */
    ULONG ConnectionId;
    ULONG LinkInfo;
    ULONG Link;             /* from the link info, or the route's first level */
    ULONG Depth;            /* from the link info, or the route's length */
    BOOLEAN Approved;
    BOOLEAN Rejected;
    BOOLEAN Boot;
    BOOLEAN RouteValid;     /* carried by the event (Titan Ridge, Ice Lake) */
    TBT_ROUTE Route;
    TBT_ICM_NAMES Names;
    ULONG RawWords;         /* the frame as received, for the log */
    ULONG Raw[TBT_FRAME_DWORDS];
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
    BOOLEAN ApproveUnknown; /* an approve never answered: outcome unknown */
    BOOLEAN ApproveFailed;  /* an approve answered with the error flag */
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

#define TBT_ICM_GEN_FALCON_RIDGE    1UL
#define TBT_ICM_GEN_ALPINE_RIDGE    2UL
#define TBT_ICM_GEN_TITAN_RIDGE     3UL
#define TBT_ICM_GEN_ICE_LAKE        4UL

/* Row capabilities, each the presence of a Linux icm_probe hook or op. */
#define TBT_ICM_CAP_GET_ROUTE       0x0001UL /* icm->get_route: icm_ar_get_route */
#define TBT_ICM_CAP_TOPOLOGY        0x0200UL /* icm->get_route: icm_fr_get_route's topology */
#define TBT_ICM_CAP_KEY             0x0002UL /* events and approve carry a connection key */
#define TBT_ICM_CAP_ROUTE_IN_EVENTS 0x0004UL /* devices addressed by route string */
#define TBT_ICM_CAP_APPROVE         0x0008UL /* tb_cm_ops.approve_switch exists */
#define TBT_ICM_CAP_GET_MODE        0x0010UL /* icm->get_mode: auth poll, mode, then 0x23 */
#define TBT_ICM_CAP_SAVE_DEVICES    0x0020UL /* icm->save_devices: mailbox 0x05 at suspend */
#define TBT_ICM_CAP_SUSPEND_UNLOAD  0x0040UL /* tb_cm_ops.suspend: mailbox 0x07 at suspend */
#define TBT_ICM_CAP_FORCE_POWER     0x0080UL /* pci.c icl_nhi_ops: force power before start */
#define TBT_ICM_CAP_EXTRA_PACKETS   0x0100UL /* a connected event's index > 0 is ignored */

typedef struct _TBT_ICM_PROFILE {
    const char *Name;
    ULONG Generation;
    ULONG Caps;
    ULONG ReadyTimeoutMs;   /* driver ready per attempt; 0: the command timeout */
    ULONG ReadyRetries;     /* after the first attempt */
    ULONG ReadyWords;       /* the driver-ready response's structure, in DWs */
    /* Each decoder returns FALSE for a frame it cannot read; the client
     * then counts and ignores it. */
    BOOLEAN (*DecodeReady)(const ULONG *dw, ULONG words, PTBT_ICM_READY out);
    BOOLEAN (*DecodeConnected)(const ULONG *dw, ULONG words, PTBT_ICM_ANNOUNCE out);
    BOOLEAN (*DecodeDisconnected)(const ULONG *dw, ULONG words, PTBT_ICM_GONE out);
    /* Fills `dw` (room for TBT_ICM_APPROVE_MAX_WORDS) and returns its
     * length; the response echoes words 1 to 4, the UUID. NULL on a row
     * with no approve. */
    ULONG (*EncodeApprove)(const TBT_ICM_DEVICE *dev, PULONG dw);
} TBT_ICM_PROFILE, *PTBT_ICM_PROFILE;

#define TBT_ICM_APPROVE_MAX_WORDS   8UL

extern const TBT_ICM_PROFILE TbtIcmProfileFalconRidge;
extern const TBT_ICM_PROFILE TbtIcmProfileAlpineRidge;
extern const TBT_ICM_PROFILE TbtIcmProfileTitanRidge;
extern const TBT_ICM_PROFILE TbtIcmProfileIceLake;


/* The name-block walk every row shares, exposed for the host suite. `dw`
 * is the block's first DW, `words` its length. Any IRQL. */
VOID TbtIcmDecodeNames(const ULONG *dw, ULONG words, PTBT_ICM_NAMES out);

/* ---------------------------------------------------------------------- */
/* Actions and the upcall                                                 */
/* ---------------------------------------------------------------------- */

#define TBT_ICM_ACT_TUNNEL_UP       1UL
#define TBT_ICM_ACT_TUNNEL_DOWN     2UL
/* Every tunnel port of this domain to be treated as changed (record 21
 * section 6): an action lost to a full queue, or a disconnect that
 * matched no entry (section 1.4.6). */
#define TBT_ICM_ACT_RESYNC          3UL

#define TBT_ICM_WHY_APPROVED        1UL     /* approve succeeded */
#define TBT_ICM_WHY_ANNOUNCED       2UL     /* announced already approved, new to the table */
#define TBT_ICM_WHY_DISCONNECTED    3UL     /* a device-disconnected event */
#define TBT_ICM_WHY_RECONCILED      4UL     /* not re-announced after driver ready */
#define TBT_ICM_WHY_REPLACED        5UL     /* another announcement took its place */
#define TBT_ICM_WHY_LOST            6UL     /* RESYNC: an action did not fit */
#define TBT_ICM_WHY_APPROVE_UNKNOWN 7UL     /* TUNNEL_UP: approve unanswered, tunnel may be up */
#define TBT_ICM_WHY_UNMATCHED       8UL     /* RESYNC: a disconnect that matched no entry */

/*
 * One tunnel change for the driver's thread to act on (record 21 section
 * 6.5), a snapshot of the entry when it was queued. Every entry that
 * leaves the table queues exactly one TUNNEL_DOWN, approved or not
 * (WasApproved says which); a TUNNEL_UP is queued for an approved one, and
 * for one whose approve was never answered (why APPROVE_UNKNOWN: the
 * tunnel may be up, record 21 section 1.4.6). Actions of a session that
 * ended (unbind, a failure) are retired and never live.
 * A disconnect, or a replacement, takes the devices below the one named
 * with it. If the queue is ever full, a stale TUNNEL_UP is given up first,
 * and anything else that does not fit turns into one TBT_ICM_ACT_RESYNC
 * after the queue drains. A lost announcement is recovered differently:
 * the next idle poll sends driver ready again and reconciles.
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
 * announced unapproved and not rejected, on a row that can approve, before
 * any approve is sent; returns TRUE to approve. NULL means the policy value
 * alone decides (TbtIcmSetApprovePolicy). Task 39-A.6 sets either.
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
    ULONG EventsIgnored;    /* host-to-host, DisplayPort, RTD3 veto, extra packets */
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
    ULONG ApprovalsUnsupported; /* the row has no approve */
    ULONG ApprovalsUnknown;     /* never answered: outcome unknown */
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
    ULONG Rediscoveries;
    ULONG ResumeBudgetExpired;  /* resumes abandoned at their budget */
} TBT_ICM_COUNTERS;

typedef struct _TBT_ICM {
    const TBT_ICM_PROFILE *Profile;
    ULONG RingFlags;            /* tbt_hw.c TbtHwRingFlags */
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
    BOOLEAN Started;            /* rings were started since the last unbind */
    BOOLEAN Session;            /* driver ready was sent: 0x07 ends it */
    BOOLEAN VetoActive;         /* Ice Lake's RTD3 veto, for the log */
    BOOLEAN InWait;             /* a command or a poll is outstanding */
    ULONG WaitSlot;             /* the entry the outstanding command is for */
    ULONG WaitSerial;           /* 0 when it is for none */
    BOOLEAN WaitGone;           /* that entry was dropped during the wait */
    BOOLEAN SettleExtend;       /* a device-connected event arrived in the window */
    BOOLEAN ResyncPending;      /* a RESYNC is owed */
    ULONG ResyncWhy;            /* its reason, TBT_ICM_WHY_* */
    BOOLEAN Failed;             /* a failure after activation: stays idle */
    BOOLEAN RediscoverPending;  /* an announcement did not fit: driver ready again */
    ULONG RediscoverRun;        /* rediscoveries in a row */
    /* The resume's budget (TBT_ICM_RESUME_BUDGET_MS): on only inside
     * TbtIcmResume; Spent once a wait found nothing left of it. */
    BOOLEAN BudgetOn;
    BOOLEAN BudgetSpent;
    ULONG BudgetStartMs;
    ULONG BudgetMs;
    ULONG BudgetStalledMs;      /* waits made under it while the clock did
                                   not move: the fallback that still ends
                                   it then (as TbtDeadline's does)         */
    ULONG ResumeBudgetMs;       /* the budget the next resume gets */

    TBT_ICM_READY Ready;
    ULONG NextSerial;
    TBT_ICM_DEVICE Devices[TBT_ICM_MAX_DEVICES];

    TBT_ICM_ANNOUNCE Queue[TBT_ICM_EVENT_QUEUE];
    ULONG QueueHead;
    ULONG QueueCount;

    TBT_ICM_ACTION Actions[TBT_ICM_ACTION_QUEUE];
    ULONG ActionHead;
    ULONG ActionCount;

    /* Falcon Ridge's topology, one entry per packet (struct
     * icm_fr_pkg_get_topology_response: route_lo, route_hi, first_data,
     * switch_index, ports[16]; the rest is not kept). */
    ULONG TopoReceived;         /* bit per packet index */
    ULONG TopoRouteLo[TBT_ICM_TOPO_PACKETS];
    ULONG TopoRouteHi[TBT_ICM_TOPO_PACKETS];
    ULONG TopoInfo[TBT_ICM_TOPO_PACKETS];   /* DW 3: first_data .. switch_index */
    ULONG TopoPorts[TBT_ICM_TOPO_PACKETS][TBT_ICM_TOPO_PORTS];

    TBT_ICM_COUNTERS Counters;
} TBT_ICM, *PTBT_ICM;

/* Bind `icm` to its row (tbt_hw.h TBT_HW_PROFILE.Icm), its ring, the
 * accessor and the ring start flags (TbtHwRingFlags), with every policy and
 * timing at its default. Installs the ring's unsolicited receiver. */
VOID TbtIcmInit(PTBT_ICM icm,
                const TBT_ICM_PROFILE *profile,
                PTBT_RING ring,
                const TBT_HOST *host,
                ULONG ringFlags);

VOID TbtIcmSetLinkDown(PTBT_ICM icm, TBT_ICM_LINKDOWN fn, PVOID context);
VOID TbtIcmSetApprovePolicy(PTBT_ICM icm, BOOLEAN approveAll);
VOID TbtIcmSetApproveHook(PTBT_ICM icm, TBT_ICM_APPROVE_HOOK fn, PVOID context);
/* Each held to TBT_ICM_TIMEOUT_MIN_MS..MAX_MS; 0 means the default. */
VOID TbtIcmSetTimeouts(PTBT_ICM icm, ULONG cmdMs, ULONG approveMs);

/* The budget of the next resumes, ms; 0 means TBT_ICM_RESUME_BUDGET_MS.
 * For the host suite: the driver keeps the default. */
VOID TbtIcmSetResumeBudget(PTBT_ICM icm, ULONG ms);


/* One NHI mailbox command (ICM 2.3; Linux nhi.c nhi_mailbox_cmd): TBT_OK,
 * TBT_E_ERROR_FLAG or TBT_E_TIMEOUT. */
LONG TbtIcmMailbox(PTBT_ICM icm, ULONG code, ULONG data);

/*
 * Record 21 section 5.1 steps 6 to 10, after TbtHwSelect said TBT_SEL_ICM
 * (which ran steps 2 to 5), over the common buffer the driver allocated
 * (`va`, `phys`, `bytes`, as TbtRingStart): ring 0, mailbox 0x23 where the
 * row reads the mode, driver ready, the host-router poll and the events it
 * collected. TBT_OK, or a negative result: a failure before the rings
 * start leaves everything as it was; one after (record 21 section 1.4.2,
 * "failure after activation") stops this driver's rings - nothing of
 * theirs is pending when it returns, so the common buffer may be freed -
 * and leaves the instance idle (Failed); TbtIcmUnbind at the stop then
 * sends mailbox 0x07 only if driver ready had been sent. No tunnel is
 * touched.
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

/* Record 21 section 5.6: system suspend - rings stopped, then mailbox
 * 0x05 and 0x07 where the row sends them (Linux icm.c icm_suspend), each
 * failure logged and tolerated. The table is kept. */
VOID TbtIcmSuspend(PTBT_ICM icm);

/*
 * Resume to D0: section 5.1 from step 6 without mailbox 0x23, on the
 * block TbtIcmStart was given, then the reconciliation of section 5.3 - a
 * table entry not re-announced within TBT_ICM_RECONCILE_MS of the
 * host-router poll (pushed back by each announcement) is dropped with a
 * TUNNEL_DOWN. TBT_OK, or a negative result after which the instance is
 * failed and idle (rings stopped; 0x07 left for TbtIcmUnbind). A failed
 * instance does not resume.
 *
 * The whole of it runs within TBT_ICM_RESUME_BUDGET_MS of its entry: every
 * command attempt, poll and settling window is clipped to what is left,
 * and the announcement drain stops when nothing is. A resume that ran out
 * is abandoned whole - TBT_E_TIMEOUT, the instance failed as above, its
 * queued work and actions retired, nothing reconciled away for want of
 * time - so the caller's failure path (the bridges' holds released, their
 * buses invalidated) follows at once, before the filters' fixed hold.
 */
LONG TbtIcmResume(PTBT_ICM icm);

/* Unbind (stop, remove, disable, shutdown): rings stopped, then mailbox
 * 0x07 (ICM 6.7 [obs]; Linux icm.c icm_stop). Tunnels and approvals
 * survive it; nothing is queued. Idempotent: 0x07 only after a driver
 * ready, nothing at all if the rings never started. */
VOID TbtIcmUnbind(PTBT_ICM icm);

#endif /* TBT_ICM_H */
