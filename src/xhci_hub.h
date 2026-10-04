/*
 * xhci_hub.h - the hub class inside the bus, its pure half (roadmap-hcd.md
 * task 27-A.1; design record 13 sections 10.1 to 10.3).
 *
 * The decisions the controller thread takes about an external hub that need
 * no register, lock or kernel service, so a host suite can drive each with
 * no hub at all:
 *
 *   - the hub descriptor's fields (bNbrPorts, wHubCharacteristics,
 *     bPwrOn2PwrGood) and how many of its ports the bus manages;
 *   - the status-change report: its length and the bitmap it carries;
 *   - one port's GET_STATUS answer: which change bits to clear, whether the
 *     port's enumeration machine (xhci_enum.c) is fed a disconnect, a
 *     connect or both, and whether the port lost its power;
 *   - a port reset's progress and the speed it left;
 *   - the depth rule: whether a hub at a tier may have routable children;
 *   - the multi-TT rule: whether alternate setting 1 is selected;
 *   - the instance key a device PDO behind hubs is named by.
 *
 * The Route String, the Parent Hub Slot ID and Port Number, and the TT a
 * device sits behind are xhci_topo.c's (XhciTopoChildOf, XhciTopoTtFor),
 * which the bus feeds with its own hub traffic (27-A.2). This file only
 * decides what the hub class does next.
 *
 * Request codes and port feature selectors: design record 13 section 10.1's
 * table (xhci_topo.h lines 112-135 as measured on the wire; ReactOS
 * usbport.h lines 48-59 as interface documentation). wPortStatus and
 * wPortChange bits: xhci.h's XHCI_HUB_* (USB 2.0 Tables 11-21 and 11-22,
 * with the provenance note there). Every number marked "to transcribe" in
 * section 10 stays so: none here is read from the USB 2.0 specification,
 * which docs/references does not hold.
 *
 * DDK-free: part of the pure core. IRQL: any.
 */

#ifndef XHCI_HUB_H
#define XHCI_HUB_H

#include "xhci_compat.h"

#define XHCI_HUB_OK             0UL
#define XHCI_HUB_MALFORMED      1UL
#define XHCI_HUB_BAD_PARAM      2UL

/* bmRequestType values (section 10.1). */
#define XHCI_HUB_RT_HUB_IN      0xA0U   /* GET_DESCRIPTOR(Hub), GET_STATUS(hub) */
#define XHCI_HUB_RT_HUB_OUT     0x20U   /* CLEAR_FEATURE(hub)                   */
#define XHCI_HUB_RT_PORT_IN     0xA3U   /* GET_STATUS(port)                     */
#define XHCI_HUB_RT_PORT_OUT    0x23U   /* SET_FEATURE / CLEAR_FEATURE(port)    */
#define XHCI_HUB_RT_INTERFACE   0x01U   /* SET_INTERFACE                        */

/* bRequest. */
#define XHCI_HUB_REQ_GET_STATUS     0x00U
#define XHCI_HUB_REQ_CLEAR_FEATURE  0x01U
#define XHCI_HUB_REQ_SET_FEATURE    0x03U
#define XHCI_HUB_REQ_GET_DESCRIPTOR 0x06U
#define XHCI_HUB_REQ_SET_CONFIG     0x09U
#define XHCI_HUB_REQ_SET_INTERFACE  0x0BU

/* GET_DESCRIPTOR(Hub): wValue 0x2900 first, and 0x0000 - what both
 * Windows hub drivers send (design record 02, "The graph") - after a STALL;
 * wLength 71, as they asked (section 10.1). */
#define XHCI_HUB_DESC_VALUE         0x2900U
#define XHCI_HUB_DESC_VALUE_WINDOWS 0x0000U
#define XHCI_HUB_DESC_REQUEST_BYTES 71UL

