/*
 * tbt_xdp.h - the inter-domain (XDomain) discovery protocol and the USB4NET
 * service, their pure half (roadmap-usb4.md task 45.59, step S1; design
 * record 35 sections 2.1, 3 and 4): the packets of the USB4 Inter-Domain
 * Service Protocol 2.0 ("IDS") chapters 2, 5 and 6, the properties block of
 * its chapter 4 (built and parsed), the UUID rule of its section 1.2, the
 * MAC derivation of its section 5.1.5.3, the transmit HopID choice, and the
 * peer engine: the discovery state machine of record 35 section 3.5, the
 * responder of section 3.6 and the login and logout of section 4.
 *
 * Written from the IDS and record 35. Linux's drivers/thunderbolt
 * (xdomain.c, property.c) and drivers/net/thunderbolt (main.c) were read
 * for facts only, as record 35 cites them by symbol; nothing of either is
 * adapted.
 *
 * Packets are arrays of host-order DWs as the ring carries them
 * (tbt_ring.h, TBT_FRAME.Dw and TbtRingSend): word 0 is the Route String
 * High, that is the IDS figures' DW 1; the transport header (their DW 0)
 * and the CRC are the ring's. TBT_XDP_W_* name the word indexes.
 *
 * The engine never waits: TbtXdpPeerTick resends what is due and
 * TbtXdpPeerOnPacket takes what arrived, both from the connection-manager
 * thread, so a one-second inter-domain timeout never holds the thread the
 * way a control request's busy-poll does (record 35 section 3.2).
 *
 * DDK-free: part of the pure core. C89, no 64-bit arithmetic, no floating
 * point.
 *
 * IRQL and locking: every function that takes a TBT_XDP_PEER runs at
 * PASSIVE_LEVEL on the connection-manager thread only (record 21 section
 * 4.4), and calls its TBT_XDP_OPS from there; no lock is taken. The packet,
 * block, UUID, MAC and HopID functions are pure and callable at any IRQL.
 */

#ifndef TBT_XDP_H
#define TBT_XDP_H

#include "../xhci_compat.h"
#include "tbt_ring.h"

/* ---------------------------------------------------------------------- */
/* UUIDs (IDS 1.2)                                                        */
/* ---------------------------------------------------------------------- */

/* A UUID as a packet carries it: DW n is the canonical form's bytes 4n to
 * 4n+3 taken little-endian, so the discovery protocol's
 * B638D70E-42FF-40BB-97C2-90E2C0B2FF07 is 0x0ED738B6, 0xBB40FF42,
 * 0xE290C297, 0x07FFB2C0. */
typedef struct _TBT_UUID {
    ULONG Dw[4];
} TBT_UUID, *PTBT_UUID;

VOID    TbtUuidFromBytes(PTBT_UUID uuid, const UCHAR *bytes16);
VOID    TbtUuidToBytes(const TBT_UUID *uuid, PUCHAR bytes16);
BOOLEAN TbtUuidEqual(const TBT_UUID *a, const TBT_UUID *b);
/* The comparison of IDS 3.4.1: the canonical form byte by byte, the most
 * significant first. Negative, 0 or positive. */
LONG    TbtUuidCompare(const TBT_UUID *a, const TBT_UUID *b);

extern const TBT_UUID TbtXdpProtocolUuid;  /* B638D70E-42FF-40BB-97C2-90E2C0B2FF07 */
extern const TBT_UUID TbtNetProtocolUuid;  /* 798F589E-3616-8A47-97C6-5664A920C8DD */
extern const TBT_UUID TbtNetDirectoryUuid; /* C66189CA-1CCE-4195-BDB8-49592E5F5A4F */

/* ---------------------------------------------------------------------- */
/* Packets (IDS chapters 2, 5, 6)                                         */
/* ---------------------------------------------------------------------- */

/* TBT_PDF_XDOMAIN_REQ and TBT_PDF_XDOMAIN_RESP (6 and 7) are tbt_regs.h's. */

/* The longest packet the ring carries: 256 bytes less the CRC. */
#define TBT_XDP_MAX_WORDS           63UL

