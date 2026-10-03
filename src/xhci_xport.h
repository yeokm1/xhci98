/*
 * xhci_xport.h - the mass-storage transport policy of xhci98.sys, the pure
 * half (roadmap-hcd.md task 31-A.3; design record 13 section 10.7).
 *
 * A storage interface that offers both Bulk-Only (protocol 0x50) and UAS
 * (protocol 0x62) in its alternate settings gets ONE transport, chosen by the
 * bus, and its PDO exposes only that transport's class ids. Id order decides
 * nothing on the NT targets: their setup engines rank a signed inbox
 * usbstor.inf above an unsigned match, so a Prot_50 id beside the UAS
 * driver's Prot_62 would hand the device to usbstor.sys. The UAS driver
 * (xhciuas.inf) binds only USB\Class_08&SubClass_06&Prot_62.
 *
 * What the targets' usbstor.inf files match on (read 2026-10-04, static, a
 * text read of the INFs design record 13 section 10.6 hashes, and NUSB 3.3's
 * and 3.6's): the full class triple only - Class_08 with SubClass 02, 05 or
 * 06 (and 04, 08 on some) and Prot 50 (NUSB also 00, 01; NUSB 3.6 and Vista/7
 * also SubClass_08&Prot_52) - never USB\Class_08&SubClass_06 or USB\Class_08
 * alone, never Prot_62; and by hand, USB\VID_v&PID_p (16 lines on 2000 SP4,
 * 61 on XP to 7, 135 and 152 under NUSB) and USB\VID_v&PID_p&MI_nn (6 to 7
 * lines, not on 2000), never a &REV_ form and never a vendor id alone. Under
 * UAS the VID/PID hardware ids stay, so a device usbstor.inf lists by hand
 * still binds usbstor.sys - the residual case roadmap 31-A.3 records rather
 * than fights. A REFUSED interface (no transport it can run) shows no
 * VID/PID-derived hardware id at all: its one hardware id is
 * USB\XHCI98_NOXPORT&VID_v&PID_p&REV_r[&MI_nn], a project-owned form no
 * INF names, so neither usbstor.inf nor a vendor INF can bind a storage
 * driver to it, and the device shows in Device Manager with no driver.
 *
 * The decision:
 *   - no UAS alternate on the interface: XHCI_XPORT_NONE, the ids as
 *     before (section 10.7's, alternate 0's triple);
 *   - UAS is usable at the speed the device enumerated at: at SuperSpeed only
 *     when the controller streams (HCCPARAMS1.MaxPSASize > 0), below it
 *     always (streamless UAS);
 *   - Bulk-Only is usable only at alternate 0, because usbstor.sys selects
 *     alternate 0 and never looks further; a Bulk-Only setting elsewhere is
 *     one it cannot reach;
 *   - Bulk-Only when it is usable and the force value is set or UAS is not
 *     usable; else UAS when it is usable (a UAS-only device stays UAS under
 *     the force value, counted); else no transport - the UAS-only device at
 *     SuperSpeed on a controller that does not stream: no storage id at all,
 *     and the caller's to send back to USB 2.0 (29-A.5) where it can.
 *
 * The chosen transport decides the alternate the class driver selects: the
 * UAS driver finds the Prot_62 setting and selects it; usbstor.sys selects
 * alternate 0, which is why only an alternate-0 Bulk-Only setting counts.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_XPORT_H
#define XHCI_XPORT_H

#include "xhci_compat.h"

#define XHCI_XPORT_OK           0UL
#define XHCI_XPORT_BAD_PARAM    1UL
#define XHCI_XPORT_MALFORMED    2UL
#define XHCI_XPORT_TOO_SMALL    3UL /* the id did not fit; *used = needed */
#define XHCI_XPORT_NO_IDS       4UL /* this query has no answer: refused   */

/* The transport. */
#define XHCI_XPORT_NONE         0UL /* not a dual or UAS interface         */
#define XHCI_XPORT_BOT          1UL
#define XHCI_XPORT_UAS          2UL
#define XHCI_XPORT_REFUSED      3UL /* no transport the device can run     */

/* Why: one per branch, so each is counted (31-A.3). */
#define XHCI_XPORT_WHY_NOT_UAS          0UL /* no UAS alternate           */
#define XHCI_XPORT_WHY_UAS              1UL /* both offered, UAS chosen   */
#define XHCI_XPORT_WHY_UAS_ONLY         2UL /* UAS alone, UAS             */
#define XHCI_XPORT_WHY_UAS_ONLY_FORCED  3UL /* UAS alone, value ignored   */
#define XHCI_XPORT_WHY_BOT_FORCED       4UL /* both, the value chose BOT  */
#define XHCI_XPORT_WHY_BOT_NO_STREAMS   5UL /* both, SS without streams   */
#define XHCI_XPORT_WHY_NO_STREAMS       6UL /* UAS alone, SS, no streams  */
#define XHCI_XPORT_WHY_COUNT            7UL

