/*
 * hcd_quirk.c - Linux's xHCI controller quirks, the controller FDO's half
 * (roadmap-hcd task 38.10, design record 20 sections 4 and 6).
 *
 * The pure half - the values, the lists, the match, the packed word - is
 * xhci_quirk.c. This file reads the values at each start, establishes the
 * identity XhciMsiAvoid needs before the delivery is settled, and runs the
 * quirks that act from the FDO's own paths: the ASMedia flow-control write
 * after each reset and resume (Q9), the AMD bit before each suspend (Q16),
 * the shutdown's two (Q4, Q5) and the bounded log flush after them, and the
 * post-resume arming of the stuck-link check (Q12). The quirks that act in
 * the pure core read XhciQuirkOn where they act (xhci_init.c, xhci_ring.c,
 * xhci_xfer.c, xhci_link.c, xhci_stream.c, xhci_tol.c and their callers).
 *
 * Every quirk here is written from design record 20; Linux's code is cited
 * there by file and line for the behaviour and the constants, and none of
 * it is copied (section 6.6), so no legal-provenance.md row is owed.
 *
 * Every register write is made only on a controller whose window decodes
 * (HcInfoStatus good, not latched unreadable), in D0, and a skip or failure
 * is logged and recorded as the quirk's outcome (section 6.4).
 *
 * IRQL: each function carries its own.
 */

#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/* PCI configuration offsets. */
#define HCD_QK_PCI_ID               0x00UL
#define HCD_QK_PCI_REVISION         0x08UL

/* Q4: the Intel routing registers (xhci_psw.h's, restated for the write). */
#define HCD_QK_XUSB2PR              0xD0UL
#define HCD_QK_USB3_PSSEN           0xD8UL

/* Q9: the ASM1042A's vendor write (design record 20 section 4.8; the
 * constants are Linux's, pci-quirks.c:64-71): the control byte and its
 * write bit, the two data dwords, the command, address and data written. */
#define HCD_QK_ASM_CONTROL          0xE0UL
#define HCD_QK_ASM_WRITE_BIT        0x02UL
#define HCD_QK_ASM_DATA0            0xF8UL
#define HCD_QK_ASM_DATA1            0xFCUL
#define HCD_QK_ASM_CMD_WRITEREG     0x00010423UL
#define HCD_QK_ASM_FLOWCTL_ADDR     0x0000FA30UL
#define HCD_QK_ASM_FLOWCTL_DATA     0x000000BAUL
#define HCD_QK_ASM_PSEUDO_DATA      0x00000000UL
#define HCD_QK_ASM_WAIT_TRIES       1000UL
#define HCD_QK_ASM_WAIT_US          50UL

/* Q16: AMD Raven2's register and bit (Linux's xhci-pci.c:27-28). */
#define HCD_QK_AMD_SPARSE_OFFSET    0xC12CUL
#define HCD_QK_AMD_SPARSE_BIT       0x00020000UL    /* bit 17            */

/* The shutdown's log flush: its bound and the step it waits in. */
#define HCD_QK_FLUSH_WAIT_MS        500UL
#define HCD_QK_FLUSH_STEP_MS        10UL

/* The value names, from xhci_quirk.h's one list, for the registry reads. */
#define HCD_QK_WIDE(s) L ## s,

static const PCWSTR hcdQuirkValues[] = {
    XHCI_QK_VALUE_NAMES(HCD_QK_WIDE)
};
XHCI_C_ASSERT(hcd_quirk_values_count,
              sizeof(hcdQuirkValues) / sizeof(hcdQuirkValues[0]) ==
                  XHCI_QK_COUNT);

/* One value, as found: XHCI_QK_FOUND_*. IRQL: PASSIVE_LEVEL. */
static ULONG hcdQuirkRead(PHCD_CONTROLLER hc, ULONG quirk, PULONG value)
{
    NTSTATUS status;

    *value = 0;
    status = HcdReadValueDword(hc, hcdQuirkValues[quirk], value);
    if (NT_SUCCESS(status)) {
        return XHCI_QK_FOUND_DWORD;
    }
    *value = 0;
    return (status == STATUS_OBJECT_TYPE_MISMATCH) ? XHCI_QK_FOUND_OTHER
                                                   : XHCI_QK_FOUND_NONE;
}