#define TBT_XDP_W_ROUTE_HI          0UL
#define TBT_XDP_W_ROUTE_LO          1UL
#define TBT_XDP_W_LEN               2UL     /* Data Size 5:0, SN 28:27 */
#define TBT_XDP_W_PROTO             3UL     /* 4 DWs */
#define TBT_XDP_W_TYPE              7UL     /* discovery: Packet Type */
#define TBT_XDP_W_DATA              8UL     /* discovery: the payload */
#define TBT_NET_W_REQUESTER         7UL     /* USB4NET: 4 DWs */
#define TBT_NET_W_RESPONDER         11UL    /* 4 DWs */
#define TBT_NET_W_TYPE              15UL
#define TBT_NET_W_REQID             16UL
#define TBT_NET_W_DATA              17UL

#define TBT_XDP_LEN_SIZE_MASK       0x0000003FUL
#define TBT_XDP_LEN_SN_SHIFT        27
#define TBT_XDP_LEN_SN_MASK         0x18000000UL

/* Discovery Packet Types (IDS Table 2-2, Table 6-1). */
#define TBT_XDP_UUID_REQ_TBT3       1UL
#define TBT_XDP_UUID_RESP           2UL
#define TBT_XDP_PROPS_REQ           3UL
#define TBT_XDP_PROPS_RESP          4UL
#define TBT_XDP_CHANGED             5UL
#define TBT_XDP_CHANGED_RESP        6UL
#define TBT_XDP_ERROR               7UL
#define TBT_XDP_UUID_REQ            12UL
#define TBT_XDP_LINK_STATUS_REQ     15UL
#define TBT_XDP_LINK_STATUS_RESP    16UL
#define TBT_XDP_LINK_CHANGE_REQ     17UL
#define TBT_XDP_LINK_CHANGE_RESP    18UL
#define TBT_XDP_TIME_SYNC           19UL

/* Error Response codes (IDS Table 2-6, read with Linux's enum tb_xdp_error;
 * record 35 section 2.1). */
#define TBT_XDP_ERR_UNKNOWN_TYPE    1UL
#define TBT_XDP_ERR_NOT_SUPPORTED   3UL
#define TBT_XDP_ERR_NOT_READY       4UL

/* USB4NET packet types (IDS Table 5-4). */
#define TBT_NET_LOGIN               0UL
#define TBT_NET_LOGIN_RESP          1UL
#define TBT_NET_LOGOUT              2UL
#define TBT_NET_LOGOUT_RESP         3UL
#define TBT_NET_SERVICE_REVISION    1UL
/* The Login Response failure this driver sends (a vendor failure, IDS
 * Table 5-6; record 35 section 4.1). */
#define TBT_NET_STATUS_REFUSED      0x80000001UL

/* The most a Properties Read Response carries of the block: the 63-word
 * packet less its 18 words before the data (record 35 section 2.3: Linux's
 * TB_XDP_PROPERTIES_MAX_DATA_LENGTH is the same 45). */
#define TBT_XDP_PROPS_CHUNK         45UL
/* The largest block this driver reads (record 35 section 3.7). */
#define TBT_XDP_PROPS_MAX           500UL

/* What TbtXdpDecode found. */
#define TBT_XDP_K_MALFORMED         0UL     /* lengths or fields wrong: drop and count */
#define TBT_XDP_K_OTHER             1UL     /* another protocol UUID: ignore (IDS ch. 5) */
#define TBT_XDP_K_DISCOVERY         2UL
#define TBT_XDP_K_NET               3UL

