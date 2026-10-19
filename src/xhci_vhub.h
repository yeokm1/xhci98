/*
 * xhci_vhub.h - the virtual USB 2.0 hub on a root port, roadmap task 24.3,
 * design record 12 (docs/contributing/design/12-virtual-hub-on-root-ports.md).
 *
 * **The virtual hub was the miniport's, and the HCD has none.** Its pure core
 * (xhci_vhub.c), its host suite (test_vhub.c) and the root-hub and device
 * halves that drove it (xhci_rh.c, xhci_slot.c) left the tree on 2026-10-02;
 * the HCD reads no XhciVirtualHSHub* value (hcd_ctl.c). What stays here is
 * the types and values XHCI_EXTENSION embeds (`VhubConfig`, `Vhub[]`), kept
 * for the layout the offset tables and snapshot readers depend on; those
 * fields are never written, and the text below describes the miniport. The
 * functions this header used to declare have no definition anywhere and are
 * no longer declared.
 *
 * C89 only.
 */

#ifndef XHCI_VHUB_H
#define XHCI_VHUB_H

/*
 * Only the compat shim, on src/xhci_topo.h's terms: the extension embeds
 * XHCI_VHUB, so src/xhci.h has to be able to include this header, and nothing
 * here may include xhci.h back.
 */
#include "xhci_compat.h"

/* ------------------------------------------------------------------ */
/* The switch and the ids (record 12 section 3.1)                      */
/* ------------------------------------------------------------------ */

/* `XhciVirtualHSHub`'s values. Anything else is refused and applied as OFF. */
#define XHCI_VHUB_MODE_OFF          0U
#define XHCI_VHUB_MODE_ON_DEMAND    1U
#define XHCI_VHUB_MODE_ALWAYS       2U

/* Why the applied mode is OFF when the value read was not. NONE covers every
 * case that is not a refusal: 0 read, nothing read, and a mode applied. */
#define XHCI_VHUB_WHY_NONE          0U
#define XHCI_VHUB_WHY_SWITCH        1U  /* the switch held a value but 0/1/2 */
#define XHCI_VHUB_WHY_VID           2U  /* XhciVirtualHSHubVid failed        */
#define XHCI_VHUB_WHY_PID           3U  /* XhciVirtualHSHubPid failed        */

/*
 * One id string's verdict. **0 is "not consulted"**, so a zeroed record - which
 * is what usbport hands the miniport at every start - reads correctly before
 * the read has run, and with the switch off, where the ids are never read at
 * all (3.1: "their state cannot affect rule 2").
 */
#define XHCI_VHUB_ID_UNREAD         0U  /* not consulted                     */
#define XHCI_VHUB_ID_OK             1U
#define XHCI_VHUB_ID_MISSING        2U  /* the service's one failure code:
                                           absent, unreadable or too long   */
#define XHCI_VHUB_ID_NO_NUL         3U  /* no terminator inside the buffer   */
#define XHCI_VHUB_ID_DIGITS         4U  /* fewer or more than four digits    */
#define XHCI_VHUB_ID_CHAR           5U  /* a character that is not hex       */
#define XHCI_VHUB_ID_ZERO_VID       6U  /* vendor id 0000                    */

/*
 * Which encoding the parser found. Recorded because it is the one fact about
 * the read no reading has taken on Windows 98 or ME (3.1), and 24.3.4 reads it
 * first; a snapshot that says which one arrived is that reading.
 */
#define XHCI_VHUB_ENC_NONE          0U
#define XHCI_VHUB_ENC_UTF16         1U
#define XHCI_VHUB_ENC_BYTE          2U

/*
 * The byte count asked of the service for each id: the longest accepted form,
 * `0x` plus four digits plus a terminator, seven characters, at two bytes each.
 * usbport's service copies exactly this many whatever the value holds and
 * fails a value longer than it (the ABI document,
 * "`UsbPortGetMiniportRegistryKeyValue` - read out of both binaries"), so a
 * shorter value arrives followed by bytes nobody may read, and a longer one
 * does not arrive at all.
 */
#define XHCI_VHUB_ID_CHARS          7U
#define XHCI_VHUB_ID_BUF_BYTES      (XHCI_VHUB_ID_CHARS * 2U)