/* Port feature selectors (section 10.1). */
#define XHCI_HUB_FEAT_PORT_ENABLE       1UL
#define XHCI_HUB_FEAT_PORT_SUSPEND      2UL
#define XHCI_HUB_FEAT_PORT_RESET        4UL
#define XHCI_HUB_FEAT_PORT_POWER        8UL
#define XHCI_HUB_FEAT_C_PORT_CONNECTION 16UL
#define XHCI_HUB_FEAT_C_PORT_ENABLE     17UL
#define XHCI_HUB_FEAT_C_PORT_SUSPEND    18UL
#define XHCI_HUB_FEAT_C_PORT_OVER_CURRENT 19UL
#define XHCI_HUB_FEAT_C_PORT_RESET      20UL
/* Hub feature selectors: C_HUB_LOCAL_POWER 0, C_HUB_OVER_CURRENT 1 (section
 * 10.1, to transcribe from Table 11-17). */
#define XHCI_HUB_FEAT_C_HUB_LOCAL_POWER 0UL
#define XHCI_HUB_FEAT_C_HUB_OVER_CURRENT 1UL

/* bDeviceClass of a hub, and bDeviceProtocol of a multi-TT one
 * (test-equipment.md rows 3 and 4). */
#define XHCI_HUB_CLASS              0x09UL
#define XHCI_HUB_PROTOCOL_MULTI_TT  0x02UL

/*
 * The ports the bus manages on one hub. A Route String nibble names ports 1
 * to 15, and a port above 14 is written as 15 (xHCI Table 6-4 footnote 106,
 * xhci_topo.h XHCI_TOPO_ROUTE_MAX_PORT), so ports 15 and up of a wider hub
 * would share one route; the bus manages ports 1 to 14 and leaves the rest
 * unpowered, which XhciHubManagedPorts says.
 */
#define XHCI_HUB_MAX_PORTS          14UL

/* The status-change report's longest form: bit 0 the hub and bit n port n,
 * sized by the declared port count, not the managed one, since the hub sends
 * its whole bitmap (XhciHubStatusBytes) - 32 bytes for 255 ports. */
#define XHCI_HUB_STATUS_MAX_BYTES   32UL

/* The hub descriptor's fields (section 10.1; usb100.h's packed
 * USB_HUB_DESCRIPTOR). */
#define XHCI_HUB_DESC_MIN_BYTES     7UL
#define XHCI_HUB_DESC_TYPE          0x29UL

typedef struct _XHCI_HUB_DESC {
    ULONG Ports;            /* bNbrPorts                                  */
    ULONG Characteristics;  /* wHubCharacteristics                        */
    ULONG PowerGoodMs;      /* bPwrOn2PwrGood x 2                         */
    ULONG ControllerCurrent;/* bHubContrCurrent                           */
    ULONG ThinkTime;        /* wHubCharacteristics 6:5 (TTT)              */
    ULONG Managed;          /* XhciHubManagedPorts(Ports)                 */
} XHCI_HUB_DESC, *PXHCI_HUB_DESC;

/*
 * Parse a hub descriptor reply of `length` bytes. XHCI_HUB_MALFORMED for a
 * reply shorter than the 7-byte header, a bLength below it or past what
 * arrived, a descriptor type other than 0x29, or no ports at all - the same
 * reply xhci_topo.c's fold would count bad or portless, here refused, since
 * the bus cannot serve a hub it cannot describe.
 */
ULONG XhciHubParseDescriptor(const UCHAR *data, ULONG length,
                             PXHCI_HUB_DESC out);

/* Ports 1 to min(declared, XHCI_HUB_MAX_PORTS). */
ULONG XhciHubManagedPorts(ULONG declared);

/* The status-change report's length for `declared` ports: one bit for the
 * hub and one per port, rounded up to bytes, at most
 * XHCI_HUB_STATUS_MAX_BYTES. */
ULONG XhciHubStatusBytes(ULONG declared);

/* The bits of a report the bus acts on: bit 0 the hub, bit n port n for n
 * up to `managed`; a bit past what arrived is 0. */
ULONG XhciHubStatusBitmap(const UCHAR *data, ULONG bytes, ULONG managed);

/* Whether a report of `bytes` bytes carries bit `bit` - any bit, managed or
 * not: the bus reads the unmanaged ones only to silence them (hcd_hub.c,
 * HcdHubSilence). */
ULONG XhciHubReportHas(const UCHAR *data, ULONG bytes, ULONG bit);

/* Every bit the bus acts on for a hub of `managed` ports: the hub's own and
 * each port's - what a poll, or a hub's first look, treats as changed. */
ULONG XhciHubAllBits(ULONG managed);