/* A decoded packet. Only the fields of its type are filled. */
typedef struct _TBT_XDP_PKT {
    ULONG Kind;                 /* TBT_XDP_K_* */
    BOOLEAN Response;           /* PDF 7 */
    TBT_ROUTE Route;            /* the CM bit cleared */
    ULONG Sn;
    ULONG Type;                 /* discovery or USB4NET type */
    /* discovery */
    TBT_UUID Src;               /* UUID Response; Properties Read Request and
                                 * Response; Changed Notification */
    TBT_UUID Dst;               /* Properties Read Request and Response */
    BOOLEAN HasSrcRoute;        /* UUID Response to a type-12 request */
    ULONG Offset;               /* Properties Read Request and Response */
    ULONG BlockSize;            /* Properties Read Response */
    ULONG Generation;
    const ULONG *Data;          /* the block's words in a Read Response */
    ULONG DataWords;
    ULONG Error;                /* Error Response */
    ULONG Status;               /* Link State responses */
    ULONG Slw;
    ULONG Tlw;
    /* USB4NET */
    TBT_UUID Requester;
    TBT_UUID Responder;
    ULONG RequestId;
    ULONG Revision;             /* Login */
    ULONG ServiceHop;           /* Login */
    ULONG NetStatus;            /* Login and Logout Responses */
    UCHAR Mac[6];               /* Login Response */
    ULONG MacLength;
} TBT_XDP_PKT, *PTBT_XDP_PKT;

/* Take one received frame (PDF 6 or 7). Never fails: the kind says what it
 * is, TBT_XDP_K_MALFORMED for one IDS chapter 2's rules have dropped. */
ULONG TbtXdpDecode(ULONG pdf, const ULONG *dw, ULONG words, PTBT_XDP_PKT pkt);

/* The builders: each fills `out` (TBT_XDP_MAX_WORDS words) and returns the
 * words to send, 0 for arguments it refuses. `route` is the peer's. */
ULONG TbtXdpBuildUuidRequest(PULONG out, const TBT_ROUTE *route, ULONG sn,
                             ULONG type);
ULONG TbtXdpBuildUuidResponse(PULONG out, const TBT_ROUTE *route, ULONG sn,
                              const TBT_UUID *self, BOOLEAN withRoute);
ULONG TbtXdpBuildPropsRequest(PULONG out, const TBT_ROUTE *route, ULONG sn,
                              const TBT_UUID *src, const TBT_UUID *dst,
                              ULONG offset);
ULONG TbtXdpBuildPropsResponse(PULONG out, const TBT_ROUTE *route, ULONG sn,
                               const TBT_UUID *src, const TBT_UUID *dst,
                               ULONG offset, const ULONG *block,
                               ULONG blockWords, ULONG generation);
ULONG TbtXdpBuildChanged(PULONG out, const TBT_ROUTE *route, ULONG sn,
                         const TBT_UUID *src);
ULONG TbtXdpBuildChangedResponse(PULONG out, const TBT_ROUTE *route, ULONG sn);
ULONG TbtXdpBuildError(PULONG out, const TBT_ROUTE *route, ULONG sn,
                       ULONG code);
ULONG TbtXdpBuildLinkStatusRequest(PULONG out, const TBT_ROUTE *route,
                                   ULONG sn);
ULONG TbtXdpBuildLinkStatusResponse(PULONG out, const TBT_ROUTE *route,
                                    ULONG sn, ULONG slw, ULONG tlw);
ULONG TbtXdpBuildLinkChangeRequest(PULONG out, const TBT_ROUTE *route,
                                   ULONG sn, ULONG tlw);
ULONG TbtXdpBuildLinkChangeResponse(PULONG out, const TBT_ROUTE *route,
                                    ULONG sn, ULONG status);
/* USB4NET: `requester` and `responder` as the transaction's roles (IDS
 * Table 5-3, record 35 section 3.2). */
ULONG TbtNetBuildLogin(PULONG out, const TBT_ROUTE *route,
                       const TBT_UUID *requester, const TBT_UUID *responder,
                       ULONG requestId, ULONG serviceHop);
ULONG TbtNetBuildLoginResponse(PULONG out, const TBT_ROUTE *route,
                               const TBT_UUID *requester,
                               const TBT_UUID *responder, ULONG requestId,
                               ULONG status, const UCHAR *mac6);
ULONG TbtNetBuildLogout(PULONG out, const TBT_ROUTE *route,
                        const TBT_UUID *requester, const TBT_UUID *responder,
                        ULONG requestId);