/*
 * What the snapshot header carries (3.1: "what was read, what was applied and
 * the status"). The statuses are the service's MPSTATUS values as read; an id's
 * status means something only when its verdict is not XHCI_VHUB_ID_UNREAD.
 * Four ULONGs then eight bytes: a multiple of eight, so embedding it moves no
 * amd64 tail padding (the trap test_packet_amd64's trailing signature caught
 * once).
 */
typedef struct _XHCI_VHUB_CONFIG {
    ULONG SwitchStatus;
    ULONG SwitchValue;      /* as read; 0 when the read failed               */
    ULONG VidStatus;
    ULONG PidStatus;
    USHORT Vid;             /* valid only when VidResult is XHCI_VHUB_ID_OK  */
    USHORT Pid;
    UCHAR Applied;          /* XHCI_VHUB_MODE_*                              */
    UCHAR Refused;          /* XHCI_VHUB_WHY_*                               */
    UCHAR VidResult;        /* XHCI_VHUB_ID_*                                */
    UCHAR PidResult;
    UCHAR VidEncoding;      /* XHCI_VHUB_ENC_*                               */
    UCHAR PidEncoding;
    UCHAR Reserved[6];
} XHCI_VHUB_CONFIG, *PXHCI_VHUB_CONFIG;

/* ------------------------------------------------------------------ */
/* The request table (record 12 section 3.3)                           */
/* ------------------------------------------------------------------ */

/*
 * Everything the descriptors need that is not a constant of the device. The
 * ids are XHCI_VHUB_CONFIG's; `BcdDevice` (the driver version, as 3.3's row
 * asks) and `PowerOnToPowerGood` (the root hub's own value, 2 ms units) are the
 * caller's, so this file carries no version and no timing of its own.
 */
typedef struct _XHCI_VHUB_IDENTITY {
    USHORT Vid;
    USHORT Pid;
    USHORT BcdDevice;
    UCHAR PowerOnToPowerGood;
    UCHAR Reserved;
} XHCI_VHUB_IDENTITY, *PXHCI_VHUB_IDENTITY;

/*
 * What one SETUP packet asks of the virtual device. DATA is answered from
 * `reply`, already truncated to `wLength`; OK is a success with no data; STALL
 * completes the transfer with a stall status (3.3: a refusal that can never
 * stop being true fails the transfer rather than returning nonzero). The rest
 * are the caller's to carry out on the per-port state below, and each carries
 * its argument in `*arg`:
 *
 *   SET_ADDRESS        the new address (wValue)
 *   SET_CONFIG         the configuration value, 0 or 1
 *   PORT_STATUS        nothing; answer with XhciVhubPort1Report's four bytes
 *   PORT_SET, PORT_CLEAR   the feature selector (wValue), port 1 checked here
 */
#define XHCI_VHUB_REQ_STALL         0U
#define XHCI_VHUB_REQ_DATA          1U
#define XHCI_VHUB_REQ_OK            2U
#define XHCI_VHUB_REQ_SET_ADDRESS   3U
#define XHCI_VHUB_REQ_SET_CONFIG    4U
#define XHCI_VHUB_REQ_PORT_STATUS   5U
#define XHCI_VHUB_REQ_PORT_SET      6U
#define XHCI_VHUB_REQ_PORT_CLEAR    7U

/*
 * The largest DATA answer: the configuration descriptor, 25 bytes, is shorter
 * than the product string's 44. A caller's reply buffer must hold this many.
 */
#define XHCI_VHUB_REPLY_MAX         44U

/* The descriptors' fixed lengths, which vectors and callers both read. */
#define XHCI_VHUB_DEVICE_DESC_LEN   18U
#define XHCI_VHUB_CONFIG_DESC_LEN   25U
#define XHCI_VHUB_HUB_DESC_LEN      9U
#define XHCI_VHUB_LANGID_DESC_LEN   4U
#define XHCI_VHUB_PRODUCT_DESC_LEN  44U

