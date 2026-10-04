/*
 * hcd_pdo.c - the device PDOs of xhci98.sys: one per device the bus has
 * enumerated, every one a child of the root hub (roadmap-hcd.md task
 * 26-A.4; design record 13 sections 5.2, 6.5 and 10.7).
 *
 * Created by the controller thread when a port's machine reaches Present
 * (hcd_enum.c), listed on the controller under PdoListLock - the one lock of
 * design record 13 section 5.4's layer 2, held only to link, unlink or copy
 * the list - and reported through the root hub's BusRelations. When the
 * device leaves, its PDO is unlinked and the root hub's relations
 * invalidated; the PnP manager then removes it and the remove deletes it. A
 * PDO the PnP manager removes while it is still listed (a disable of the
 * device) stays, as the WDM bus rule requires.
 *
 * The ids are section 10.7's: USB\VID_vvvv&PID_pppp with &REV_rrrr ahead of
 * it, and the USB\Class_ compatible ids from the device descriptor or, when
 * bDeviceClass is 0 and there is one interface, from it. The instance id is
 * the device's serial number string when it has a usable one (task 33.2;
 * HcdDeviceReadSerial, XhciFuncSerialId), with UniqueID TRUE, as usbhub
 * answers - so a device moved to another port keeps its devnode - and
 * otherwise the device's place: the root port, with the Route String above
 * it for a device behind hubs (XhciHubInstanceKey). A function adds its
 * MI_nn to either (XhciFuncInstanceId). A serial a listed PDO of the same
 * VID and PID already carries is not used again (hcdSerialTakenLocked).
 *
 * Hubs (task 33.4; design record 13 section 10.11). The bus serves every
 * hub itself (hcd_hub.c), and since 33.4 also presents each as a devnode: a
 * PDO here with Hub set and the project-owned ids XHCI98\HUB or
 * XHCI98\HUB30, bound to this driver as a hub FDO (hcd_hubfdo.c). Every PDO
 * names the parent it is presented under (ParentSerial: 0 the root hub,
 * else a hub PDO's Serial), and a relations answer carries only its own
 * parent's. A gone PDO under a hub PnP has let go of is missing whichever
 * parent answers (hcdAncestorGoneLocked), and a change is announced to the
 * parent's PDO once PnP has started it, else to the root hub
 * (hcdInvalidateFor).
 *
 * A storage interface offering UAS gets one transport, chosen at creation
 * (31-A.3; xhci_xport.h, hcdXportDecide), and its hardware and compatible
 * ids follow it: only that transport's class ids. A device with no
 * transport it can run shows the project-owned XHCI98_NOXPORT hardware id
 * and no compatible id, and on a companion-paired root port is offered to
 * 29-A.5's hold (hcdXportRefusal, HcdHoldRequestUsb2).
 *
 * A composite device the bus splits (26-A.7; sections 10.8 and 10.9) gets no
 * PDO of its own: one function PDO per function, each an HCD_DEVICE_PDO with
 * Function set, its ids carrying &MI_nn (xhci_func.c), all sharing the one
 * device record and created, listed and unlisted together (the Group).
 *
 * The URB contract is 26-A.5's, in hcd_urb.c: IRP_MJ_INTERNAL_DEVICE_CONTROL
 * reaches it, and until the transfer path lands every URB is refused, so a
 * class driver binds and its start fails.
 *
 * IRQL: PASSIVE_LEVEL, except HcdDevicePdoList (<= DISPATCH_LEVEL inside the
 * lock it takes).
 */

#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

static LONG hcdDevicePdoSerial;

/* ----------------------------------------------------------------------- */
/* Strings                                                                  */
/* ----------------------------------------------------------------------- */

static const WCHAR hcdHex[] = L"0123456789ABCDEF";

/* Append `s` to `out` at *n. */
static VOID hcdPut(PWCHAR out, PULONG n, const WCHAR *s)
{
    while (*s != 0) {
        out[(*n)++] = *s++;
    }
}

/* Append `digits` hex digits of `value`. */
static VOID hcdPutHex(PWCHAR out, PULONG n, ULONG value, ULONG digits)
{
    while (digits > 0) {
        digits--;
        out[(*n)++] = hcdHex[(value >> (digits * 4UL)) & 0xFUL];
    }
}

static VOID hcdPutDecimal(PWCHAR out, PULONG n, ULONG value)
{
    WCHAR digits[12];
    ULONG d;

    d = 0;
    do {
        digits[d++] = (WCHAR)(L'0' + (value % 10UL));
        value /= 10UL;
    } while (value != 0 && d < 11);
    while (d > 0) {
        out[(*n)++] = digits[--d];
    }
}

/* A handed-off copy of `count` WCHARs. */
static PWCHAR hcdHandOff(const WCHAR *s, ULONG count)
{
    PWCHAR out;
    ULONG i;

    out = (PWCHAR)HcdPoolAllocHandedOff(count * sizeof(WCHAR));
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        out[i] = s[i];
    }
    return out;
}

/* The class triple a device PDO's compatible ids carry: the device
 * descriptor's, or, when bDeviceClass is 0 and the configuration has
 * exactly one interface, that interface's (section 10.7). */
static VOID hcdClassTriple(PHCD_DEVICE_PDO pdo, PULONG cls, PULONG sub,
                           PULONG prot)
{
    ULONG found;
    ULONG count;
    ULONG at;
    ULONG len;

    *cls = pdo->DeviceDesc[4];
    *sub = pdo->DeviceDesc[5];
    *prot = pdo->DeviceDesc[6];
    if (*cls != 0) {
        return;
    }
    found = 0;
    count = 0;
    at = 0;
    while (at + 2 <= pdo->ConfigLength) {
        len = pdo->Config[at];
        if (len < 2 || at + len > pdo->ConfigLength) {
            break;
        }
        if (pdo->Config[at + 1] == 4 && len >= 9 && pdo->Config[at + 3] == 0) {
            if (count++ == 0) {
                found = at;
            }
        }
        at += len;
    }
    /* A device the bus did not split keeps its own (zero) class when it
     * has several interfaces: the first one's id would bind a class driver
     * to the whole device, which Windows 2000's usbaudio.sys survives only
     * as a function (usbaudio-crash.md, STOP 0x1E). */
    if (count == 1) {
        *cls = pdo->Config[found + 5];
        *sub = pdo->Config[found + 6];
        *prot = pdo->Config[found + 7];
    }
}

/* A function PDO's ids (design record 13 section 10.7), xhci_func.c's
 * ASCII widened. */
static NTSTATUS hcdFunctionQueryId(PHCD_DEVICE_PDO pdo, PIRP irp,
                                   BUS_QUERY_ID_TYPE type)
{
    char text[128];
    WCHAR buf[128];
    PWCHAR out;
    ULONG which;
    ULONG used;
    ULONG i;

    switch (type) {
    case BusQueryDeviceID:
        which = XHCI_FUNC_ID_DEVICE;
        break;
    case BusQueryHardwareIDs:
        which = XHCI_FUNC_ID_HARDWARE;
        break;
    case BusQueryCompatibleIDs:
        which = XHCI_FUNC_ID_COMPATIBLE;
        break;
    default:
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);
    }
    used = 0;
    if (XhciFuncId(pdo->DeviceDesc, &pdo->Func, pdo->InstanceKey, which, text,
                   sizeof(text), &used) != XHCI_FUNC_OK) {
        return HcdCompleteIrp(irp, STATUS_UNSUCCESSFUL, 0);
    }
    for (i = 0; i < used; i++) {
        buf[i] = (WCHAR)(UCHAR)text[i];
    }
    out = hcdHandOff(buf, used);
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)out);
}

/* The hardware or compatible ids of a PDO whose storage transport the bus
 * chose (31-A.3; xhci_xport.c): only that transport's class ids, and no
 * VID/PID-only hardware id under UAS. A refused interface answers its
 * compatible-id query with none, as an id type the PDO does not handle. */
static NTSTATUS hcdXportQueryId(PHCD_DEVICE_PDO pdo, PIRP irp, ULONG which)
{
    char text[160];
    WCHAR buf[160];
    PWCHAR out;
    ULONG used;
    ULONG answer;
    ULONG i;

    used = 0;
    answer = XhciXportId(pdo->DeviceDesc, &pdo->Xport,
                         pdo->Function ? pdo->Func.FirstInterface
                                       : XHCI_XPORT_NO_MI,
                         which, text, sizeof(text), &used);
    if (answer == XHCI_XPORT_NO_IDS) {
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);
    }
    if (answer != XHCI_XPORT_OK) {
        return HcdCompleteIrp(irp, STATUS_UNSUCCESSFUL, 0);
    }
    for (i = 0; i < used; i++) {
        buf[i] = (WCHAR)(UCHAR)text[i];
    }
    out = hcdHandOff(buf, used);
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)out);
}

/* A hub PDO's device and hardware ids (task 33.4; design record 13 section
 * 10.11): XHCI98\HUB or XHCI98\HUB30, project-owned so no OS hub INF
 * matches, and no compatible id - the query keeps the status it arrived
 * with. Its instance id is any PDO's (hcdInstanceQueryId). */
static NTSTATUS hcdHubQueryId(PHCD_DEVICE_PDO pdo, PIRP irp,
                              BUS_QUERY_ID_TYPE type)
{
    char text[128];
    WCHAR buf[128];
    PWCHAR out;
    ULONG which;
    ULONG used;
    ULONG i;

    switch (type) {
    case BusQueryDeviceID:
        which = XHCI_HUBPDO_ID_DEVICE;
        break;
    case BusQueryHardwareIDs:
        which = XHCI_HUBPDO_ID_HARDWARE;
        break;
    default:
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);
    }
    used = 0;
    if (XhciHubPdoId(pdo->DeviceDesc, pdo->HubUsb3, pdo->InstanceKey, which,
                     text, sizeof(text), &used) != XHCI_HUBPDO_OK) {
        return HcdCompleteIrp(irp, STATUS_UNSUCCESSFUL, 0);
    }
    for (i = 0; i < used; i++) {
        buf[i] = (WCHAR)(UCHAR)text[i];
    }
    out = hcdHandOff(buf, used);
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)out);
}

/* Any PDO's instance id (task 33.2; xhci_func.c): its serial id, a
 * function's with &nn, or the location form - "303" for port 3's MI_03. */
static NTSTATUS hcdInstanceQueryId(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    char text[XHCI_SERIAL_ID_BYTES + 3];
    WCHAR buf[XHCI_SERIAL_ID_BYTES + 3];
    PWCHAR out;
    ULONG used;
    ULONG i;

    used = 0;
    if (XhciFuncInstanceId(pdo->SerialId, pdo->InstanceKey,
                           pdo->Function ? pdo->Func.FirstInterface
                                         : XHCI_INSTANCE_NO_MI,
                           text, sizeof(text), &used) != XHCI_FUNC_OK) {
        return HcdCompleteIrp(irp, STATUS_UNSUCCESSFUL, 0);
    }
    for (i = 0; i < used; i++) {
        buf[i] = (WCHAR)(UCHAR)text[i];
    }
    out = hcdHandOff(buf, used);
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)out);
}