/*
 * One port's GET_STATUS answer, decided. `state` is the port's enumeration
 * machine state (XHCI_ENUM_*). The rules are the root port's (hcd_enum.c,
 * hcdPortChanged), with the hub's own two additions:
 *
 *   C_PORT_CONNECTION  whatever the port held goes (Disconnect, unless the
 *                      machine holds nothing), and a connection now is a
 *                      new device (Connect);
 *   no connect change  a port that reads disconnected goes; an Empty one
 *                      that reads connected starts (the hub's first look,
 *                      a device present at power-on raising no change);
 *   C_PORT_ENABLE      with the port now disabled under a device the
 *                      machine holds: the hub disabled it (babble, an
 *                      error) and the device is enumerated afresh -
 *                      Disconnect, then Connect if still connected;
 *   C_PORT_OVER_CURRENT with the port unpowered: OverCurrent, and the port
 *                      is powered again (Repower) - its device, if any, is
 *                      gone with the power and leaves by the rule above.
 *
 * Clear is the change bits seen, as wPortChange bits (XHCI_HUB_C_PORT_*):
 * each is cleared with its C_PORT_ selector, 16 + its bit position.
 */
typedef struct _XHCI_HUB_PORT_DECISION {
    ULONG Clear;            /* wPortChange bits to clear                  */
    ULONG Disconnect;       /* feed the machine a disconnect first        */
    ULONG Connect;          /* then a connect                             */
    ULONG OverCurrent;
    ULONG Repower;
    ULONG Suspended;        /* C_PORT_SUSPEND: a resume finished          */
    ULONG Resume;           /* connected and suspended: resume it         */
    ULONG Retry;            /* a resume did not finish: look again later  */
    ULONG GaveUp;           /* XHCI_HUB_RESUME_TRIES resumes did not
                             * finish: the port is enumerated afresh     */
} XHCI_HUB_PORT_DECISION, *PXHCI_HUB_PORT_DECISION;

VOID XhciHubPortDecide(ULONG state, ULONG status, ULONG change,
                       PXHCI_HUB_PORT_DECISION out);

/*
 * Suspend and resume, handled and never initiated (the owner's decision of
 * 2026-10-04; selective suspend waits for 28.3's idle policy): the bus
 * starts no suspend, but a hub may report a port suspended - its device
 * suspended by something else, or left so across the hub's own reset - or
 * a resume finished (C_PORT_SUSPEND, a device's remote wake among them).
 * XhciHubPortDecide sets Resume for a connected port whose wPortStatus shows
 * PORT_SUSPEND, so the bus resumes it before anything else is asked of the
 * device; a C_PORT_SUSPEND on a connected, enabled port is a finished resume
 * and needs no re-enumeration (Suspended, and neither Disconnect nor
 * Connect). XhciHubResumeBeforeReset says the same before a reset.
 *
 * The bus resumes a port with ClearPortFeature(PORT_SUSPEND); the hub then
 * drives resume signalling for at least TDRSMDN, 20 ms (USB 2.0 7.1.7.7,
 * quoted by xHCI p.256; to transcribe), and ends it with C_PORT_SUSPEND and
 * the suspend bit clear. Then the resume recovery, TRSMRCY 10 ms (USB 2.0
 * 7.1.7.7, to transcribe), before the device is addressed.
 */
#define XHCI_HUB_RESUME_FIRST_MS    20UL    /* TDRSMDN, to transcribe   */
#define XHCI_HUB_RESUME_WAIT_MS     100UL   /* bus policy               */
#define XHCI_HUB_RESUME_RECOVERY_MS 10UL    /* TRSMRCY, to transcribe   */

/* Whether a port must be resumed before it is reset: connected and
 * suspended. */
ULONG XhciHubResumeBeforeReset(ULONG status);

/*
 * A resume's outcome, from one GET_STATUS answer (XhciHubResumeProgress):
 * PENDING while the suspend bit is still set; DONE once it is clear on a
 * connected, enabled port; DISABLED when it cleared with the enable bit -
 * a port error during the resume, which USB 2.0 11.24.2.7.1.3 allows (to
 * transcribe) - the device to be enumerated afresh; GONE when the device
 * left. STUCK is the caller's: the request failed or the resume did not
 * end by its deadline.
 */