ULONG TbtNetBuildLogoutResponse(PULONG out, const TBT_ROUTE *route,
                                const TBT_UUID *requester,
                                const TBT_UUID *responder, ULONG requestId,
                                ULONG status);

/* ---------------------------------------------------------------------- */
/* The properties block (IDS chapter 4; record 35 section 3.7)            */
/* ---------------------------------------------------------------------- */

#define TBT_XDP_ROOT_MAGIC          0x55584401UL
#define TBT_XDP_ENTRY_IMMEDIATE     0x76UL  /* 'v' */
#define TBT_XDP_ENTRY_DATA          0x64UL  /* 'd' */
#define TBT_XDP_ENTRY_TEXT          0x74UL  /* 't' */
#define TBT_XDP_ENTRY_DIRECTORY     0x44UL  /* 'D' */
#define TBT_XDP_TEXT_MAX            64UL    /* characters kept of a text */
#define TBT_XDP_MAX_DEPTH           8UL
/* The largest block this driver builds. */
#define TBT_XDP_LOCAL_MAX           64UL

/* prtcstns (IDS Table 5-2). */
#define TBT_NET_STNS_E2E            0x00000001UL
#define TBT_NET_STNS_FRAME_ID       0x00000002UL
#define TBT_NET_STNS_64K            0x00000004UL

typedef struct _TBT_XDP_LOCAL {
    ULONG VendorId;
    const char *VendorText;     /* NULL: none */
    ULONG DeviceId;
    const char *DeviceText;
    ULONG DeviceRevision;
    ULONG MaxHopId;
    BOOLEAN Network;            /* publish the USB4NET directory */
    ULONG Prtcstns;
} TBT_XDP_LOCAL, *PTBT_XDP_LOCAL;

/* Build the block into `block`; the words it took, 0 when `capacity` is
 * too small or a text is too long. */
ULONG TbtXdpPropsBuild(const TBT_XDP_LOCAL *local, PULONG block,
                       ULONG capacity);

typedef struct _TBT_XDP_REMOTE {
    ULONG VendorId;
    ULONG DeviceId;
    ULONG DeviceRevision;
    BOOLEAN HasMaxHopId;
    ULONG MaxHopId;
    char VendorText[TBT_XDP_TEXT_MAX + 1];
    char DeviceText[TBT_XDP_TEXT_MAX + 1];
    BOOLEAN Network;            /* a network directory, prtcid 1, prtcvers 1 */
    TBT_UUID NetDirectory;
    ULONG Prtcstns;
    ULONG Prtcrevs;
    ULONG FaultOffset;          /* where a refused block went wrong */
} TBT_XDP_REMOTE, *PTBT_XDP_REMOTE;

/* Parse a whole block of `words` (at most TBT_XDP_PROPS_MAX). TBT_OK, or
 * TBT_E_PARAM with FaultOffset set for a block refused whole. */
LONG TbtXdpPropsParse(const ULONG *block, ULONG words, PTBT_XDP_REMOTE remote);

/* ---------------------------------------------------------------------- */
/* MAC and HopID                                                          */
/* ---------------------------------------------------------------------- */

/* IDS 5.1.5.3 over a host router's 64-bit UUID (ROUTER_CS_7 low, 8 high),
 * its router id and a USB4 port number, as the PDF's page 40 writes it,
 * C's precedence included. bit_swap_16 is taken as the reversal of the 16
 * bits [inf: the specification does not define it]. */
VOID TbtNetMacFromUuid(ULONG uuidLo, ULONG uuidHi, ULONG routerId,
                       ULONG portNumber, PUCHAR mac6);

/* Record 35 section 4.3's transmit HopID: the lowest from 8 that is at
 * most the peer's maxhopid and the lane's Max Output HopID and not set in
 * `inUse` (128 bits); exactly 15 for a peer with no maxhopid. 0: none. */
ULONG TbtNetChooseTxHop(BOOLEAN peerHasMax, ULONG peerMax, ULONG laneMaxOut,
                        const ULONG *inUse4);