static NTSTATUS hcdDeviceQueryId(PHCD_DEVICE_PDO pdo, PIRP irp,
                                 BUS_QUERY_ID_TYPE type)
{
    WCHAR buf[200];
    ULONG n;
    ULONG vid;
    ULONG pid;
    ULONG rev;
    ULONG cls;
    ULONG sub;
    ULONG prot;
    PWCHAR out;
    const WCHAR *prefix;

    if (type == BusQueryInstanceID) {
        return hcdInstanceQueryId(pdo, irp);
    }
    if (pdo->Hub) {
        return hcdHubQueryId(pdo, irp, type);
    }
    if (pdo->Xport.Transport != XHCI_XPORT_NONE) {
        if (type == BusQueryHardwareIDs) {
            return hcdXportQueryId(pdo, irp, XHCI_XPORT_ID_HARDWARE);
        }
        if (type == BusQueryCompatibleIDs) {
            return hcdXportQueryId(pdo, irp, XHCI_XPORT_ID_COMPATIBLE);
        }
    }
    if (pdo->Function) {
        return hcdFunctionQueryId(pdo, irp, type);
    }
    prefix = L"USB\\VID_";
    vid = (ULONG)pdo->DeviceDesc[8] | ((ULONG)pdo->DeviceDesc[9] << 8);
    pid = (ULONG)pdo->DeviceDesc[10] | ((ULONG)pdo->DeviceDesc[11] << 8);
    rev = (ULONG)pdo->DeviceDesc[12] | ((ULONG)pdo->DeviceDesc[13] << 8);
    n = 0;

    switch (type) {
    case BusQueryDeviceID:
        hcdPut(buf, &n, prefix);
        hcdPutHex(buf, &n, vid, 4);
        hcdPut(buf, &n, L"&PID_");
        hcdPutHex(buf, &n, pid, 4);
        buf[n++] = 0;
        break;

    case BusQueryHardwareIDs:
        hcdPut(buf, &n, prefix);
        hcdPutHex(buf, &n, vid, 4);
        hcdPut(buf, &n, L"&PID_");
        hcdPutHex(buf, &n, pid, 4);
        hcdPut(buf, &n, L"&REV_");
        hcdPutHex(buf, &n, rev, 4);
        buf[n++] = 0;
        hcdPut(buf, &n, prefix);
        hcdPutHex(buf, &n, vid, 4);
        hcdPut(buf, &n, L"&PID_");
        hcdPutHex(buf, &n, pid, 4);
        buf[n++] = 0;
        buf[n++] = 0;
        break;

    case BusQueryCompatibleIDs:
        hcdClassTriple(pdo, &cls, &sub, &prot);
        hcdPut(buf, &n, L"USB\\Class_");
        hcdPutHex(buf, &n, cls, 2);
        hcdPut(buf, &n, L"&SubClass_");
        hcdPutHex(buf, &n, sub, 2);
        hcdPut(buf, &n, L"&Prot_");
        hcdPutHex(buf, &n, prot, 2);
        buf[n++] = 0;
        hcdPut(buf, &n, L"USB\\Class_");
        hcdPutHex(buf, &n, cls, 2);
        hcdPut(buf, &n, L"&SubClass_");
        hcdPutHex(buf, &n, sub, 2);
        buf[n++] = 0;
        hcdPut(buf, &n, L"USB\\Class_");
        hcdPutHex(buf, &n, cls, 2);
        buf[n++] = 0;
        buf[n++] = 0;
        break;

    default:
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);
    }

    out = hcdHandOff(buf, n);
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)out);
}

/* ----------------------------------------------------------------------- */
/* Creation, listing and loss - the controller thread's side               */
/* ----------------------------------------------------------------------- */

/*
 * THE LIFECYCLE (Codex review of batch (b), round 1, findings 1-4 and 12).
 * Only the controller thread lists and unlists a PDO; BusRelations answers
 * mark it, under PdoListLock, in the same hold that copies the list. A PDO is
 *
 *   listed    - on DevicePdos while its device is present; every relations
 *               answer carries it and sets Reported;
 *   gone      - on GonePdos once its device left; the next relations answer
 *               omits it and sets MissingReported, and from then on its
 *               port waits for it no longer (HcdDevicePdoExists): the
 *               place re-enumerates a new device with a new PDO while
 *               this one waits for its REMOVE apart, as usbport's children
 *               do. Windows ME may never send that REMOVE - a device pulled
 *               while its install was under way was started (refused,
 *               below), stopped and left (2026-10-04, r3 t5) - and a port
 *               held until it came was dead for good;
 *   removed   - its own IRP_MN_REMOVE_DEVICE came once it was gone and
 *               either reported missing or never reported at all - or came
 *               first, and the relations answer that omits it follows
 *               (below): it leaves GonePdos for RemovedPdos, its port is
 *               told PDO_REMOVED, and it is deleted at the next
 *               BusRelations answer (HcdDevicePdoRelations), not at the
 *               REMOVE;
 *   deleted   - at that answer; by the thread at once when it leaves before
 *               PnP ever saw it; or by its parent's removal when PnP has
 *               already removed it;
 *   dormant   - listed with no device: the controller stopped while PnP
 *               had this PDO (and every sibling of its group) stopped, not
 *               removed or surprise-removed - Windows 98 SE's and ME's
 *               disable of the controller, which STOPs the whole tree and
 *               STARTs it again at the enable, expecting the same devnodes
 *               back (HcdDevicePdoDormantAll). Its device's re-enumeration
 *               at the same place with the same descriptors revives it
 *               (HcdDevicePdoCreate); its START waits for that, and if it
 *               does not come, or a different device comes, the PDO is
 *               reported gone. A new PDO beside a stopped one at the same
 *               instance id wedged ME's configuration manager (2026-10-04,
 *               28-V.1 clause 8: START refused on the stale PDOs, new PDOs
 *               never started, the shell hung);
 *   orphaned  - when its parent goes first while it still awaits a REMOVE
 *               (a surprise removal with handles open): it leaves both lists,
 *               forgets the controller, and is deleted after that REMOVE:
 *               at once on Windows 2000 and later, at the next relations
 *               answer of any of this driver's buses on Windows 98
 *               (HcdPdoRetire, below).
 *
 * Why not at the REMOVE: Windows 98 SE's configuration manager sends the
 * removed PDO more IRPs after its REMOVE has completed - a
 * QUERY_DEVICE_RELATIONS (BusRelations) and a QUERY_ID, in
 * the same removal pass (2026-10-04, a diagnostic build that kept the PDO:
 * REMOVE, then minor 7 type 0 and minor 0x13, on every unplug of a bound
 * usb-audio). Deleted at the REMOVE, those IRPs reached a freed device
 * object, and once its memory was reused - a device whose audio stack had
 * opened it, its devnode installed at an earlier boot - the guest took
 * fatal exception 0E at 0028:C002A3A7 right after the REMOVE. The next
 * relations answer comes from a later pass of the configuration manager,
 * which runs one at a time; Windows 2000 sends nothing after a REMOVE, so
 * the deferral only holds the object a little longer there. Until it is
 * deleted the PDO answers as before: QUERY_ID from its own descriptor
 * copies (Config is freed only by hcdDeletePdo), a BusRelations query
 * with the status it came with.
 *
 * A REMOVE while still listed (a disable of the device) keeps the PDO, the
 * WDM bus rule. If its device then leaves, or the REMOVE reached it gone
 * but not yet reported missing, the relations answer that omits it moves
 * it to RemovedPdos itself: Windows 98 SE sends such a PDO no second REMOVE
 * (2026-10-04: a usb-audio behind a hub, its New Hardware wizard cancelled
 * - REMOVE while listed - then the hub unplugged; the PDO stayed on
 * GonePdos for good, its port waited on it, and a hub plugged into the
 * same root port was never enumerated). Windows 2000 does send one, after
 * that answer and before the next; it finds DeletePending and does nothing
 * more. A deletion is fed back to the port's machine as PDO_REMOVED
 * (the port bit PortPdoRemoved), and a start as PDO_STARTED, so the machine
 * waits in Gone for the PDO it reported and reaches Bound (design record 13
 * section 5.3).
 */

/* A port's handshake bit, under the controller lock. IRQL: <= DISPATCH. */
static VOID hcdPortNotify(PHCD_CONTROLLER hc, PULONG bits, ULONG port)
{
    KIRQL oldIrql;

    if (port == 0 || port > HCD_PORT_COUNT) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    bits[(port - 1) / 32UL] |= 1UL << ((port - 1) % 32UL);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdThreadWake(hc);
}

/* The one IoDeleteDevice of a PDO. Deleted stays readable while a reference
 * keeps the object, so a later REMOVE completes and deletes nothing again,
 * and touches no controller (Codex review of batch (b), round 2, finding 1). */
static VOID hcdDeletePdo(PHCD_DEVICE_PDO pdo)
{
    /* Its refusal timer and DPC live in the extension (hcd_io.c), as do
     * the IRPs a departed device left held. */
    pdo->Closing = 1;
    (VOID)HcdIoParkedRelease(pdo, 0);
    HcdIoRefusedDrain(pdo);
    HcdUrbIdleDrain(pdo);
    /* Its interface context forgets it before the extension goes, and no
     * interface call is inside it then (Codex review of 28-A.1, round 1,
     * finding 3). */
    HcdUrbBusifRelease(pdo);
    pdo->Deleted = 1;
    pdo->Controller = NULL;
    HcdPoolFree(pdo->Config);
    pdo->Config = NULL;
    IoDeleteDevice(pdo->Common.Self);
}

/*
 * The storage transport for a new PDO (roadmap-hcd.md 31-A.3; xhci_xport.h),
 * decided once from its own configuration copy: a device PDO's one interface
 * when the device's class is its interfaces' (section 10.7), or a function's
 * one interface when no IAD groups it. Anything else keeps section 10.7's ids
 * (XHCI_XPORT_NONE). Each branch but NOT_UAS is counted and traced. Thread
 * only, PASSIVE_LEVEL.
 */
static VOID hcdXportDecide(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo,
                           ULONG flags)
{
    ULONG iface;

    pdo->Xport.Transport = XHCI_XPORT_NONE;
    pdo->Xport.Why = XHCI_XPORT_WHY_NOT_UAS;
    if (pdo->Function) {
        if (pdo->Func.InterfaceCount != 1 || pdo->Func.IadOffset != 0) {
            return;
        }
        iface = pdo->Func.FirstInterface;
    } else if (pdo->DeviceDesc[4] != 0 ||
               !XhciXportSingleInterface(pdo->Config, pdo->ConfigLength,
                                         &iface)) {
        return;
    }
    if (pdo->SpeedClass >= XHCI_SPEED_SUPER) {
        flags |= XHCI_XPORT_F_SUPERSPEED;
    }
    if (XhciXportChoose(pdo->Config, pdo->ConfigLength, iface, flags,
                        &pdo->Xport) != XHCI_XPORT_OK) {
        pdo->Xport.Transport = XHCI_XPORT_NONE;
        pdo->Xport.Why = XHCI_XPORT_WHY_NOT_UAS;
        return;
    }
    if (pdo->Xport.Why == XHCI_XPORT_WHY_NOT_UAS) {
        return;
    }
    hc->XportDecisions[pdo->Xport.Why]++;
    XHCI_DBG_VALUE("hcd: storage transport, port/transport/why/alternate",
                   (pdo->Port << 16) | (pdo->Xport.Transport << 12) |
                       (pdo->Xport.Why << 8) | (pdo->Xport.Alternate & 0xFFUL));
}

/*
 * A device one of whose PDOs has no transport (31-A.3: UAS-only at
 * SuperSpeed on a controller that does not stream), once per device, before
 * any PDO is listed. Where it sits decides (XhciXportRefusedAt): on a
 * companion-paired root port 29-A.5's hold is asked to send it back to USB
 * 2.0 (HcdHoldRequestUsb2); on a root port with no companion, behind a
 * SuperSpeed hub, or when the hold is not taken, it is refused in place -
 * its PDOs list with the XHCI98_NOXPORT hardware id and no compatible ids,
 * its hub port and siblings untouched. Each place is counted. Returns
 * nonzero when the hold request was accepted - queued for the thread's next
 * pass, which reads the device's identity on the SuperSpeed port, writes PED
 * and feeds the port a disconnect. Thread only, at DISPATCH_LEVEL under
 * hcdSerialLock (HcdDevicePdoCreate); it takes the controller lock inside
 * HcdHoldRequestUsb2 and nothing that waits.
 */