#define XHCI_HUB_RESUME_PENDING     0UL
#define XHCI_HUB_RESUME_DONE        1UL
#define XHCI_HUB_RESUME_DISABLED    2UL
#define XHCI_HUB_RESUME_GONE        3UL
#define XHCI_HUB_RESUME_STUCK       4UL

/* Resumes of one port that may fail in a row before the bus gives up and
 * enumerates the port afresh (a bus policy number). */
#define XHCI_HUB_RESUME_TRIES       3UL

ULONG XhciHubResumeProgress(ULONG status);

/*
 * A resume's outcome settled against the change bits its last GET_STATUS
 * read: DONE with C_PORT_CONNECTION raised is DISABLED - the device was
 * replaced during the resume, so the held devices are not let go and the
 * port is enumerated afresh (Codex review of the Phase 28-31 merge,
 * finding 1). C_PORT_CONNECTION is bit 0 at USB 2.0 and SuperSpeed alike
 * (xhci_sshub.h), so one rule serves both. Every other outcome stands.
 */
ULONG XhciHubResumeSettle(ULONG progress, ULONG change);

/*
 * What a resume's outcome makes of the port's decision *d, the port's
 * enumeration machine in `state`, whether the resume held devices (`held`:
 * the device on the port, and below it if it is a hub, quiesced for the
 * resume) and the port's failures in a row in *tries:
 *
 *   DONE      the decision stands (an Empty port goes on to Connect), and
 *             the count restarts; only now are the held devices let go;
 *   DISABLED  the device is enumerated afresh: Disconnect if the machine
 *             holds one, then Connect;
 *   GONE      Disconnect if the machine holds one, no Connect;
 *   STUCK     with devices held: they stay held and are torn down at once
 *             (GaveUp, Disconnect, then Connect) - a resume that may yet
 *             finish must never meet their traffic before its recovery
 *             (Codex review of the Phase 27 integration, round 5, finding
 *             1); the port's reset tries the resume once more first. With
 *             nothing held: nothing now - the port is looked at again
 *             (Retry) - until XHCI_HUB_RESUME_TRIES have failed in a row,
 *             when the port is enumerated afresh (GaveUp, Connect).
 *
 * A held device is never let go on any outcome but DONE: the teardown the
 * others ask for frees it with its pipes still paused.
 */
VOID XhciHubResumeOutcome(ULONG state, ULONG outcome, ULONG held,
                          PULONG tries, PXHCI_HUB_PORT_DECISION d);

/* The C_PORT_ feature selector that clears one wPortChange bit (bit 0 to
 * 4), or 0 for any other bit. */
ULONG XhciHubClearSelector(ULONG changeBit);

/*
 * A port reset's progress from one GET_STATUS answer: XHCI_HUB_RESET_PENDING
 * until C_PORT_RESET is set with the reset bit clear - the hub's own word
 * that the reset ended, which the caller makes fresh by clearing any older
 * C_PORT_RESET before it starts the reset - then XHCI_HUB_RESET_ENABLED with
 * the port enabled and XHCI_HUB_RESET_FAILED without; FAILED at once when
 * the device left.
 */
#define XHCI_HUB_RESET_PENDING  0UL
#define XHCI_HUB_RESET_ENABLED  1UL
#define XHCI_HUB_RESET_FAILED   2UL

ULONG XhciHubResetProgress(ULONG status, ULONG change);

/* The speed class (XHCI_SPEED_LOW, _FULL, _HIGH) wPortStatus reports: the
 * low-speed bit, the high-speed bit, or neither for Full Speed (section
 * 10.2 step 5). */
ULONG XhciHubPortSpeedClass(ULONG status);

/* The enumeration machine's speed value (XHCI_ENUM_SPEED_*, the default
 * Protocol Speed IDs) for a speed class, 0 for none: the machine takes its
 * initial EP0 size from it (XhciEnumInitialMps0). SuperSpeed is a
 * SuperSpeed hub's port (xhci_sshub.h, 30-A.1). */
ULONG XhciHubEnumSpeed(ULONG speedClass);

/*
 * Whether a hub at topology tier `tier` (xhci_topo.h: 0 for a hub on a root
 * port) may be configured: its children sit at tier + 1, and a child past
 * XHCI_TOPO_MAX_TIER has no Route String. A hub that may not is addressed
 * and left unconfigured with its ports unpowered (section 10.3, "Depth").
 */