/* ---------------------------------------------------------------------- */
/* The peer engine (record 35 sections 3.5, 3.6, 4)                       */
/* ---------------------------------------------------------------------- */

/* Discovery states. */
#define TBT_XDP_S_IDLE              0UL
#define TBT_XDP_S_UUID              1UL
#define TBT_XDP_S_LINK              2UL
#define TBT_XDP_S_BOND_LOW          3UL
#define TBT_XDP_S_BOND_HIGH         4UL
#define TBT_XDP_S_PROPS             5UL
#define TBT_XDP_S_READY             6UL
#define TBT_XDP_S_ERROR             7UL
#define TBT_XDP_S_LOOP              8UL

/* Service states. */
#define TBT_NET_S_OFF               0UL     /* not wanted, or nothing to log in to */
#define TBT_NET_S_LOGIN             1UL     /* our Login outstanding or due */
#define TBT_NET_S_UP                2UL     /* both logins done, paths built */
#define TBT_NET_S_LOGOUT            3UL     /* our Logout outstanding */
#define TBT_NET_S_ABANDONED         4UL     /* no HopID to log in with */

/* Timing (IDS 3.1 to 3.4.1, 5.1.1, 5.1.2; record 35 sections 3.5, 4.1). */
#define TBT_XDP_TRY_MS              1000UL
#define TBT_XDP_RESENDS             10UL
#define TBT_XDP_FIRST_MS            100UL   /* the first request after a start */
#define TBT_NET_LOGIN_FAST_MS       100UL
#define TBT_NET_LOGIN_FAST_FOR_MS   2000UL
#define TBT_NET_LOGIN_SLOW_MS       1000UL
#define TBT_NET_LOGIN_FOR_MS        60000UL
/* After TBT_NET_LOGIN_FOR_MS unanswered, a Login every 5 s for as long as
 * the service is wanted: a Linux peer answers a Login only while its
 * interface is up, which may come much later (run-45 45-E.15: Linux
 * itself retries every 5.12 s and never stops). */
#define TBT_NET_LOGIN_IDLE_MS       5000UL
#define TBT_XDP_BOND_WAIT_MS        10000UL /* BOND_HIGH: the peer's Change Request */
#define TBT_XDP_WIDTH_WAIT_MS       3000UL  /* BOND_HIGH: the width after it (IDS 3.4.1) */
#define TBT_XDP_WIDTH_LOW_MS        500UL   /* BOND_LOW: record 22 section 5.2 */

/* Lane widths as LANE_ADP_CS_0/1 code them (IDS Table 2-7). */
#define TBT_XDP_WIDTH_SINGLE        1UL
#define TBT_XDP_WIDTH_DUAL          3UL

typedef struct _TBT_XDP_OPS {
    PVOID Context;
    /* Send one inter-domain packet on ring 0. TBT_OK or a TBT_E_* code. */
    LONG  (*Send)(PVOID context, ULONG pdf, const ULONG *dw, ULONG words);
    ULONG (*NowMs)(PVOID context);
    VOID  (*Log)(PVOID context, const char *label, ULONG value);

    /* The link, under the software CM only (NULL under the firmware CM,
     * where the firmware owns it): this port's lane 0 Supported and
     * Target Link Width and its Negotiated Link Width (1 or 2 lanes). */
    LONG  (*LinkRead)(PVOID context, PULONG slw, PULONG tlw, PULONG lanes);
    LONG  (*LinkSetTarget)(PVOID context, ULONG tlw);  /* both lane adapters */
    LONG  (*LinkBond)(PVOID context);                  /* Lane Bonding set */
    VOID  (*LinkLane1Off)(PVOID context);              /* Guide 7.5.2 */

    /* The service: build the paths and rings for `txHop` out and `rxHop`
     * in (record 35 section 5); TBT_OK or the reason it could not. And
     * tear them down; `peerGone` says no peer is there to drain against. */
    LONG  (*ServiceUp)(PVOID context, ULONG txHop, ULONG rxHop);
    VOID  (*ServiceDown)(PVOID context, BOOLEAN peerGone);
    /* The peer's block has been read (or read again). */
    VOID  (*PeerReady)(PVOID context, const TBT_XDP_REMOTE *remote);
} TBT_XDP_OPS, *PTBT_XDP_OPS;