static ULONG hcdXportRefusal(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                             PHCD_DEVICE_PDO first)
{
    PHCD_DEVICE_PDO pdo;
    ULONG companion;
    ULONG at;

    for (pdo = first; pdo != NULL; pdo = pdo->Sibling) {
        if (pdo->Xport.Transport == XHCI_XPORT_REFUSED) {
            break;
        }
    }
    if (pdo == NULL) {
        return 0;
    }
    companion = 0;
    if (dev->Route == 0 && dev->Port >= 1 &&
        dev->Port <= XHCI_MAX_ROOT_PORTS) {
        companion = (ULONG)hc->Hc.PortMap.Companion[dev->Port - 1];
    }
    at = XhciXportRefusedAt(dev->Route, companion);
    hc->XportRefusedAt[at]++;
    XHCI_DBG_VALUE("hcd: UAS-only at SuperSpeed without streams, port/where",
                   (dev->Port << 16) | at);
    if (at != XHCI_XPORT_AT_ROOT_COMPANION) {
        return 0;
    }
    if (dev->HoldRefused ||
        hc->Ports[dev->Location - 1].HoldRecoverFails != 0) {
        /* Asked once and refused late (hcd_enum.c, hcdHoldResolve), or a
         * device on a port whose refused send-back's PDOs could not be
         * created: refused in place, not asked again. */
        hc->XportHoldsNotTaken++;
        return 0;
    }
    if (HcdHoldRequestUsb2(hc, dev, HCD_HOLD_REASON_UAS_NO_STREAMS)) {
        return 1;
    }
    hc->XportHoldsNotTaken++;
    return 0;
}

/*
 * The hub PDO a new PDO of `dev` is presented under (task 33.4; design
 * record 13 section 10.11): the nearest hub above the device's port that has
 * a PDO, by that PDO's Serial, or 0 for the root hub (XhciHubPresentedParent
 * over the live hub objects). Thread only: the hub objects and dev->Pdo are
 * its own. PASSIVE_LEVEL.
 */
static ULONG hcdPresentedParent(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    ULONG parent[HCD_MAX_HUBS];
    ULONG serial[HCD_MAX_HUBS];
    PHCD_HUB hub;
    PHCD_PORT u;
    PHCD_PORT q;
    ULONG i;

    for (i = 0; i < HCD_MAX_HUBS; i++) {
        hub = &hc->Hubs[i];
        u = hub->Upstream;
        parent[i] = XHCI_HUB_DETACHED;
        serial[i] = 0;
        if (!hub->Used || hub->Draining || hub->Device == NULL || u == NULL) {
            continue;
        }
        parent[i] = (u->Hub == NULL) ? XHCI_HUB_NO_PARENT : u->Hub->Index;
        if (hub->Device->Pdo != NULL) {
            serial[i] =
                ((PHCD_DEVICE_PDO)hub->Device->Pdo->DeviceExtension)->Serial;
        }
    }
    if (dev->Location == 0 || dev->Location > HCD_PORT_COUNT) {
        return 0;
    }
    q = &hc->Ports[dev->Location - 1];
    return XhciHubPresentedParent(parent, serial, HCD_MAX_HUBS,
                                  (q->Hub == NULL) ? XHCI_HUB_NO_PARENT
                                                   : q->Hub->Index);
}

/* One PDO, not yet listed: the device's (func NULL) with the whole
 * configuration, or a function's with its filtered one. The descriptors are
 * copied into the PDO, which outlives the device record. IoCreateDevice
 * zeroes the extension. */