/* The controller's PCI identity through the PCI PDO's configuration
 * service; kept for the FDO's lifetime when read (section 4.1). Returns 1
 * when this read succeeded. IRQL: PASSIVE_LEVEL. */
static ULONG hcdQuirkReadIdentity(PHCD_CONTROLLER hc)
{
    ULONG id;

    id = 0xFFFFFFFFUL;
    if (HcdSvcConfigSpace(&hc->Hc, TRUE, &id, HCD_QK_PCI_ID, sizeof(id)) !=
            MP_STATUS_SUCCESS ||
        id == 0xFFFFFFFFUL || id == 0) {
        return 0;
    }
    hc->QuirkIdentity = id;
    hc->QuirkIdentityValid = 1;
    return 1;
}

/*
 * AddDevice, after the attach (hcd_entry.c), on NT 6 alone, where the
 * requirements filter runs before the start and reads no registry: the
 * identity and XhciMsiAvoid cached for it beside the mode (record 18
 * section 4.2; record 20 section 4.1). An identity that cannot be read
 * leaves the filter as it is. IRQL: PASSIVE_LEVEL (AddDevice's system
 * thread).
 */
VOID HcdQuirkAddDevice(PHCD_CONTROLLER hc)
{
    ULONG value;

    hc->QuirkIdentity = 0;
    hc->QuirkIdentityValid = 0;
    hc->MsiAvoidCacheRaw = 0;
    hc->MsiAvoidCacheFound = XHCI_QK_FOUND_NONE;
    hc->MsiAvoidFilterStrips = 0;
    if (!HcdMsiSystem()) {
        return;
    }
    hc->MsiAvoidCacheFound = hcdQuirkRead(hc, XHCI_QK_MSI_AVOID, &value);
    hc->MsiAvoidCacheRaw = value;
    (VOID)hcdQuirkReadIdentity(hc);
}

/*
 * The requirements filter's question (hcd_msi86.c): whether, under mode 0,
 * the line configurations alone are asked for, as XhciInterruptMode 1 asks.
 * From the caches alone. Unlike the start, an identity never read leaves
 * the list as it is: the start's own decision still protects the
 * controller. IRQL: PASSIVE_LEVEL.
 */
ULONG HcdQuirkFilterAvoid(PHCD_CONTROLLER hc)
{
    ULONG norm;
    ULONG mode;

    mode = XhciQuirkMode(XHCI_QK_MSI_AVOID, hc->MsiAvoidCacheFound,
                         hc->MsiAvoidCacheRaw, &norm);
    if (mode == XHCI_QK_MODE_EVERY) {
        return 1;
    }
    if (mode != XHCI_QK_MODE_LISTED || !hc->QuirkIdentityValid) {
        return 0;
    }
    return XhciQuirkMatches(XHCI_QK_MSI_AVOID, hc->QuirkIdentity);
}

/* The start, just after the extension is zeroed: every quirk at its
 * default and unread. IRQL: PASSIVE_LEVEL. */
VOID HcdQuirkStartBegin(PHCD_CONTROLLER hc)
{
    XhciQuirkInit(&hc->Hc.Quirk);
}

/*
 * XhciMsiAvoid at the start (record 20 section 4.1), from hcdSettleDelivery
 * before XhciPollDecideEx: the value read and cached for a later filter,
 * the identity read through the configuration service - the BAR is not
 * mapped yet, and XhciInitController's own read comes after the delivery is
 * connected - and the decision. A failed read keeps an identity already
 * established for this FDO; with none ever read, value 1 avoids the message
 * as on a listed controller (XhciQuirkMsiAvoidDecide). Under
 * XhciInterruptMode 3 the user's message stands and the quirk is recorded
 * as overridden. Returns whether mode 0 avoids a message. Logged later, at
 * HcdQuirkStart, once the log is open. IRQL: PASSIVE_LEVEL.
 */