/*
 * Hub-class port feature selectors (USB 2.0 Table 11-17). Same provenance note
 * as src/xhci.h's XHCI_HUB_PORT_* bits: no USB 2.0 PDF is in docs/references.
 * PORT_RESET and PORT_POWER are also the two src/xhci_topo.h measured on the
 * bus; the change selectors are the status bits' positions plus 16, which is
 * what `CLEAR_FEATURE(C_PORT_CONNECTION)` measured as 16 in record 02 says.
 */
#define XHCI_VHUB_SEL_PORT_ENABLE       1U
#define XHCI_VHUB_SEL_PORT_SUSPEND      2U
#define XHCI_VHUB_SEL_PORT_RESET        4U
#define XHCI_VHUB_SEL_PORT_POWER        8U
#define XHCI_VHUB_SEL_C_PORT_CONNECTION 16U
#define XHCI_VHUB_SEL_C_PORT_ENABLE     17U
#define XHCI_VHUB_SEL_C_PORT_SUSPEND    18U
#define XHCI_VHUB_SEL_C_PORT_OVER_CURRENT 19U
#define XHCI_VHUB_SEL_C_PORT_RESET      20U

/* The one LANGID the language table lists: English (United States). */
#define XHCI_VHUB_LANGID            0x0409U

/* The interrupt endpoint's bInterval (3.3's configuration row). */
#define XHCI_VHUB_INTERRUPT_INTERVAL 12U

/* ------------------------------------------------------------------ */
/* The per-port state (record 12 sections 3.2, 3.5, 3.8)               */
/* ------------------------------------------------------------------ */

/*
 * What a call asks the caller to do, as a bit set. Each names a body that
 * exists today or a step 24.3.3 adds; nothing here performs it.
 *
 *   PHYS_RESET     set PORTSC.PR through the existing reset path, under the
 *                  generation passed in (the one this record now owns)
 *   RESET_HELD     a disable is owed on this port: complete the request, and
 *                  start nothing until XhciVhubDisownCollected (3.3's gate)
 *   DISABLE        the existing RH_ClearFeaturePortEnable body on the
 *                  physical port: the PED write, the disown, the confirmed half
 *   POWER_OFF, POWER_ON    the existing port-power bodies
 *   SUSPEND, RESUME        the existing suspend and resume bodies (3.5)
 *   ROOT_CHANGE    a root-port view change was latched: announce it through
 *                  UsbPortInvalidateRootHub, as a shadow latch is today
 *   PIPE           a port-1 change was latched: complete the held
 *                  status-change transfer if XhciVhubPipeByte says so (3.4)
 *   CANCEL_PIPE    complete the held status-change transfer as cancelled
 *   ARM_HUB        the next address-0 open on this port is the hub's
 *   ARM_DEVICE     arm today's root-port enumeration claim for the device
 *                  behind port 1 (3.6), which drops the hub's
 *   FORCE_CONNECT  latch C_PORT_CONNECTION in the root port's shadow (3.2)
 *   DROP           value 1: the virtual record is gone - its address leaves
 *                  the map and its held transfer is completed as cancelled
 */
#define XHCI_VHUB_DO_NONE           0x0000UL
#define XHCI_VHUB_DO_PHYS_RESET     0x0001UL
#define XHCI_VHUB_DO_RESET_HELD     0x0002UL
#define XHCI_VHUB_DO_DISABLE        0x0004UL
#define XHCI_VHUB_DO_POWER_OFF      0x0008UL
#define XHCI_VHUB_DO_POWER_ON       0x0010UL
#define XHCI_VHUB_DO_SUSPEND        0x0020UL
#define XHCI_VHUB_DO_RESUME         0x0040UL
#define XHCI_VHUB_DO_ROOT_CHANGE    0x0080UL
#define XHCI_VHUB_DO_PIPE           0x0100UL
#define XHCI_VHUB_DO_CANCEL_PIPE    0x0200UL
#define XHCI_VHUB_DO_ARM_HUB        0x0400UL
#define XHCI_VHUB_DO_ARM_DEVICE     0x0800UL
#define XHCI_VHUB_DO_FORCE_CONNECT  0x1000UL
#define XHCI_VHUB_DO_DROP           0x2000UL

/* Value 1's decision on one root port (3.2). NONE until a reset has decided,
 * and again after a disconnect or a decision that flipped. */