typedef struct _TBT_XDP_COUNTERS {
    ULONG Sent;
    ULONG Resends;
    ULONG Received;
    ULONG Malformed;
    ULONG Strays;           /* a response no request was waiting for */
    ULONG Answered;         /* requests of the peer answered */
    ULONG ErrorsSent;
    ULONG PropsReads;
    ULONG PropsRefused;
    ULONG Restarts;
    ULONG Bonded;
    ULONG BondSkipped;
    ULONG LoginsSent;
    ULONG LoginsReceived;
    ULONG LoginsRefused;
    ULONG LoginFailures;    /* a failure status in a response to ours */
    ULONG LogoutsSent;
    ULONG LogoutsReceived;
    ULONG ServicesUp;
    ULONG ServiceFailures;
    ULONG ErrorsReceived;   /* Error Responses (IDS 2.11) the peer sent */
    ULONG LoginIdOther;     /* Login Responses with a Request ID not ours,
                               taken (record 35 section 4.5)             */
} TBT_XDP_COUNTERS;

/*
 * Why a packet the engine took was dropped, for the log (record 35
 * section 3.12, task 48.16; the owner's bench of 2026-11-09): the first
 * TBT_XDP_DROPS_LOGGED drops of a record each write "cm.xdp.drop" =
 * reason 23:16, PDF 15:12, TbtXdpDecode's kind 11:8, packet type 7:0;
 * the first that is a UUID mismatch also writes the four UUIDs as
 * "cm.xdp.uuidw" lines (TBT_XDP_UUIDW_*). Counted always, as Strays or
 * Malformed.
 */
#define TBT_XDP_DROPS_LOGGED        16UL
#define TBT_XDP_DROP_MALFORMED      1UL     /* IDS chapter 2's rules       */
#define TBT_XDP_DROP_ROUTE          2UL     /* not this record's route     */
#define TBT_XDP_DROP_NOREMOTE       3UL     /* USB4NET before the peer's UUID */
#define TBT_XDP_DROP_PAIR           4UL     /* USB4NET UUIDs not the pair  */
#define TBT_XDP_DROP_LOGIN_UUID     5UL     /* a Login not Remote to Local */
#define TBT_XDP_DROP_LOGOUT_UUID    6UL     /* a Logout, the same          */
#define TBT_XDP_DROP_LOGIN_RESP     7UL     /* no Login of ours awaits it
                                               (also "cm.xdp.drop.id")     */
#define TBT_XDP_DROP_LOGOUT_RESP    8UL     /* no Logout of ours awaits it */
#define TBT_XDP_DROP_NET_TYPE       9UL     /* another USB4NET type        */
#define TBT_XDP_DROP_DISC_NONE      10UL    /* a discovery response with no
                                               request out, or another SN  */
#define TBT_XDP_DROP_DISC_TYPE      11UL    /* the wrong response type     */
/* "cm.xdp.uuidw" = index 23:16, a 16-bit half 15:0; index = UUID * 8 +
 * word * 2 + half (half 0 the low one), word 0 to 3 as TBT_UUID's Dw. */
#define TBT_XDP_UUIDW_LOCAL         0UL
#define TBT_XDP_UUIDW_REMOTE        1UL
#define TBT_XDP_UUIDW_REQUESTER     2UL     /* the dropped packet's        */
#define TBT_XDP_UUIDW_RESPONDER     3UL

/* One outstanding request and its schedule: a discovery request in
 * Disc, a USB4NET one in Net, each with its own tries and deadline, so a
 * properties re-read never waits for a Login and the reverse. */