ULONG HcdQuirkMsiAvoid(PHCD_CONTROLLER hc, ULONG interruptMode)
{
    PXHCI_QUIRK_STATE st;
    ULONG value;
    ULONG found;
    ULONG fresh;
    ULONG known;
    ULONG avoid;
    ULONG matched;

    st = &hc->Hc.Quirk;
    found = hcdQuirkRead(hc, XHCI_QK_MSI_AVOID, &value);
    XhciQuirkSetValue(st, XHCI_QK_MSI_AVOID, found, value);
    if (HcdMsiSystem()) {
        hc->MsiAvoidCacheFound = found;
        hc->MsiAvoidCacheRaw = value;
    }
    fresh = hcdQuirkReadIdentity(hc);
    known = hc->QuirkIdentityValid;
    avoid = XhciQuirkMsiAvoidDecide(st->Mode[XHCI_QK_MSI_AVOID], known,
                                    hc->QuirkIdentity);
    matched = known ? XhciQuirkMatches(XHCI_QK_MSI_AVOID, hc->QuirkIdentity)
                    : 0UL;
    if (!known) {
        XhciQuirkOutcome(st, XHCI_QK_MSI_AVOID, XHCI_QK_OUT_NONE,
                         XHCI_QK_WHY_ID_UNREAD);
    } else if (!fresh) {
        XhciQuirkOutcome(st, XHCI_QK_MSI_AVOID, XHCI_QK_OUT_NONE,
                         XHCI_QK_WHY_ID_KEPT);
    }
    if (interruptMode == XHCI_POLL_MODE_MESSAGE) {
        XhciQuirkSetEffective(st, XHCI_QK_MSI_AVOID, matched, 0, avoid);
        return 0;
    }
    XhciQuirkSetEffective(st, XHCI_QK_MSI_AVOID, matched, avoid, 0);
    /* The requirements filter that ran before this start stripped the
     * messages at the quirk's asking: the quirk acted there, so the line
     * this start was given is its doing. */
    if (hc->MsiAvoidFilterStrips != 0) {
        st->MsiRefused += hc->MsiAvoidFilterStrips;
        XhciQuirkOutcome(st, XHCI_QK_MSI_AVOID, XHCI_QK_OUT_DONE,
                         st->Why[XHCI_QK_MSI_AVOID]);
    }
    return (interruptMode == XHCI_POLL_MODE_AUTO) ? avoid : 0UL;
}

/* hcdSettleDelivery's answer under the quirk: a message refused. IRQL:
 * PASSIVE_LEVEL. */
VOID HcdQuirkMsiRefused(PHCD_CONTROLLER hc)
{
    hc->Hc.Quirk.MsiRefused++;
    XhciQuirkOutcome(&hc->Hc.Quirk, XHCI_QK_MSI_AVOID, XHCI_QK_OUT_DONE,
                     hc->Hc.Quirk.Why[XHCI_QK_MSI_AVOID]);
}

/* The counter block's copy of the quirk state: the packed words and the
 * counts. Controller lock held, or no thread yet. IRQL: <= DISPATCH_LEVEL. */
VOID HcdQuirkPublishLocked(PHCD_CONTROLLER hc)
{
    const XHCI_QUIRK_STATE *st;
    PXHCIHC_COUNTERS c;
    PULONG packed;
    ULONG i;

    st = &hc->Hc.Quirk;
    c = &hc->Counters;
    packed = &c->QuirkMsiAvoid;
    for (i = 0; i < XHCI_QK_COUNT; i++) {
        packed[i] = XhciQuirkPacked(st, i);
    }
    c->QuirkPciRevision = st->PciRevision;
    c->QuirkIdentity = st->Identity;
    c->QuirkMsiRefused = st->MsiRefused;
    c->QuirkHaltsLong = st->HaltsLong;
    c->QuirkHaltDelays = st->HaltDelays;
    c->QuirkSavesSkipped = st->SavesSkipped;
    c->QuirkStuckLinkResets = st->StuckLinkResets;
    c->QuirkPaddedBlocks = st->PaddedBlocks;
    c->QuirkLinkNoOps = st->LinkNoOps;
    c->QuirkIsochDropped = st->IsochDropped;
    c->QuirkTiCapped = st->TiCapped;
    c->QuirkAsmWrites = st->AsmWrites;
    c->QuirkAsmGiveUps = st->AsmGiveUps;
    c->QuirkAmdBitClears = st->AmdBitClears;
    c->QuirkAmdBitSkips = st->AmdBitSkips;
    c->QuirkShutEhciWrites = st->ShutEhciWrites;
    c->QuirkShutResets = st->ShutResets;
    c->QuirkShutFlushes = st->ShutFlushes;
    c->QuirkShutFlushSinkOff = st->ShutFlushSinkOff;
    c->QuirkShutFlushUnacked = st->ShutFlushUnacked;
}