#define XHCI_VHUB_DECIDED_NONE      0U
#define XHCI_VHUB_DECIDED_DIRECT    1U
#define XHCI_VHUB_DECIDED_HUB       2U

/* The virtual hub's own USB device state. */
#define XHCI_VHUB_DEV_DEFAULT       0U
#define XHCI_VHUB_DEV_ADDRESSED     1U
#define XHCI_VHUB_DEV_CONFIGURED    2U

/*
 * Whose physical reset is in flight. One PORTSC.PR serves two views, so its
 * completion - a PRC or the deadline - is routed by the owner recorded when it
 * was started and the generation it was started under, never by what the port
 * looks like when it ends (record 12 3.2, as amended in 24.3.2).
 */
#define XHCI_VHUB_OWNER_NONE        0U
#define XHCI_VHUB_OWNER_ROOT        1U
#define XHCI_VHUB_OWNER_PORT1       2U
/* A port-1 reset still running on the port when a value-2 root reset, which
 * is synthetic, overtook it (3.8): its end is neither the device's claim
 * nor the root port's change, and the hub's open the root reset armed
 * stands. */
#define XHCI_VHUB_OWNER_SUPERSEDED  3U

/*
 * One root port's virtual hub. Indexed as `RootHub.Ports` is, one per managed
 * port, embedded in the extension (no pool) and outside the topology graph
 * (3.3). All zero is "no hub, nothing decided", which is what usbport's
 * zeroing of the extension at every start leaves.
 *
 * Two views of one physical port, each with its own bits (3.4, 3.5):
 *
 *   Up*   the upstream view - the root port as usbport reads it. At 2 it is
 *         the whole root report (3.8); at 1 it supplies the enable, reset
 *         and suspend groups of a port in virtual-hub mode and nothing else.
 *   P1*   port 1 - what GET_PORT_STATUS(1) reads.
 *
 * Four ULONG-sized words of bytes after the generation: a multiple of eight.
 */
typedef struct _XHCI_VHUB {
    ULONG ResetGeneration;  /* the physical reset ResetOwner owns          */
    UCHAR Present;          /* a virtual hub stands on this port            */
    UCHAR Decision;         /* XHCI_VHUB_DECIDED_*, value 1 only            */
    UCHAR DevState;         /* XHCI_VHUB_DEV_*                              */
    UCHAR Address;          /* usbport's address for the hub, 0 in Default  */
    UCHAR Ep0Bound;         /* usbport holds the hub's EP0 open             */
    UCHAR HubOpenArmed;     /* the next address-0 open here is the hub's    */
    UCHAR ResetSuppressed;  /* the one-per-claim bound of 3.6               */
    UCHAR ResetKeeps;       /* the reset in flight keeps the EP0 binding    */
    UCHAR ResetOwner;       /* XHCI_VHUB_OWNER_*                            */
    UCHAR ResetHeld;        /* XHCI_VHUB_OWNER_* of a reset held by 3.3     */
    UCHAR PhysSuspended;    /* suspend asked of the port and not undone     */
    UCHAR UpPower;
    UCHAR UpEnabled;
    UCHAR UpResetting;
    UCHAR UpSuspend;
    UCHAR UpResumeOwed;     /* its C_PORT_SUSPEND waits on the port         */
    UCHAR UpChanges;        /* XHCI_HUB_C_PORT_* this view latched          */
    UCHAR P1Power;          /* port 1's own power bit, never PORTSC.PP      */
    UCHAR P1Enabled;
    UCHAR P1Resetting;
    UCHAR P1Suspend;
    UCHAR P1ResumeOwed;
    UCHAR P1Changes;
    UCHAR LateEnd;          /* a port-1 reset's deadline passed: its PRC,
                             * if one comes, is nobody's (task 24.3.4)    */
    UCHAR Reserved[4];
} XHCI_VHUB, *PXHCI_VHUB;

XHCI_C_ASSERT(vhub_config_size, sizeof(XHCI_VHUB_CONFIG) == 32);
XHCI_C_ASSERT(vhub_size, sizeof(XHCI_VHUB) == 32);

#endif /* XHCI_VHUB_H */