/* Input flags. */
#define XHCI_XPORT_F_SUPERSPEED 0x1UL /* enumerated at SuperSpeed or above  */
#define XHCI_XPORT_F_HC_STREAMS 0x2UL /* the controller streams            */
#define XHCI_XPORT_F_FORCE_BOT  0x4UL /* the force-Bulk-Only value is set  */

/* The Mass Storage Class Specification Overview's subclass and protocol
 * codes; 31-0 transcribes UAS's. */
#define XHCI_XPORT_CLASS_STORAGE 0x08UL
#define XHCI_XPORT_SUBCLASS_SCSI 0x06UL
#define XHCI_XPORT_PROT_BOT      0x50UL
#define XHCI_XPORT_PROT_UAS      0x62UL

/* No MI_ suffix: the ids of a device PDO rather than a function's. */
#define XHCI_XPORT_NO_MI        0xFFFFFFFFUL

typedef struct _XHCI_XPORT {
    ULONG Transport;        /* XHCI_XPORT_NONE .. _REFUSED                */
    ULONG Why;              /* XHCI_XPORT_WHY_*                           */
    ULONG Class;            /* the triple the compatible ids carry: the   */
    ULONG SubClass;         /* chosen setting's, or alternate 0's when    */
    ULONG Protocol;         /* Transport is NONE                          */
    ULONG Alternate;        /* the setting the class driver will select   */
    ULONG ShortHardwareId;  /* 1: USB\VID_v&PID_p[&MI_nn] is exposed      */
} XHCI_XPORT, *PXHCI_XPORT;

/*
 * The one interface a device PDO's class ids come from (design record 13
 * section 10.7): 1 and *iface its bInterfaceNumber when `config` (`length`
 * bytes) has exactly one alternate-0 interface descriptor; 0 otherwise or
 * for a header that is not a sound configuration descriptor.
 */
ULONG XhciXportSingleInterface(const UCHAR *config, ULONG length,
                               PULONG iface);

/*
 * The transport for interface `iface` of `config` (`length` bytes: the
 * device's configuration, or a function's filtered one) under `flags`.
 * XHCI_XPORT_MALFORMED for a header that is not a configuration descriptor,
 * a wTotalLength below 9 or above `length`, a descriptor whose bLength is
 * below 2 or runs past wTotalLength, or an interface descriptor below 9
 * bytes; XHCI_XPORT_BAD_PARAM for a NULL pointer. An interface with no
 * alternate-0 descriptor is XHCI_XPORT_NONE with a zero triple.
 */
ULONG XhciXportChoose(const UCHAR *config, ULONG length, ULONG iface,
                      ULONG flags, PXHCI_XPORT out);

#define XHCI_XPORT_ID_HARDWARE   1UL
#define XHCI_XPORT_ID_COMPATIBLE 2UL

/*
 * A hardware- or compatible-id answer under `x` for the device with the
 * 18-byte descriptor `device`; `mi` is the function's first interface, or
 * XHCI_XPORT_NO_MI for a device PDO. ASCII multi-string, upper-case hex,
 * section 10.7's forms: hardware USB\VID_v&PID_p&REV_r[&MI_nn] and, when
 * x->ShortHardwareId, USB\VID_v&PID_p[&MI_nn]; compatible
 * USB\Class_cc&SubClass_ss&Prot_pp, USB\Class_cc&SubClass_ss, USB\Class_cc.
 * A refused interface: the one hardware id
 * XHCI_XPORT_REFUSED_PREFIX "VID_v&PID_p&REV_r[&MI_nn]", and
 * XHCI_XPORT_NO_IDS (*used 0) for its compatible ids - the PDO answers that
 * query with none. *used is the whole answer's length even when it did not
 * fit.
 */
#define XHCI_XPORT_REFUSED_PREFIX "USB\\XHCI98_NOXPORT&"

ULONG XhciXportId(const UCHAR *device, const XHCI_XPORT *x, ULONG mi,
                  ULONG which, char *out, ULONG capacity, PULONG used);

/*
 * Where a refused device sits, which decides what 31-A.3 does with it:
 *   - XHCI_XPORT_AT_ROOT_COMPANION: a root port (Route String 0) paired with
 *     a USB 2.0 companion - 29-A.5's hold sends it back to USB 2.0, where it
 *     runs streamless UAS;
 *   - XHCI_XPORT_AT_ROOT_ALONE: a root port with no companion - refused in
 *     place, nowhere to send it;
 *   - XHCI_XPORT_AT_BEHIND_HUB: behind a SuperSpeed hub (Route String
 *     nonzero) - refused in place: 29-A.5's mechanism is the controller's own
 *     PORTSC, and a USB 3 hub's two halves are unpaired (30-A.1).
 * `route` is the device's Route String; `companion` the root port's paired
 * port number from the port map, 0 for none.
 */
#define XHCI_XPORT_AT_ROOT_COMPANION 0UL
#define XHCI_XPORT_AT_ROOT_ALONE     1UL
#define XHCI_XPORT_AT_BEHIND_HUB     2UL
#define XHCI_XPORT_AT_COUNT          3UL

ULONG XhciXportRefusedAt(ULONG route, ULONG companion);

#endif /* XHCI_XPORT_H */