/* The same from a context that holds no lock. IRQL: <= DISPATCH_LEVEL. */
static VOID hcdQuirkPublish(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    HcdQuirkPublishLocked(hc);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* The packed word of one quirk to the log. IRQL: <= DISPATCH_LEVEL. */
static VOID hcdQuirkNote(PHCD_CONTROLLER hc, ULONG quirk)
{
    XhciLogNote(&hc->Hc, XhciQuirkLabel(quirk),
                XhciQuirkPacked(&hc->Hc.Quirk, quirk));
}

/* Whether a register write may be made: a window that decodes, not
 * latched unreadable. IRQL: any. */
static ULONG hcdQuirkWindow(PHCD_CONTROLLER hc)
{
    return (hc->Hc.HcInfoStatus == XHCI_HC_OK && !hc->Hc.Tol.Unreadable)
               ? 1UL
               : 0UL;
}

/* One configuration byte, 0xFF when it cannot be read. IRQL:
 * PASSIVE_LEVEL. */
static UCHAR hcdQuirkConfigByte(PHCD_CONTROLLER hc, ULONG offset)
{
    UCHAR b;

    b = 0xFF;
    if (HcdSvcConfigSpace(&hc->Hc, TRUE, &b, offset, sizeof(b)) !=
        MP_STATUS_SUCCESS) {
        b = 0xFF;
    }
    return b;
}

/* One configuration write of `length` bytes. IRQL: PASSIVE_LEVEL. */
static ULONG hcdQuirkConfigWrite(PHCD_CONTROLLER hc, ULONG offset,
                                 ULONG value, ULONG length)
{
    ULONG d;
    UCHAR b;

    if (length == 1) {
        b = (UCHAR)value;
        return HcdSvcConfigSpace(&hc->Hc, FALSE, &b, offset, 1) ==
               MP_STATUS_SUCCESS;
    }
    d = value;
    return HcdSvcConfigSpace(&hc->Hc, FALSE, &d, offset, sizeof(d)) ==
           MP_STATUS_SUCCESS;
}

/* Q9's wait for the write bit of the control byte to clear, bounded at
 * 1000 x 50 us, giving up on a read of FFh. XHCI_QK_WHY_NONE when clear.
 * IRQL: PASSIVE_LEVEL. */
static ULONG hcdQuirkAsmWait(PHCD_CONTROLLER hc)
{
    UCHAR b;
    ULONG i;

    for (i = 0; i < HCD_QK_ASM_WAIT_TRIES; i++) {
        b = hcdQuirkConfigByte(hc, HCD_QK_ASM_CONTROL);
        if (b == 0xFF) {
            return XHCI_QK_WHY_READ_FF;
        }
        if ((b & HCD_QK_ASM_WRITE_BIT) == 0) {
            return XHCI_QK_WHY_NONE;
        }
        KeStallExecutionProcessor(HCD_QK_ASM_WAIT_US);
    }
    return XHCI_QK_WHY_BUSY;
}

/*
 * Q9, XhciAsmediaFlowControl (record 20 section 4.8): the ASM1042A's
 * vendor flow-control setting, after every path that leaves the
 * controller reset or restored - the start, the recovery and both kinds of
 * resume - at PASSIVE_LEVEL, where a configuration access can be made (the
 * recovery's reset runs raised). Wait for the write bit; command and
 * address, write bit; wait; data and pseudo-data, write bit. A wait that
 * gives up or a failed access skips the rest; service goes on. A second
 * kind of configuration write above the standard header, beside the Intel
 * switchover's (implementation-invariants.md, "PnP Resources").
 * IRQL: PASSIVE_LEVEL, in a D0 context (the start after
 * XhciInitController, or under the power gate).
 */
VOID HcdQuirkAsmFlow(PHCD_CONTROLLER hc)
{
    PXHCI_QUIRK_STATE st;
    ULONG why;

    st = &hc->Hc.Quirk;
    if (!XhciQuirkOn(st, XHCI_QK_ASM_FLOW)) {
        return;
    }
    if (!hcdQuirkWindow(hc)) {
        why = XHCI_QK_WHY_UNREADABLE;
        goto gaveUp;
    }
    why = hcdQuirkAsmWait(hc);
    if (why != XHCI_QK_WHY_NONE) {
        goto gaveUp;
    }
    if (!hcdQuirkConfigWrite(hc, HCD_QK_ASM_DATA0, HCD_QK_ASM_CMD_WRITEREG,
                             4) ||
        !hcdQuirkConfigWrite(hc, HCD_QK_ASM_DATA1, HCD_QK_ASM_FLOWCTL_ADDR,
                             4) ||
        !hcdQuirkConfigWrite(hc, HCD_QK_ASM_CONTROL, HCD_QK_ASM_WRITE_BIT,
                             1)) {
        why = XHCI_QK_WHY_WRITE_FAILED;
        goto gaveUp;
    }
    why = hcdQuirkAsmWait(hc);
    if (why != XHCI_QK_WHY_NONE) {
        goto gaveUp;
    }
    if (!hcdQuirkConfigWrite(hc, HCD_QK_ASM_DATA0, HCD_QK_ASM_FLOWCTL_DATA,
                             4) ||
        !hcdQuirkConfigWrite(hc, HCD_QK_ASM_DATA1, HCD_QK_ASM_PSEUDO_DATA,
                             4) ||
        !hcdQuirkConfigWrite(hc, HCD_QK_ASM_CONTROL, HCD_QK_ASM_WRITE_BIT,
                             1)) {
        why = XHCI_QK_WHY_WRITE_FAILED;
        goto gaveUp;
    }
    st->AsmWrites++;
    XhciQuirkOutcome(st, XHCI_QK_ASM_FLOW, XHCI_QK_OUT_DONE,
                     XHCI_QK_WHY_NONE);
    hcdQuirkNote(hc, XHCI_QK_ASM_FLOW);
    hcdQuirkPublish(hc);
    return;

gaveUp:
    st->AsmGiveUps++;
    XhciQuirkOutcome(st, XHCI_QK_ASM_FLOW,
                     (why == XHCI_QK_WHY_UNREADABLE) ? XHCI_QK_OUT_SKIPPED
                                                     : XHCI_QK_OUT_FAILED,
                     why);
    hcdQuirkNote(hc, XHCI_QK_ASM_FLOW);
    hcdQuirkPublish(hc);
}

/*
 * The start, once XhciInitController has succeeded: the other thirteen
 * values read, the revision read and logged (never matched, section 6.3),
 * every list matched against the PCI id the start read, and each value's
 * packed word logged and published. XhciMsiAvoid was decided before the
 * delivery on the identity read then; a disagreement with this read is
 * logged. Then Q9's write, before the enumeration begins (the thread does
 * not exist yet). IRQL: PASSIVE_LEVEL.
 */
VOID HcdQuirkStart(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    PXHCI_QUIRK_STATE st;
    UCHAR revision;
    ULONG value;
    ULONG found;
    ULONG i;

    ext = &hc->Hc;
    st = &ext->Quirk;
    for (i = 0; i < XHCI_QK_COUNT; i++) {
        if (i == XHCI_QK_MSI_AVOID) {
            continue;
        }
        found = hcdQuirkRead(hc, i, &value);
        XhciQuirkSetValue(st, i, found, value);
    }
    revision = 0xFF;
    if (HcdSvcConfigSpace(ext, TRUE, &revision, HCD_QK_PCI_REVISION,
                          sizeof(revision)) == MP_STATUS_SUCCESS) {
        st->PciRevision = (ULONG)revision;
    }
    XhciQuirkMatchAll(st, ext->PciVendorDevice);
    XhciLogNote(ext, "hc.pci.rev", st->PciRevision);
    XhciLogNote(ext, "quirk.identity", st->Identity);
    if (hc->QuirkIdentityValid &&
        hc->QuirkIdentity != ext->PciVendorDevice) {
        XhciLogNote(ext, "quirk.msiavoid.idmismatch", hc->QuirkIdentity);
    }
    for (i = 0; i < XHCI_QK_COUNT; i++) {
        hcdQuirkNote(hc, i);
    }
    hcdQuirkPublish(hc);
    HcdQuirkAsmFlow(hc);
}

/*
 * Q16, XhciAmdSuspendBitClear (record 20 section 4.15): bit 17 of BAR
 * offset C12Ch cleared before each suspend on AMD Raven2, as Linux does;
 * the record claims no knowledge of what the bit does. One read-modify-
 * write, made only on a started lifetime with its BAR mapped (a stopped FDO
 * keeps IoSpaceLength, HcInfoStatus and its recorded D0 with the BAR
 * unmapped), a window that reaches the dword, in D0, decoding, and a read
 * other than all ones; otherwise skipped and logged. Before
 * XhciSuspendController, inside the D3 path's guarded suspend. The power
 * gate held; IRQL: PASSIVE_LEVEL.
 */
VOID HcdQuirkSuspend(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    PXHCI_QUIRK_STATE st;
    ULONG value;
    ULONG why;

    ext = &hc->Hc;
    st = &ext->Quirk;
    if (!XhciQuirkOn(st, XHCI_QK_AMD_BIT)) {
        return;
    }
    if (!hc->ControllerStarted || hc->BarVa == NULL) {
        why = XHCI_QK_WHY_NOT_STARTED;
    } else if (hc->Common.DevicePower != PowerDeviceD0) {
        why = XHCI_QK_WHY_NOT_D0;
    } else if (!hcdQuirkWindow(hc)) {
        why = XHCI_QK_WHY_UNREADABLE;
    } else if (ext->IoSpaceLength < HCD_QK_AMD_SPARSE_OFFSET + 4UL) {
        why = XHCI_QK_WHY_WINDOW_SMALL;
    } else {
        value = XhciRead32(ext, HCD_QK_AMD_SPARSE_OFFSET);
        if (value == 0xFFFFFFFFUL) {
            why = XHCI_QK_WHY_ALL_ONES;
        } else {
            XhciWrite32(ext, HCD_QK_AMD_SPARSE_OFFSET,
                        value & ~HCD_QK_AMD_SPARSE_BIT);
            st->AmdBitClears++;
            XhciQuirkOutcome(st, XHCI_QK_AMD_BIT, XHCI_QK_OUT_DONE,
                             XHCI_QK_WHY_NONE);
            hcdQuirkNote(hc, XHCI_QK_AMD_BIT);
            hcdQuirkPublish(hc);
            return;
        }
    }
    st->AmdBitSkips++;
    XhciQuirkOutcome(st, XHCI_QK_AMD_BIT, XHCI_QK_OUT_SKIPPED, why);
    hcdQuirkNote(hc, XHCI_QK_AMD_BIT);
    hcdQuirkPublish(hc);
}

/* Q4's one register: written 0 and read back. IRQL: PASSIVE_LEVEL. */
static ULONG hcdQuirkEhciWrite(PHCD_CONTROLLER hc, ULONG offset)
{
    ULONG back;

    if (!hcdQuirkConfigWrite(hc, offset, 0, 4)) {
        return XHCI_QK_WHY_WRITE_FAILED;
    }
    back = 0xFFFFFFFFUL;
    if (HcdSvcConfigSpace(&hc->Hc, TRUE, &back, offset, sizeof(back)) !=
        MP_STATUS_SUCCESS) {
        return XHCI_QK_WHY_WRITE_FAILED;
    }
    return (back == 0) ? XHCI_QK_WHY_NONE : XHCI_QK_WHY_READBACK;
}

/*
 * The shutdown's D3 (record 20 sections 4.4 and 4.5), after the existing
 * flush and switchover release and before the D3 IRP goes down:
 *
 *   - Q4, XhciIntelShutdownToEhci: on Panther Point, Lynx Point-LP and
 *     Wildcat Point-LP, USB3_PSSEN (D8h) and XUSB2PR (D0h) written 0 and
 *     read back, whatever XhciIntelPortSwitch says (owner, 2026-10-13;
 *     record 16 section 5's exception), with no Sony exemption, as
 *     Linux's shutdown write has none;
 *   - Q5, XhciShutdownReset (off by default): an HCRST after the halt,
 *     only on a halt proven (XhciShutdownReset in xhci_init.c), then the
 *     saved image invalidated.
 *
 * Each on a started lifetime with its BAR mapped, in D0, on a window that
 * decodes; a skip or failure is logged and the shutdown goes on. Returns 1
 * when either was attempted, so the caller flushes the log once the power
 * gate is left. The power gate held; IRQL: PASSIVE_LEVEL.
 */
ULONG HcdQuirkShutdown(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    PXHCI_QUIRK_STATE st;
    ULONG attempted;
    ULONG ready;
    ULONG why;
    ULONG why2;

    ext = &hc->Hc;
    st = &ext->Quirk;
    attempted = 0;
    why = XHCI_QK_WHY_NONE;
    if (!hc->ControllerStarted || hc->BarVa == NULL) {
        why = XHCI_QK_WHY_NOT_STARTED;
    } else if (hc->Common.DevicePower != PowerDeviceD0) {
        why = XHCI_QK_WHY_NOT_D0;
    } else if (!hcdQuirkWindow(hc)) {
        why = XHCI_QK_WHY_UNREADABLE;
    }
    ready = (why == XHCI_QK_WHY_NONE) ? 1UL : 0UL;

    if (XhciQuirkOn(st, XHCI_QK_SHUT_TO_EHCI)) {
        attempted = 1;
        if (ready) {
            /* Both registers whatever the first's outcome: a USB3_PSSEN
             * that would not clear does not keep the USB 2.0 pairs on the
             * xHCI. The first failure is the one reported. */
            why = hcdQuirkEhciWrite(hc, HCD_QK_USB3_PSSEN);
            why2 = hcdQuirkEhciWrite(hc, HCD_QK_XUSB2PR);
            if (why == XHCI_QK_WHY_NONE) {
                why = why2;
            }
            if (why == XHCI_QK_WHY_NONE) {
                st->ShutEhciWrites++;
            }
            XhciQuirkOutcome(st, XHCI_QK_SHUT_TO_EHCI,
                             (why == XHCI_QK_WHY_NONE) ? XHCI_QK_OUT_DONE
                                                       : XHCI_QK_OUT_FAILED,
                             why);
        } else {
            XhciQuirkOutcome(st, XHCI_QK_SHUT_TO_EHCI, XHCI_QK_OUT_SKIPPED,
                             why);
        }
        hcdQuirkNote(hc, XHCI_QK_SHUT_TO_EHCI);
    }

    if (XhciQuirkOn(st, XHCI_QK_SHUT_RESET)) {
        attempted = 1;
        if (ready) {
            why = XhciShutdownReset(ext);
            if (why == XHCI_QK_WHY_NONE) {
                st->ShutResets++;
                XhciQuirkOutcome(st, XHCI_QK_SHUT_RESET, XHCI_QK_OUT_DONE,
                                 XHCI_QK_WHY_NONE);
            } else {
                XhciQuirkOutcome(st, XHCI_QK_SHUT_RESET,
                                 (why == XHCI_QK_WHY_RESET_REFUSED)
                                     ? XHCI_QK_OUT_FAILED
                                     : XHCI_QK_OUT_SKIPPED,
                                 why);
            }
        } else {
            XhciQuirkOutcome(st, XHCI_QK_SHUT_RESET, XHCI_QK_OUT_SKIPPED,
                             (why == XHCI_QK_WHY_NONE)
                                 ? XHCI_QK_WHY_NOT_STARTED
                                 : why);
        }
        hcdQuirkNote(hc, XHCI_QK_SHUT_RESET);
    }
    if (attempted) {
        hcdQuirkPublish(hc);
    }
    return attempted;
}

/*
 * The shutdown's records to the sink (record 20 section 6.4): the log ring
 * lives in memory and the next boot cannot read it, so after Q4 and Q5 the
 * D3 path takes a ticket, wakes the thread, and waits - bounded at 500 ms in
 * all - for the thread to publish that ticket as acknowledged after a flush
 * it began after capturing it (HcdQuirkFlushAck). Called with the power
 * gate left: the thread's pass takes the gate before it flushes. A thread
 * that has left its loop, or does not get there in the bound, is not waited
 * for further, and the attempt is counted unacknowledged. An
 * acknowledgement with the DebugView sink off is counted apart: nothing was
 * emitted. Before the D3 IRP goes down. IRQL: PASSIVE_LEVEL.
 */
VOID HcdQuirkShutdownFlush(PHCD_CONTROLLER hc)
{
    PXHCI_QUIRK_STATE st;
    LARGE_INTEGER due;
    LONG ticket;
    ULONG waited;
    ULONG acked;

    st = &hc->Hc.Quirk;
    ticket = InterlockedIncrement(&hc->ShutFlushWanted);
    acked = 0;
    if (hc->ThreadRunning && !hc->ThreadStop) {
        KeClearEvent(&hc->ShutFlushDone);
        HcdThreadWake(hc);
        for (waited = 0; waited < HCD_QK_FLUSH_WAIT_MS;
             waited += HCD_QK_FLUSH_STEP_MS) {
            if (hc->ShutFlushAcked - ticket >= 0) {
                acked = 1;
                break;
            }
            if (hc->ThreadStop || !hc->ThreadRunning) {
                break;
            }
            HcdRelativeMs(&due, HCD_QK_FLUSH_STEP_MS);
            (VOID)KeWaitForSingleObject(&hc->ShutFlushDone, Executive,
                                        KernelMode, FALSE, &due);
            KeClearEvent(&hc->ShutFlushDone);
        }
        if (!acked && hc->ShutFlushAcked - ticket >= 0) {
            acked = 1;
        }
    }
    if (!acked) {
        st->ShutFlushUnacked++;
    } else if (hc->ShutFlushSink) {
        st->ShutFlushes++;
    } else {
        /* Serviced, nothing emitted: the sink off, or nothing recorded. */
        st->ShutFlushSinkOff++;
    }
    hcdQuirkPublish(hc);
}

/* The thread, after the HcdLogFlush it began with `ticket` captured: the
 * ticket published as acknowledged, with whether that flush emitted any
 * record to the sink (`emitted`, HcdLogFlush's own count: 0 with the sink
 * off or the records not kept). A flush already under way
 * when the records were written captured an older ticket, so it never
 * acknowledges them. IRQL: PASSIVE_LEVEL (the controller thread). */
VOID HcdQuirkFlushAck(PHCD_CONTROLLER hc, LONG ticket, ULONG emitted)
{
    if (hc->ShutFlushAcked == ticket) {
        return;
    }
    hc->ShutFlushSink = emitted ? 1UL : 0UL;
    (VOID)InterlockedExchange(&hc->ShutFlushAcked, ticket);
    (VOID)KeSetEvent(&hc->ShutFlushDone, IO_NO_INCREMENT, FALSE);
}

/*
 * A resume's end, either branch (hcd_power.c), the power gate held:
 * Q12, XhciStuckLinkReset (record 20 section 4.11) - after a resume that
 * restored (`restoresBefore` is Quirk.Restores before it), the first
 * reading of each USB3 root port is armed to warm-reset one stuck in
 * Polling with no CAS; a reinitialization starts from HCRST and needs
 * nothing - then Q9's write. IRQL: PASSIVE_LEVEL.
 */
VOID HcdQuirkResumed(PHCD_CONTROLLER hc, ULONG restoresBefore)
{
    PXHCI_EXTENSION ext;
    ULONG i;

    ext = &hc->Hc;
    if (ext->Quirk.Restores != restoresBefore &&
        XhciQuirkOn(&ext->Quirk, XHCI_QK_STUCK_LINK)) {
        for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
            if (i + 1 <= ext->PortMap.PortCount &&
                XhciPortIsUsb3(&ext->PortMap, i + 1)) {
                hc->Ports[i].Link.StuckCheck = 1;
            }
        }
    }
    HcdQuirkAsmFlow(hc);
}

/* Q14a's input: the quirk applies and the device is SuperSpeed or faster.
 * IRQL: any. */
ULONG HcdQuirkLinkNoOp(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    if (!XhciQuirkOn(&hc->Hc.Quirk, XHCI_QK_LINK_NOOP) || dev == NULL) {
        return 0;
    }
    return (HcdDeviceSpeedClass(hc, dev) >= XHCI_SPEED_SUPER) ? 1UL : 0UL;
}

/* Q14a's count, beside the submission. Controller lock held or not; a
 * lost count costs nothing. IRQL: <= DISPATCH_LEVEL. */
VOID HcdQuirkLinkPadded(PHCD_CONTROLLER hc, ULONG padded)
{
    if (!padded) {
        return;
    }
    hc->Hc.Quirk.LinkNoOps++;
    XhciQuirkOutcome(&hc->Hc.Quirk, XHCI_QK_LINK_NOOP, XHCI_QK_OUT_DONE,
                     XHCI_QK_WHY_NONE);
}