static NTSTATUS hcdPdoNew(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                          const XHCI_FUNC *func, ULONG xportFlags,
                          PHCD_DEVICE_PDO *made)
{
    WCHAR nameBuffer[40];
    UNICODE_STRING name;
    PDEVICE_OBJECT obj;
    PHCD_DEVICE_PDO pdo;
    NTSTATUS status;
    ULONG serial;
    ULONG length;
    ULONG n;
    ULONG i;

    *made = NULL;
    length = dev->ConfigLength;
    if (func != NULL &&
        XhciFuncConfig(dev->Config, dev->ConfigLength, func, NULL, 0,
                       &length) != XHCI_FUNC_OK) {
        return STATUS_UNSUCCESSFUL;
    }

    serial = (ULONG)InterlockedIncrement(&hcdDevicePdoSerial);
    if (serial == 0) {
        serial = (ULONG)InterlockedIncrement(&hcdDevicePdoSerial);
    }
    n = 0;
    hcdPut(nameBuffer, &n, L"\\Device\\XHCI98DEV");
    hcdPutDecimal(nameBuffer, &n, serial);
    nameBuffer[n] = 0;
    RtlInitUnicodeString(&name, nameBuffer);
    status = IoCreateDevice(HcdDriverObject, sizeof(HCD_DEVICE_PDO), &name,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &obj);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    pdo = (PHCD_DEVICE_PDO)obj->DeviceExtension;
    pdo->Config = (PUCHAR)HcdPoolAlloc(length);
    if (pdo->Config == NULL) {
        /* Without the configuration the compatible ids would be wrong for
         * a device whose class is its interfaces' (round 1, finding 13). */
        IoDeleteDevice(obj);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (func == NULL) {
        for (i = 0; i < length; i++) {
            pdo->Config[i] = dev->Config[i];
        }
    } else {
        (VOID)XhciFuncConfig(dev->Config, dev->ConfigLength, func,
                             pdo->Config, length, &length);
        pdo->Function = 1;
        pdo->Func = *func;
        pdo->InterfaceMask = func->InterfaceMask;
    }
    pdo->ConfigLength = length;
    pdo->Common.Kind = HCD_KIND_DEVICE_PDO;
    pdo->Common.Self = obj;
    HcdIoRefusedInit(pdo);
    pdo->Common.PnpState = HCD_PNP_ADDED;
    pdo->Common.DevicePower = PowerDeviceD0;
    pdo->Common.SystemPower = PowerSystemWorking;
    pdo->Controller = hc;
    pdo->Device = dev;
    pdo->Serial = serial;
    pdo->Port = dev->Location;
    pdo->InstanceKey = XhciHubInstanceKey(dev->Port, dev->Route);
    pdo->RootPort = dev->Port;
    pdo->Route = dev->Route;
    pdo->Speed = dev->Speed;
    pdo->SpeedClass = XHCI_SPEED_UNKNOWN;
    (VOID)XhciPortSpeedClass(&hc->Hc.PortMap, dev->Port, dev->Speed,
                             &pdo->SpeedClass);
    for (i = 0; i < sizeof(pdo->DeviceDesc); i++) {
        pdo->DeviceDesc[i] = dev->DeviceDesc[i];
    }
    pdo->SerialId[0] = 0;
    pdo->ReadSerialId[0] = 0;
    pdo->SerialUnread = (dev->SerialState == HCD_SERIAL_FAILED) ? 1 : 0;
    if (dev->SerialState == HCD_SERIAL_OK) {
        for (i = 0; i < sizeof(pdo->SerialId); i++) {
            pdo->SerialId[i] = dev->SerialId[i];
            pdo->ReadSerialId[i] = dev->SerialId[i];
        }
    }
    pdo->Group = serial;
    pdo->ParentSerial = hcdPresentedParent(hc, dev);
    if (dev->Hub != NULL) {
        /* What the hub FDO's door answers, copied now so it never reads
         * the thread's hub object (task 33.4). A hub refused as too deep
         * was never described: no ports. */
        pdo->Hub = 1;
        pdo->HubUsb3 = dev->Hub->SpeedClass == XHCI_SPEED_SUPER;
        pdo->HubIndex = dev->Hub->Index;
        pdo->HubDesc = dev->Hub->Desc;
        pdo->HubBusPowered =
            (length >= 9 && (pdo->Config[7] & 0x40) == 0) ? 1UL : 0UL;
        pdo->HubMttCapable = pdo->DeviceDesc[6] == XHCI_HUB_PROTOCOL_MULTI_TT;
        pdo->HubMttOn = dev->Hub->Alternate == 1;
    }
    hcdXportDecide(hc, pdo, xportFlags);

    obj->Flags |= DO_POWER_PAGABLE;
    obj->Flags &= ~DO_DEVICE_INITIALIZING;
    *made = pdo;
    return STATUS_SUCCESS;
}

/* ----------------------------------------------------------------------- */
/* Parents: the root hub, or a hub's own PDO (task 33.4)                    */
/* ----------------------------------------------------------------------- */

/* The hub PDO named `serial`, on any of the three lists, or NULL once it is
 * deleted. PdoListLock held. */
static PHCD_DEVICE_PDO hcdHubFindLocked(PHCD_CONTROLLER hc, ULONG serial)
{
    PHCD_DEVICE_PDO pdo;

    for (pdo = hc->DevicePdos; pdo != NULL; pdo = pdo->Next) {
        if (pdo->Hub && pdo->Serial == serial) {
            return pdo;
        }
    }
    for (pdo = hc->GonePdos; pdo != NULL; pdo = pdo->Next) {
        if (pdo->Hub && pdo->Serial == serial) {
            return pdo;
        }
    }
    for (pdo = hc->RemovedPdos; pdo != NULL; pdo = pdo->Next) {
        if (pdo->Hub && pdo->Serial == serial) {
            return pdo;
        }
    }
    return NULL;
}

/*
 * Whether some hub PDO above `pdo` has left PnP's view, taking `pdo` with
 * it (design record 13 section 10.11, the ancestor rule): reported missing,
 * removed by PnP (which removes a devnode's children first, so each child
 * has had its own REMOVE), on its way to deletion, or deleted already. A
 * gone PDO under such a parent is missing whichever relations answer looks
 * at it, since its parent's FDO answers nothing more. 0 for a child of the
 * root hub. PdoListLock held.
 */
static ULONG hcdAncestorGoneLocked(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo)
{
    PHCD_DEVICE_PDO up;
    ULONG serial;
    ULONG depth;

    serial = pdo->ParentSerial;
    for (depth = 0; serial != 0 && depth <= HCD_MAX_HUBS; depth++) {
        up = hcdHubFindLocked(hc, serial);
        if (up == NULL || up->MissingReported || up->DeletePending ||
            up->Deleted || up->Common.PnpState == HCD_PNP_REMOVED) {
            return 1;
        }
        serial = up->ParentSerial;
    }
    return 0;
}

/*
 * IoInvalidateDeviceRelations for the parent a change of PDOs is presented
 * under: the root-hub PDO for serial 0, otherwise that hub's PDO - only when
 * PnP has started it (a START and no STOP or REMOVE since), because a PDO
 * PnP has not yet made a devnode of may not be named (a fatal PnP error on
 * NT), and PnP asks a hub FDO for its relations after it starts anyway.
 * Otherwise the root hub's relations are asked for, whose answer runs the
 * ancestor rule over every gone PDO. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdInvalidateFor(PHCD_CONTROLLER hc, ULONG serial)
{
    PHCD_DEVICE_PDO hub;
    PDEVICE_OBJECT obj;
    KIRQL oldIrql;

    obj = NULL;
    if (serial != 0) {
        KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
        for (hub = hc->DevicePdos; hub != NULL; hub = hub->Next) {
            if (hub->Hub && hub->Serial == serial) {
                break;
            }
        }
        if (hub != NULL && hub->Listed && hub->Reported && !hub->Deleted &&
            hub->Common.PnpState == HCD_PNP_STARTED) {
            obj = hub->Common.Self;
            ObReferenceObject(obj);
        }
        KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    }
    if (obj != NULL) {
        IoInvalidateDeviceRelations(obj, BusRelations);
        ObDereferenceObject(obj);
        return;
    }
    if (hc->RootHubPdo != NULL) {
        IoInvalidateDeviceRelations(hc->RootHubPdo, BusRelations);
    }
}

/* ----------------------------------------------------------------------- */
/* Dormant PDOs: kept across a controller stop PnP stopped them for         */
/* ----------------------------------------------------------------------- */

static ULONG hcdBytesEqual(const UCHAR *a, const UCHAR *b, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* Whether PDO `old` stands for the same device as the freshly built `nu`,
 * its instance id aside (hcdDormantFindLocked weighs that): the device
 * descriptor, the (filtered) configuration, the function, the speed and
 * the transport decision. PdoListLock held. */
static ULONG hcdDormantSame(PHCD_DEVICE_PDO old, PHCD_DEVICE_PDO nu)
{
    return old->ParentSerial == nu->ParentSerial && old->Hub == nu->Hub &&
           old->HubUsb3 == nu->HubUsb3 &&
           old->Function == nu->Function &&
           old->InterfaceMask == nu->InterfaceMask &&
           old->ConfigLength == nu->ConfigLength &&
           old->Speed == nu->Speed &&
           hcdBytesEqual(old->DeviceDesc, nu->DeviceDesc,
                         sizeof(old->DeviceDesc)) &&
           hcdBytesEqual(old->Config, nu->Config, old->ConfigLength) &&
           hcdBytesEqual((const UCHAR *)&old->Xport, (const UCHAR *)&nu->Xport,
                         sizeof(old->Xport));
}

/* One device's PDO group kept dormant, or not (HcdDevicePdoDormantAll). IRQL:
 * PASSIVE_LEVEL. */
static VOID hcdDormantKeep(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO first;
    PHCD_DEVICE_PDO pdo;
    PHCD_DEVICE_PDO up;
    KIRQL oldIrql;
    ULONG keep;

    first = (PHCD_DEVICE_PDO)dev->Pdo->DeviceExtension;
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    keep = 1;
    for (pdo = first; pdo != NULL; pdo = pdo->Sibling) {
        if (!pdo->Listed || pdo->Surprised || pdo->UrbsPending != 0) {
            keep = 0;
        } else if (pdo->RemoveReceived) {
            /* Disabled in Device Manager and still present (the WDM rule
             * keeps it listed). Behind a hub it is kept with its group and
             * revived still disabled (task 33.4): dropped, it would wait as
             * gone for an answer only its dormant hub's FDO could give,
             * and that hub cannot be enumerated again while a port below
             * it waits - nor may the PDO be taken for absent, since PnP
             * still holds it present (Codex review of 33.4, round 2,
             * findings 1 and 2). Under the root hub the root's own answer
             * settles it, as before 33.4. */
            if (first->ParentSerial == 0) {
                keep = 0;
            }
        } else if (pdo->Common.PnpState != HCD_PNP_STOPPED) {
            keep = 0;
        }
    }
    /* Behind a hub, only under a hub PDO kept too (task 33.4), so a revived
     * hub is the parent its revived children name. */
    if (keep && first->ParentSerial != 0) {
        up = hcdHubFindLocked(hc, first->ParentSerial);
        if (up == NULL || !up->Listed || !up->Dormant) {
            keep = 0;
        }
    }
    if (keep) {
        for (pdo = first; pdo != NULL; pdo = pdo->Sibling) {
            pdo->Device = NULL;
            pdo->Dormant = 1;
        }
        dev->Pdo = NULL;
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    if (keep) {
        XHCI_DBG_VALUE("hcd: stopped PDOs kept dormant, port", dev->Port);
    }
}

/*
 * The controller stops (HcdEnumDrop, the thread stopped): every device
 * whose PDOs PnP has all stopped - not removed, not surprise-removed, still
 * listed - keeps them listed with no device, Dormant, and lets go of them
 * (dev->Pdo NULL), so the drop that follows reports nothing gone and leaves
 * the port Empty for the restart's rescan. Only on the controller's
 * orderly PnP STOP (StopPreserve). Windows 2000 onward remove the children
 * before a controller disable, so nothing goes dormant there, and a PnP
 * stop of the controller alone (a rebalance) leaves the children started,
 * which are dropped and reported gone as before. Matched by the instance
 * id the dormant PDOs answer, and descriptors (hcdDormantFindLocked): a
 * group named by its place by a device at that place, one named by its
 * serial id by a device with that id wherever it comes back; so two
 * identical units without a serial id swapped while disabled are taken for
 * each other. IRQL: PASSIVE_LEVEL.
 */
VOID HcdDevicePdoDormantAll(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;
    ULONG tier;
    ULONG i;

    /* Tier by tier from the root ports (task 33.4): every hub is decided
     * before the devices behind it. */
    for (tier = 0; tier <= XHCI_TOPO_MAX_TIER + 1UL; tier++) {
        for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
            dev = hc->SlotDevice[i];
            if (dev == NULL || dev->Pdo == NULL) {
                continue;
            }
            if (dev->Tier == tier ||
                (tier == XHCI_TOPO_MAX_TIER + 1UL && dev->Tier > tier)) {
                hcdDormantKeep(hc, dev);
            }
        }
    }
}

/* Every dormant PDO of the group named by its place `instanceKey` - a group
 * named by a serial id is not its place's (task 33.2) - or of `group` when
 * that is not 0, unlisted onto GonePdos, its siblings with it, for the
 * next relations answer to report missing. Returns how many. PdoListLock
 * held. */
static ULONG hcdDormantRetireLocked(PHCD_CONTROLLER hc, ULONG instanceKey,
                                   ULONG group)
{
    PHCD_DEVICE_PDO *at;
    PHCD_DEVICE_PDO pdo;
    ULONG n;

    n = 0;
    at = &hc->DevicePdos;
    while (*at != NULL) {
        pdo = *at;
        if (!pdo->Dormant ||
            (group != 0 ? pdo->Group != group
                        : (pdo->InstanceKey != instanceKey ||
                           pdo->SerialId[0] != 0))) {
            at = &pdo->Next;
            continue;
        }
        *at = pdo->Next;
        pdo->Dormant = 0;
        pdo->Listed = 0;
        pdo->Sibling = NULL;
        pdo->Next = hc->GonePdos;
        hc->GonePdos = pdo;
        n++;
    }
    /* A dormant group behind a hub whose own dormant PDO has just gone
     * goes with it (task 33.4): no device can revive it under a parent
     * PnP is removing. Repeated for each tier below. */
    for (;;) {
        at = &hc->DevicePdos;
        while (*at != NULL) {
            pdo = *at;
            if (pdo->Dormant && pdo->ParentSerial != 0) {
                PHCD_DEVICE_PDO up;

                for (up = hc->DevicePdos; up != NULL; up = up->Next) {
                    if (up->Hub && up->Serial == pdo->ParentSerial) {
                        break;
                    }
                }
                if (up == NULL) {
                    break;
                }
            }
            at = &pdo->Next;
        }
        if (*at == NULL) {
            break;
        }
        pdo = *at;
        *at = pdo->Next;
        pdo->Dormant = 0;
        pdo->Listed = 0;
        pdo->Sibling = NULL;
        pdo->Next = hc->GonePdos;
        hc->GonePdos = pdo;
        n++;
    }
    return n;
}

static ULONG hcdSameVidPid(PHCD_DEVICE_PDO a, PHCD_DEVICE_PDO b)
{
    return hcdBytesEqual(a->DeviceDesc + 8, b->DeviceDesc + 8, 4);
}

/* Whether a listed PDO of `hc` of `pdo`'s vendor and product id carries
 * its serial id, case aside (XhciFuncSerialSame) - a dormant one only when
 * `dormantToo`. One already unlisted - gone, its missing report or its
 * REMOVE pending - does not count, as usbhub's check counts only its
 * ports' present devices: a device moved from one port to another keeps
 * its id even when the move beats PnP's next relations query. `hc`'s
 * PdoListLock held. */
static ULONG hcdSerialTakenLocked(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo,
                                  ULONG dormantToo)
{
    PHCD_DEVICE_PDO other;

    for (other = hc->DevicePdos; other != NULL; other = other->Next) {
        if (!hcdSameVidPid(other, pdo) ||
            !XhciFuncSerialSame(other->SerialId, pdo->SerialId)) {
            continue;
        }
        /* A dormant PDO under another parent can never be revived by this
         * device (hcdDormantSame), and PnP holds it present (task 33.4). */
        if (dormantToo || !other->Dormant ||
            other->ParentSerial != pdo->ParentSerial) {
            return 1;
        }
    }
    /* Under another parent (task 33.4), a gone PDO PnP has not yet been
     * told is missing holds the id too: the parent that will report it
     * absent is not the one that reports the newcomer, so the two answers
     * could present one instance id twice (Codex review of 33.4, final
     * round, finding 1). Under the same parent one answer does both, as
     * the move between root ports above relies on. */
    for (other = hc->GonePdos; other != NULL; other = other->Next) {
        if (!other->MissingReported &&
            other->ParentSerial != pdo->ParentSerial &&
            hcdSameVidPid(other, pdo) &&
            XhciFuncSerialSame(other->SerialId, pdo->SerialId)) {
            return 1;
        }
    }
    return 0;
}

/*
 * The serial ids' reach is the machine, not one root hub: an instance id
 * with UniqueID TRUE names one devnode wherever its parent is. Every
 * controller of this driver is linked here, from AddDevice to its remove,
 * through SerialNext under hcdSerialLock. The order is hcdSerialLock, then
 * one PdoListLock at a time (another controller's in
 * hcdSerialTakenElsewhere, released before this controller's is taken in
 * hcdDormantRevive and at the listing), or the controller lock inside
 * hcdXportRefusal; hcdSerialLock is never taken under either.
 */
static KSPIN_LOCK hcdSerialLock;
static PHCD_CONTROLLER hcdSerialControllers;

/* IRQL: PASSIVE_LEVEL (DriverEntry). */
VOID HcdSerialInit(VOID)
{
    KeInitializeSpinLock(&hcdSerialLock);
    hcdSerialControllers = NULL;
}

/* IRQL: PASSIVE_LEVEL (AddDevice), its PdoListLock initialised. */
VOID HcdSerialControllerAdd(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hcdSerialLock, &oldIrql);
    hc->SerialNext = hcdSerialControllers;
    hcdSerialControllers = hc;
    KeReleaseSpinLock(&hcdSerialLock, oldIrql);
}

/* IRQL: PASSIVE_LEVEL (the controller's remove, before its deletion). */
VOID HcdSerialControllerRemove(PHCD_CONTROLLER hc)
{
    PHCD_CONTROLLER *at;
    KIRQL oldIrql;

    KeAcquireSpinLock(&hcdSerialLock, &oldIrql);
    for (at = &hcdSerialControllers; *at != NULL; at = &(*at)->SerialNext) {
        if (*at == hc) {
            *at = hc->SerialNext;
            break;
        }
    }
    hc->SerialNext = NULL;
    KeReleaseSpinLock(&hcdSerialLock, oldIrql);
}

/* Whether a PDO of another controller carries `pdo`'s serial id for its
 * vendor and product id while PnP still holds it as present: listed,
 * present or dormant, or gone and not yet omitted from a relations answer
 * (Codex review of 33.2, round 2, finding 1). Another root hub's answer is
 * not ordered with this one's, so a device moved between controllers
 * faster than that answer takes the location form for that plug rather
 * than reuse an instance id PnP may still see present. hcdSerialLock
 * held, no PdoListLock. */
static ULONG hcdSerialTakenElsewhere(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo)
{
    PHCD_CONTROLLER other;
    PHCD_DEVICE_PDO gone;
    KIRQL oldIrql;
    ULONG taken;

    taken = 0;
    for (other = hcdSerialControllers; other != NULL && !taken;
         other = other->SerialNext) {
        if (other == hc) {
            continue;
        }
        KeAcquireSpinLock(&other->PdoListLock, &oldIrql);
        taken = hcdSerialTakenLocked(other, pdo, 1);
        for (gone = other->GonePdos; gone != NULL && !taken;
             gone = gone->Next) {
            if (gone->Reported && !gone->MissingReported &&
                hcdSameVidPid(gone, pdo) &&
                XhciFuncSerialSame(gone->SerialId, pdo->SerialId)) {
                taken = 1;
            }
        }
        KeReleaseSpinLock(&other->PdoListLock, oldIrql);
    }
    return taken;
}

/* A device left on the location form because its serial id was taken:
 * counted and traced once the locks are let go. */
static VOID hcdSerialDuplicateCount(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                    ULONG duplicate)
{
    if (duplicate) {
        hc->SerialIdsDuplicate++;
        XHCI_DBG_VALUE("hcd: serial already in use, location id, port",
                       dev->Port);
    }
}

/* Every dormant group of `first`'s vendor and product id and serial id,
 * case aside, retired as hcdDormantRetireLocked retires one (its device
 * came back and did not revive it). PdoListLock held. */
static VOID hcdDormantRetireSerialLocked(PHCD_CONTROLLER hc,
                                         PHCD_DEVICE_PDO first)
{
    PHCD_DEVICE_PDO old;

    if (first->SerialId[0] == 0) {
        return;
    }
    do {
        for (old = hc->DevicePdos; old != NULL; old = old->Next) {
            if (old->Dormant && hcdSameVidPid(old, first) &&
                XhciFuncSerialSame(old->SerialId, first->SerialId)) {
                break;
            }
        }
        if (old != NULL) {
            (VOID)hcdDormantRetireLocked(hc, 0, old->Group);
        }
    } while (old != NULL);
}

/* The dormant group, by its first PDO, that the device built as `first`
 * revives: every PDO matching the dormant one in the same position, and
 * the group named by `first`'s place (`byPlace`: a group without a serial
 * id whose device read the same serial id as `first`, or none, so a unit
 * on the location form for a duplicate keeps its own and never takes a
 * different unit's - Codex review of 33.2, round 2, finding 2; when every
 * read of `first`'s failed its serial is unknown, not different, and the
 * place and descriptors decide - round 3; likewise when every read of the
 * group's failed and `first` answers the location form too - round 4;
 * XhciFuncReviveByPlace) or by
 * `first`'s serial id, exactly. NULL for none. PdoListLock held. */
static PHCD_DEVICE_PDO hcdDormantFindLocked(PHCD_CONTROLLER hc,
                                            PHCD_DEVICE_PDO first,
                                            ULONG byPlace)
{
    PHCD_DEVICE_PDO old;
    PHCD_DEVICE_PDO a;
    PHCD_DEVICE_PDO b;
    ULONG same;

    for (old = hc->DevicePdos; old != NULL; old = old->Next) {
        if (!old->Dormant || old->Group != old->Serial) {
            continue;
        }
        if (byPlace ? (old->InstanceKey != first->InstanceKey ||
                       !XhciFuncReviveByPlace(old->SerialId,
                                              old->ReadSerialId,
                                              old->SerialUnread,
                                              first->ReadSerialId,
                                              first->SerialUnread,
                                              first->SerialId[0] == 0))
                    : !XhciFuncReviveBySerial(old->SerialId,
                                              first->SerialId)) {
            continue;
        }
        same = 1;
        for (a = old, b = first; a != NULL || b != NULL;
             a = a->Sibling, b = b->Sibling) {
            if (a == NULL || b == NULL || !a->Dormant ||
                !hcdDormantSame(a, b)) {
                same = 0;
                break;
            }
        }
        if (same) {
            return old;
        }
    }
    return NULL;
}

/*
 * The instance id a device built as `first` (its PDOs not listed) will
 * answer, and the dormant group it revives, decided in one hold of
 * PdoListLock under hcdSerialLock (task 33.2). A dormant group named by
 * this place comes first - it is what PnP kept here, and a group a
 * duplicate or a failed read left on the location form keeps that form -
 * then one named by this device's serial id, wherever it was; the serial
 * id is not looked for when another controller carries it
 * (`takenElsewhere`). With no revival, a serial id another controller or
 * a present PDO of this one carries is cleared from every new PDO, which
 * then answer the location form (*duplicate set); a dormant group with it
 * is not a holder, and is retired at the listing. When a group is found,
 * it gets the device back - dev->Pdo names it, each PDO names the record,
 * its new port object and its place - and 1 is returned for the caller to
 * delete the new ones; the instance id each answers is unchanged. Thread
 * only, hcdSerialLock held.
 */
static ULONG hcdDormantRevive(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              PHCD_DEVICE_PDO first, ULONG takenElsewhere,
                              PULONG duplicate)
{
    PHCD_DEVICE_PDO old;
    PHCD_DEVICE_PDO a;
    PHCD_DEVICE_PDO b;
    KIRQL oldIrql;
    ULONG same;

    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    old = hcdDormantFindLocked(hc, first, 1);
    if (old == NULL && !takenElsewhere) {
        old = hcdDormantFindLocked(hc, first, 0);
    }
    if (old == NULL && first->SerialId[0] != 0 &&
        (takenElsewhere || hcdSerialTakenLocked(hc, first, 0))) {
        for (a = first; a != NULL; a = a->Sibling) {
            a->SerialId[0] = 0;
        }
        *duplicate = 1;
        /* Now on the location form: a group at this place whose own reads
         * failed answers the same id (Codex review of 33.2, round 4). */
        old = hcdDormantFindLocked(hc, first, 1);
    }
    same = (old != NULL);
    if (same) {
        for (a = old, b = first; a != NULL && b != NULL;
             a = a->Sibling, b = b->Sibling) {
            a->Device = dev;
            a->Port = dev->Location;
            a->RootPort = dev->Port;
            a->Route = dev->Route;
            a->InstanceKey = b->InstanceKey;
            /* A revived hub's object may sit at another index now, its
             * ports at other locations (task 33.4). */
            a->HubIndex = b->HubIndex;
            a->HubDesc = b->HubDesc;
            a->HubMttOn = b->HubMttOn;
            a->Dormant = 0;
        }
        dev->Pdo = old->Common.Self;
        dev->PdoGroup = old->Group;
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    return same;
}

/* REMOVE on a dormant PDO: its group reported gone. IRQL: PASSIVE_LEVEL. */
static VOID hcdDormantRemoved(PHCD_DEVICE_PDO pdo)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG retired;

    hc = pdo->Controller;
    if (hc == NULL) {
        return;
    }
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    retired = pdo->Dormant ? hcdDormantRetireLocked(hc, 0, pdo->Group) : 0;
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    if (retired != 0) {
        hcdInvalidateFor(hc, pdo->ParentSerial);
    }
}

/*
 * START on a dormant PDO (Windows 98 SE and ME start the tree again right
 * after the root hub, before the restarted controller has rescanned): waits
 * up to HCD_DORMANT_WAIT_MS for the device to come back to it. If it does
 * not - unplugged meanwhile, or another device there - the group is
 * reported gone and the START is refused as any departed PDO's is. IRQL:
 * PASSIVE_LEVEL.
 */
#define HCD_DORMANT_WAIT_MS 10000UL

static VOID hcdDormantWait(PHCD_DEVICE_PDO pdo)
{
    PHCD_CONTROLLER hc;
    LARGE_INTEGER due;
    KIRQL oldIrql;
    ULONG waited;
    ULONG retired;

    hc = pdo->Controller;
    if (hc == NULL) {
        return;
    }
    HcdThreadWake(hc);
    for (waited = 0; waited < HCD_DORMANT_WAIT_MS && pdo->Dormant &&
                     pdo->Controller != NULL;
         waited += 50) {
        HcdRelativeMs(&due, 50);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    retired = 0;
    if (pdo->Controller == NULL) {
        /* Orphaned meanwhile: the parent's release settled it. */
        return;
    }
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    if (pdo->Dormant) {
        retired = hcdDormantRetireLocked(hc, 0, pdo->Group);
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    if (retired != 0) {
        XHCI_DBG_VALUE("hcd: dormant PDO not revived, reported gone, port",
                       pdo->Port);
        hcdInvalidateFor(hc, pdo->ParentSerial);
    }
}

/*
 * The PDO(s) for an enumerated device: one device PDO, or - for a device the
 * bus splits (xhci_func.c, design record 13 section 10.8) - the device
 * configured by the bus and one function PDO per function, all created
 * before any is listed, listed in one hold of PdoListLock, and announced by
 * one relations invalidation, so PnP meets them together. On a failure
 * nothing is listed and the port's machine fails; a device the bus already
 * configured stays so until the slot is disabled. Thread only (the bus's
 * configure is a thread control transfer).
 */
NTSTATUS HcdDevicePdoCreate(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    XHCI_FUNC_SET set;
    HCD_TEXT_READ textRead;
    PHCD_DEVICE_PDO first;
    PHCD_DEVICE_PDO last;
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;
    KIRQL serialIrql;
    NTSTATUS status;
    ULONG indexes[XHCI_TEXT_PICKS];
    ULONG textFlags;
    ULONG picks;
    ULONG xportFlags;
    ULONG elsewhere;
    ULONG duplicate;
    ULONG count;
    ULONG i;

    if (hc->RootHubPdo == NULL || !hc->RootHubStarted ||
        dev->ConfigLength == 0 || dev->Config == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    /* The instance id's serial (33.2), before anything is configured; a
     * read that timed out leaves the device to the reset it requested. */
    if (!HcdDeviceReadSerial(hc, dev)) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (XhciFuncSplit(dev->DeviceDesc, dev->Config, dev->ConfigLength,
                      &set) != XHCI_FUNC_OK) {
        set.Count = 0;
    }
    if (set.Count != 0 && !HcdCfgParentConfigure(hc, dev)) {
        XHCI_DBG_VALUE("hcd: split device not configured, port", dev->Port);
        return STATUS_UNSUCCESSFUL;
    }
    /* The transport inputs that are the controller's (31-A.3): whether it
     * streams, and the force value, read here so a change takes effect at
     * the next enumeration. */
    xportFlags = 0;
    if (XhciStreamHcEntries(XhciBarReader(&hc->Hc, XHCI_CAP_HCCPARAMS1)) !=
        0) {
        xportFlags |= XHCI_XPORT_F_HC_STREAMS;
    }
    if (HcdCtlForceBulkOnly(hc)) {
        xportFlags |= XHCI_XPORT_F_FORCE_BOT;
    }
    count = (set.Count != 0) ? set.Count : 1;
    first = NULL;
    last = NULL;
    status = STATUS_SUCCESS;
    textRead.LangidRead = 0;
    textRead.Langid = 0;
    for (i = 0; i < 8; i++) {
        textRead.Failed[i] = 0;
    }
    /* Windows 98 and ME keep the description in the ANSI devnode, made
     * from this text by a conversion this project has not read; ASCII
     * passes any (design record 13 section 10.7). */
    textFlags = IoIsWdmVersionAvailable(1, 0x10) ? 0 : XHCI_TEXT_FOLD_ASCII;
    for (i = 0; i < count; i++) {
        status = hcdPdoNew(hc, dev, (set.Count != 0) ? &set.Func[i] : NULL,
                           xportFlags, &pdo);
        if (!NT_SUCCESS(status)) {
            goto cleanup;
        }
        if (first == NULL) {
            first = pdo;
        } else {
            pdo->Group = first->Group;
            last->Sibling = pdo;
        }
        last = pdo;
        /* Its name (33.6), read now, while the device is here: a revived
         * PDO keeps the one it had, and these go. */
        picks = XhciFuncTextIndexes(dev->DeviceDesc,
                                    (set.Count != 0) ? &set.Func[i] : NULL,
                                    dev->Config, dev->ConfigLength, indexes);
        if (!HcdDeviceReadText(hc, dev, indexes, picks, textFlags, &textRead,
                               pdo->Text)) {
            status = STATUS_DEVICE_NOT_READY;
            goto cleanup;
        }
    }
    /* One hold of hcdSerialLock from the identity's choice to the listing,
     * so no other controller lists the same serial id in between. */
    duplicate = 0;
    KeAcquireSpinLock(&hcdSerialLock, &serialIrql);
    elsewhere = first->SerialId[0] != 0 && hcdSerialTakenElsewhere(hc, first);
    if (hcdDormantRevive(hc, dev, first, elsewhere, &duplicate)) {
        KeReleaseSpinLock(&hcdSerialLock, serialIrql);
        hcdSerialDuplicateCount(hc, dev, duplicate);
        /* The device came back to the PDOs PnP kept across the controller
         * stop: they are its own again, and the new ones, never listed,
         * go. Nothing changes in the relations. */
        while (first != NULL) {
            pdo = first;
            first = first->Sibling;
            hcdDeletePdo(pdo);
        }
        XHCI_DBG_VALUE("hcd: stopped PDOs revived, port", dev->Port);
        return STATUS_SUCCESS;
    }
    if (hcdXportRefusal(hc, dev, first)) {
        KeReleaseSpinLock(&hcdSerialLock, serialIrql);
        /* The hold is queued (29-A.5): the device stays enumerated, with
         * no PDO, until the hold service's PED write and disconnect on the
         * thread's next pass, whose identity read still finds it on its
         * port. Its PDOs were never listed, so PnP never saw them and they
         * go now; nothing is reported and then removed. Not a failure: the
         * port's machine waits in Present (hcd_enum.c) for the hold
         * service, whose disconnect takes it from there like an unplug or
         * whose refusal brings it back here, refused in place (Codex
         * review of Phase 31, round 2, unit C; of the Phase 28-31
         * integration, finding 2). HcdHoldRequestUsb2 set HoldAsked. */
        XHCI_DBG_VALUE("hcd: sent back to USB 2.0, no PDO, port", dev->Port);
        status = STATUS_SUCCESS;
        goto cleanup;
    }
    dev->Pdo = first->Common.Self;
    dev->PdoGroup = first->Group;

    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    /* A dormant group this device did not revive is reported gone in the
     * same hold, so the relations answer that brings the new PDOs omits
     * it: one named by this place, or one with the serial id these PDOs
     * keep (hcdDormantRevive left it only if no present PDO carries it).
     * A dormant group named by its serial is not retired for its old
     * place: its device may come back elsewhere, and if it does not, its
     * START's wait retires it (hcdDormantWait). */
    hcdDormantRetireLocked(hc, first->InstanceKey, 0);
    hcdDormantRetireSerialLocked(hc, first);
    for (pdo = first; pdo != NULL; pdo = pdo->Sibling) {
        pdo->Next = hc->DevicePdos;
        hc->DevicePdos = pdo;
        pdo->Listed = 1;
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    KeReleaseSpinLock(&hcdSerialLock, serialIrql);
    hcdSerialDuplicateCount(hc, dev, duplicate);

    XHCI_DBG_VALUE("hcd: device PDOs created, port/count",
                   (dev->Port << 16) | count);
    /* The first guest run reads the grouping off these (design record 13
     * section 10.10: the real units' interface numbering is unread). */
    for (i = 0; i < set.Count; i++) {
        XHCI_DBG_VALUE("hcd: function PDO, port/MI",
                       (dev->Port << 16) | set.Func[i].FirstInterface);
        XHCI_DBG_VALUE("hcd: function PDO, interface mask",
                       set.Func[i].InterfaceMask);
        XHCI_DBG_VALUE("hcd: function PDO, class/subclass/protocol",
                       (set.Func[i].Class << 16) |
                           (set.Func[i].SubClass << 8) |
                           set.Func[i].Protocol);
    }
    hcdInvalidateFor(hc, first->ParentSerial);
    return STATUS_SUCCESS;

cleanup:
    while (first != NULL) {
        pdo = first;
        first = first->Sibling;
        hcdDeletePdo(pdo);
    }
    return status;
}

static VOID hcdUnlinkLocked(PHCD_DEVICE_PDO *head, PHCD_DEVICE_PDO pdo)
{
    PHCD_DEVICE_PDO *at;

    for (at = head; *at != NULL; at = &(*at)->Next) {
        if (*at == pdo) {
            *at = pdo->Next;
            pdo->Next = NULL;
            return;
        }
    }
}

/*
 * The device left. Every PDO of it - one device PDO, or every function PDO
 * of a split device - leaves the relations in one hold of PdoListLock, so the
 * next relations answer omits them all together (design record 13 sections
 * 10.5 step 3 and 10.9, CYCLE_PORT); a PDO PnP never saw is deleted here and
 * now. Returns 0 when nothing remains for the port to wait for (no PDO, or
 * every one deleted at once), otherwise the group serial the port must wait
 * on until the relations answer that omits them (HcdDevicePdoExists; the
 * PortPdoRemoved bit only says when to look - round 2, finding 2). The
 * device record goes with the slot; the PDOs keep nothing of it. Thread, or
 * the stop.
 */
ULONG HcdDevicePdoGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_DEVICE_PDO next;
    PHCD_DEVICE_PDO doomed;
    KIRQL oldIrql;
    ULONG waits;
    ULONG group;
    ULONG parent;

    if (dev == NULL || dev->Pdo == NULL) {
        return 0;
    }
    pdo = (PHCD_DEVICE_PDO)dev->Pdo->DeviceExtension;
    dev->Pdo = NULL;
    group = pdo->Group;
    parent = pdo->ParentSerial;
    doomed = NULL;
    waits = 0;

    /* The chain is whole here: every PDO on it is still listed, and a
     * listed PDO is never deleted by its own REMOVE (hcdPdoRemoved); the
     * parent's release, the one other deleter, clears dev->Pdo first. */
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    for (; pdo != NULL; pdo = next) {
        next = pdo->Sibling;
        pdo->Sibling = NULL;
        hcdUnlinkLocked(&hc->DevicePdos, pdo);
        pdo->Listed = 0;
        pdo->Device = NULL;
        if (!pdo->Reported) {
            pdo->Next = doomed;
            doomed = pdo;
        } else if (hcdAncestorGoneLocked(hc, pdo)) {
            /* Under a hub PnP has let go of (task 33.4): missing now, since
             * no answer of its parent's FDO can come first, and its port
             * must not wait for one (Codex review of 33.4, round 1). */
            pdo->MissingReported = 1;
            if (pdo->RemoveReceived) {
                pdo->DeletePending = 1;
                pdo->Next = hc->RemovedPdos;
                hc->RemovedPdos = pdo;
            } else {
                pdo->Next = hc->GonePdos;
                hc->GonePdos = pdo;
            }
        } else {
            pdo->Next = hc->GonePdos;
            hc->GonePdos = pdo;
            waits = 1;
        }
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);

    while (doomed != NULL) {
        pdo = doomed;
        doomed = pdo->Next;
        pdo->Next = NULL;
        hcdDeletePdo(pdo);
    }
    if (!waits) {
        return 0;
    }
    hcdInvalidateFor(hc, parent);
    return group;
}

/* Whether any PDO of that group still holds its port: listed, or gone and
 * not yet reported missing (the lifecycle above; one reported missing, on
 * GonePdos or RemovedPdos, waits for its REMOVE without its port). A lone
 * device PDO's group is its own serial. 0 for serial 0. IRQL: <=
 * DISPATCH_LEVEL. */
ULONG HcdDevicePdoExists(PHCD_CONTROLLER hc, ULONG serial)
{
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;
    ULONG found;

    if (serial == 0) {
        return 0;
    }
    found = 0;
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    for (pdo = hc->GonePdos; pdo != NULL && !found; pdo = pdo->Next) {
        found = pdo->Group == serial && !pdo->MissingReported;
    }
    for (pdo = hc->DevicePdos; pdo != NULL && !found; pdo = pdo->Next) {
        found = pdo->Group == serial;
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    return found;
}

/* Until no dispatch is inside the PDO's internal-IOCTL entry. IRQL:
 * PASSIVE_LEVEL. */
static VOID hcdWaitBusy(PHCD_DEVICE_PDO pdo)
{
    LARGE_INTEGER due;

    while (pdo->Busy != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}

/* The PDOs removed before this answer, already detached from RemovedPdos,
 * deleted: their removal passes are over (the lifecycle above). IRQL:
 * PASSIVE_LEVEL. */
static VOID hcdDeleteChain(PHCD_DEVICE_PDO pdo)
{
    PHCD_DEVICE_PDO next;

    for (; pdo != NULL; pdo = next) {
        next = pdo->Next;
        pdo->Next = NULL;
        hcdWaitBusy(pdo);
        hcdDeletePdo(pdo);
    }
}

/*
 * ORPHANS ON WINDOWS 98 (the Phase 28-31 integration's Codex review, finding
 * 2). An orphan - a device PDO, or the root-hub PDO, whose parent went
 * first while it still awaited its REMOVE - has no controller left to hold
 * it for the next relations answer, so it was deleted inside its REMOVE,
 * exactly the use after free c038326 removed for the attached case. On a
 * system whose configuration manager may send a removed PDO more IRPs (a
 * WDM below 1.10: Windows 98 SE and ME) it is retired to this driver-wide
 * list instead and deleted at the next BusRelations answer of any of this
 * driver's controllers or root hubs - a later pass, as c038326's rule has
 * it - or kept for good, a bounded leak, when none comes. Windows 2000 and
 * later send nothing after a REMOVE, and there it is deleted at once, as
 * before, so the image can still unload.
 */
static KSPIN_LOCK hcdRetiredLock;
static PDEVICE_OBJECT hcdRetired;       /* linked through RetiredNext */
static ULONG hcdRetireDefers;

/* IRQL: PASSIVE_LEVEL (DriverEntry). */
VOID HcdPdoRetireInit(VOID)
{
    KeInitializeSpinLock(&hcdRetiredLock);
    hcdRetired = NULL;
    hcdRetireDefers = !IoIsWdmVersionAvailable(1, 0x10);
}

/* An orphan's REMOVE, already completed: 1 when it is kept for a later
 * relations answer to delete (always, on Windows 98: the list is linked
 * through the extension, so it never fills - Codex review of the merge,
 * round 2), 0 when the caller deletes it now. IRQL: PASSIVE_LEVEL. */
ULONG HcdPdoRetire(PDEVICE_OBJECT obj)
{
    PHCD_COMMON common;
    KIRQL oldIrql;

    if (!hcdRetireDefers) {
        return 0;
    }
    common = (PHCD_COMMON)obj->DeviceExtension;
    KeAcquireSpinLock(&hcdRetiredLock, &oldIrql);
    common->RetiredNext = hcdRetired;
    hcdRetired = obj;
    KeReleaseSpinLock(&hcdRetiredLock, oldIrql);
    return 1;
}

/* Every retired orphan deleted. IRQL: PASSIVE_LEVEL. */
VOID HcdPdoReapRetired(VOID)
{
    PDEVICE_OBJECT obj;
    PDEVICE_OBJECT next;
    PHCD_COMMON common;
    KIRQL oldIrql;

    KeAcquireSpinLock(&hcdRetiredLock, &oldIrql);
    obj = hcdRetired;
    hcdRetired = NULL;
    KeReleaseSpinLock(&hcdRetiredLock, oldIrql);
    for (; obj != NULL; obj = next) {
        common = (PHCD_COMMON)obj->DeviceExtension;
        next = common->RetiredNext;
        common->RetiredNext = NULL;
        if (common->Kind == HCD_KIND_DEVICE_PDO) {
            hcdWaitBusy((PHCD_DEVICE_PDO)common);
            hcdDeletePdo((PHCD_DEVICE_PDO)common);
        } else {
            IoDeleteDevice(obj);
        }
    }
}

/*
 * A parent's BusRelations - the root hub's (`parent` 0) or a hub FDO's (the
 * Serial of its hub PDO; task 33.4): whatever the list already held, then
 * every listed PDO presented under that parent, referenced and marked
 * Reported. A gone PDO is marked MissingReported when this answer omits it -
 * it was this parent's - or when the ancestor rule says a hub above it has
 * left PnP's view (hcdAncestorGoneLocked), whichever parent answers; and a
 * gone PDO so marked whose REMOVE has already come moves to RemovedPdos,
 * its port told PDO_REMOVED (the lifecycle above). The marking repeats until
 * nothing more moves, since a hub marked in one sweep frees its children in
 * the next. One hold of PdoListLock for the count, the allocation, the copy
 * and the moves (round 1, finding 3); the PDOs removed before this answer
 * are detached in the same hold, so one moved now waits for the next
 * answer. NULL when the pool has nothing, and then nothing moves. IRQL:
 * PASSIVE_LEVEL (the allocation is NonPagedPool, so it is legal under the
 * spin lock).
 */
PDEVICE_RELATIONS HcdDevicePdoRelations(PHCD_CONTROLLER hc,
                                        PDEVICE_RELATIONS old, ULONG parent)
{
    PDEVICE_RELATIONS rel;
    PHCD_DEVICE_PDO pdo;
    PHCD_DEVICE_PDO *at;
    PHCD_DEVICE_PDO reap;
    ULONG ports[HCD_PORT_WORDS];
    KIRQL oldIrql;
    ULONG listed;
    ULONG count;
    ULONG moved;
    ULONG sweeps;
    ULONG i;

    for (i = 0; i < HCD_PORT_WORDS; i++) {
        ports[i] = 0;
    }
    count = (old != NULL) ? old->Count : 0;
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    reap = hc->RemovedPdos;
    hc->RemovedPdos = NULL;
    listed = 0;
    for (pdo = hc->DevicePdos; pdo != NULL; pdo = pdo->Next) {
        if (pdo->ParentSerial == parent) {
            listed++;
        }
    }
    rel = (PDEVICE_RELATIONS)HcdPoolAllocHandedOff(
        sizeof(DEVICE_RELATIONS) + (count + listed) * sizeof(PDEVICE_OBJECT));
    if (rel != NULL) {
        for (i = 0; i < count; i++) {
            rel->Objects[i] = old->Objects[i];
        }
        for (pdo = hc->DevicePdos; pdo != NULL; pdo = pdo->Next) {
            if (pdo->ParentSerial != parent) {
                continue;
            }
            rel->Objects[i++] = pdo->Common.Self;
            ObReferenceObject(pdo->Common.Self);
            pdo->Reported = 1;
        }
        rel->Count = i;
        for (sweeps = 0; sweeps <= HCD_MAX_HUBS + 1UL; sweeps++) {
            moved = 0;
            at = &hc->GonePdos;
            while (*at != NULL) {
                pdo = *at;
                if (pdo->ParentSerial != parent &&
                    !hcdAncestorGoneLocked(hc, pdo)) {
                    at = &pdo->Next;
                    continue;
                }
                if (!pdo->MissingReported) {
                    moved = 1;
                    if (pdo->Port != 0 && pdo->Port <= HCD_PORT_COUNT) {
                        /* Reported missing now: its port waits for it no
                         * longer (HcdDevicePdoExists). */
                        ports[(pdo->Port - 1) / 32UL] |=
                            1UL << ((pdo->Port - 1) % 32UL);
                    }
                }
                pdo->MissingReported = 1;
                if (!pdo->RemoveReceived) {
                    at = &pdo->Next;
                    continue;
                }
                moved = 1;
                *at = pdo->Next;
                pdo->DeletePending = 1;
                pdo->Next = hc->RemovedPdos;
                hc->RemovedPdos = pdo;
                if (pdo->Port != 0 && pdo->Port <= HCD_PORT_COUNT) {
                    ports[(pdo->Port - 1) / 32UL] |=
                        1UL << ((pdo->Port - 1) % 32UL);
                }
            }
            if (!moved) {
                break;
            }
        }
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    hcdDeleteChain(reap);
    HcdPdoReapRetired();
    for (i = 0; i < HCD_PORT_COUNT; i++) {
        if ((ports[i / 32UL] & (1UL << (i % 32UL))) != 0) {
            hcdPortNotify(hc, hc->PortPdoRemoved, i + 1);
        }
    }
    return rel;
}

/*
 * The parent is going (the root hub's remove, or the controller's): every
 * PDO left on either list is settled - deleted when PnP has removed it or
 * never knew it, orphaned when it still awaits its REMOVE (round 1, finding
 * 2). The thread has detached the bus first, so nothing lists more PDOs
 * meanwhile. IRQL: PASSIVE_LEVEL.
 */
VOID HcdDevicePdoReleaseAll(PHCD_CONTROLLER hc)
{
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;
    ULONG deleteIt;

    for (;;) {
        KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
        pdo = hc->DevicePdos;
        if (pdo != NULL) {
            hc->DevicePdos = pdo->Next;
        } else if (hc->GonePdos != NULL) {
            pdo = hc->GonePdos;
            hc->GonePdos = pdo->Next;
        } else {
            pdo = hc->RemovedPdos;
            if (pdo != NULL) {
                hc->RemovedPdos = pdo->Next;
            }
        }
        deleteIt = 0;
        if (pdo != NULL) {
            pdo->Next = NULL;
            pdo->Listed = 0;
            if (pdo->Device != NULL) {
                pdo->Device->Pdo = NULL;
            }
            pdo->Device = NULL;
            deleteIt = pdo->RemoveReceived || !pdo->Reported;
            pdo->Controller = NULL;
        }
        KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
        if (pdo == NULL) {
            break;
        }
        /* A dispatch that read the controller before the line above is
         * still inside hcd_urb.c; the controller's storage outlives it
         * (Codex review of batch (c), round 1, finding 1). */
        hcdWaitBusy(pdo);
        if (deleteIt) {
            hcdDeletePdo(pdo);
        }
    }
}

/* ----------------------------------------------------------------------- */
/* The device PDO's PnP and power                                           */
/* ----------------------------------------------------------------------- */

/* The refusals pended here (hcd_io.c, HcdIoRefuseLater), at a REMOVE only:
 * each completes at a clock tick, but a client may resubmit from its
 * completion routine until its own stack hears of the stop, so a STOP or
 * SURPRISE_REMOVAL that waited for them might never return (Codex review
 * of batch (c), round 8, finding 3). A REMOVE comes after every driver
 * above has handled it and stopped submitting. IRQL: PASSIVE_LEVEL. */
static VOID hcdPdoRefusalsWait(PHCD_DEVICE_PDO pdo, ULONG removing)
{
    if (removing) {
        HcdIoRefusedDrain(pdo);
    }
}

/*
 * Before a stop or a removal completes, every URB this PDO's stack has
 * pended here completes: a client driver unloads once its REMOVE is done,
 * and a completion arriving after that runs code that is gone (c4-2k,
 * 2026-10-03: bugcheck 0xCE in hidusb+0xb0a on an unplug, its interrupt
 * read completed by the device-gone drain after the REMOVE). A device
 * still present has its pipes aborted by the thread (AbortAll); a gone one
 * is drained by the thread's free. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdPdoQuiesce(PHCD_DEVICE_PDO pdo, ULONG removing)
{
    PHCD_CONTROLLER hc;
    PHCD_USB_DEVICE dev;
    LARGE_INTEGER due;
    KIRQL oldIrql;

    /* Admission closes first and the dispatches already inside are
     * waited out, so every URB this stack will ever pend here is counted
     * before the count is read (Codex review of batch (c), round 3,
     * finding 1). */
    pdo->Closing = 1;
    while (pdo->Busy != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    /* A held idle notification (hcd_urb.c, task 28-A.1) is no URB: it
     * completes here, and its completion has returned, before the
     * client's stop or removal goes on (Codex review of 28-A.1, round 1,
     * finding 2). */
    HcdUrbIdleDrain(pdo);
    /* What a departed device left held here completes now, CANCELED; with
     * Closing set nothing more is held (hcd_io.c, HcdIoPark). */
    (VOID)HcdIoParkedRelease(pdo, 0);
    if (pdo->UrbsPending == 0) {
        hcdPdoRefusalsWait(pdo, removing);
        return;
    }
    hc = pdo->Controller;
    if (hc != NULL) {
        /* Referenced while the PDO still names it, as a URB's dispatch
         * does, so the record cannot be freed under the flag (round 3,
         * finding 2). */
        KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
        dev = pdo->Device;
        if (dev != NULL) {
            (VOID)InterlockedIncrement(&dev->Refs);
        }
        KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
        if (dev != NULL) {
            if (pdo->Function) {
                /* Its own requests only, EP0's included: its siblings
                 * share the device and keep running (design record 13
                 * section 10.9). Its endpoints stay enabled with nothing
                 * on them until its next SELECT_CONFIGURATION, or the
                 * release a REMOVE queues (hcdPdoFunctionRelease). */
                HcdIoCancelPdo(hc, dev, pdo);
            } else {
                XhciControllerLockAcquire(&hc->Hc, &oldIrql);
                dev->AbortAll = 1;
                hc->CancelWork = 1;
                XhciControllerLockRelease(&hc->Hc, oldIrql);
                HcdThreadWake(hc);
            }
            (VOID)InterlockedDecrement(&dev->Refs);
        }
    }
    while (pdo->UrbsPending != 0 &&
           !(hc != NULL && hc->CommonBufferPinned)) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    hcdPdoRefusalsWait(pdo, removing);
}

/* A removed function's endpoints and alternates handed back while its
 * siblings run on (hcd_cfg.c, HcdCfgReleaseFunction): a REMOVE without a
 * prior unconfigure would otherwise leave an audio function's periodic
 * bandwidth reserved for as long as the device stays (Codex review of
 * batch (c), round 19, finding 4). After hcdPdoQuiesce, so its URBs are
 * complete. IRQL: PASSIVE_LEVEL. */
static VOID hcdPdoFunctionRelease(PHCD_DEVICE_PDO pdo)
{
    PHCD_CONTROLLER hc;
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;

    hc = pdo->Controller;
    if (!pdo->Function || hc == NULL) {
        return;
    }
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    dev = pdo->Device;
    if (dev != NULL) {
        (VOID)InterlockedIncrement((PLONG)&dev->Refs);
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    if (dev != NULL) {
        HcdCfgReleaseFunction(hc, dev, pdo->InterfaceMask);
        (VOID)InterlockedDecrement((PLONG)&dev->Refs);
    }
}

/*
 * A start after a remove (an enable after a disable) makes the PDO PnP's
 * again, so the remove it received no longer lets a parent's release delete
 * it. Then PDO_STARTED for the port's machine. Returns 0, and changes
 * nothing, for an orphan or a PDO whose device has left: its START is
 * failed STATUS_UNSUCCESSFUL, the status Windows 2000's hub driver fails
 * a departed device's START with (USBHUB20.SYS 5.00.2195.6655, 0x142E8,
 * static; it fails only one already removed, and lets a START with no
 * prior REMOVE succeed, which is the case refused here). Windows ME
 * started a mouse PDO whose slot had gone during its install, stopped it,
 * and never removed it: its port waited on it for good and the shell
 * wedged (2026-10-04, r3 t5); a start that fails is one the configuration
 * manager tears down. IRQL: PASSIVE_LEVEL.
 */
static ULONG hcdPdoStarted(PHCD_DEVICE_PDO pdo)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG listed;

    hc = pdo->Controller;
    if (hc == NULL) {
        return 0;
    }
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    listed = pdo->Listed;
    if (listed) {
        pdo->RemoveReceived = 0;
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    if (!listed) {
        return 0;
    }
    pdo->Closing = 0;
    /* A hub's port machine was told at once that its PDO started: the bus
     * serves a hub whether PnP has started it or not (task 33.4). */
    if (!pdo->Hub) {
        hcdPortNotify(hc, hc->PortPdoStarted, pdo->Port);
    }
    return 1;
}

/*
 * IRP_MN_REMOVE_DEVICE, already completed. The lifecycle above decides: an
 * orphan deletes itself; a gone PDO that PnP has seen reported missing (or
 * never saw) moves to RemovedPdos, once, for the next relations answer to
 * delete, and its port is told PDO_REMOVED; a listed PDO stays (a disable),
 * as does a gone one whose absence PnP has not yet been told - the
 * relations answer that omits it moves it to RemovedPdos, since this
 * REMOVE has come (HcdDevicePdoRelations), or the parent's release deletes
 * it.
 * Nothing here reads the PDO once it is on RemovedPdos: on Windows 2000 a
 * relations answer on another thread may delete it from then on. IRQL:
 * PASSIVE_LEVEL.
 */
/* An orphan's REMOVE: retired on Windows 98 (above), deleted now
 * elsewhere. DeletePending makes a REMOVE again a no-op either way. IRQL:
 * PASSIVE_LEVEL. */
static VOID hcdPdoOrphanRemoved(PHCD_DEVICE_PDO pdo)
{
    pdo->DeletePending = 1;
    if (!HcdPdoRetire(pdo->Common.Self)) {
        hcdDeletePdo(pdo);
    }
}

static VOID hcdPdoRemoved(PHCD_DEVICE_PDO pdo)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG deleteIt;
    ULONG port;

    if (pdo->Deleted || pdo->DeletePending) {
        /* A REMOVE again while a reference keeps the object: already
         * deleted or to be, and the controller may be gone. */
        return;
    }
    hc = pdo->Controller;
    if (hc == NULL) {
        hcdPdoOrphanRemoved(pdo);
        return;
    }
    deleteIt = 0;
    port = pdo->Port;
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    if (pdo->Controller == NULL) {
        deleteIt = 2;
    } else if (pdo->DeletePending) {
        /* A relations answer moved it since the test above (a REMOVE
         * again, on Windows 2000): it is RemovedPdos' already. */
    } else {
        pdo->RemoveReceived = 1;
        if (!pdo->Listed && (pdo->MissingReported || !pdo->Reported ||
                             hcdAncestorGoneLocked(hc, pdo))) {
            /* Gone under a hub PnP has let go of (task 33.4): its parent's
             * FDO answers no more, and this REMOVE is the subtree's. */
            hcdUnlinkLocked(&hc->GonePdos, pdo);
            pdo->DeletePending = 1;
            pdo->Next = hc->RemovedPdos;
            hc->RemovedPdos = pdo;
            deleteIt = 1;
        }
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    if (deleteIt == 2) {
        hcdPdoOrphanRemoved(pdo);
    } else if (deleteIt == 1) {
        hcdPortNotify(hc, hc->PortPdoRemoved, port);
    }
}

static NTSTATUS hcdDeviceCapabilities(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_CAPABILITIES caps;
    ULONG i;

    stack = IoGetCurrentIrpStackLocation(irp);
    caps = stack->Parameters.DeviceCapabilities.Capabilities;
    /*
     * Every field written here precedes D1Latency, so a structure at least
     * that long is enough. Windows 98 SE's hidclass.sys sends a shorter
     * DEVICE_CAPABILITIES than the Windows 2000 DDK's (inferred: its start
     * failed without ever reaching this PDO, 2026-10-03, c6-98), and a
     * refusal here fails the client's start - Code 10.
     */
    XHCI_DBG_VALUE("hcd: capabilities query, version/size",
                   ((ULONG)caps->Version << 16) | caps->Size);
    /* Windows 98 SE's hidclass.sys sends Version 0 and Size 0 - the fields
     * left unset - in the query its start depends on (measured, c7-98,
     * 2026-10-03: every query from the PnP manager read version 1 size
     * 0x40, hidclass's read 0/0, and refusing it was the Code 10). A
     * caller that sets nothing still passes a whole structure, and every
     * field written here is in its fixed prefix; only a caller that states
     * a size too short for them is refused. */
    if (!(caps->Version == 0 && caps->Size == 0) &&
        (caps->Version < 1 ||
         caps->Size < FIELD_OFFSET(DEVICE_CAPABILITIES, D1Latency))) {
        return HcdCompleteIrp(irp, STATUS_UNSUCCESSFUL, 0);
    }
    caps->DeviceD1 = FALSE;
    caps->DeviceD2 = FALSE;
    caps->LockSupported = FALSE;
    caps->EjectSupported = FALSE;
    caps->Removable = TRUE;
    caps->DockDevice = FALSE;
    /* TRUE exactly when the instance id is the serial id (33.2), as
     * usbhub answers; the location form is unique only under the root
     * hub, and PnP qualifies it. */
    caps->UniqueID = (pdo->SerialId[0] != 0) ? TRUE : FALSE;
    caps->SilentInstall = FALSE;
    caps->RawDeviceOK = FALSE;
    /* FALSE, as Windows 2000's usbhub reports a device PDO: the hot-plug
     * applet lists a removable device only when it is FALSE. A split
     * function keeps TRUE: stopping one function does not make the shared
     * connector safe to pull while its siblings run, and no relations tie
     * them for the applet (Codex review of 9001ebd). */
    caps->SurpriseRemovalOK = pdo->Function ? TRUE : FALSE;
    /* A hub too (task 33.4), so the hot-plug applet offers the devices
     * behind it rather than the hub itself; the applet's rule is unread
     * (design record 13 section 10.11). */
    if (pdo->Hub) {
        caps->SurpriseRemovalOK = TRUE;
    }
    caps->Address = pdo->InstanceKey;
    caps->UINumber = pdo->InstanceKey;
    caps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    for (i = PowerSystemSleeping1; i < PowerSystemMaximum; i++) {
        caps->DeviceState[i] = PowerDeviceD3;
    }
    caps->SystemWake = PowerSystemUnspecified;
    caps->DeviceWake = PowerDeviceUnspecified;
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/*
 * A QUERY_INTERFACE no PDO answers (USBDI's is answered in hcd_urb.c, task
 * 28-A.1), traced once per interface GUID by its first ULONG,
 * so a guest run shows what a class driver looked for. Past the table's room
 * every query is traced. PnP IRPs come one at a time per device; two devices
 * at once can at worst trace one GUID twice.
 */
static VOID hcdQueryInterfaceTrace(PIO_STACK_LOCATION stack)
{
#ifdef XHCI_DBG_TRACE
    static ULONG seen[8];
    static LONG count;
    ULONG data1;
    ULONG limit;
    LONG slot;
    ULONG i;

    if (stack->Parameters.QueryInterface.InterfaceType == NULL) {
        return;
    }
    data1 = stack->Parameters.QueryInterface.InterfaceType->Data1;
    /* The slot is claimed interlocked: two PDOs' queries on two
     * processors share the table (Codex review of batch (c), round 16,
     * finding 6). */
    limit = (ULONG)count;
    if (limit > 8) {
        limit = 8;
    }
    for (i = 0; i < limit; i++) {
        if (seen[i] == data1) {
            return;
        }
    }
    if (count < 8) {
        /* Reserved only while the table has room, so the count stops a
         * little past 8 rather than wrapping (round 17, finding 3). */
        slot = InterlockedIncrement(&count) - 1;
        if (slot >= 0 && slot < 8) {
            seen[slot] = data1;
        }
    }
    XHCI_DBG_VALUE("hcd: QUERY_INTERFACE not answered, GUID Data1", data1);
    XHCI_DBG_VALUE("hcd: QUERY_INTERFACE not answered, version/size",
                   ((ULONG)stack->Parameters.QueryInterface.Version << 16) |
                       stack->Parameters.QueryInterface.Size);
#else
    UNREFERENCED_PARAMETER(stack);
#endif
}

NTSTATUS HcdDevicePdoPnp(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_RELATIONS rel;
    PWCHAR text;
    ULONG n;

    stack = IoGetCurrentIrpStackLocation(irp);
    XHCI_DBG_VALUE("hcd: device PDO PnP minor", stack->MinorFunction);

    /* A hub's own name (task 33.4): the INF's model text replaces it once
     * the hub is installed. */
    if (pdo->Hub && stack->MinorFunction == IRP_MN_QUERY_DEVICE_TEXT &&
        stack->Parameters.QueryDeviceText.DeviceTextType ==
            DeviceTextDescription) {
        text = pdo->HubUsb3 ? hcdHandOff(L"xHCI98 USB 3.x Hub", 19)
                            : hcdHandOff(L"xHCI98 USB Hub", 15);
        if (text == NULL) {
            return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        }
        return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)text);
    }

    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        if (pdo->Dormant) {
            hcdDormantWait(pdo);
        }
        if (!hcdPdoStarted(pdo)) {
            XHCI_DBG_VALUE("hcd: START refused, device gone, port",
                           pdo->Port);
            return HcdCompleteIrp(irp, STATUS_UNSUCCESSFUL, 0);
        }
        pdo->Common.PnpState = HCD_PNP_STARTED;
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_STOP_DEVICE:
    case IRP_MN_SURPRISE_REMOVAL:
        if (stack->MinorFunction == IRP_MN_SURPRISE_REMOVAL) {
            pdo->Surprised = 1;
        }
        pdo->Common.PnpState = HCD_PNP_STOPPED;
        hcdPdoQuiesce(pdo, 0);
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_REMOVE_DEVICE:
        if (pdo->Dormant) {
            /* Removed while its controller was stopped: no device will
             * come back to it. Reported gone, then handled as any gone
             * PDO's REMOVE (hcdPdoRemoved). */
            hcdDormantRemoved(pdo);
        }
        pdo->Common.PnpState = HCD_PNP_REMOVED;
        hcdPdoQuiesce(pdo, 1);
        hcdPdoFunctionRelease(pdo);
        HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
        hcdPdoRemoved(pdo);
        return STATUS_SUCCESS;

    case IRP_MN_QUERY_ID:
        return hcdDeviceQueryId(pdo, irp, stack->Parameters.QueryId.IdType);

    case IRP_MN_QUERY_DEVICE_TEXT:
        if (stack->Parameters.QueryDeviceText.DeviceTextType ==
            DeviceTextDescription) {
            /* Its product, function or interface string (33.6, design
             * record 13 section 10.7), else what every PDO answered
             * before 2.1.0.0. */
            for (n = 0; n < XHCI_TEXT_CHARS && pdo->Text[n] != 0; n++) {
            }
            text = (n != 0) ? hcdHandOff(pdo->Text, n + 1)
                            : hcdHandOff(L"USB Device", 11);
            if (text == NULL) {
                return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            }
            return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)text);
        }
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);

    case IRP_MN_QUERY_CAPABILITIES:
        return hcdDeviceCapabilities(pdo, irp);

    case IRP_MN_QUERY_PNP_DEVICE_STATE:
        return HcdCompleteIrp(irp, STATUS_SUCCESS,
                              irp->IoStatus.Information);

    case IRP_MN_QUERY_DEVICE_RELATIONS:
        if (stack->Parameters.QueryDeviceRelations.Type ==
            TargetDeviceRelation) {
            rel = (PDEVICE_RELATIONS)HcdPoolAllocHandedOff(
                sizeof(DEVICE_RELATIONS));
            if (rel == NULL) {
                return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            }
            rel->Count = 1;
            rel->Objects[0] = pdo->Common.Self;
            ObReferenceObject(pdo->Common.Self);
            return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)rel);
        }
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);

    case IRP_MN_QUERY_INTERFACE:
        if (HcdUrbIsUsbdiQuery(
                stack->Parameters.QueryInterface.InterfaceType)) {
            return HcdUrbQueryInterface(pdo, irp);
        }
        hcdQueryInterfaceTrace(stack);
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);

    default:
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);
    }
}

NTSTATUS HcdDevicePdoPower(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    status = irp->IoStatus.Status;
    if (stack->MinorFunction == IRP_MN_SET_POWER) {
        if (stack->Parameters.Power.Type == DevicePowerState) {
            pdo->Common.DevicePower =
                stack->Parameters.Power.State.DeviceState;
            (VOID)PoSetPowerState(pdo->Common.Self, DevicePowerState,
                                  stack->Parameters.Power.State);
        } else {
            pdo->Common.SystemPower =
                stack->Parameters.Power.State.SystemState;
        }
        status = STATUS_SUCCESS;
    } else if (stack->MinorFunction == IRP_MN_QUERY_POWER) {
        status = STATUS_SUCCESS;
    } else if (stack->MinorFunction == IRP_MN_WAIT_WAKE) {
        status = STATUS_NOT_SUPPORTED;
    }
    PoStartNextPowerIrp(irp);
    return HcdCompleteIrp(irp, status, irp->IoStatus.Information);
}