typedef struct _TBT_XDP_SLOT {
    BOOLEAN Active;             /* a request is out, its answer awaited */
    BOOLEAN Armed;              /* DueMs is a deadline */
    ULONG Type;                 /* what was sent */
    ULONG Sn;                   /* discovery: its SN */
    ULONG Offset;               /* a Properties Read Request's */
    ULONG FirstId;              /* USB4NET: the first Request ID used */
    ULONG LastId;               /* and the latest */
    ULONG Tries;                /* sends of this step */
    ULONG DueMs;                /* the next send, or the step's end */
    ULONG StartMs;              /* the step's start */
} TBT_XDP_SLOT, *PTBT_XDP_SLOT;

typedef struct _TBT_XDP_PEER {
    TBT_XDP_OPS Ops;
    TBT_ROUTE Route;
    TBT_UUID Local;
    TBT_UUID Remote;
    BOOLEAN HaveRemote;
    BOOLEAN FirmwareCm;         /* the firmware CM: no UUID step, no bonding */
    BOOLEAN BondingPossible;    /* both local lanes dual, Gen 2 or 3, bonding on */
    BOOLEAN NetWanted;          /* the network driver holds the service open */

    ULONG State;                /* TBT_XDP_S_* */
    ULONG Sub;                  /* BOND_LOW: 0 change, 1 status, 2 width;
                                 * BOND_HIGH: 0 waiting, 1 width */
    TBT_XDP_SLOT Disc;
    TBT_XDP_SLOT Net;
    ULONG NextSn;

    /* Our block. */
    TBT_XDP_LOCAL LocalProps;
    ULONG LocalBlock[TBT_XDP_LOCAL_MAX];
    ULONG LocalWords;
    ULONG LocalGeneration;
    BOOLEAN ChangedOwed;        /* a Properties Changed Notification to send */
    ULONG ChangedSends;         /* its sends since it was first owed: at
                                   most TBT_XDP_RESENDS more, whatever
                                   re-read came between (record 35 3.18) */
    BOOLEAN ReadOwed;           /* the peer's notification came while ours
                                   was out: its read waits for our answer
                                   or give-up, so neither aborts the
                                   other's; or while a read was out: read
                                   again once that one completes (record
                                   35 section 3.18) */
    ULONG PropsRestarts;        /* reads made afresh (a notification during
                                   a read, or the block's generation changed
                                   in it) since the last block adopted or
                                   the last new peer: at most
                                   TBT_XDP_RESENDS, then the read fails
                                   (record 35 section 3.18) */

    /* The peer's block. */
    ULONG RemoteBlock[TBT_XDP_PROPS_MAX];
    ULONG RemoteWords;          /* the size its first response gave */
    ULONG RemoteGot;            /* words read so far */
    ULONG RemoteGeneration;
    BOOLEAN RemoteValid;
    TBT_XDP_REMOTE RemoteInfo;

    /* The service. */
    ULONG NetState;             /* TBT_NET_S_* */
    BOOLEAN LoginSentOk;        /* our Login answered with success */
    BOOLEAN LoginReceived;      /* we answered the peer's Login with success */
    BOOLEAN WaitPeer;           /* after a Logout either way: no Login of ours
                                 * until the peer logs in or its block changes */
    BOOLEAN Ending;             /* TbtXdpPeerEnd(FALSE): IDLE once logged out */
    BOOLEAN PathsUp;            /* ServiceUp succeeded and no ServiceDown since:
                                 * the one owner of the paths, whatever the
                                 * protocol state says */
    BOOLEAN PendingLogin;       /* a Login answered during our Logout, kept
                                 * for when it ends */
    ULONG PendingHop;
    BOOLEAN LoginSlow;          /* a failure answered: the slow rate now */
    BOOLEAN LoginIdle;          /* a minute unanswered: the idle rate now */
    /* The firmware CM's UUID Request at a start (record 35 section 3.11):
     * resent each TBT_XDP_TRY_MS, at most TBT_XDP_RESENDS times, until a
     * UUID Response comes. */
    BOOLEAN NudgeDone;
    ULONG NudgeTries;
    ULONG NudgeDueMs;
    ULONG PeerTlw;              /* BOND_HIGH: the TLW the peer asked for */
    ULONG NextRequestId;
    ULONG TxHop;
    ULONG RxHop;
    ULONG LocalMaxHop;          /* our published maxhopid, the input bound */
    ULONG LaneMaxOut;           /* the lane's Max Output HopID */
    ULONG HopsInUse[4];         /* HopIDs other paths hold on the lane */
    UCHAR Mac[6];               /* ours, for the Login Response */
    UCHAR PeerMac[6];

    TBT_XDP_COUNTERS Counters;
    ULONG DropsLogged;          /* "cm.xdp.drop" lines written       */
    ULONG ErrsLogged;           /* "cm.xdp.err.rx" lines written     */
    BOOLEAN UuidsLogged;        /* the UUIDs written once            */
    BOOLEAN LoginIdLogged;      /* "cm.net.login.id" written this login */
} TBT_XDP_PEER, *PTBT_XDP_PEER;