ULONG XhciHubTierServable(ULONG tier);

/* Whether the bus selects alternate setting 1, the multi-TT interface
 * (section 10.3 step 1): a High-Speed hub (`speedClass`) whose
 * bDeviceProtocol is 2 and whose configuration has that alternate. */
ULONG XhciHubWantsMultiTt(ULONG speedClass, ULONG deviceProtocol,
                          ULONG hasAlternate1);

/*
 * The instance key a device PDO is named by: the root port alone for a
 * device on a root port, as before Phase 27, and the Route String above it
 * for one behind hubs (route << 8 | root port), so two devices under the
 * root hub never share one and a device that comes back to the same place
 * is given the same one.
 */
ULONG XhciHubInstanceKey(ULONG rootPort, ULONG route);

/* Milliseconds to wait after powering a hub's ports before reading any
 * (section 10.2 step 1: bPwrOn2PwrGood x 2), bounded below by 20 - the root
 * port's own (xHCI 5.4.8) - and above by 1000. */
ULONG XhciHubPowerWaitMs(ULONG powerGoodMs);

/*
 * Attempts at a device on a hub's port - reset, address, device descriptor -
 * before the bus gives the port up with CLEAR_FEATURE(PORT_ENABLE) and
 * leaves it disabled until its next connect change (section 10.2 step 7).
 * A root port keeps the enumeration machine's own XHCI_ENUM_RETRIES.
 */
#define XHCI_HUB_PORT_ATTEMPTS      3UL

/*
 * CLEAR_TT_BUFFER (section 10.1's table; USB 2.0 11.24.2.3, to transcribe):
 * bmRequestType 0x23, bRequest 8, wLength 0. wValue packs the endpoint the
 * TT buffer held - endpoint number in bits 3:0, the device's USB address in
 * 10:4, the endpoint type in 12:11 (the bmAttributes encoding, 0 control and
 * 2 bulk, as XHCI_PIPE_XFER_*), the direction in bit 15 (1 IN) - and wIndex
 * names the TT: the port the device's subtree hangs from on a multi-TT hub,
 * 1 on a single-TT one. Only a control or bulk endpoint's TT buffer is
 * cleared: xHCI 4.6.8 p.116 ("If the device was behind a TT and it is a
 * Control or Bulk endpoint") and p.102 (an Address Device transaction
 * error, on the default control endpoint at address 0).
 */
#define XHCI_HUB_REQ_CLEAR_TT_BUFFER 0x08U
#define XHCI_HUB_TT_EP_CONTROL      0UL
#define XHCI_HUB_TT_EP_BULK         2UL

/* Returns 1 with *value for a control or bulk endpoint, 0 for any other
 * type (no TT buffer is cleared for it) or a field out of range. */
ULONG XhciHubClearTtValue(ULONG address, ULONG endpoint, ULONG type,
                          ULONG in, PULONG value);

/* The CLEAR_TT_BUFFER wIndex: `ttPort` on a multi-TT hub, else 1. */
ULONG XhciHubClearTtPort(ULONG multiTt, ULONG ttPort);

/*
 * The order a departing subtree's hubs are released in (section 10.5 step
 * 4): every hub after each hub below it, the departing one last, so no slot
 * that is still enabled names a disabled one as its parent or TT hub.
 * parent[i] is hub i's parent - the index of the hub whose port it sits on,
 * XHCI_HUB_NO_PARENT on a root port, XHCI_HUB_DETACHED for an object not in
 * the tree (unused, or already departing) - for `count` hubs. Writes the
 * indices of `top` and of every hub below it into order[] (count entries at
 * most), deepest first, and returns how many. A chain that does not end
 * within `count` steps (a loop) leaves its hubs out; a `top` out of range or
 * detached gives 0.
 */
#define XHCI_HUB_NO_PARENT          0xFFFFFFFFUL
#define XHCI_HUB_DETACHED           0xFFFFFFFEUL

ULONG XhciHubReleaseOrder(const ULONG *parent, ULONG count, ULONG top,
                          PULONG order);

#endif /* XHCI_HUB_H */