/* Prepare a peer: `local` this domain's UUID; `remote` NULL under the
 * software CM (the UUID step finds it) or the firmware CM's connected
 * event's. The local block is built from `props`. */
VOID TbtXdpPeerInit(PTBT_XDP_PEER peer, const TBT_XDP_OPS *ops,
                    const TBT_ROUTE *route, const TBT_UUID *local,
                    const TBT_UUID *remote, BOOLEAN firmwareCm,
                    BOOLEAN bondingPossible, const TBT_XDP_LOCAL *props,
                    const UCHAR *mac6, ULONG laneMaxOut,
                    const ULONG *hopsInUse4);

/* Start the discovery (record 35 section 3.5): UUID, or PROPS under the
 * firmware CM. */
VOID TbtXdpPeerStart(PTBT_XDP_PEER peer);

/* Resend what is due, end what timed out, start what is owed. */
VOID TbtXdpPeerTick(PTBT_XDP_PEER peer);

/* One PDF 6 or 7 frame for this link. TRUE when it was this peer's (taken
 * or answered), FALSE for a frame of another protocol the caller may
 * offer elsewhere. */
BOOLEAN TbtXdpPeerOnPacket(PTBT_XDP_PEER peer, ULONG pdf, const ULONG *dw,
                           ULONG words);

/* A hot-plug event with UPG 1 on the port's lane 0: during a bonding it is
 * the bonding's end (IDS 3.4.1), so the width is read; at any other time
 * it means nothing here, the caller's port check deciding (record 35
 * section 3.8). */
VOID TbtXdpPeerOnLinkEvent(PTBT_XDP_PEER peer);

/* The network driver opened or closed the service (record 35 section
 * 6.3): the block rebuilt with or without the directory, the peer told,
 * a Login started or a Logout sent. */
VOID TbtXdpPeerSetNetWanted(PTBT_XDP_PEER peer, BOOLEAN wanted,
                            ULONG prtcstns);

/* The peer ended (record 35 section 3.8). `gone` TRUE for an unplug or a
 * lost host interface: no Logout, the service down at once. FALSE: a
 * Logout first where the service is up (the engine ends it when the
 * Logout is answered or times out, then goes IDLE). */
VOID TbtXdpPeerEnd(PTBT_XDP_PEER peer, BOOLEAN gone);

/* The record's counters into the log (record 35 section 3.12): at the
 * Login's minute unanswered, and at the record's end (an unplug, a stop):
 * "cm.xdp.n.rx" packets taken, "cm.xdp.n.malformed", "cm.xdp.n.strays",
 * "cm.xdp.n.errors" Error Responses, "cm.net.n.logins" sent 23:12 and
 * received 11:0, "cm.net.n.loginfail" failures answered 23:12 and refused
 * 11:0 (each saturating), "cm.net.n.loginid" Login Responses taken with a
 * Request ID not this host's. */
VOID TbtXdpPeerLogCounters(PTBT_XDP_PEER peer);

/* Whether the engine has nothing outstanding and owes nothing (a stop may
 * proceed). */
BOOLEAN TbtXdpPeerQuiet(const TBT_XDP_PEER *peer);

#endif /* TBT_XDP_H */
