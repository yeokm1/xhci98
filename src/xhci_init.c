/*
 * xhci_init.c - the controller initialization sequence.
 *
 * Roadmap Phase 4 tasks 2, 3 and 5. usbport calls StartController once it has
 * mapped BAR0, connected the interrupt and placed the fixed common buffer; this
 * file turns that into a controller whose DCBAA, scratchpad, command ring and
 * event ring are programmed, which is running, and whose managed USB 2.0 ports
 * are powered - with every interrupt source still masked, because usbport calls
 * EnableInterrupts (task 6) the moment StartController returns success.
 *
 * The sequence is docs/usb-xhci-info/xhci-programming.md "Initialization Sequence" steps
 * 0-16, in that order, with three deliberate departures noted at their steps:
 * the INTx gate runs first, the capability set is derived twice, and the port
 * classification (step 12) runs before the common buffer is carved.
 *
 * Almost none of the decisions are made here. The window bounds come from
 * XhciDeriveHcInfo, the carve from XhciComputeLayout, the rings from
 * XhciRingInit / XhciEventRingInit, the capability walk from
 * XhciFindExtendedCap - all of it pure code with host tests
 * (docs/contributing/design/03-host-unit-tests.md). What is left here is ordering,
 * MMIO, and bounded waiting, which is exactly the part no host test can cover.
 *
 * Every refusal records ext->InitStep and ext->InitStatus before returning, so
 * a release build - which has no trace channel at all - still says where it
 * stopped and why to anything that can read the extension.
 *
 * C89 only. IRQL: PASSIVE_LEVEL throughout - usbport calls StartController from
 * its start-device path, which is where UsbPortWait is legal - **except with
 * `ext->InitBelowPassive` set, where the same functions run at DISPATCH_LEVEL**
 * (task 13-R.1). XhciRecoverController sets that bit and calls
 * XhciInitController from a DPC, so this file's waits take their stall-only
 * path and never call UsbPortWait; the failure exits that reach
 * XhciStopController / XhciQuiesceController are below PASSIVE for the same
 * reason. Their own headers in `xhci_hw.h` carry the same two-form tag.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/*
 * Bounded waits, all specification-derived.
 *
 *   HALT    spec 5.4.1 / 5.4.1.1: "The xHC shall halt within 16 ms. after
 *           software clears the Run/Stop bit"; 5.4.1.1 restates it as a hard
 *           bound "irrespective of any queued Transfer or Command Ring
 *           activity". Doubled, because the consequence of being 1 ms early is
 *           declaring healthy hardware dead.
 *   RESET   spec 4.22.2 gives no figure; 1 s is the same bound the Phase 0
 *           qualifier used and cleared on both fleet machines, and it covers
 *           CNR on a controller that reloads firmware state after reset.
 *   HANDOFF docs/usb-xhci-info/xhci-programming.md "BIOS handoff": ~1 s, then proceed with
 *           a warning - some firmware simply never answers and the controller
 *           still works.
 *   RUN     The spec states no bound for the other direction, because there is
 *           nothing to wait for: HCH "is a '0' whenever the Run/Stop (R/S) bit
 *           is a '1'" (5.4.2, p.363). So this is not a timing allowance, it is
 *           how long to keep asking before concluding the controller did not
 *           take the write at all. The halt bound is reused for symmetry.
 */
#define XHCI_HALT_TIMEOUT_MS        32UL
#define XHCI_RESET_TIMEOUT_MS       1000UL
#define XHCI_HANDOFF_TIMEOUT_MS     1000UL
#define XHCI_RUN_TIMEOUT_MS         XHCI_HALT_TIMEOUT_MS

/*
 * Attempts either interrupt-enable transition makes before giving up. Each one
 * is a full read, write and read back, because operands that looked healthy are
 * not evidence the write landed. Not a timing allowance and deliberately not a
 * stall: both transitions run at DISPATCH under usbport's MiniportSpinLock and
 * this driver's controller lock, where no bounded wait is permitted. It covers
 * only a glitch that clears on the next access; anything longer-lived is the
 * caller's escalation to answer.
 */
#define XHCI_INTERRUPT_WRITE_ATTEMPTS   3UL

/* ------------------------------------------------------------------ */
/* Common-buffer helpers                                               */
/* ------------------------------------------------------------------ */

/*
 * The common buffer is cached DMA memory (docs/contributing/design/04-controller-
 * common-buffer.md section 6), so every access to it goes through a volatile
 * pointer and ordering rests on the compiler not reordering the stores.
 * Nothing here publishes anything the controller is already looking at: all of
 * it happens before DCBAAP, CRCR and ERSTBA are written.
 */
volatile ULONG *XhciCommonAt(PXHCI_EXTENSION ext, ULONG offset)
{
    return (volatile ULONG *)(ext->StartVA + offset);
}

ULONG XhciCommonPA(PXHCI_EXTENSION ext, ULONG offset)
{
    return ext->StartPA + offset;
}


/*
 * Zero the whole carve before anything is written into it. usbport zeroes the
 * buffer when it allocates it, but a stop/start pair is not guaranteed to
 * produce a fresh allocation, and every structure below has fields whose
 * "unused" encoding is zero - a stale DCBAA entry is a device context pointer
 * the controller will follow. It is also where the scratchpad buffers get
 * cleared, which the spec asks for by name (4.20 step 4b: "software clears the
 * Scratchpad Buffer to 0").
 *
 * A hand-written loop rather than RtlZeroMemory for the reason given in
 * xhci_dispatch.c: the DDK routes that to either a compiler intrinsic or an
 * ntoskrnl import depending on build flags, and this driver's import list is a
 * decision, not a side effect.
 */
static VOID xhciZeroCommonBuffer(PXHCI_EXTENSION ext, ULONG bytes)
{
    volatile ULONG *words;
    ULONG i;
    ULONG count;

    words = XhciCommonAt(ext, 0);
    count = bytes / sizeof(ULONG);
    for (i = 0; i < count; i++) {
        words[i] = 0;
    }
}

/*
 * Store a 64-bit pointer field inside the common buffer - a DCBAA entry or a
 * scratchpad array entry. The high half is always zero (no 64-bit DMA); it is
 * written explicitly rather than left to the zeroing pass so that the two
 * halves of a pointer are set in one place.
 *
 * Low half first, matching XhciWrite64 and spec 5.1's rule for the registers.
 * Unlike a register, ordering here is not load-bearing: every structure this
 * writes into is still unpublished - DCBAAP and the scratchpad array pointer
 * are written afterwards - so the controller cannot be reading it yet. Phase 6
 * writes DCBAA entries while the controller *is* running, and will need the
 * ordering argument this function currently does not have to make.
 */
static VOID xhciStorePointer(PXHCI_EXTENSION ext, ULONG offset, ULONG low)
{
    volatile ULONG *entry;

    entry = XhciCommonAt(ext, offset);
    entry[0] = low;
    entry[1] = 0;
}

/* ------------------------------------------------------------------ */
/* USBCMD                                                              */
/* ------------------------------------------------------------------ */

/*
 * The only way USBCMD is ever written in this driver.
 *
 * `definedBits` names the defined controls the caller wants set; every other
 * defined control is cleared, and the reserved fields are carried back from the
 * read. That last part is not tidiness: bits 6:4, 12 and 31:17 are RsvdP, and
 * "software shall preserve the value read for writes to bits" (5.1.1, p.338).
 * A literal write clears them, which is a specification violation on any
 * controller that implements something there - and the whole point of RsvdP is
 * that a driver written today cannot know which controllers those are.
 *
 * Reading first also means a caller cannot accidentally leave INTE or HSEE set
 * from whatever state the register was in: naming what should be on is a
 * stronger contract than masking off what should be off, because a bit nobody
 * remembered to mask is off rather than on.
 *
 * Two forms, because a caller that has already *validated* a read must be able
 * to write from that exact value. The interrupt paths reject an all-ones USBCMD
 * before deriving anything from it; if the write then re-read the register, the
 * reserved half would come from an unvalidated second read and a window that
 * died in between would supply RsvdP as all ones. Bounded rather than
 * catastrophic - XHCI_USBCMD_RSVDP_MASK holds no R/S, HCRST, LHCRST, CSS or CRS
 * - but it is a specification violation for exactly the controllers RsvdP
 * exists to protect, and passing the validated value costs nothing.
 *
 * IRQL: any.
 */
static VOID xhciWriteUsbCmdFrom(PXHCI_EXTENSION ext,
                                ULONG usbcmd,
                                ULONG definedBits)
{
    XhciWriteOp(ext, XHCI_OP_USBCMD,
                (usbcmd & XHCI_USBCMD_RSVDP_MASK) |
                    (definedBits & XHCI_USBCMD_DEFINED_MASK));
}

/*
 * The convenience form, and it validates its own operand for the reason the
 * interrupt paths already did: an undecoding window reads all ones, and feeding
 * that through the RsvdP-preserving form above publishes every reserved bit as
 * one. Bounded rather than catastrophic here - XHCI_USBCMD_RSVDP_MASK carries
 * no R/S, HCRST, LHCRST, CSS or CRS - but it is a specification violation for
 * exactly the controllers RsvdP exists to protect, and these three callers are
 * the last ones in the driver that were making it.
 *
 * Returns 0 when no valid operand could be read, in which case **nothing was
 * written**. Every caller is a cold PASSIVE-level init step whose own bounded
 * wait would refuse a dead window a moment later; answering here means the
 * refusal names the register that actually failed.
 *
 * IRQL: any.
 */
static ULONG xhciWriteUsbCmd(PXHCI_EXTENSION ext, ULONG definedBits)
{
    ULONG usbcmd;
    ULONG attempt;

    for (attempt = 0; attempt < XHCI_INTERRUPT_WRITE_ATTEMPTS; attempt++) {
        usbcmd = XhciReadOp(ext, XHCI_OP_USBCMD);
        if (usbcmd != 0xFFFFFFFFUL) {
            xhciWriteUsbCmdFrom(ext, usbcmd, definedBits);
            return 1;
        }
    }
    XHCI_DBG_TEXT("USBCMD: no valid operand for a read-modify-write");
    return 0;
}

/*
 * IMAN, the same shape and for the same rule: bits 31:2 are RsvdP, so the two
 * sites that used to write a literal (the post-HCRST acknowledge and the
 * controller-state restore) were clearing a reserved field they are required to
 * carry back. `definedBits` names IP and IE; IP is RW1C, so naming it
 * acknowledges and leaving it out preserves.
 *
 * Returns 0 when no valid operand could be read, in which case nothing was
 * written.
 *
 * IRQL: any.
 */
static ULONG xhciWriteIman(PXHCI_EXTENSION ext, ULONG definedBits)
{
    ULONG iman;
    ULONG attempt;

    for (attempt = 0; attempt < XHCI_INTERRUPT_WRITE_ATTEMPTS; attempt++) {
        iman = XhciReadIr0(ext, XHCI_IR_IMAN);
        if (iman != 0xFFFFFFFFUL) {
            XhciWriteIr0(ext, XHCI_IR_IMAN,
                         (iman & XHCI_IMAN_RSVDP_MASK) |
                             (definedBits & XHCI_IMAN_DEFINED_MASK));
            return 1;
        }
    }
    XHCI_DBG_TEXT("IMAN: no valid operand for a read-modify-write");
    return 0;
}

/* ------------------------------------------------------------------ */
/* The other RsvdP registers                                           */
/* ------------------------------------------------------------------ */

/*
 * USBCMD and IMAN got the read-modify-write treatment above in Phase 4, one
 * register at a time and each for its own reason. The rule they were both
 * applications of is general - "RsvdP Reserved and Preserved: Reserved for
 * future RW implementations. Software shall preserve the value read for writes
 * to bits" (5.1.1, p.338) - and five more register families in this driver were
 * still composing or literalising a whole register over a reserved field:
 * CONFIG (31:10, p.370), DNCTRL (31:16, p.367), ERSTSZ (31:16, p.393), ERSTBA
 * (5:0, p.394) and CRCR (5:4, p.367).
 *
 * None of them was a read-modify-write, so none of them had anywhere to put the
 * preserved half; that is what these three helpers are. The shape is USBCMD's
 * exactly, because the hard-won part is not the masking:
 *
 *   **An all-ones read is not a legal operand.** An undecoding window answers
 *   0xFFFFFFFF, and feeding that through a preserving compose publishes every
 *   reserved bit as one - which is worse than the literal it replaced. Five
 *   Phase 4 task 7 rounds went into establishing that, and the answer is to
 *   refuse and let the caller decide, not to invent a fallback per site.
 *
 *   **A refusal writes nothing at all**, so a caller that fails the step has not
 *   half-programmed the register it was refusing over.
 *
 * `IMAN` is deliberately not routed through these. Its literal fallback in
 * src/xhci_evt.c fires only after every bounded read returned all ones, at
 * DIRQL, where the alternative is a shared level-triggered INTx left asserted;
 * docs/contributing/implementation-invariants.md records why no legal
 * preservation operand exists there. It is a stated exception, and it stays
 * isolated rather than being swept in with these.
 *
 * IRQL: any.
 */
static ULONG xhciReadRmwOperand(PXHCI_EXTENSION ext,
                                ULONG barOffset,
                                ULONG *operand)
{
    ULONG attempt;
    ULONG value;

    for (attempt = 0; attempt < XHCI_INTERRUPT_WRITE_ATTEMPTS; attempt++) {
        value = XhciRead32(ext, barOffset);
        if (value != 0xFFFFFFFFUL) {
            *operand = value;
            return 1;
        }
    }
    return 0;
}

/*
 * A 32-bit register: carry `rsvdpMask` back from the read, write `definedBits`
 * over everything else. The caller masks `definedBits` to the register's defined
 * half, so that a bit outside it cannot arrive here and be written anyway.
 */
static ULONG xhciWriteRsvdP(PXHCI_EXTENSION ext,
                            ULONG barOffset,
                            ULONG rsvdpMask,
                            ULONG definedBits)
{
    ULONG operand;

    if (!xhciReadRmwOperand(ext, barOffset, &operand)) {
        return 0;
    }
    XhciWrite32(ext, barOffset, (operand & rsvdpMask) | definedBits);
    return 1;
}

/*
 * The 64-bit form - ERSTBA and CRCR. Only the low DWORD has a reserved field to
 * preserve; the high DWORD is address bits this driver always writes as zero.
 * Low half first, second half after, for the reason XhciWrite64 gives (spec 5.1,
 * p.337) - which is why this does not simply call it: the whole point is that
 * the low half is composed rather than supplied.
 */
static ULONG xhciWrite64RsvdP(PXHCI_EXTENSION ext,
                              ULONG barOffset,
                              ULONG rsvdpMask,
                              ULONG low)
{
    ULONG operand;

    if (!xhciReadRmwOperand(ext, barOffset, &operand)) {
        return 0;
    }
    XhciWrite32(ext, barOffset, (operand & rsvdpMask) | low);
    XhciWrite32(ext, barOffset + 4UL, 0);
    return 1;
}

/* CONFIG. `maxSlotsEn` is the only defined field this driver sets; U3E and CIE
 * stay clear, and 31:10 comes back from the read. */
static ULONG xhciWriteConfig(PXHCI_EXTENSION ext, ULONG maxSlotsEn)
{
    if (!xhciWriteRsvdP(ext, ext->HcInfo.OperationalOffset + XHCI_OP_CONFIG,
                        XHCI_CONFIG_RSVDP_MASK,
                        maxSlotsEn & XHCI_CONFIG_MAXSLOTSEN_MASK)) {
        XHCI_DBG_TEXT("CONFIG: no valid operand for a read-modify-write");
        return 0;
    }
    return 1;
}

/* DNCTRL. This driver enables no Device Notification type, so the defined half
 * is always zero and the whole call is about 31:16. */
static ULONG xhciWriteDnctrl(PXHCI_EXTENSION ext, ULONG notificationEnables)
{
    if (!xhciWriteRsvdP(ext, ext->HcInfo.OperationalOffset + XHCI_OP_DNCTRL,
                        XHCI_DNCTRL_RSVDP_MASK,
                        notificationEnables & XHCI_DNCTRL_DEFINED_MASK)) {
        XHCI_DBG_TEXT("DNCTRL: no valid operand for a read-modify-write");
        return 0;
    }
    return 1;
}

/* ERSTSZ of interrupter 0. */
static ULONG xhciWriteErstsz(PXHCI_EXTENSION ext, ULONG entries)
{
    if (!xhciWriteRsvdP(ext,
                        ext->HcInfo.RuntimeOffset + XHCI_RT_IR0 +
                            XHCI_IR_ERSTSZ,
                        XHCI_ERSTSZ_RSVDP_MASK,
                        entries & XHCI_ERSTSZ_DEFINED_MASK)) {
        XHCI_DBG_TEXT("ERSTSZ: no valid operand for a read-modify-write");
        return 0;
    }
    return 1;
}

/*
 * ERSTBA of interrupter 0. The table is carved 64-byte aligned, so masking the
 * address to 31:6 discards nothing - it is here so that an address that somehow
 * carried low bits becomes a refused-looking write rather than an address the
 * controller silently rounds.
 */
static ULONG xhciWriteErstba(PXHCI_EXTENSION ext, ULONG tablePA)
{
    if (!xhciWrite64RsvdP(ext,
                          ext->HcInfo.RuntimeOffset + XHCI_RT_IR0 +
                              XHCI_IR_ERSTBA,
                          XHCI_ERSTBA_RSVDP_MASK,
                          tablePA & XHCI_ERSTBA_ADDR_MASK)) {
        XHCI_DBG_TEXT("ERSTBA: no valid operand for a read-modify-write");
        return 0;
    }
    return 1;
}

ULONG XhciWriteCrcr(PXHCI_EXTENSION ext,
                    ULONG pointerLow,
                    ULONG definedBits,
                    ULONG *crcrRead)
{
    ULONG operand;

    if (crcrRead != NULL) {
        *crcrRead = 0xFFFFFFFFUL;
    }
    if (!xhciReadRmwOperand(ext,
                            ext->HcInfo.OperationalOffset + XHCI_OP_CRCR,
                            &operand)) {
        XHCI_DBG_TEXT("CRCR: no valid operand for a read-modify-write");
        return 0;
    }
    if (crcrRead != NULL) {
        *crcrRead = operand;
    }
    XhciWrite32(ext, ext->HcInfo.OperationalOffset + XHCI_OP_CRCR,
                (operand & XHCI_CRCR_RSVDP_MASK) |
                    (pointerLow & XHCI_CRCR_PTR_MASK) |
                    (definedBits & XHCI_CRCR_DEFINED_MASK));
    XhciWrite32(ext, ext->HcInfo.OperationalOffset + XHCI_OP_CRCR + 4UL, 0);
    return 1;
}

ULONG XhciWriteCrcrAbort(PXHCI_EXTENSION ext, ULONG *crcrRead)
{
    ULONG operand;

    if (crcrRead != NULL) {
        *crcrRead = 0xFFFFFFFFUL;
    }
    if (!xhciReadRmwOperand(ext,
                            ext->HcInfo.OperationalOffset + XHCI_OP_CRCR,
                            &operand)) {
        XHCI_DBG_TEXT("CRCR: no valid operand for the abort write");
        return 0;
    }
    if (crcrRead != NULL) {
        *crcrRead = operand;
    }
    /*
     * The gate, and it is the specification's own division of this register: CA
     * does something only while CRR is '1' (p.367), while the pointer half is
     * latched only while CRR is '0' (p.368). Writing CA on a stopped ring is
     * therefore not a harmless no-op - it is a pointer write of whatever the
     * composition put in 31:6, which here is zero.
     */
    if ((operand & XHCI_CRCR_CRR) == 0) {
        return 0;
    }
    /*
     * Low DWORD only. The high half is nothing but address bits 63:32, this
     * write sets no address, and on a running command ring an address write is
     * ignored anyway.
     */
    XhciWrite32(ext, ext->HcInfo.OperationalOffset + XHCI_OP_CRCR,
                (operand & XHCI_CRCR_RSVDP_MASK) | XHCI_CRCR_CA);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Steps                                                               */
/* ------------------------------------------------------------------ */

/*
 * Read the whole capability register set and decode it.
 *
 * Called twice in the sequence: once before the BIOS handoff, which needs
 * HCCPARAMS1.xECP to find the Legacy Support capability, and once after the
 * reset, because the roadmap's step 4 ("re-check HCIVERSION") is really the
 * question "is this still the same controller". Re-deriving everything instead
 * of just the version costs six register reads and answers it completely.
 *
 * Exported (rather than static, as it began) because ResumeController asks the
 * same question of the same registers, and asking it a second way would mean
 * two places that could disagree about what "the same controller" means.
 */
ULONG XhciDeriveControllerInfo(PXHCI_EXTENSION ext, PXHCI_HC_INFO info)
{
    ULONG capDword0;
    ULONG hcsparams1;
    ULONG hcsparams2;
    ULONG hccparams1;
    ULONG hccparams2;
    ULONG dboff;
    ULONG rtsoff;

    capDword0 = XhciRead32(ext, XHCI_CAP_CAPLENGTH);
    hcsparams1 = XhciRead32(ext, XHCI_CAP_HCSPARAMS1);
    hcsparams2 = XhciRead32(ext, XHCI_CAP_HCSPARAMS2);
    hccparams1 = XhciRead32(ext, XHCI_CAP_HCCPARAMS1);
    dboff = XhciRead32(ext, XHCI_CAP_DBOFF);
    rtsoff = XhciRead32(ext, XHCI_CAP_RTSOFF);

    /*
     * The seventh read is conditional, and the condition is the *mapping*
     * rather than the version: this is the one place in the driver that reads
     * outside XHCI_CAP_REGISTERS_BYTES, so nothing above has proved BAR0 + 1Ch
     * is inside the window usbport handed over. `XhciRead32` is unbounded by
     * design (the bounds were established once, by the derivation), which is
     * exactly why the bound has to be here. Whether the value *means* anything
     * is the derivation's decision, not this one's.
     */
    hccparams2 = 0;
    if (ext->IoSpaceLength >= XHCI_CAP_HCCPARAMS2_BYTES) {
        hccparams2 = XhciRead32(ext, XHCI_CAP_HCCPARAMS2);
    }

    return XhciDeriveHcInfo(capDword0, hcsparams1, hcsparams2, hccparams1,
                            hccparams2, dboff, rtsoff, ext->IoSpaceLength,
                            info);
}

/*
 * BIOS -> OS ownership handoff (spec 4.22.1 / 7.1, and the procedure in
 * docs/usb-xhci-info/xhci-programming.md "BIOS handoff").
 *
 * Three of the four outcomes are not failures. A controller with no Legacy
 * Support capability has nothing to hand over; firmware that ignores the
 * semaphore is a documented real-world case where the controller still works;
 * and a successful claim is the normal path. What the SMI disable buys is the
 * difference between the "never acknowledged" case being harmless and the
 * firmware continuing to intercept USB events behind the driver's back.
 *
 * The fourth outcome is a **malformed capability chain**, and that one is
 * fatal. "No such capability" (XHCI_CAPS_NO_LIST, XHCI_CAPS_NOT_FOUND) is a
 * statement about the controller; a chain that leaves the BAR, does not
 * terminate, or names a capability whose body is outside the mapping is a
 * statement that this driver's reading of the controller's memory map is
 * wrong - and everything after this point is built on that reading. The port
 * classification refuses the same conditions and runs first, so by here the
 * chain has already been walked once cleanly; what this finder can still
 * refuse that the parser could not is a USBLEGSUP whose *second* DWORD lies
 * outside the mapping, since only this path writes it.
 *
 * IRQL: PASSIVE_LEVEL (the semaphore wait).
 */
static ULONG xhciBiosHandoff(PXHCI_EXTENSION ext)
{
    ULONG status;
    ULONG offset;
    ULONG dw0;
    ULONG dw1;

    ext->LegacyCapOffset = 0;

    offset = 0;
    status = XhciFindExtendedCap(XhciBarReader, ext, ext->HcInfo.XecpDwords,
                                 ext->IoSpaceLength, XHCI_XECP_ID_LEGACY,
                                 XHCI_USBLEGSUP_BYTES, &offset);
    if (status == XHCI_CAPS_NO_LIST || status == XHCI_CAPS_NOT_FOUND) {
        XHCI_DBG_VALUE("handoff: no legacy capability, status", status);
        return XHCI_CAPS_OK;
    }
    if (status != XHCI_CAPS_OK) {
        XHCI_DBG_VALUE("handoff: malformed capability chain, status", status);
        return status;
    }

    ext->LegacyCapOffset = offset;
    XHCI_DBG_VALUE("handoff: USBLEGSUP at BAR0 offset", offset);

    dw0 = XhciRead32(ext, offset);
    XHCI_DBG_VALUE("handoff: USBLEGSUP before", dw0);

    /*
     * Both writes below carry bits back from a read, so both need the operand
     * validated first: all ones is a window that has stopped decoding, and
     * writing it back sets every reserved bit and, in DW1, every SMI enable
     * this step exists to clear. There is nothing to hand off from a controller
     * that is not answering, so the whole step declines.
     */
    if (dw0 == 0xFFFFFFFFUL) {
        XHCI_DBG_TEXT("handoff: USBLEGSUP is not decoding");
        return XHCI_CAPS_NOT_DECODING;
    }

    if ((dw0 & XHCI_USBLEGSUP_BIOS_OWNED) != 0 ||
        (dw0 & XHCI_USBLEGSUP_OS_OWNED) == 0) {
        /* Bits 15:0 of this DWORD are the capability ID and NEXT pointer and
         * are read-only, so writing the read value back is safe; bit 16 is
         * read-only from the OS side too. Only bit 24 is ours. */
        XhciWrite32(ext, offset, dw0 | XHCI_USBLEGSUP_OS_OWNED);

        if (!XhciWaitForBits(ext, offset, XHCI_USBLEGSUP_BIOS_OWNED, 0,
                             XHCI_HANDOFF_TIMEOUT_MS, &dw0)) {
            XHCI_DBG_VALUE("handoff: BIOS never released, USBLEGSUP", dw0);
            /*
             * Task 11-V.9, first tier. **This is the one handoff outcome worth
             * a record of its own**: the driver went on anyway - it has no
             * other option, since refusing would leave the machine with no USB
             * at all - so every later symptom on this controller is one a
             * second owner could have caused, and nothing else in the log would
             * say so.
             */
            XhciLogNote(ext, "handoff.bios.held", dw0);
        }
    }

    /*
     * Disable firmware SMI generation whether or not the semaphore was
     * answered: DW1's enable bits are what make the firmware re-enter on USB
     * events, and leaving them set is how a handoff that "succeeded" still
     * ends with two owners. Clear every enable in 15:0 and acknowledge the
     * RW1C status bits in 31:29 (docs/usb-xhci-info/xhci-data-structures.md section 6).
     *
     * This DWORD is why the search above had to prove eight bytes rather than
     * four: it is written, not just read.
     */
    dw1 = XhciRead32(ext, offset + XHCI_USBLEGCTLSTS_OFFSET);
    if (dw1 == 0xFFFFFFFFUL) {
        XHCI_DBG_TEXT("handoff: USBLEGCTLSTS is not decoding");
        return XHCI_CAPS_NOT_DECODING;
    }
    XhciWrite32(ext, offset + XHCI_USBLEGCTLSTS_OFFSET,
                (dw1 & ~XHCI_USBLEGCTLSTS_SMI_ENABLES) |
                    XHCI_USBLEGCTLSTS_SMI_STATUS);

    XHCI_DBG_VALUE("handoff: USBLEGSUP after", XhciRead32(ext, offset));
    XHCI_DBG_VALUE("handoff: USBLEGCTLSTS after",
                   XhciRead32(ext, offset + XHCI_USBLEGCTLSTS_OFFSET));
    return XHCI_CAPS_OK;
}

/*
 * Record a refusal against the map it is about, and hand it back to the caller.
 * Only the post-reset pass owns ext->PortMapStatus; a preflight refusal is
 * reported through ext->InitStep / ext->InitStatus like every other preflight
 * step, and leaves the status saying "no authoritative map" - which is the
 * truth at that point.
 */
static ULONG xhciPortMapRefused(PXHCI_EXTENSION ext,
                                ULONG afterReset,
                                ULONG status)
{
    if (afterReset) {
        ext->PortMapStatus = status;
    }
    return status;
}

/*
 * Walk the extended-capability chain to classify every logical port (roadmap
 * Phase 4 task 3). The walk, the classification and the retained PSI tables are
 * XhciParseExtendedCaps in src/xhci_caps.c, which has host tests; what belongs
 * here is the reader, the refusals, and the trace.
 *
 * Called twice, and the two calls answer different questions.
 *
 * The **preflight** call runs before the handoff claims the controller and
 * before the halt and reset, so a topology this driver cannot serve is
 * declined while the controller is still exactly as its firmware left it.
 * Refusing after the reset would mean turning a working controller into a dead
 * one with a check that reads nothing but the capability chain - the same
 * argument the INTx gate is placed on.
 *
 * The **post-reset** call (afterReset nonzero) is the one that writes
 * ext->PortMap. It has to exist for a reason the preflight one cannot cover:
 * everything downstream - Phase 5's port shadow, the root-hub port count, every
 * Enable Slot's Slot Type - reads that structure, and a pre-reset reading of a
 * controller's memory map is exactly the stale data the post-reset capability
 * re-derivation exists to rule out. So the second parse is authoritative,
 * re-runs every refusal against what the controller says *now*, and is
 * additionally required to agree with the first, field for field: the Supported
 * Protocol capabilities are read-only hardware description, and HCRST does not
 * change them, so any difference is an anomaly rather than an update.
 *
 * The preflight parse therefore lands in its own structure and leaves
 * ext->PortMap untouched. ext->PortMapStatus certifies ext->PortMap, and the
 * two must not be able to disagree: if a preflight success recorded
 * XHCI_CAPS_OK there and the start then failed at the handoff, halt or reset,
 * the pair would read as a valid map that no pass had written this start.
 *
 * Every nonzero parser status refuses the controller, including
 * XHCI_CAPS_NO_LIST - and that is the one place this differs from the handoff,
 * which treats "no such capability" as nothing to claim. The two questions are
 * not the same. A controller with no Legacy Support capability has no firmware
 * to take it from; a controller with no Supported Protocol capability has not
 * said which of its ports carry USB 2.0, and this driver's whole port strategy
 * (AGENTS.md) is a statement about exactly that. Guessing would mean powering
 * SuperSpeed ports.
 *
 * IRQL: PASSIVE_LEVEL (reached only from XhciInitController).
 */
static ULONG xhciBuildPortMap(PXHCI_EXTENSION ext, ULONG afterReset)
{
    PXHCI_PORT_MAP map;
    ULONG status;
    ULONG i;
    ULONG counts[XHCI_PORT_CLASS_COUNT];

    map = afterReset ? &ext->PortMap : &ext->PreflightPortMap;

    status = XhciParseExtendedCaps(XhciBarReader, ext, ext->HcInfo.XecpDwords,
                                   ext->IoSpaceLength, ext->HcInfo.MaxPorts,
                                   map);
    if (status != XHCI_CAPS_OK) {
        return xhciPortMapRefused(ext, afterReset, status);
    }

    /*
     * A controller whose ports are all USB 3.x is one this driver cannot serve
     * at all - SuperSpeed is out of scope, so there is nothing left to manage.
     * The parser is right to accept such a chain; the refusal is this driver's,
     * which is why it is raised here. It also keeps Phase 5 out of a documented
     * trap: usbport's root hub creation sizes its removable/power masks from
     * the reported port count and asks for roughly 1 GB of nonpaged pool at
     * zero (docs/usb-xhci-info/usbport-miniport-abi.md section 9; roadmap Phase 5 task 1).
     */
    if (map->ManagedPortCount == 0) {
        XHCI_DBG_TEXT("port map: no USB 2.0 protocol port to manage - "
                      "refusing");
        return xhciPortMapRefused(ext, afterReset, XHCI_CAPS_NO_MANAGED_PORTS);
    }

    if (afterReset) {
        /*
         * Two ways the chain can have changed under the driver, and they are
         * separated because they say different things. USBLEGSUP moving means
         * the capability this driver *wrote to* is not where it wrote; any
         * other difference means the topology it is about to act on is not the
         * one it validated. The capability re-derivation ahead of this already
         * compared xECP, so what is left is everything hanging off it.
         */
        if (map->LegacySupportOffset != ext->LegacyCapOffset) {
            XHCI_DBG_VALUE("port map: USBLEGSUP moved, now at",
                           map->LegacySupportOffset);
            return xhciPortMapRefused(ext, 1, XHCI_CAPS_LEGACY_MOVED);
        }
        /*
         * Every field, not a digest of them: this comparison is what says the
         * topology is the one the preflight validated, and a 32-bit summary of
         * a kilobyte can only say it might be (XhciPortMapEqual). The trace
         * gives the preflight totals rather than the field that differed -
         * naming that would mean a second walk, and a traced build (`qemu`) has
         * already printed the preflight parse group by group at the earlier pass.
         */
        if (!XhciPortMapEqual(map, &ext->PreflightPortMap)) {
            XHCI_DBG_VALUE("port map: topology changed across reset, groups "
                           "were", ext->PreflightPortMap.ProtocolCount);
            XHCI_DBG_VALUE("port map: managed ports were",
                           ext->PreflightPortMap.ManagedPortCount);
            return xhciPortMapRefused(ext, 1, XHCI_CAPS_TOPOLOGY_CHANGED);
        }
        ext->PortMapStatus = XHCI_CAPS_OK;
    }

    /*
     * Traced as per-group lines and per-class totals rather than one line per
     * port: MaxPorts is eight bits, and 255 lines per start is the kind of
     * volume the Phase 3 trace-noise defect was about. The totals are what a
     * wrong classification shows up in anyway - a companion pairing that missed
     * moves ports between two of these counters.
     */
    for (i = 0; i < XHCI_PORT_CLASS_COUNT; i++) {
        counts[i] = 0;
    }
    for (i = 0; i < map->PortCount; i++) {
        /* The parser only ever writes XHCI_PORT_CLASS_*, so the bound is
         * unreachable - but this indexes an array with data read out of a
         * device, and that is not a place to rely on an argument. */
        if (map->Class[i] < XHCI_PORT_CLASS_COUNT) {
            counts[map->Class[i]]++;
        }
    }

    XHCI_DBG_VALUE("port map: pass (0 = preflight, 1 = post-reset)",
                   afterReset);
    XHCI_DBG_VALUE("port map: protocol groups", map->ProtocolCount);
    for (i = 0; i < map->ProtocolCount; i++) {
        const XHCI_PROTOCOL *proto;

        proto = &map->Protocols[i];
        XHCI_DBG_VALUE("  group major revision", proto->Major);
        XHCI_DBG_VALUE("  first port", proto->PortOffset);
        XHCI_DBG_VALUE("  port count", proto->PortCount);
        XHCI_DBG_VALUE("  slot type", proto->SlotType);
        XHCI_DBG_VALUE("  PSI entries", proto->PsiCount);
    }
    XHCI_DBG_VALUE("port map: unclaimed ports", counts[XHCI_PORT_CLASS_NONE]);
    XHCI_DBG_VALUE("port map: USB2-only ports",
                   counts[XHCI_PORT_CLASS_USB2_ONLY]);
    XHCI_DBG_VALUE("port map: USB2 companion ports",
                   counts[XHCI_PORT_CLASS_USB2_COMPANION]);
    XHCI_DBG_VALUE("port map: USB3 companion ports",
                   counts[XHCI_PORT_CLASS_USB3_COMPANION]);
    XHCI_DBG_VALUE("port map: USB3 orphan ports",
                   counts[XHCI_PORT_CLASS_USB3_ORPHAN]);
    XHCI_DBG_VALUE("port map: managed ports", map->ManagedPortCount);
    XHCI_DBG_VALUE("port map: debug capability offset",
                   map->DebugCapabilityOffset);

    /*
     * Task 11-V.9's first producer tier: the port map, into the log, on the
     * authoritative pass only.
     *
     * **Per managed port and not per port**, which is the one place this
     * departs from the task's wording, and for the reason the trace above
     * already records: `MaxPorts` is eight bits, so a per-port record is up to
     * 255 of them - a quarter of the ring on a controller nobody has ever
     * plugged anything into. The ports this driver does not manage are ports it
     * leaves unpowered by policy, and the group lines below say how many of
     * each class there were, so nothing about the classification becomes
     * unreadable. What a reader needs per port is the thing that changes
     * between machines: which port number a device will arrive on and what
     * protocol it speaks.
     */
    if (afterReset) {
        XhciLogNote(ext, "map.groups", map->ProtocolCount);
        XhciLogNote(ext, "map.ports", map->PortCount);
        XhciLogNote(ext, "map.managed", map->ManagedPortCount);
        XhciLogNote(ext, "map.usb2only", counts[XHCI_PORT_CLASS_USB2_ONLY]);
        XhciLogNote(ext, "map.usb2companion",
                    counts[XHCI_PORT_CLASS_USB2_COMPANION]);
        XhciLogNote(ext, "map.usb3", counts[XHCI_PORT_CLASS_USB3_COMPANION] +
                                         counts[XHCI_PORT_CLASS_USB3_ORPHAN]);
        for (i = 0; i < map->PortCount; i++) {
            if (map->Class[i] == XHCI_PORT_CLASS_USB2_ONLY ||
                map->Class[i] == XHCI_PORT_CLASS_USB2_COMPANION) {
                /* port << 8 | class - the port number is one-based, as every
                 * PORTSC reference in this driver is. */
                XhciLogNote(ext, "map.port",
                            ((i + 1) << 8) | (ULONG)map->Class[i]);
            }
        }
    }

    return XHCI_CAPS_OK;
}

/*
 * Halt, then reset. Both are required before the operational registers below
 * mean anything: the spec leaves HCRST undefined on a running controller, and
 * a controller inherited from firmware in an arbitrary state is exactly what
 * this driver starts from.
 */
static ULONG xhciHalt(PXHCI_EXTENSION ext)
{
    ULONG usbsts;
    ULONG halted;

    usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
    /*
     * All ones is a window that is not decoding, and it carries HCH set. Read
     * as a status report it says "already halted" and retires XHCI_EXT_FLAG_-
     * RUNNING on a controller nothing has been shown to have stopped - a
     * bookkeeping lie in the one direction that matters, since the flag is what
     * tells XhciQuiesceController there is something left to stop.
     */
    if (usbsts == 0xFFFFFFFFUL) {
        XHCI_DBG_TEXT("halt: USBSTS is not decoding");
        return 0;
    }
    if ((usbsts & XHCI_USBSTS_HCH) != 0) {
        (VOID)XhciControllerUpdateFlags(
            ext, XHCI_EXT_FLAG_RUNNING | XHCI_EXT_FLAG_HW_RUNNING, 0);
        return 1;
    }

    /*
     * Every defined control off, not just R/S. This driver is about to
     * reprogram every structure the controller points at, and an interrupt or a
     * host-system-error response in the middle of that - from a configuration
     * set up by firmware, aimed at memory this driver does not own - has no
     * useful outcome.
     */
    if (!xhciWriteUsbCmd(ext, 0)) {
        return 0;
    }

    halted = XhciWaitForBits(ext,
                             ext->HcInfo.OperationalOffset + XHCI_OP_USBSTS,
                             XHCI_USBSTS_HCH, XHCI_USBSTS_HCH,
                             XHCI_HALT_TIMEOUT_MS, &usbsts);
    if (halted) {
        /*
         * Retired only on proof, not on the write. Together the two bits mean
         * "the xHC may be executing", and a controller that took the R/S write
         * but never set HCHalted is exactly the case where that is still true -
         * clearing them there would tell XhciQuiesceController there is nothing
         * left to stop.
         *
         * Both, because a proof of HCHalted is a proof about the hardware
         * whoever started it - the same reason the quiesce retires both.
         * (XHCI_EXT_FLAG_RUNNING on its own is narrower: it records this
         * driver's R/S ownership, which is what xhciUnpowerPorts reads.)
         */
        (VOID)XhciControllerUpdateFlags(
            ext, XHCI_EXT_FLAG_RUNNING | XHCI_EXT_FLAG_HW_RUNNING, 0);
    }
    return halted;
}

static ULONG xhciReset(PXHCI_EXTENSION ext)
{
    ULONG value;

    /* HCRST alone. Every other *defined* bit is reset by the operation anyway,
     * and the read value came from firmware - but the write still goes through
     * xhciWriteUsbCmd, because RsvdP is a rule about the write rather than
     * about what the register ends up holding. */
    if (!xhciWriteUsbCmd(ext, XHCI_USBCMD_HCRST)) {
        return 0;
    }

    if (!XhciWaitForBits(ext,
                         ext->HcInfo.OperationalOffset + XHCI_OP_USBCMD,
                         XHCI_USBCMD_HCRST, 0,
                         XHCI_RESET_TIMEOUT_MS, &value)) {
        XHCI_DBG_VALUE("reset: HCRST never cleared, USBCMD", value);
        return 0;
    }

    /*
     * CNR is the separate half of the same question, and it gates more than
     * this function's next line: "Software shall not write any Doorbell or
     * Operational register of the xHC, other than the USBSTS register, until
     * CNR = 0" (spec 5.4.2). CONFIG, DCBAAP and CRCR are all operational, so
     * every remaining write in the sequence depends on this wait.
     */
    if (!XhciWaitForBits(ext,
                         ext->HcInfo.OperationalOffset + XHCI_OP_USBSTS,
                         XHCI_USBSTS_CNR, 0,
                         XHCI_RESET_TIMEOUT_MS, &value)) {
        XHCI_DBG_VALUE("reset: CNR never cleared, USBSTS", value);
        return 0;
    }

    /* HCRST completing is a proof about the enables, not just about the reset:
     * it clears USBCMD and every interrupter register, so both are down without
     * this driver having written either. The ISR's decline gates may rely on it
     * for the rest of the sequence, which programs the controller with the
     * enables deliberately masked. */
    ext->InterruptDeliverySuppressed = 1;
    return 1;
}

/*
 * DCBAA, the scratchpad array and its pages, CONFIG.MaxSlotsEn, DCBAAP.
 *
 * Ordering inside the step: every byte the controller could follow is in place
 * before the register that points at it is written, and MaxSlotsEn is set
 * before DCBAAP so the array is never larger than the enabled slot count that
 * indexes it.
 */
static ULONG xhciProgramDcbaa(PXHCI_EXTENSION ext)
{
    const XHCI_HC_LAYOUT *layout;
    ULONG status;
    ULONG i;
    ULONG offset;

    layout = &ext->Layout;

    for (i = 0; i < layout->ScratchpadCount; i++) {
        /*
         * Unreachable - the loop bound and the accessor's bound are the same
         * field - but the failure it would produce is not one to leave to an
         * argument. Skipping an entry leaves the array's zero in place, and a
         * zero scratchpad pointer is an address the controller will DMA to.
         */
        status = XhciScratchpadPageOffset(layout, i, &offset);
        if (status != XHCI_LAYOUT_OK) {
            return status;
        }
        xhciStorePointer(ext,
                         layout->ScratchpadArrayOffset +
                             i * XHCI_SCRATCHPAD_ENTRY_BYTES,
                         XhciCommonPA(ext, offset));
    }

    /*
     * DCBAA entry 0 is the scratchpad buffer array, or zero when the
     * controller asks for no scratchpad at all - "if Max Scratchpad Buffers =
     * 0 then the first entry is reserved and shall be cleared to 0" (spec
     * 6.1). Entries 1..MaxSlotsEn stay zero until a slot is enabled in
     * Phase 6.
     */
    if (layout->ScratchpadCount > 0) {
        xhciStorePointer(ext, layout->DcbaaOffset,
                         XhciCommonPA(ext, layout->ScratchpadArrayOffset));
    }

    /*
     * DCBAAP's own low six bits are **RsvdZ**, not RsvdP (Table 5-25, p.369), so
     * it stays a plain composed write: a zero there is what the specification
     * asks for rather than something to carry back. That distinction is the
     * whole reason this register is not in the helper block above.
     */
    if (!xhciWriteConfig(ext, layout->MaxSlotsEn)) {
        return XHCI_INIT_NO_RMW_OPERAND;
    }
    XhciWrite64(ext, ext->HcInfo.OperationalOffset + XHCI_OP_DCBAAP,
                XhciCommonPA(ext, layout->DcbaaOffset));
    return XHCI_LAYOUT_OK;
}

static ULONG xhciProgramCommandRing(PXHCI_EXTENSION ext)
{
    const XHCI_HC_LAYOUT *layout;
    ULONG status;
    ULONG crcr;

    layout = &ext->Layout;

    /* CRCR may only be written while the ring is not running (spec 5.4.5). It
     * cannot be after a completed reset - which is the point of checking: if
     * it is, the reset did not do what the wait above said it did. */
    crcr = XhciReadOp(ext, XHCI_OP_CRCR);
    if ((crcr & XHCI_CRCR_CRR) != 0) {
        XHCI_DBG_VALUE("command ring: CRR set after reset, CRCR", crcr);
        return XHCI_RING_BAD_PARAM;
    }

    status = XhciRingInit(&ext->CommandRing,
                          (volatile XHCI_TRB *)
                              XhciCommonAt(ext, layout->CommandRingOffset),
                          XhciCommonPA(ext, layout->CommandRingOffset),
                          layout->CommandRingTrbs,
                          XHCI_RING_KIND_COMMAND);
    if (status != XHCI_RING_OK) {
        return status;
    }

    /*
     * RCS must be the ring's own producer cycle rather than a literal 1: the
     * two are the same at init and there is no reason for a second place to
     * decide it.
     *
     * Through XhciWriteCrcr rather than XhciWrite64 because bits 5:4 are RsvdP
     * (Table 5-24, p.367) and this composed write used to clear them. That the
     * read answers zero for RCS, CS, CA and the pointer alike is what made the
     * old form look complete - the enumeration simply never reached 5:4.
     */
    if (!XhciWriteCrcr(ext,
                       ext->CommandRing.BasePA,
                       ext->CommandRing.Cycle ? XHCI_CRCR_RCS : 0,
                       NULL)) {
        return XHCI_INIT_NO_RMW_OPERAND;
    }
    return XHCI_RING_OK;
}

/*
 * The event ring, and the one register-order rule in this whole file that is
 * not "point at it before you enable it": ERSTSZ and ERDP are programmed
 * before ERSTBA, because writing ERSTBA is what latches the table and starts
 * the Event Ring State Machine (spec 4.9.4).
 */
static ULONG xhciProgramEventRing(PXHCI_EXTENSION ext)
{
    const XHCI_HC_LAYOUT *layout;
    volatile ULONG *erst;
    ULONG status;
    ULONG segmentPA;

    layout = &ext->Layout;

    status = XhciEventRingInit(&ext->EventRing,
                               (volatile XHCI_TRB *)
                                   XhciCommonAt(ext, layout->EventRingOffset),
                               XhciCommonPA(ext, layout->EventRingOffset),
                               layout->EventRingTrbs);
    if (status != XHCI_RING_OK) {
        return status;
    }

    segmentPA = ext->EventRing.BasePA;

    erst = XhciCommonAt(ext, layout->ErstOffset);
    erst[0] = segmentPA;                    /* Ring Segment Base Address lo */
    erst[1] = 0;                            /* hi - no 64-bit DMA           */
    erst[2] = layout->EventRingTrbs;        /* Ring Segment Size, bits 15:0 */
    erst[3] = 0;                            /* RsvdZ                        */

    /*
     * Acknowledge any interrupt the interrupter is already holding, with IE
     * left clear. IP is RW1C, so naming it is a write of the bit rather than of
     * the register's read value - but bits 31:2 are RsvdP and this used to be a
     * literal, which clears them. That HCRST has just run is not a licence:
     * a controller implementing something reserved there sets it at reset too.
     *
     * A refused operand does not fail the step. Every enable this driver later
     * asks for goes through XhciUnmaskInterrupts/XhciRearmInterrupter, which
     * validate, prove by read-back and escalate; and HCRST has already left IE
     * clear, so an unacknowledged IP behind it generates nothing. Refusing here
     * would turn a single flaky read into a failed start.
     */
    if (!xhciWriteIman(ext, XHCI_IMAN_IP)) {
        XHCI_DBG_TEXT("event ring: IMAN acknowledge declined, enables are "
                      "still down from HCRST");
    }

    /*
     * ERSTSZ carries RsvdP in 31:16 and ERSTBA in 5:0, so both go through the
     * preserving writes; ERDP has no reserved field at all (Table 5-42, p.394)
     * and stays a plain XhciWrite64. A refusal here **does** fail the step,
     * unlike the IMAN acknowledge above: these two are the registers that tell
     * the controller where its event ring is, and a start that could not write
     * them has not programmed an interrupter.
     */
    if (!xhciWriteErstsz(ext, layout->ErstEntries)) {
        return XHCI_INIT_NO_RMW_OPERAND;
    }
    XhciWrite64(ext,
                ext->HcInfo.RuntimeOffset + XHCI_RT_IR0 + XHCI_IR_ERDP,
                XhciEventRingErdpValue(&ext->EventRing, 0));
    if (!xhciWriteErstba(ext, XhciCommonPA(ext, layout->ErstOffset))) {
        return XHCI_INIT_NO_RMW_OPERAND;
    }

    return XHCI_RING_OK;
}

/* ------------------------------------------------------------------ */
/* Running, and port power                                             */
/* ------------------------------------------------------------------ */

/*
 * Turn the controller on.
 *
 * Everything the spec asks to be in place first is: "these operations shall be
 * completed before setting the USBCMD register Run/Stop (R/S) bit to '1'" -
 * MaxSlotsEn, DCBAAP, the command ring, and the interrupter's event ring (4.2,
 * p.69). All four are steps 6-10 above.
 *
 * Two details that are the spec's rather than this driver's taste:
 *
 *   The halted precondition. "Software shall not write a '1' to this flag
 *   unless the xHC is in the Halted state (i.e. HCH in the USBSTS register is
 *   '1'). Doing so may yield undefined results" (5.4.1, p.359). The reset above
 *   leaves it halted, so a controller failing this check is one whose reset did
 *   not do what the wait said it did - which is worth refusing over rather than
 *   writing R/S into anyway.
 *
 *   The write names R/S and nothing else, through xhciWriteUsbCmd - which
 *   clears every other *defined* control (INTE and HSEE among them, which this
 *   step must not enable) and carries the reserved fields back from the read. A
 *   literal `XHCI_USBCMD_RS` would have been simpler and wrong: bits 6:4, 12
 *   and 31:17 are RsvdP, and "software shall preserve the value read for writes
 *   to bits" (5.1.1, p.338). That HCRST has just reset the register is not a
 *   licence to clear them - a controller implementing something reserved there
 *   sets it at reset too, and RsvdP exists precisely because a driver written
 *   today cannot know which controllers those are.
 *
 * The wait afterwards is not a timing allowance: HCH "is a '0' whenever the
 * Run/Stop (R/S) bit is a '1'" (5.4.2, p.363), so it is really the question
 * "did the controller take that write at all", and the answer arrives at once
 * or not at all.
 *
 * IRQL: PASSIVE_LEVEL.
 */
static ULONG xhciRunController(PXHCI_EXTENSION ext, ULONG *usbstsOut)
{
    ULONG usbsts;
    ULONG previous;

    usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
    *usbstsOut = usbsts;
    /*
     * All ones carries HCH set, so the halted precondition below would be
     * satisfied by a window that is not decoding at all - and this driver would
     * then set XHCI_EXT_FLAG_RUNNING and write R/S into it. Refused for the
     * same reason xhciHalt refuses it: the bit pattern is not a status report.
     */
    if (usbsts == 0xFFFFFFFFUL) {
        XHCI_DBG_TEXT("run: USBSTS is not decoding");
        return 0;
    }
    if ((usbsts & XHCI_USBSTS_HCH) == 0) {
        /*
         * **A flag is published here too, and this is the one branch that has
         * *observed* the thing it asserts.** A valid USBSTS with HCH clear is
         * the hardware saying the xHC is executing (5.4.2 p.363: HCH is 0
         * whenever R/S is 1), and something must record that.
         *
         * "The xHC may be executing" is the union of the two bits, not the
         * meaning of `XHCI_EXT_FLAG_RUNNING` alone: that one specifically
         * records *this driver's* R/S ownership, which is what `xhciUnpowerPorts`
         * reads it for.
         *
         * Without it the start returns failure with the flag down, and the
         * failure cleanup's `XhciQuiesceController` then takes its
         * `RUNNING == 0` early exit and performs **neither the halt nor the Bus
         * Master fallback**. By this point DCBAAP, CRCR, ERSTBA and ERDP are
         * all programmed and pointing into the common buffer, and usbport
         * reclaims that buffer the moment this start is reported failed - so a
         * controller the driver just watched *not* halt would have been left
         * mastering into freed pages, which is the exact hazard
         * `XhciFailClosedDma` exists to make impossible. Publishing it sends
         * the cleanup down the halt/BME path and, if neither can be proven,
         * fails closed.
         *
         * **`XHCI_EXT_FLAG_HW_RUNNING`, not `XHCI_EXT_FLAG_RUNNING`**, because
         * the two are read for different questions and this case answers them
         * differently: the quiesce must stop this controller, and the port pass
         * must still leave its ports alone - nothing here wrote R/S and nothing
         * here powered a port.
         *
         * Found by the second-reader review. The branch predates
         * this phase; what the phase changed is the branch above it, which is
         * how it came to be read.
         */
        (VOID)XhciControllerUpdateFlags(ext, 0, XHCI_EXT_FLAG_HW_RUNNING);
        XHCI_DBG_VALUE("run: controller is not halted, USBSTS", usbsts);
        return 0;
    }

    /*
     * Set before the write, never after. From this instruction the xHC may be
     * executing and writing into the common buffer, and that is true whether or
     * not the confirmation below succeeds - so this is the flag that says
     * XhciQuiesceController has something to stop. Recording it afterwards
     * would leave a controller that started but could not be confirmed looking,
     * to every later path, exactly like one that was never started. The single
     * exception is the refusal immediately below, which is the case where
     * nothing was written at all.
     */
    previous = XhciControllerUpdateFlags(ext, 0, XHCI_EXT_FLAG_RUNNING);
    if (!xhciWriteUsbCmd(ext, XHCI_USBCMD_RS)) {
        /*
         * **The one refusal that may retire the flag again, and only because
         * nothing was written.** `xhciWriteUsbCmd` answers 0 exactly when it
         * could not read a valid operand, and its contract is that it then
         * performed no write at all - so R/S never reached the controller, and
         * the validated `USBSTS` read above already established HCH set. The
         * xHC is provably still halted and this driver is provably not what
         * started it.
         *
         * Leaving it set was wrong in the expensive direction: the start's
         * failure cleanup would run the live-controller quiesce over a
         * controller that never began mastering, which can clear PCI Bus Master
         * Enable and, when it can prove nothing, **fails closed through
         * `UsbPortBugCheck`** - taking the machine down over a controller that
         * was never started. Every *other* exit below keeps the flag, because
         * from the instant the write lands the xHC may be executing whether or
         * not the confirmation succeeds.
         *
         * Retired only if this call is what set it. `XhciControllerUpdateFlags`
         * returns the previous flags, so a caller that arrived with RUNNING
         * already published keeps it - clearing that would be this function
         * answering a question about somebody else's start.
         */
        if ((previous & XHCI_EXT_FLAG_RUNNING) == 0) {
            (VOID)XhciControllerUpdateFlags(ext, XHCI_EXT_FLAG_RUNNING, 0);
        }
        return 0;
    }

    if (!XhciWaitForBits(ext, ext->HcInfo.OperationalOffset + XHCI_OP_USBSTS,
                         XHCI_USBSTS_HCH, 0, XHCI_RUN_TIMEOUT_MS, &usbsts)) {
        XHCI_DBG_VALUE("run: HCHalted never cleared, USBSTS", usbsts);
        *usbstsOut = usbsts;
        return 0;
    }

    /*
     * HCE is RO and controller-internal: it "shall be set to indicate that an
     * internal error condition has been detected which requires software to
     * reset and reinitialize the xHC" (5.4.2, p.365). A controller that raises
     * it in the microseconds between R/S and this read is not one to hand back
     * to usbport as started, and this driver has nothing better to offer it
     * than the refusal - the reset it asks for is a whole second start.
     */
    *usbstsOut = usbsts;
    if ((usbsts & XHCI_USBSTS_HCE) != 0) {
        XHCI_DBG_VALUE("run: Host Controller Error at start, USBSTS", usbsts);
        return 0;
    }

    return 1;
}

/*
 * Is this controller able to do DMA at all?
 *
 * A controller running without PCI Bus Master Enable produces no command
 * completions, no events and no transfers - it is indistinguishable from one
 * that never answers, which is the hardest kind of failure to diagnose from a
 * VM. The bit therefore has to be *read*, on every start, and this is the gate
 * that does it. An earlier version only looked when ext->BusMasterCleared said
 * this driver had cleared the bit itself, which trusted bookkeeping instead of
 * hardware: BME clear for any other reason - a power transition, firmware that
 * never enabled it, a bus driver that did not - sailed straight through to R/S.
 *
 * The flag decides only whether this driver is allowed to *fix* it, which is
 * the write half below. PCI configuration is usbport's and the bus driver's to
 * manage; the one exception this driver has claimed is undoing its own act.
 * A BME that is clear for somebody else's reason is therefore a refusal, not a
 * repair - and a loud one, because that state is worth a diagnostic rather than
 * a controller that silently never delivers an event.
 *
 * **A config read that fails refuses too, and this is where the analogy with the
 * INTx gate breaks.** That gate treats an unreadable Interrupt Pin as a service
 * failure rather than a statement about the hardware, and can afford to: PCI
 * derives a device's interrupt resource from that very register, so
 * `USBPORT_RESOURCES_INTERRUPT` - already required at step 1 - is a second,
 * independent witness that a pin exists. Bus Master Enable has no such witness.
 * `USBPORT_RESOURCES_MEMORY` says a window was assigned and says nothing about
 * mastering, so a Command register that cannot be read leaves this driver
 * knowing *nothing* about the one bit the step exists to establish. Proceeding
 * on that is the very failure being guarded against, dressed as tolerance.
 *
 * The leniency was copied across before its precondition was checked. It is the
 * precondition, not the shape of the rule, that decides.
 *
 * Reads only, so it runs in the preflight. IRQL: PASSIVE_LEVEL.
 */
static ULONG xhciCheckBusMaster(PXHCI_EXTENSION ext, ULONG *commandOut)
{
    USHORT command;

    *commandOut = 0;

    command = 0;
    if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command,
                          sizeof(USHORT)) != MP_STATUS_SUCCESS) {
        /* Distinct from a successful read of 0xFFFF, which is a device that is
         * not answering: this is the service failing, and a release build
         * has only InitStatus to tell the two apart. */
        *commandOut = 0xFFFFFFFFUL;
        XHCI_DBG_TEXT("bus master: Command register unreadable - refusing, "
                      "because nothing else can say whether this controller "
                      "can do DMA");
        return 0;
    }
    *commandOut = (ULONG)command;

    if (command == 0xFFFFU) {
        XHCI_DBG_TEXT("bus master: device is not answering config space");
        return 0;
    }
    if ((command & XHCI_PCI_COMMAND_BME) != 0) {
        return 1;
    }
    if (ext->BusMasterCleared) {
        /* Ours to put back, at XHCI_INIT_STEP_BUS_MASTER_RESTORE. */
        return 1;
    }

    XHCI_DBG_VALUE("bus master: bus mastering is disabled and this driver did "
                   "not disable it - refusing rather than running a controller "
                   "that cannot DMA, PCI command", command);
    return 0;
}

/*
 * Put back the Bus Master Enable this driver's own quiesce took away, and
 * **prove it is back**.
 *
 * If that path fell back to clearing BME, the bit is still clear now: PCI sets
 * it when the bus driver starts the device, and a stop/start pair is not a PnP
 * restart. So this is a refusal, not a best-effort: an unreadable Command
 * register, a write that fails, and a bit that does not come back are all the
 * same answer, and none of them reaches the R/S write.
 *
 * The read-back is the same rule the quiesce's own fallback follows in the other
 * direction, and for the same reason - a write that returned success is not a
 * bit that took. An earlier version cleared BusMasterCleared on the strength of
 * the write alone and let the sequence continue regardless, which would have
 * reported a running controller that could not touch memory.
 *
 * A separate step from the gate above because this one *writes*, and the
 * preflight's guarantee is that it does not.
 *
 * IRQL: PASSIVE_LEVEL.
 */
static ULONG xhciRestoreBusMaster(PXHCI_EXTENSION ext, ULONG *commandOut)
{
    USHORT command;

    *commandOut = 0;
    if (!ext->BusMasterCleared) {
        return 1;
    }
    /*
     * Task 13-R.1. Below PASSIVE_LEVEL there is no legal way to put the bit
     * back, so the sequence refuses rather than running a controller that
     * cannot reach memory. Unreachable from the in-place recovery as things
     * stand - only a stop or a suspend clears BME, and neither of those leaves a
     * controller for the recovery to find - which is exactly why it is a refusal
     * and not a fallback.
     */
    if (ext->InitBelowPassive) {
        XHCI_DBG_TEXT("bus master: cleared by this driver and this sequence is "
                      "below PASSIVE_LEVEL - refusing");
        return 0;
    }

    command = 0;
    if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command,
                          sizeof(USHORT)) != MP_STATUS_SUCCESS) {
        XHCI_DBG_TEXT("bus master: Command register unreadable - cannot "
                      "restore the DMA this controller needs");
        return 0;
    }
    *commandOut = (ULONG)command;
    if (command == 0xFFFFU) {
        XHCI_DBG_TEXT("bus master: device is not answering config space");
        return 0;
    }

    if ((command & XHCI_PCI_COMMAND_BME) == 0) {
        command = (USHORT)(command | XHCI_PCI_COMMAND_BME);
        if (XhciWritePciConfig(ext, XHCI_PCI_COMMAND, &command,
                               sizeof(USHORT)) != MP_STATUS_SUCCESS) {
            XHCI_DBG_TEXT("bus master: could not set Bus Master Enable");
            return 0;
        }
        command = 0;
        if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command,
                              sizeof(USHORT)) != MP_STATUS_SUCCESS) {
            XHCI_DBG_TEXT("bus master: set, but could not be confirmed");
            return 0;
        }
        *commandOut = (ULONG)command;
    }

    if ((command & XHCI_PCI_COMMAND_BME) == 0) {
        XHCI_DBG_VALUE("bus master: Bus Master Enable will not set, PCI "
                       "command", command);
        return 0;
    }

    ext->BusMasterCleared = 0;
    XHCI_DBG_VALUE("bus master: restored, PCI command", command);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Interrupt masking                                                   */
/* ------------------------------------------------------------------ */

/*
 * Mask and unmask this controller's two interrupt enables, in the two orders
 * docs/contributing/implementation-invariants.md, "Interrupt Ordering" requires.
 *
 * Masking goes USBCMD.INTE first, then IMAN.IE, so an ISR racing the mask - it
 * runs at DIRQL and neither usbport's MiniportSpinLock nor this driver's
 * DISPATCH-level controller lock excludes it - costs a spurious interrupt
 * rather than a delivered one. Unmasking is the reverse.
 *
 * Task 9 made that ordering defence in depth rather than the whole defence:
 * XhciIsr now writes IMAN.IE as 0 instead of carrying it through, so the ISR
 * moves both enables in the same direction this function does and no
 * interleaving of the two can leave an enable up that this function cleared
 * (docs/contributing/design/05-locking-model.md, "The DIRQL exception"). The order is
 * kept because it is what makes the *residual* case - a mask that writes only
 * the half it could derive - safe.
 * IMAN can only be a read-modify-write (IP is RW1C in bit 0, IE is RW in bit 1,
 * 31:2 are RsvdP), and IP is written as 0 either way: a 1 would acknowledge an
 * interrupt nothing has seen.
 *
 * Task 6's DisableInterrupts callback is exactly XhciMaskInterrupts. Its
 * EnableInterrupts callback is **not** exactly XhciUnmaskInterrupts: it
 * releases Event Handler Busy first, because the controller can have raised IP
 * while masked and an interrupter left busy never asserts again
 * (XhciEnableInterrupts in src/xhci_evt.c). So the unmask half stays private to
 * that caller and the mask half is shared with the suspend and quiesce paths,
 * which have to mask without waiting to be asked - Win98's idle suspend/resume
 * pairs are not bracketed by DisableInterrupts.
 *
 * IRQL: any.
 */
VOID XhciMaskInterrupts(PXHCI_EXTENSION ext)
{
    ULONG iman;
    ULONG usbcmd;
    ULONG attempt;
    ULONG degraded;

    degraded = 0;
    for (attempt = 0; attempt < XHCI_INTERRUPT_WRITE_ATTEMPTS; attempt++) {
        usbcmd = XhciReadOp(ext, XHCI_OP_USBCMD);
        iman = XhciReadIr0(ext, XHCI_IR_IMAN);
        /*
         * An undecoding window reads as all ones. Feeding that through the
         * reserved-preserving RMW would assert HCRST, LHCRST, CSS and CRS together
         * on a controller that may merely have returned one transient bad read.
         * IMAN has the same rule: its reserved-preserving RMW would publish every
         * reserved bit as one. Read both operands before either write, so neither
         * write is derived from a value the other register's failure exposed.
         *
         * **Each operand is judged on its own, and that asymmetry with
         * XhciUnmaskInterrupts is the point.** Refusing an unmask is safe - the
         * interrupt stays off. Refusing a mask is not: it leaves the enable up. If
         * USBCMD is readable and only IMAN is dead, clearing INTE is still a
         * well-formed write derived entirely from a validated read, and it is the
         * half that matters, because INTE is the enable this driver masks *first*
         * for the reason above. Skipping it would leave xhciResetController
         * publishing ControllerFailed with interrupts live, and the ISR then
         * declines a still-asserted level-triggered INTx without acknowledging it -
         * the shared-line livelock the mask order exists to prevent. So write
         * whichever half is derivable, in the usual order, and count the rest.
         */
        if (usbcmd == 0xFFFFFFFFUL || iman == 0xFFFFFFFFUL) {
            degraded = 1;
            XHCI_DBG_TEXT("interrupt mask: a register window is not decoding");
        }
        if (usbcmd != 0xFFFFFFFFUL) {
            xhciWriteUsbCmdFrom(ext, usbcmd,
                                (usbcmd & XHCI_USBCMD_DEFINED_MASK) &
                                    ~XHCI_USBCMD_INTE);
        }
        if (iman != 0xFFFFFFFFUL) {
            XhciWriteIr0(ext, XHCI_IR_IMAN,
                         iman & ~(XHCI_IMAN_IE | XHCI_IMAN_IP));
        }

        /*
         * Publish the fact the ISR's decline gates depend on, rather than returning
         * it for three callers to store identically.
         *
         * **From a read back, not from the reads above.** Those only prove the
         * operands were derivable; a window that stopped decoding between them and
         * the writes swallows both silently, and deriving the proof from the
         * pre-write reads would then claim delivery was suppressed while both
         * enables are still up - the same decline-behind-an-unapplied-mask livelock
         * this word exists to prevent, one level further in.
         *
         * Either enable clear is sufficient and neither is necessary, which is what
         * the word is named for: INTE is the global enable for all interrupters and
         * IE is this interrupter's, so with one of them down this controller cannot
         * assert. An undecoding read back needs no special case - all ones has both
         * bits set, which is exactly "no proof".
         */
        if ((XhciReadOp(ext, XHCI_OP_USBCMD) & XHCI_USBCMD_INTE) == 0 ||
            (XhciReadIr0(ext, XHCI_IR_IMAN) & XHCI_IMAN_IE) == 0) {
            ext->InterruptDeliverySuppressed = 1;
            if (degraded) {
                /*
                 * **Not a failure**, and it used to share the failure counter.
                 * This exit has read back a proof that delivery is suppressed;
                 * a bad operand window on the way there cost nothing. The
                 * other exit is the opposite verdict - delivery possibly live -
                 * and two opposite diagnoses may not share a counter in a free
                 * build (Phase 4 task 8).
                 */
                ext->InterruptMaskDegraded++;
            }
            return;
        }
    }

    /*
     * Out of attempts with delivery still unsuppressed. Counted here rather
     * than only on the unreadable-operand path, because the swallowed-write
     * case reaches this line with every operand having looked healthy - and
     * that incident would otherwise leave no trace at all once later lifecycle
     * activity moved the state word. CheckController traces this counter.
     *
     * The degraded reading is taken here too, because a pass can be both: an
     * undecoding window that never recovers reaches this line, and "the
     * operands were unreadable" and "delivery is not suppressed" are two facts
     * about it rather than one.
     */
    ext->InterruptDeliverySuppressed = 0;
    if (degraded) {
        ext->InterruptMaskDegraded++;
    }
    ext->InterruptMaskFailures++;
    XHCI_DBG_TEXT("interrupt mask: delivery is NOT proven suppressed");
}

/*
 * Put IMAN.IE back after the ISR cleared it, and prove it landed.
 *
 * **This is the sole restorer of interrupt delivery on a running controller,
 * which is what earns it the same machinery as XhciUnmaskInterrupts.** Task 9
 * made XhciIsr write IE as 0 rather than carrying it through, so every claimed
 * interrupt now leaves the interrupter disabled until this runs - and usbport
 * calls EnableInterrupts once after StartController and not again on a running
 * controller (docs/usb-xhci-info/usbport-miniport-abi.md). A skipped or swallowed re-arm is
 * therefore permanent silence, not a missed cycle.
 *
 * The DPC used to do this inline as a bare read-modify-write, and it had exactly
 * the defect rounds 6-10 of task 7 spent five rounds removing from the mask and
 * unmask paths, in its worst form: an all-ones read has **IE set**, so
 * `(iman & IE) == 0` was false and the caller concluded the interrupter was
 * already armed and wrote nothing. One transient undecoding read, and delivery
 * was gone for the life of the driver with no counter, no trace and no
 * escalation. So:
 *
 *   - **All ones is "cannot prove armed", never "already armed".** It is not a
 *     legal RMW operand either - `(0xFFFFFFFF & ~IP) | IE` publishes every RsvdP
 *     bit as 1 - so the attempt is retried rather than acted on.
 *   - The write is derived from the read that was validated, not from a second
 *     read a dying window could answer differently.
 *   - Success is a **read back** showing IE set, not the accessor returning.
 *   - Bounded retries, no stall: two spin locks are held here.
 *
 * IP is written as 0 throughout: it is RW1C, so a 0 preserves an interrupt that
 * arrived since the ISR ran instead of acknowledging one nothing has seen.
 *
 * InterruptDeliverySuppressed is deliberately **not** touched. That word means
 * "either enable is confirmed clear", and this function reads only IMAN - it has
 * no evidence about USBCMD.INTE, so claiming un-suppression here would assert
 * something unmeasured. XhciUnmaskInterrupts remains the only authority on it.
 *
 * Returns 1 if IE is confirmed set, 0 if it could not be proved - which the
 * caller must escalate rather than absorb, for the same reason a refused unmask
 * must (docs/contributing/implementation-invariants.md, "Interrupt Ordering").
 *
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
ULONG XhciRearmInterrupter(PXHCI_EXTENSION ext)
{
    ULONG iman;
    ULONG imanBack;
    ULONG attempt;

    for (attempt = 0; attempt < XHCI_INTERRUPT_WRITE_ATTEMPTS; attempt++) {
        iman = XhciReadIr0(ext, XHCI_IR_IMAN);
        if (iman == 0xFFFFFFFFUL) {
            continue;
        }
        if ((iman & XHCI_IMAN_IE) != 0) {
            /* Already armed, and proved so by a read that was not all ones -
             * which is the whole distinction the old inline test missed. */
            return 1;
        }

        XhciWriteIr0(ext, XHCI_IR_IMAN,
                     (iman & ~XHCI_IMAN_IP) | XHCI_IMAN_IE);

        imanBack = XhciReadIr0(ext, XHCI_IR_IMAN);
        if (imanBack != 0xFFFFFFFFUL && (imanBack & XHCI_IMAN_IE) != 0) {
            return 1;
        }
    }

    ext->InterruptRearmFailures++;
    XHCI_DBG_TEXT("interrupter re-arm: IE did not come back up");
    return 0;
}

ULONG XhciUnmaskInterrupts(PXHCI_EXTENSION ext)
{
    ULONG iman;
    ULONG usbcmd;
    ULONG imanBack;
    ULONG usbcmdBack;
    ULONG attempt;

    /*
     * The enable path is no more entitled to derive a write from all ones than
     * the mask path. In particular, an all-ones USBCMD would assert every
     * defined control together, and an all-ones IMAN would write all RsvdP bits
     * as one. Refuse *both* writes if either operand is unavailable - the
     * opposite rule to XhciMaskInterrupts, and safe here for the reason that
     * one is not: a half-enable buys nothing.
     *
     * **But a refusal here is not self-correcting, which is why it is retried
     * and then escalated.** usbport calls EnableInterrupts once after
     * StartController and does *not* call it after a successful resume - only
     * after restarting a controller whose resume failed
     * (docs/usb-xhci-info/usbport-miniport-abi.md; ReactOS power.c:192-212). The callback
     * returns void, so a refusal is invisible to usbport and the start still
     * counts as successful. Win2000 may never idle-suspend, so "the next
     * resume will re-enable" is not a recovery path: without one, a single
     * transient all-ones read leaves a started controller that never
     * interrupts again. The retries below cost nothing and cover a glitch that
     * clears immediately; anything longer-lived is the caller's to escalate -
     * which contains the failure rather than repairing it, since no miniport
     * can initiate the stop/start that would.
     */
    for (attempt = 0; attempt < XHCI_INTERRUPT_WRITE_ATTEMPTS; attempt++) {
        iman = XhciReadIr0(ext, XHCI_IR_IMAN);
        usbcmd = XhciReadOp(ext, XHCI_OP_USBCMD);
        if (iman == 0xFFFFFFFFUL || usbcmd == 0xFFFFFFFFUL) {
            continue;
        }

        XhciWriteIr0(ext, XHCI_IR_IMAN,
                     (iman & ~XHCI_IMAN_IP) | XHCI_IMAN_IE);
        xhciWriteUsbCmdFrom(ext, usbcmd,
                            (usbcmd & XHCI_USBCMD_DEFINED_MASK) |
                                XHCI_USBCMD_INTE);

        /*
         * Derivable operands are not an applied write: a window that stops
         * decoding after the reads swallows both silently, and this callback
         * is the one place where believing an unapplied write strands a
         * started controller for good. So the retry covers the write too, and
         * success is a read back rather than a return from the accessor.
         *
         * All ones must be rejected explicitly here, which is the mirror of
         * XhciMaskInterrupts needing no such test: there, all ones has both
         * enable bits set and so reads as "no proof" on its own; here, that is
         * indistinguishable from the success this function is looking for.
         */
        imanBack = XhciReadIr0(ext, XHCI_IR_IMAN);
        usbcmdBack = XhciReadOp(ext, XHCI_OP_USBCMD);
        if (imanBack != 0xFFFFFFFFUL && usbcmdBack != 0xFFFFFFFFUL &&
            (imanBack & XHCI_IMAN_IE) != 0 &&
            (usbcmdBack & XHCI_USBCMD_INTE) != 0) {
            ext->InterruptDeliverySuppressed = 0;
            return 1;
        }
    }

    ext->InterruptUnmaskFailures++;
    XHCI_DBG_TEXT("interrupt unmask: the enables did not come back up");
    return 0;
}

/*
 * What this driver wants PORTSC.PP to be on one logical port.
 *
 * XHCI_PORT_CLASS_NONE - a port no Supported Protocol capability named - is
 * deliberately neither: this driver has no statement about it, and both writing
 * and refusing to write would be one. Leaving it as the firmware and the reset
 * left it is the only answer that claims nothing.
 */
#define XHCI_PP_WANT_OFF        0UL
#define XHCI_PP_WANT_ON         1UL
#define XHCI_PP_WANT_LEAVE      2UL

/*
 * Which pass is asking. The start pass and the teardown pass differ in exactly
 * one answer - what a managed USB 2.0 port's PP should be - so they share the
 * driver, the port counter and the bounded confirmation below rather than
 * getting a second copy of each.
 *
 * **A port the driver has no opinion about keeps that answer at teardown too**,
 * which is the one place the two passes could have been made symmetric and
 * deliberately are not. XHCI_PORT_CLASS_NONE means no Supported Protocol
 * capability named this port; the start pass leaves it as the firmware left it
 * because both writing and refusing to write would be a statement. A stop is
 * routinely followed by a restart, and that restart would leave the port alone
 * again - so clearing PP here would be a one-way change dressed up as a
 * teardown, permanently unpowering a port on nothing more than a stop/start
 * cycle. 4.19.4's note asks for "all Root Hub ports"; this driver reads that as
 * all the ports it powered, because that is the only set it can put back.
 */
#define XHCI_PP_PHASE_START     0UL
#define XHCI_PP_PHASE_TEARDOWN  1UL

static ULONG xhciWantPortPower(const XHCI_PORT_MAP *map,
                               ULONG port,
                               ULONG phase)
{
    switch (XhciPortClass(map, port)) {
    case XHCI_PORT_CLASS_USB2_ONLY:
    case XHCI_PORT_CLASS_USB2_COMPANION:
        return phase == XHCI_PP_PHASE_TEARDOWN ? XHCI_PP_WANT_OFF
                                               : XHCI_PP_WANT_ON;
    case XHCI_PORT_CLASS_USB3_COMPANION:
    case XHCI_PORT_CLASS_USB3_ORPHAN:
        return XHCI_PP_WANT_OFF;
    default:
        return XHCI_PP_WANT_LEAVE;
    }
}

/*
 * Drive one port's PP to `want`, and report whether that took a write.
 *
 * The read-first is not an optimisation. A Root Hub port comes out of HCRST in
 * the Disconnected state, "i.e. Port Power (PP) is asserted, and the port is
 * waiting for signaling on the USB that indicates a device is attached"
 * (4.19.4, p.295), so on a healthy controller every managed port is already
 * where this driver wants it and the whole assert pass writes nothing. What the
 * count buys is the settle delay below: it is owed only for a real '0' to '1'
 * transition.
 *
 * The value comes from src/xhci_port.c either way. PORTSC is the register where
 * writing back what was read disables the port (PED), restarts a reset (PR), or
 * silently discards a connect nobody has handled (the RW1C change bits).
 *
 * IRQL: any (no wait here; the caller owns the delay).
 */
static ULONG xhciDrivePortPower(PXHCI_EXTENSION ext, ULONG port, ULONG want)
{
    ULONG portsc;

    portsc = XhciReadPortsc(ext, port);
    if (portsc == 0xFFFFFFFFUL) {
        /* Not decoding. Reported by the confirmation pass, which is the one
         * place that decides what a port's final state was. */
        return 0;
    }
    if (((portsc & XHCI_PORTSC_PP) != 0) == (want == XHCI_PP_WANT_ON)) {
        return 0;
    }

    if (want == XHCI_PP_WANT_ON) {
        XhciWritePortsc(ext, port, XhciPortscWith(portsc, XHCI_PORTSC_PP));
    } else {
        XhciWritePortsc(ext, port,
                        XhciPortscNeutral(portsc) & ~XHCI_PORTSC_PP);
    }
    return 1;
}

/*
 * Power the managed USB 2.0 ports, and take power off the USB 3.x ones.
 *
 * **The second half is not a no-op, and that is the whole reason this is one
 * function rather than a loop over the managed ports.** AGENTS.md's port
 * strategy says USB 3.x logical ports are left unpowered and unmanaged, and
 * docs/usb-xhci-info/xhci-programming.md builds on that:
 * an unpowered SuperSpeed half is what makes a USB 3.x capable device fail SS
 * link training and fall back to its D+/D- path, onto the USB 2.0 companion
 * this driver does serve. But PP defaults to *asserted* on every port after
 * HCRST (4.19.4, p.295), so "left unpowered" is something this driver has to
 * do, not something it gets by not acting. Leaving them powered would leave
 * SuperSpeed devices trained onto ports nothing services - a device that
 * appears dead rather than one that falls back.
 *
 * Assertions run before deassertions, **and are confirmed to have taken effect
 * before any deassertion is written**. The order is load-bearing on a connector
 * whose two logical ports are both being written: "implementations shall OR
 * together the output of the PORTSC register Port Power pins for Root Hub Ports
 * that map to the same Physical USB Connector" (4.19.7 implementation note,
 * p.303), so deasserting first drops VBus on that connector. Ordering the
 * *writes* is not enough on its own, because "the PP flag may be delayed in
 * reflecting this change" (footnote 91 to Table 5-27, p.375): if the USB 2.0
 * half has not actually come up yet when the USB 3.x half goes down, the OR is
 * momentarily zero and VBus drops anyway. Confirming between the passes is what
 * closes that.
 *
 * **HCCPARAMS1.PPC is read and recorded but not consulted here**, which is a
 * departure from the roadmap's "PORTSC.PP when PPC is supported". PPC says
 * whether the controller has power *switches*, not whether PP means anything.
 * "Software cannot change the state of the port unless Port Power (PP) is
 * asserted ('1'), regardless of the Port Power Control (PPC) capability"
 * (5.4.8, p.371), and at PPC = 0 a port with PP = 0 "is nonfunctional and shall
 * not report attaches, detaches, or Port Link State (PLS) changes" (Table 5-27,
 * p.375) even though its VBus is hard-wired on. So both halves of this step are
 * needed at either value of PPC. The one place PPC does appear in the spec's
 * requirements is the settle delay, which it makes software's responsibility
 * only at PPC = 1 - and taking it unconditionally on a real transition is the
 * safe direction, besides being what the PP read-back below needs anyway.
 *
 * This step never refuses the controller. A port that will not take its power
 * state is one port - counted, traced, and left to Phase 5's root hub to report
 * as not connected - while refusing would decline a controller whose other
 * eleven ports work.
 *
 * IRQL: PASSIVE_LEVEL (the settle delay).
 */
/* How many ports of one class this driver wants PP written to. */
static ULONG xhciCountPortsWanting(const XHCI_PORT_MAP *map,
                                   ULONG want,
                                   ULONG phase)
{
    ULONG port;
    ULONG n;

    n = 0;
    for (port = 1; port <= map->PortCount; port++) {
        if (xhciWantPortPower(map, port, phase) == want) {
            n++;
        }
    }
    return n;
}

/*
 * Wait for every port of one class to report `want`, boundedly, and return how
 * many got there.
 *
 * Two different waits, deliberately combined into one place. `minimumMs` is the
 * spec's flat obligation after asserting power - "the host is required to have
 * power stable to the port within 20 milliseconds of the '0' to '1' transition
 * of PP ... software is responsible for waiting 20 ms. after asserting PP,
 * before attempting to change the state of the port" (5.4.8, p.371) - and is
 * passed only by the assert pass, because deasserting owes no such thing. The
 * polling that follows is for footnote 91's separate allowance, which applies
 * to a change in *either* direction: "a port implementation shall initiate a
 * Port Power change immediately when PP is written, however the PP flag may be
 * delayed in reflecting this change" (p.375).
 *
 * The first version of this step waited only when a port was asserted, and then
 * read every port back once. On the ordinary controller - the one that comes out
 * of HCRST with PP asserted everywhere, so the only writes are the USB 3.x
 * deassertions - that meant reading a just-written register with no allowance
 * at all, and any controller taking footnote 91's latitude would have had every
 * one of its USB 3.x ports counted as a failure.
 *
 * The loop costs nothing when there is nothing to wait for: a pass where every
 * port already reports the wanted state exits before its first delay.
 *
 * IRQL: PASSIVE_LEVEL.
 */
static ULONG xhciSettlePortPower(PXHCI_EXTENSION ext,
                                 ULONG want,
                                 ULONG phase,
                                 ULONG minimumMs)
{
    const XHCI_PORT_MAP *map;
    ULONG waited;
    ULONG port;
    ULONG portsc;
    ULONG reached;
    ULONG pending;

    map = &ext->PortMap;

    if (minimumMs > 0) {
        XhciDelayMs(ext, minimumMs);
    }

    waited = 0;
    for (;;) {
        reached = 0;
        pending = 0;
        for (port = 1; port <= map->PortCount; port++) {
            if (xhciWantPortPower(map, port, phase) != want) {
                continue;
            }
            portsc = XhciReadPortsc(ext, port);
            if (portsc != 0xFFFFFFFFUL &&
                (((portsc & XHCI_PORTSC_PP) != 0) ==
                 (want == XHCI_PP_WANT_ON))) {
                reached++;
            } else {
                pending++;
                XHCI_DBG_VALUE_CHANGED("port power: still waiting on port",
                                       port);
            }
        }
        if (pending == 0 || waited >= XHCI_PORT_POWER_SETTLE_MS) {
            break;
        }
        XhciDelayMs(ext, XHCI_PORT_POWER_POLL_MS);
        waited += XHCI_PORT_POWER_POLL_MS;
    }

    return reached;
}

static VOID xhciPowerPorts(PXHCI_EXTENSION ext)
{
    const XHCI_PORT_MAP *map;
    ULONG port;
    ULONG asserted;
    ULONG managed;
    ULONG superSpeed;

    map = &ext->PortMap;
    managed = xhciCountPortsWanting(map, XHCI_PP_WANT_ON,
                                    XHCI_PP_PHASE_START);
    superSpeed = xhciCountPortsWanting(map, XHCI_PP_WANT_OFF,
                                       XHCI_PP_PHASE_START);

    asserted = 0;
    for (port = 1; port <= map->PortCount; port++) {
        if (xhciWantPortPower(map, port, XHCI_PP_PHASE_START) ==
            XHCI_PP_WANT_ON) {
            asserted += xhciDrivePortPower(ext, port, XHCI_PP_WANT_ON);
        }
    }

    /*
     * Confirmed before a single deassertion is written - see the VBus argument
     * in the header comment. The flat 20 ms is owed only when a port really
     * transitioned, which is why the ordinary controller pays nothing here.
     *
     * "After modifying PP, software shall read PP and confirm that it is
     * reached its target state before modifying it again" (Table 5-27, p.375 -
     * quoted with the specification's own grammar so the citation sweep can
     * find it). Every port of the class is read back, not only the ones this
     * pass wrote: what comes out is how many ports are actually live, which is
     * the number Phase 5's root hub is built on.
     */
    ext->PortsPowered =
        xhciSettlePortPower(ext, XHCI_PP_WANT_ON, XHCI_PP_PHASE_START,
                            asserted > 0 ? XHCI_PORT_POWER_SETTLE_MS : 0);

    for (port = 1; port <= map->PortCount; port++) {
        if (xhciWantPortPower(map, port, XHCI_PP_PHASE_START) ==
            XHCI_PP_WANT_OFF) {
            (VOID)xhciDrivePortPower(ext, port, XHCI_PP_WANT_OFF);
        }
    }
    ext->PortsUnpowered =
        xhciSettlePortPower(ext, XHCI_PP_WANT_OFF, XHCI_PP_PHASE_START, 0);

    ext->PortPowerFailures = (managed - ext->PortsPowered) +
                             (superSpeed - ext->PortsUnpowered);

    XHCI_DBG_VALUE("port power: ports transitioned to powered", asserted);
    XHCI_DBG_VALUE("port power: managed ports powered", ext->PortsPowered);
    XHCI_DBG_VALUE("port power: USB3 ports left unpowered",
                   ext->PortsUnpowered);
    XHCI_DBG_VALUE("port power: ports that did not reach target",
                   ext->PortPowerFailures);
}

/*
 * Take port power back off on the way out, before the halt.
 *
 * "Note: Before the xHC driver is unloaded, the driver should clear the Port
 * Power (PP) flag of all Root Hub ports to place them into the Disabled state
 * and reduce port power consumption" (4.19.4, p.296). It is the only step of
 * the teardown that has to run *first*: PORTSC may be written only while the
 * controller runs - "software shall ensure that the xHC is running (HCHalted
 * (HCH) = `0`) before attempting to write to this register" (5.4.8, p.371) - so
 * a halt taken before this point makes it permanently unavailable, and the
 * quiesce is what takes that halt.
 *
 * **Two gates, and they answer different questions.** XHCI_EXT_FLAG_RUNNING is
 * whether this driver ever wrote R/S, i.e. whether the power on these ports is
 * this driver's to take away; a start that refused in the preflight, or that
 * declined at the run step because the controller was not halted, has claimed
 * nothing and leaves the ports exactly as it found them. The live HCH read is
 * whether the write is legal *now* - a controller that set R/S and then never
 * came out of Halted is the case where the first gate says yes and the hardware
 * says no. Neither substitutes for the other, and neither is a fault: a
 * teardown that could not legally write PORTSC has left port power up, which is
 * a wasted watt rather than a broken machine, so it is counted separately from
 * a port that took the write and ignored it.
 *
 * The port count is taken before the USBSTS read for the sake of the refusal
 * paths: a controller whose port map was never built has PortCount 0, and the
 * whole function then reads nothing at all.
 *
 * **No HCRST follows this, deliberately** - see XhciStopController.
 *
 * IRQL: PASSIVE_LEVEL (the confirmation polls).
 */
static VOID xhciUnpowerPorts(PXHCI_EXTENSION ext)
{
    const XHCI_PORT_MAP *map;
    ULONG port;
    ULONG wanted;
    ULONG usbsts;

    map = &ext->PortMap;
    wanted = xhciCountPortsWanting(map, XHCI_PP_WANT_OFF,
                                   XHCI_PP_PHASE_TEARDOWN);
    if (wanted == 0) {
        return;
    }

    if ((ext->Flags & XHCI_EXT_FLAG_RUNNING) == 0) {
        /*
         * Not running, so there is no legal PORTSC write to make - and the two
         * ways to arrive here have opposite diagnoses, so they do not share a
         * counter.
         *
         * SUSPENDED is the **measured ordinary shutdown**: docs/contributing/lessons.md
         * records usbport's clean shutdown on Win98 as SuspendController ->
         * DisableInterrupts -> StopController(TRUE), so the stop routinely
         * arrives on a controller the suspend has already halted, and the port
         * pass above is unreachable on precisely the path 4.19.4's note is
         * written about. That is recorded rather than worked around - see
         * XhciStopController for why restarting the controller to reach the
         * write is refused, and why this is the case where the note buys least.
         *
         * Without it, the controller was never run: a start refused in the
         * preflight or at the run step, or a stop after a stop. Those ports are
         * powered by somebody else's decision.
         */
        if ((ext->Flags & XHCI_EXT_FLAG_SUSPENDED) != 0) {
            ext->PortTeardownSkippedSuspended++;
            XHCI_DBG_TEXT("teardown: the stop arrived on a suspended "
                          "controller - PORTSC is unwritable, so port power "
                          "stays up");
        } else {
            ext->PortTeardownSkipped++;
            XHCI_DBG_TEXT("teardown: this driver is not running the "
                          "controller, so its port power is not this driver's "
                          "to take");
        }
        return;
    }

    usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
    if (usbsts == 0xFFFFFFFFUL || (usbsts & XHCI_USBSTS_HCH) != 0) {
        ext->PortTeardownSkipped++;
        XHCI_DBG_VALUE("teardown: PORTSC is not writable, ports left powered, "
                       "USBSTS", usbsts);
        return;
    }

    /*
     * One pass, in port order, and no ordering obligation inside it. The start
     * pass has to power the USB 2.0 half of a connector before it unpowers the
     * USB 3.x half, because VBus is the OR of the two (4.19.7, p.303) and the
     * point there is to keep it up. Here every port of the connector is going
     * down, so the OR reaches zero whatever the order - that is the intent, not
     * a hazard to sequence around.
     */
    for (port = 1; port <= map->PortCount; port++) {
        if (xhciWantPortPower(map, port, XHCI_PP_PHASE_TEARDOWN) ==
            XHCI_PP_WANT_OFF) {
            (VOID)xhciDrivePortPower(ext, port, XHCI_PP_WANT_OFF);
        }
    }

    /*
     * Confirmed, boundedly, for the same reason the start pass is: PP "may be
     * delayed in reflecting this change" in either direction (footnote 91 to
     * Table 5-27, p.375), so a single read back counts every lagging port as a
     * failure. No flat delay is passed - 20 ms is owed to a `0` to `1`
     * transition and nothing is owed to this one.
     */
    ext->PortsUnpoweredAtStop =
        xhciSettlePortPower(ext, XHCI_PP_WANT_OFF, XHCI_PP_PHASE_TEARDOWN, 0);
    ext->PortTeardownFailures = wanted - ext->PortsUnpoweredAtStop;

    XHCI_DBG_VALUE("teardown: ports unpowered", ext->PortsUnpoweredAtStop);
    XHCI_DBG_VALUE("teardown: ports that would not give up power",
                   ext->PortTeardownFailures);
}

/*
 * Clear PCI Bus Master Enable, and prove it stayed clear.
 *
 * The quiesce's last resort, and the only one available when the MMIO window
 * has stopped answering: config space is decoded independently of Memory Space
 * Enable, so a controller whose registers read all-ones can still be reached
 * here - and still be bus-mastering, because MSE and BME are separate bits.
 * With BME clear the xHC cannot issue a memory transaction at all, which is the
 * second of the two proofs docs/contributing/implementation-invariants.md, "DMA Teardown"
 * accepts ("hardware reset has completed or PCI Bus Master Enable is confirmed
 * clear").
 *
 * Read back rather than trusting the write: this is the last claim anyone makes
 * about whether the common buffer is safe to reclaim, and an unverified write
 * would make it a guess. A device that has been physically removed answers
 * all-ones here too, which is accepted - a controller that is not on the bus is
 * not mastering it.
 *
 * Records ext->BusMasterCleared so a later start puts the bit back. Nothing
 * else in this driver writes PCI configuration space.
 *
 * One attempt. xhciClearBusMaster wraps it in the retry, for the reason given
 * there.
 *
 * IRQL: PASSIVE_LEVEL.
 */
static ULONG xhciTryClearBusMaster(PXHCI_EXTENSION ext)
{
    USHORT command;

    /*
     * **Task 13-R.1.** Below PASSIVE_LEVEL this driver may not reach
     * configuration space at all, so the fallback proof is unavailable - and the
     * one context that gets here below PASSIVE is the in-place recovery, where
     * it is also unnecessary: nothing is reclaiming the common buffer, so a
     * controller that would not halt is mastering memory this driver still owns.
     * Declining is therefore the whole answer, and the caller's counters record
     * that the proof was not obtained.
     */
    if (ext->InitBelowPassive) {
        XHCI_DBG_TEXT("quiesce: below PASSIVE_LEVEL - configuration space is "
                      "out of reach, so DMA cannot be proven stopped here");
        return 0;
    }

    command = 0;
    if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command,
                          sizeof(USHORT)) != MP_STATUS_SUCCESS) {
        XHCI_DBG_TEXT("quiesce: PCI command register unreadable - DMA cannot "
                      "be proven stopped");
        return 0;
    }
    if (command == 0xFFFFU) {
        XHCI_DBG_TEXT("quiesce: device is off the bus - it masters nothing");
        return 1;
    }
    if ((command & XHCI_PCI_COMMAND_BME) == 0) {
        XHCI_DBG_VALUE("quiesce: bus mastering was already off, PCI command",
                       command);
        return 1;
    }

    command = (USHORT)(command & ~XHCI_PCI_COMMAND_BME);
    if (XhciWritePciConfig(ext, XHCI_PCI_COMMAND, &command,
                           sizeof(USHORT)) != MP_STATUS_SUCCESS) {
        XHCI_DBG_TEXT("quiesce: could not clear Bus Master Enable");
        return 0;
    }
    ext->BusMasterCleared = 1;

    command = 0;
    if (XhciReadPciConfig(ext, XHCI_PCI_COMMAND, &command,
                          sizeof(USHORT)) != MP_STATUS_SUCCESS) {
        XHCI_DBG_TEXT("quiesce: Bus Master Enable cleared but not confirmed");
        return 0;
    }
    if (command != 0xFFFFU && (command & XHCI_PCI_COMMAND_BME) != 0) {
        XHCI_DBG_VALUE("quiesce: Bus Master Enable would not clear, PCI "
                       "command", command);
        return 0;
    }

    XHCI_DBG_VALUE("quiesce: bus mastering confirmed off, PCI command",
                   command);
    return 1;
}

/*
 * The same claim, retried - because of what the caller now does with a 0.
 *
 * Every way the single attempt above fails is a *transient* on some machine: a
 * config cycle that returned a Master Abort while the bridge was busy, a write
 * that did not stick on the first pass, a read-back taken before the bit
 * settled. Retrying costs three config cycles on a path that has already given
 * up on a 16 ms halt, and it is the last chance to answer the question without
 * taking the machine down. Nothing here waits: a config cycle is not a settle,
 * and this path is reached at PASSIVE only by accident of its callers.
 *
 * It does **not** paper over the case that matters. A device whose BME really
 * will not clear answers with the bit still set every time, so the loop cannot
 * turn a stuck bit into a proof - it only removes the single-sample flake from
 * the decision to bugcheck.
 *
 * IRQL: PASSIVE_LEVEL.
 */
static ULONG xhciClearBusMaster(PXHCI_EXTENSION ext)
{
    ULONG attempt;

    for (attempt = 0; attempt < XHCI_BUS_MASTER_ATTEMPTS; attempt++) {
        if (xhciTryClearBusMaster(ext)) {
            return 1;
        }
        ext->BusMasterClearRetries++;
    }

    XHCI_DBG_VALUE("quiesce: bus mastering would not clear after attempts",
                   XHCI_BUS_MASTER_ATTEMPTS);
    return 0;
}

/*
 * See the contract in src/xhci_hw.h. The counters and the trace go out *before*
 * the service is called, because the service passes no bugcheck parameters and
 * does not return: whatever is not recorded by this point is not recoverable
 * from the crash.
 *
 * A NULL slot leaves the caller exactly where it was before this function
 * existed - the buffer is reclaimed and the record is all there is. That is not
 * a fallback so much as an admission, and it is counted separately so a machine
 * that took the unsafe path can be told from one that never reached it.
 *
 * IRQL: any.
 */
VOID XhciFailClosedDma(PXHCI_EXTENSION ext)
{
    if (ext == NULL) {
        return;
    }

    /*
     * **Task 13-R.1: the premise of this whole function is a reclamation, and
     * the in-place recovery has none.** The bugcheck is justified by the
     * sentence below it - the buffer is about to be handed back to usbport while
     * an xHC that may still be mastering points at it. On the recovery path
     * nothing is handing anything back: there is no `StartController` in flight,
     * usbport is not reclaiming, and the block stays this driver's. That is the
     * same reasoning `XhciSuspendController` already applies by never reaching
     * this function at all - a suspend does not reclaim either, so it counts
     * `SuspendFailures` and carries on - and taking a machine down here would
     * turn a stall into a crash, which is the opposite of what this task is for.
     *
     * Counted rather than silent: a recovery attempt that could not stop the
     * controller is exactly the evidence a later corruption would need.
     */
    ext->DmaFailClosed++;
    if (ext->InitBelowPassive) {
        ext->DmaFailClosedDeferred++;
        XHCI_DBG_TEXT("teardown: DMA not proven stopped inside the in-place "
                      "recovery - nothing is reclaiming the buffer, so this is "
                      "counted rather than bugchecked");
        return;
    }
    XHCI_DBG_TEXT("teardown: DMA NOT PROVEN STOPPED and the common buffer is "
                  "about to be reclaimed - failing closed");
    XHCI_DBG_VALUE("teardown: quiesce failures", ext->QuiesceFailures);
    XHCI_DBG_VALUE("teardown: bus master clear retries",
                   ext->BusMasterClearRetries);

    if (XhciRegPacket.UsbPortBugCheck == NULL) {
        ext->DmaFailClosedUnavailable++;
        XHCI_DBG_TEXT("teardown: no UsbPortBugCheck service - the buffer will "
                      "be reclaimed under a live bus master");
        return;
    }

    XhciRegPacket.UsbPortBugCheck(ext);
}

/*
 * SuspendController / ResumeController.
 *
 * **The suspend halts the controller, and the resume reinitializes it.** That
 * is the whole design, and both halves are load-bearing:
 *
 *   Leaving USBCMD.R/S set across a suspend is unsafe on Windows 2000, which
 *   performs real D-state transitions - a running xHC entering D3 is doing DMA
 *   into memory nobody is expecting it to touch, and the spec's power-management
 *   sequence (4.23.2, p.313) begins by stopping the controller for exactly that
 *   reason.
 *
 *   Masking the interrupt enables is part of the same obligation, and cannot be
 *   left to usbport. The Phase 3 spike traced DisableInterrupts around the
 *   *shutdown* sequence, but Win98's NUSB usbport also issues suspend/resume
 *   pairs repeatedly at idle, and nothing observed says those are bracketed the
 *   same way. Once task 6 enables IMAN.IE, a suspend that left it set would leave
 *   the controller able to assert INTx while this driver has already dropped
 *   XHCI_EXT_FLAG_INITIALIZED - so the ISR would decline every one of them, and
 *   the line would stay asserted with nobody to claim it.
 *
 * The earlier version of this pair did neither, on the argument that halting
 * without a restore would stop USB dead at Win98's first idle suspend. That
 * argument was wrong, and the specification says why: "the internal state of the
 * xHC shall be valid until it enters the D3cold state ... If prior to setting
 * the xHC into the D3cold state, software decides to restart the xHC, then a
 * Restore State operation is not required" (4.23.2, p.314). A halt is not a loss
 * of state. An idle suspend/resume pair that never reaches D3cold costs a halt
 * and a restart, and nothing else.
 *
 * **What is deliberately not implemented is CSS/CRS**, the Save State and
 * Restore State flags, and the reason is that they have nothing to preserve yet.
 * Their purpose is to carry internal Slot, Endpoint and Stream state across a
 * D3cold transition (4.23.2.1, p.315); at this point in Phase 4 there are no
 * slots, no endpoints and no outstanding commands, so a full reinitialization
 * restores the controller to a state indistinguishable from the one it left -
 * it re-derives every capability register from the hardware and reprograms
 * DCBAAP, CRCR, the event ring, CONFIG and port power from a common buffer that
 * usbport does not reclaim across a suspend. The protocol becomes necessary in
 * Phase 6, when a resume would otherwise drop device contexts, and it needs the
 * register-image save and restore of steps 4 and 4 (p.313-314) to be worth
 * anything. Roadmap Phase 4 task 8 carries it with that trigger recorded.
 *
 * IRQL: PASSIVE_LEVEL (both halt and reinitialize).
 */
/* ------------------------------------------------------------------ */
/* The 32-bit frame number (task 6-B.1)                                */
/* ------------------------------------------------------------------ */

/* IRQL: <= DISPATCH_LEVEL. See the contract in src/xhci_hw.h. */
ULONG XhciFrameNumber(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    ULONG mfindex;
    ULONG frame;
    ULONG value;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return 0;
    }

    XhciControllerLockAcquire(&oldIrql);

    /*
     * The same admission the ISR and the health poll use, and for the same
     * reason: MFINDEX is a runtime register reached through HcInfo's decoded
     * base, and a controller that is halted, re-deriving those bases, or failed
     * has no frame to report. It is not an error case - see the stall path
     * below, which is the *ordinary* answer on Win98.
     */
    if (ext->HcInfoStatus == XHCI_HC_OK && !ext->ControllerFailed &&
        (ext->Flags & (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) ==
            (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) {
        mfindex = XhciReadRt(ext, XHCI_RT_MFINDEX);
        if (mfindex != 0xFFFFFFFFUL) {
            frame = (mfindex & XHCI_MFINDEX_MASK) >> XHCI_MFINDEX_FRAME_SHIFT;

            if (ext->FrameSynced) {
                /*
                 * A **delta**, not an absolute. MFINDEX carries eleven bits of
                 * frame and wraps every 2,048 of them, so an absolute reading
                 * goes backwards twice a second - and usbport compares this
                 * number against a stamp taken earlier, which a backwards step
                 * makes unreachable for good. Masking the subtraction is what makes the
                 * wrap invisible: the difference is correct modulo 2,048
                 * whichever side of a wrap the two readings are on.
                 */
                ext->FrameNumber +=
                    (frame - ext->FrameLast) & XHCI_MFINDEX_FRAME_MASK;
            } else {
                ULONG skew;

                /*
                 * The first read after a stall. MFINDEX restarts at zero after
                 * HCRST and simply stops while the controller is halted, so the
                 * previous FrameLast describes a different run of the counter
                 * and no delta can be taken from it.
                 *
                 * **Task 9-A.1: the resync also re-establishes congruence**, and
                 * that is a second job this branch did not do. Until it, the
                 * published number was simply left where the stall path had put
                 * it - so its low 11 bits stood in no relation to MFINDEX's
                 * Frame Index, and an isochronous Frame ID derived from a
                 * usbport frame stamp would have named an arbitrary frame.
                 * Advancing to the next congruent value is the smallest change
                 * that fixes it, and it is **forward only**: the masked
                 * difference is 0..2047, so the resync never subtracts, which is
                 * the one property usbport's uncapped post-open wait rests on
                 * (batch 6-0). A skew of zero is the ordinary case for the very
                 * first sync of a start, where nothing has stalled yet.
                 *
                 * "Forward only" is a statement about this addition and not
                 * about the published number, which is 32 bits wide and
                 * therefore wraps - a review round read an earlier draft of this
                 * comment as claiming otherwise. At the wrap a stamp taken just
                 * below `0xFFFFFFFF` is not passed again for a full lap, and
                 * that is a property of a 32-bit frame number rather than of the
                 * resync: the synced branch above adds a masked delta to the
                 * same field and crosses the same boundary. 2^32 is a multiple
                 * of 2,048, so congruence itself survives the wrap intact.
                 */
                skew = (frame - ext->FrameNumber) & XHCI_MFINDEX_FRAME_MASK;
                ext->FrameNumber += skew;
                ext->FrameResyncSkew += skew;
                ext->FrameSynced = 1;
                ext->FrameCongruent = 1;
            }
            ext->FrameLast = frame;
            /* This *is* a reading of MFINDEX, so it refreshes the axis exactly
             * as the health poll's own sample does - usbport's frequent calls
             * are the common case and the poll is the backstop, rather than the
             * poll being the only thing that counts. */
            ext->FrameSampleStale = 0;
            value = ext->FrameNumber;
            XhciControllerLockRelease(oldIrql);
            return value;
        }
        ext->FrameReadFailures++;
    }

    /*
     * **The stall path, and it increments rather than repeats.** usbport's
     * post-open wait is an uncapped loop that ends only when this number passes
     * a frame it stamped earlier (batch 6-0), and Win98 idle-suspends the
     * controller within about half a second of every start - so a reader that
     * answered the same value while halted would hang the enumerating thread at
     * PASSIVE_LEVEL for good.
     *
     * Answering with the call count rather than with time is a departure from
     * what this number means, and it is the safe direction: no USB traffic can
     * be in flight on a halted controller, so the only thing this advancement
     * can release is a software state transition that was waiting on nothing.
     * The counter beside it is what says how often that happened.
     */
    ext->FrameStalls++;
    ext->FrameSynced = 0;
    /* Task 9-A.1. The number about to be published counts *calls*, not frames,
     * so it stops being MFINDEX's Frame Index in its low 11 bits - and an
     * isochronous submission taken from here on must use SIA rather than a Frame
     * ID derived from a usbport stamp. Cleared before the increment rather than
     * after, so no reader can see the new value under the old claim. */
    ext->FrameCongruent = 0;
    ext->FrameNumber++;
    value = ext->FrameNumber;

    XhciControllerLockRelease(oldIrql);
    return value;
}

/* IRQL: DISPATCH_LEVEL, controller lock **held**. See src/xhci_hw.h. */
ULONG XhciFrameIdNow(PXHCI_EXTENSION ext, ULONG *frameId)
{
    ULONG mfindex;
    ULONG live;

    if (ext == NULL || frameId == NULL) {
        return 0;
    }
    *frameId = 0;
    /*
     * **The congruence claim is checked first, and it is the whole gate.** A
     * Frame ID is only meaningful beside frame *numbers* from the same axis, and
     * the numbers an isochronous request carries came from
     * `Get32BitFrameNumber`. While `FrameCongruent` is clear those two axes are
     * related by an offset nothing has recorded, so a window test against this
     * register would be arithmetic on two different clocks.
     */
    if (!ext->FrameCongruent) {
        return 0;
    }
    if (ext->HcInfoStatus != XHCI_HC_OK || ext->ControllerFailed ||
        (ext->Flags & (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) !=
            (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) {
        return 0;
    }

    mfindex = XhciReadRt(ext, XHCI_RT_MFINDEX);
    if (mfindex == 0xFFFFFFFFUL) {
        ext->FrameReadFailures++;
        return 0;
    }
    /*
     * Read live rather than taken from `FrameLast`, and the difference is not
     * cosmetic: `FrameLast` is whatever the last `Get32BitFrameNumber` saw, and
     * a stale value is *behind*, which moves the Valid Frame Window's start
     * earlier and would accept Frame IDs that are in fact inside the
     * Isochronous Scheduling Threshold. The failure direction of the cheap
     * answer is the unsafe one.
     *
     * **Answered in the published 32-bit domain**, which is the domain the
     * packet stamps it will be compared against are in. The register gives 11
     * bits; the missing high bits come from `FrameNumber`, advanced forward to
     * the live Frame Index by the same masked difference the resync uses - legal
     * precisely because `FrameCongruent`, checked above, is the statement that
     * those two agree in their low 11 bits. Returning the bare index instead is
     * what let a stamp a whole lap in the past pass the Valid Frame Window (batch
     * 9-A review round 2); the window arithmetic cannot recover a lap the caller
     * was never given.
     */
    live = (mfindex & XHCI_MFINDEX_MASK) >> XHCI_MFINDEX_FRAME_SHIFT;
    /*
     * **The reconstruction is only as good as the axis is fresh**, and the third
     * review round is why this sentence is here rather than an assumption.
     *
     * The masked difference recovers the low eleven bits and nothing more, so it
     * cannot tell "fifty frames since the last sample" from "fifty frames plus a
     * whole 2,048-frame lap". If the axis went unsampled for longer than a lap -
     * 2.048 seconds - the number this returns is a lap or more behind the
     * hardware, a request that is late reads as being in the near future, and it
     * gets an explicit Frame ID that will not execute until those eleven bits
     * come round again. That is the same class of wrong answer the 32-bit window
     * was introduced to remove, one level further down.
     *
     * What bounds it is `XhciFrameSample`, called from the health poll: while the
     * controller runs, the axis is resampled far more often than once per lap, so
     * no lap can pass unobserved. The bound is therefore the poll's period, and
     * `FrameSampleStale` is what says no reading has been taken since the last
     * poll marked it - which refuses rather than letting this arithmetic answer
     * from an axis nothing has refreshed. Any successful MFINDEX read clears it,
     * `Get32BitFrameNumber`'s included, so on a busy bus the poll is a backstop
     * rather than the only thing that keeps a Frame ID claimable.
     */
    if (ext->FrameSampleStale) {
        return 0;
    }
    *frameId = ext->FrameNumber +
               ((live - ext->FrameNumber) & XHCI_MFINDEX_FRAME_MASK);
    return 1;
}

/*
 * Resample the published frame axis from MFINDEX, from the health poll.
 *
 * This exists for one reason: to keep the gap between two consecutive readings
 * of MFINDEX's eleven-bit Frame Index below one 2,048-frame lap, so that
 * `XhciFrameIdNow`'s reconstruction of the high bits cannot silently lose one.
 * usbport calls `Get32BitFrameNumber` often in practice, but "often" is a
 * description of two binaries rather than a bound this driver may rely on.
 *
 * It advances the published number by the same masked delta the synced branch of
 * `XhciFrameNumber` uses, so it is forward-only and preserves congruence; it
 * publishes nothing usbport has not already been told it may see, because the
 * number only ever moves the way the hardware moved.
 *
 * IRQL: DISPATCH_LEVEL, controller lock **held**.
 */
VOID XhciFrameSample(PXHCI_EXTENSION ext)
{
    ULONG mfindex;
    ULONG frame;

    if (ext == NULL) {
        return;
    }
    /*
     * **Marked stale first and cleared only by a reading**, so the flag means
     * "the most recent poll actually sampled the axis" rather than "a poll ran".
     * A poll that reaches a controller it cannot read therefore refuses Frame IDs
     * instead of letting the reconstruction answer from an axis nothing
     * refreshed. What it does *not* cover is the poll never running at all: that
     * is the same condition task 7b-A.0's progress detector and task 7a-B's age
     * detector already rest on, and it is named rather than defended against
     * twice.
     */
    ext->FrameSampleStale = 1;
    if (!ext->FrameSynced || ext->HcInfoStatus != XHCI_HC_OK ||
        ext->ControllerFailed ||
        (ext->Flags & (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) !=
            (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) {
        /*
         * Nothing to sample. The axis is not congruent anyway - the stall path
         * clears that - so a Frame ID is already refused; the staleness flag is
         * left where it is rather than being cleared by a poll that read
         * nothing.
         */
        return;
    }
    mfindex = XhciReadRt(ext, XHCI_RT_MFINDEX);
    if (mfindex == 0xFFFFFFFFUL) {
        ext->FrameReadFailures++;
        return;
    }
    frame = (mfindex & XHCI_MFINDEX_MASK) >> XHCI_MFINDEX_FRAME_SHIFT;
    ext->FrameNumber += (frame - ext->FrameLast) & XHCI_MFINDEX_FRAME_MASK;
    ext->FrameLast = frame;
    ext->FrameSamples++;
    ext->FrameSampleStale = 0;
}

/*
 * Task 13-R.3.5: the health poll's millisecond clock.
 *
 * **A separate axis from the published frame number above, deliberately.** That
 * one is what usbport compares its own stamps against, so it carries a stall
 * path that increments per *call* rather than per frame, and a congruence claim
 * that an isochronous Frame ID depends on. Neither property belongs in a
 * watchdog clock: a number that advances because somebody called it would let a
 * command age out on the strength of usbport asking the time, and a clock that
 * stopped whenever a Frame ID became unclaimable would stop for reasons that
 * have nothing to do with the command it is timing. So this reads the same
 * register and keeps its own two fields.
 *
 * The arithmetic is the same masked difference, and it is exact for any gap
 * below one 2,048-frame lap. **A gap longer than that is undercounted, and that
 * is the safe direction**: the clock runs slow, so every threshold measured on
 * it fires late rather than early. That is the property the poll-count design
 * claimed and did not have - it assumed the poll could only be slower than
 * 500 ms, and the E460 polls at 36-80 ms.
 *
 * **A gap this cannot see at all is a gap it does not add.** When the axis
 * cannot be read - the controller is not running, or the window is not decoding
 * - the sync is dropped and the next reading starts a fresh difference, so the
 * unmeasured interval is simply not counted. Late again, never early.
 *
 * IRQL: DISPATCH_LEVEL, controller lock **held**. See src/xhci_hw.h.
 */
VOID XhciPollClockAdvance(PXHCI_EXTENSION ext)
{
    ULONG mfindex;
    ULONG frame;

    if (ext == NULL) {
        return;
    }
    /*
     * XHCI_EXT_FLAG_RUNNING as well as INITIALIZED, which the poll's own gate
     * does not require: MFINDEX does not count on a halted xHC, so a reading
     * taken there is not a time. It is not a case that leaves a live watchdog
     * with a frozen clock - a command can only be submitted on a running
     * controller (xhciCommandSubmitEx), and the transition that stops one
     * runs through XhciControllerBeginQuiesce, which invalidates the outstanding
     * command and moves the generation on.
     */
    if (ext->HcInfoStatus != XHCI_HC_OK || ext->ControllerFailed ||
        (ext->Flags & (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) !=
            (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) {
        ext->PollClockSynced = 0;
        ext->PollClockStalls++;
        return;
    }
    mfindex = XhciReadRt(ext, XHCI_RT_MFINDEX);
    if (mfindex == 0xFFFFFFFFUL) {
        /* Counted here and not in FrameReadFailures, which belongs to the axis
         * above and is read against its own counters. */
        ext->PollClockSynced = 0;
        ext->PollClockStalls++;
        return;
    }
    frame = (mfindex & XHCI_MFINDEX_MASK) >> XHCI_MFINDEX_FRAME_SHIFT;
    if (ext->PollClockSynced) {
        ext->PollClockMs +=
            (frame - ext->PollClockFrame) & XHCI_MFINDEX_FRAME_MASK;
    }
    ext->PollClockFrame = frame;
    ext->PollClockSynced = 1;
}

/* ------------------------------------------------------------------ */
/* Controller Save/Restore State (task 6-B.6)                          */
/* ------------------------------------------------------------------ */

/*
 * How long the SSS/RSS wait runs before giving up.
 *
 * The specification puts no bound on either operation, so this is a policy
 * number, and the *direction* of being wrong is what it is chosen for: giving up
 * too early reports a failed save or restore, which costs a reinitialisation
 * this driver was doing unconditionally before task 6-B.6, while never giving up
 * hangs a power transition. One second is two orders of magnitude above the
 * halt and reset waits beside it.
 */
#define XHCI_SAVE_RESTORE_TIMEOUT_MS 1000UL

/*
 * Wait for a Save or Restore to finish, and say whether it succeeded.
 *
 * Two things here are counter-intuitive and both are quoted in
 * docs/usb-xhci-info/xhci-data-structures.md section 3. **A cleared SSS/RSS is not success**:
 * "If the saved state is corrupted, the SRE flag ... shall be set to '1', the
 * Restore operation terminated, and the RSS flag cleared to '0'" (p.315), so the
 * status bit says only that the controller has stopped working on it. SRE is the
 * only verdict. And **CSS and CRS cannot be polled at all** - "This flag always
 * returns '0' when read" (Table 5-20, p.361) - which is also why they are
 * strobed through xhciWriteUsbCmd's read-modify-write without ever being read
 * back.
 *
 * Returns 1 on success. IRQL: PASSIVE_LEVEL (it waits).
 */
static ULONG xhciSaveRestoreWait(PXHCI_EXTENSION ext, ULONG busyBit)
{
    ULONG usbsts;

    usbsts = 0;
    if (!XhciWaitForBits(ext, ext->HcInfo.OperationalOffset + XHCI_OP_USBSTS,
                         busyBit, 0, XHCI_SAVE_RESTORE_TIMEOUT_MS, &usbsts)) {
        ext->LastSaveRestoreStatus = usbsts;
        ext->SaveRestoreTimeouts++;
        return 0;
    }
    ext->LastSaveRestoreStatus = usbsts;

    /*
     * An all-ones window cannot report a cleared busy bit - the wait above would
     * still be waiting - so reaching here with one means the read that satisfied
     * it was a real register. Checked anyway, because "cannot happen" and
     * "checked" cost the same here and only one of them survives a controller
     * that surprise-removes mid-operation.
     */
    if (usbsts == 0xFFFFFFFFUL) {
        return 0;
    }

    /*
     * SRE is RW1C and is acknowledged whichever way the verdict goes, because
     * leaving it set would make the *next* operation's check read this one's
     * answer. Written as the single bit, never as the read value ORed back, or
     * every other RW1C bit in USBSTS is acknowledged with it
     * (docs/usb-xhci-info/xhci-data-structures.md section 3).
     */
    if ((usbsts & XHCI_USBSTS_SRE) != 0) {
        XhciWriteOp(ext, XHCI_OP_USBSTS, XHCI_USBSTS_SRE);
        return 0;
    }
    return 1;
}

/*
 * Does this endpoint's quiescence state leave the driver unable to prove the
 * endpoint is not executing? The save gate below asks it of every chain.
 *
 * The bits are the ones that say a command is owed or outstanding
 * (`XHCI_EPQ_INFLIGHT`), that a chain failed (`XHCI_EPQ_FAILED`), or that TRBs
 * were given up on the ring whose Set TR Dequeue Pointer has not landed
 * (`XHCI_EPQ_REPOSITION`, `XHCI_EPQ_FORCE_DEQUEUE`).
 *
 * **`XHCI_EPQ_FAILED | XHCI_EPQ_UNAVAILABLE` is the one pair that is excluded,
 * and audit round 9 found round 8's gate deadlocking on it.** The two bits are
 * set together at exactly one site - `xhciEpArmQuiesce` when `xhciDevAdmitted`
 * refuses, i.e. when the *controller* could take no command - and its own
 * comment says "a successful restore is entitled to take it back".
 * `XhciSlotResumeSweep` is what takes it back, and it clears precisely that
 * pair. So a gate that declines the save on the pair declines the `CSS`, which
 * means no restore, which means the sweep never runs, which means the pair is
 * never cleared: the fix blocked the only recovery it depends on, permanently,
 * for the life of the driver.
 *
 * Excluding it is narrow rather than a relaxation of the gate, and the narrowness
 * is what makes it safe:
 *
 *   The pair says the controller was unavailable, **not** that a stop failed.
 *   An ordinary sticky `XHCI_EPQ_FAILED` - a Stop Endpoint that answered with an
 *   error, or one the engine abandoned - still declines, because there stop
 *   ownership genuinely is unproven.
 *   `xhciEpArmQuiesce` clears `XHCI_EPQ_INFLIGHT` when it sets the pair, so the
 *   remaining bits are still asked: a position debt declines the save whether or
 *   not the chain was marked unavailable.
 *   The queue count is asked separately and independently, so an endpoint that
 *   still has transfers on it declines regardless of any of this. The pair can
 *   only pass on an endpoint whose queue is empty and whose ring has no
 *   outstanding placement.
 *
 * The wider question round 9 also raised - that an ordinary `FAILED` is sticky
 * until a reset-pipe clears it, so one failed chain may cost this controller
 * every later `CSS` - is left as it is, and as of **task 12.1 that
 * is a decision rather than a deferral**. The task's two branches were to build
 * the synchronous suspend-path Stop Endpoint pass, which is the proof this gate
 * declines for want of, or to publish the behaviour; it was taken to publish, so
 * this gate does not change and `docs/using/release-notes.md` says what it costs
 * a user. `EndpointQuiesceFailures` counts every entry into the state, which is
 * what makes the published statement checkable on a running machine -
 * `EndpointQuiesceLost` and `EndpointQuiesceUnavailable` each name one route and
 * neither can answer the question on its own. Reopening it means reopening the
 * task, not editing this gate.
 *
 * **What is not established is that the excluded pair happens in production at
 * all**, and round 10 asked directly. It is set only by an endpoint callback
 * arriving after `XhciQuiesceController` has closed admission, and the whole of
 * that window is inside one `XhciSuspendController` call at PASSIVE - so on
 * Windows 98, which has no preemption, there may be no sequence that both
 * creates the pair and survives to the save. The carve-out is written as a
 * state-machine rule and is exercised as one: the vector sets the pair directly.
 * If a later measurement shows the pair is unreachable, the honest change is to
 * delete the carve-out and the state that produces it together - not to keep a
 * rule nothing can reach and a comment that implies something does.
 */
static ULONG xhciSaveQuiesceUnproven(ULONG flags)
{
    ULONG unproven;

    unproven = XHCI_EPQ_INFLIGHT | XHCI_EPQ_FAILED |
               XHCI_EPQ_REPOSITION | XHCI_EPQ_FORCE_DEQUEUE;
    if ((flags & (XHCI_EPQ_FAILED | XHCI_EPQ_UNAVAILABLE)) ==
        (XHCI_EPQ_FAILED | XHCI_EPQ_UNAVAILABLE)) {
        unproven &= ~XHCI_EPQ_FAILED;
    }
    return ((flags & unproven) != 0) ? 1UL : 0UL;
}

/*
 * Save State, as the last step of a suspend.
 *
 * **Two gates, and they answer two different halves of one specification step.**
 * The save procedure begins: "Stop all USB activity by issuing Stop Endpoint
 * Commands for Busy endpoints in the Running state. If the Force Save Context
 * Capability (FSC = '0') is not supported, then Stop Endpoint Commands shall be
 * issued for all Idle endpoints in the Running state as well. The Stop Endpoint
 * Command causes the xHC to update the respective Endpoint or Stream Contexts in
 * system memory, e.g. the TR Dequeue Pointer, DCS, etc. fields" (4.23.2, p.313).
 * Stop Endpoint on a suspend is task 7a-B.1's and this driver does not issue it,
 * so both halves have to be discharged by declining.
 *
 *   **Busy endpoints** are excluded by the quiescence sweep below. That gate is
 *   as old as this function and it is sound: an endpoint with nothing queued is
 *   not Busy, and an idle suspend - the only kind Win98 performs - passes it.
 *
 *   **Idle-but-Running endpoints are the half the sweep cannot reach**, and it
 *   is not a corner: an empty software queue is exactly what an Idle endpoint in
 *   the Running state looks like from here, so the sweep passes precisely the
 *   set p.313 names. The note under step 3 draws the same line from the other
 *   side - "If FSC = '1', then software shall ensure that any Running endpoint
 *   that did not receive a Stop Endpoint Command is Idle when Run/Stop (R/S) is
 *   cleared" - conditioning idleness-is-enough on FSC = '1' and saying nothing
 *   of the sort for FSC = '0'. So when the controller does not declare FSC, the
 *   only conforming thing left is to decline the save entirely.
 *
 * The cost of that refusal is a resume that reinitialises instead of restoring,
 * which drops the enumerated bus. **Which controllers pay it is measured on one
 * vehicle only**: `qemu-xhci` answers `HCCPARAMS2 = 0`, taken with the qualifier
 *, and no fleet machine's bit has been read - the earlier "two of
 * the three qualified fleet controllers" here was an inference from their
 * HCIVERSION and Appendix H.1.6 (p.593) forbids it. `xhciqual` prints the
 * register as of the same day, so the answer costs one run per machine.
 *
 * It is still the right trade: the alternative is a CSS whose saved image may
 * not carry the endpoints' TR Dequeue Pointer and DCS, and a restore from one of
 * those resumes every endpoint at a stale position with a stale cycle state -
 * silent, and worse than a re-enumeration. The full fix is the Stop Endpoint
 * pass itself, which needs a synchronous command wait on a controller whose
 * interrupts are already masked. **Task 12.1 decided not to build
 * it**, and published the behaviour instead: the pass would be new machinery on
 * the one path no vehicle this project has can exercise - QEMU implements `CRS`
 * as `usbsts |= SRE`, so the successful save/restore has never run anywhere -
 * and untestable code on a silent-failure path is the worse of the two risks.
 * `SavesDeclinedNoFsc` is what says a machine is paying it.
 *
 * Returns 1 when a state was saved. IRQL: PASSIVE_LEVEL.
 */
static ULONG xhciSaveState(PXHCI_EXTENSION ext)
{
    ULONG usbcmd;
    ULONG usbsts;
    ULONG i;

    ext->SavedStateValid = 0;

    if (ext->HcInfoStatus != XHCI_HC_OK) {
        return 0;
    }

    /*
     * FSC, before anything is read or written. See the header: without it the
     * driver cannot make the saved image complete, and this is the earliest
     * point at which that is knowable - HcInfo was decoded at start time.
     */
    if (!ext->HcInfo.Fsc) {
        ext->SavesDeclinedNoFsc++;
        XHCI_DBG_VALUE_CHANGED("save: declined - the controller does not "
                               "declare FSC, HCIVERSION",
                               ext->HcInfo.HciVersion);
        return 0;
    }

    /*
     * "When written by software with '1' and HCHalted (HCH) = '1', then the xHC
     * shall save any internal state ... When written ... with HCHalted (HCH) =
     * '0' ... no Save State operation shall be performed" (Table 5-20, p.361).
     * A save attempted on a controller that did not halt is therefore not a
     * failed save - it is nothing at all, which is worse, because the resume
     * would then restore from whatever the previous suspend left.
     */
    usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
    if (usbsts == 0xFFFFFFFFUL || (usbsts & XHCI_USBSTS_HCH) == 0) {
        return 0;
    }

    /*
     * **Every queue, not just EP0's.** The save procedure begins "Stop all USB
     * activity by issuing Stop Endpoint Commands for Busy endpoints in the
     * Running state" (4.23.2, p.313); this driver has no Stop Endpoint until
     * task 7a-B.1, so the only way to honour it is to decline the save while any
     * endpoint is busy.
     *
     * Checking `Ep0Queue` alone was sufficient until task 7a-A.2 and is not any
     * more, and the difference is not marginal: EP0 is idle between enumeration
     * steps, so the gate almost always passed, while **a HID device keeps an
     * interrupt read posted at all times**, so it would almost always have been
     * wrong. Win98 idle-suspends the controller about a second after every
     * start, which is precisely when a keyboard has a read outstanding.
     *
     * **A queue count is not the whole question, and audit round 8 closed the
     * part round 7 documented and left open.** An empty software queue does not
     * mean the xHC owns no TRBs on that ring: `XhciAbortTransfer` takes a
     * transfer out of the queue while the controller may still be executing it -
     * `AbortsBeforeStopped` counts exactly that window - and `XhciSlotCommandLost`
     * clears `ActiveOp` for a Stop Endpoint that never answered, leaving the
     * chain `XHCI_EPQ_FAILED` with nothing draining it. Both leave a Busy
     * endpoint behind an empty queue, and step 1 asks about Busy endpoints, not
     * about queued transfers: "Stop all USB activity by issuing Stop Endpoint
     * Commands for Busy endpoints in the Running state" (4.23.2, p.313).
     *
     * So the quiescence state is consulted beside the count, through
     * `xhciSaveQuiesceUnproven` - which is where the bits are, together with the
     * one pair that is deliberately not among them and the reason audit round 9
     * had to carve it out.
     *
     * **The cost is real and it is the right way round.** Declining the save
     * means the resume reinitialises and every device re-enumerates, which is
     * visible; saving over a Busy endpoint means restoring a controller whose
     * internal state describes a ring the driver has since rewritten, which is
     * not. A tighter answer - proving the stop completed rather than declining
     * when it cannot be proven - is task 12.1's synchronous Stop Endpoint pass
     * on the suspend path, and this gate is what that pass would relax.
     */
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        ULONG endpoint;
        ULONG busy;

        if (ext->Devices[i].State == XHCI_DEV_STATE_FREE) {
            continue;
        }
        busy = (ext->Devices[i].Ep0Queue.Count != 0 ||
                xhciSaveQuiesceUnproven(ext->Devices[i].Ep0Quiesce.Flags) ||
                ext->Devices[i].ActiveOp != XHCI_DEV_OP_NONE ||
                ext->Devices[i].PendingOp != XHCI_DEV_OP_NONE) ? 1UL : 0UL;
        for (endpoint = 0; endpoint < XHCI_MAX_DEVICE_ENDPOINTS; endpoint++) {
            if (ext->Devices[i].Endpoints[endpoint].Dci != 0 &&
                (ext->Devices[i].Endpoints[endpoint].Queue.Count != 0 ||
                 xhciSaveQuiesceUnproven(
                     ext->Devices[i].Endpoints[endpoint].Quiesce.Flags))) {
                busy = 1;
            }
        }
        if (busy) {
            XHCI_DBG_VALUE_CHANGED("save: declined - a device still has work "
                                   "outstanding, slot", ext->Devices[i].SlotId);
            return 0;
        }
    }

    /*
     * Step 2 of the same procedure: "Ensure that the Command Ring is in the
     * Stopped state (CRR = '0') or Idle (i.e. the Command Transfer Ring is
     * empty), and all Command Completion Events associated with them have been
     * received" (4.23.2, p.313).
     *
     * **This driver does not fully satisfy that step, and the honest thing is to
     * say so rather than to claim the quiesce discharges it.** The first version
     * of this comment did claim exactly that, and it was wrong in the way this
     * repository keeps finding: `XhciQuiesceController` runs
     * `xhciCommandInvalidateLocked`, which is *software* abandonment - it counts
     * `CommandsAbandoned`, tells the owner through `XhciSlotCommandLost`, and
     * sets the engine back to IDLE. It does not stop the ring, does not advance
     * the dequeue past the abandoned TRB, and above all does not **receive that
     * command's Command Completion Event**, which is the clause step 2 names. An
     * abandoned command is a command this driver has stopped waiting for, not
     * one the controller has finished. Clearing R/S afterwards negates CRR
     * (Table 5-24, p.367), but that is step *3*, and a later step cannot
     * retrospectively discharge an earlier one.
     *
     * **What answers the deviation is downstream, in the restore, and it takes
     * two things rather than one.** The first is the ring rebuild: the restore
     * discards the captured command-ring state wholesale, rebuilding from TRB
     * zero and writing CRCR with the rebuilt address and RCS, which
     * re-synchronises the controller's internal dequeue pointer and CCS. So a
     * TRB left owned at suspend time cannot be re-executed after a restore,
     * whatever the saved image said about it.
     *
     * **The second is `XhciEventDiscardStale`, and audit round 5 found this
     * comment claiming the residue was harmless without it.** What it said was
     * that a stale Command Completion Event naming the abandoned TRB "is already
     * classified as unmatched". That is true only while `CommandTrbPA` is zero.
     * `xhciCommandCompleted` matches on address alone, and the rebuild puts the
     * *next* command at the very address the abandoned one had - so the first
     * command the resume issues (`XhciSlotResumeSweep`, immediately after
     * interrupts come back) can be retired by the dead command's event, taking
     * its completion code and its Slot ID with it. The ring rebuild is what
     * creates that collision rather than what prevents it.
     *
     * So the restore now receives those events before setting R/S and drops
     * them, which is step 2 performed late rather than argued away. This comment
     * records the deviation because the *timing* is still not the specification's
     * - the step wants them received before the save - and that difference is
     * only harmless because nothing executes in between.
     *
     * The check below is therefore a **guard on that reasoning, and it is
     * unreachable today**: the quiesce always leaves the engine IDLE, so it
     * cannot fire. It is kept because the restore's licence to discard the ring
     * is what the guard names, and a later edit that gives the suspend path a
     * command of its own would otherwise silently remove that licence.
     */
    if (ext->CommandState != XHCI_CMD_STATE_IDLE) {
        ext->SavesDeclinedCommandBusy++;
        XHCI_DBG_VALUE_CHANGED("save: declined - the command ring is not idle, "
                               "state", ext->CommandState);
        return 0;
    }

    ext->SaveAttempts++;

    /* SRE is cleared by the controller when an operation is initiated, but it is
     * acknowledged here first so that a set bit afterwards is unambiguously this
     * operation's. */
    XhciWriteOp(ext, XHCI_OP_USBSTS, XHCI_USBSTS_SRE);

    usbcmd = XhciReadOp(ext, XHCI_OP_USBCMD);
    if (usbcmd == 0xFFFFFFFFUL) {
        ext->SaveFailures++;
        return 0;
    }
    /* Written from the validated read, so the RsvdP half is the one that was
     * checked - the rule the interrupt paths arrived at over five rounds. R/S is
     * deliberately absent from the named bits: the controller is halted and must
     * stay that way. */
    xhciWriteUsbCmdFrom(ext, usbcmd, XHCI_USBCMD_CSS);

    if (!xhciSaveRestoreWait(ext, XHCI_USBSTS_SSS)) {
        ext->SaveFailures++;
        XHCI_DBG_VALUE("save: failed, USBSTS", ext->LastSaveRestoreStatus);
        return 0;
    }

    ext->SavedStateValid = 1;
    XHCI_DBG_VALUE("save: state saved, USBSTS", ext->LastSaveRestoreStatus);
    return 1;
}

/*
 * Restore State, as the first thing a resume tries.
 *
 * The register writes follow the specification's order exactly - "write DNCTRL,
 * DCBAAP, CONFIG, ERSTSZ, ERSTBA, ERDP, IMAN, IMOD in that order and before
 * CRS" (4.23.2, p.314) - because "the Restore operation overwrites internal
 * default values asserted by a xHC reset" (p.314), i.e. the registers have to
 * describe the structures *before* the controller is told to reload from them.
 * Every one of them points into the controller common buffer, which usbport does
 * not reclaim across a suspend, so "restore the images to the same physical
 * addresses" costs nothing here: they never moved.
 *
 * **Steps 6 and 7 - the command ring - happen after CRS and before R/S**, and
 * they used to be missing outright:
 *
 *   "6. Reinitialize the Command Ring, i.e. so its Cycle bits are consistent
 *   with the RCS value to be written to the CRCR.
 *   7. Write the CRCR with the address and RCS value of the reinitialized
 *   Command Ring. Note that this write will cause the Command Ring to restart at
 *   the address specified by the CRCR." (4.23.2, p.314)
 *
 * Skipping them is not a no-op, because the alternative the specification states
 * for an unwritten CRCR is not "keep the old pointer" but "the Command Ring shall
 * begin fetching Command TRBs using the current value of the internal Command
 * Ring CCS flag" (Table 5-24, p.367) - a value the restore reloaded from the
 * saved image, which the driver's own software ring has no reason to agree with.
 * The first command after such a resume is then ignored or a stale TRB is
 * consumed, and the watchdog escalates to a controller reset.
 *
 * **This is unexercised by construction on every run this project has taken.**
 * QEMU implements CRS as `usbsts |= SRE` and nothing else (batch 6-0), so both
 * target VMs go down the error path and never reach these lines. Whatever result
 * box closes this must say so rather than counting a green suite as evidence.
 *
 * **Step 10 - "Restart each of the previously Running endpoints by ringing
 * their doorbells" - is still not performed, and the reason is not the one this
 * comment first gave.** It said there was "no previously-Running endpoint with a
 * queued TD to restart", which is true and does not discharge the step: step 10
 * names *previously Running* endpoints, not endpoints with queued work, and
 * after the save gate every Running endpoint is precisely an Idle-but-Running
 * one - the same set the FSC clause is about. The actual argument is that the
 * doorbell would be a no-op and its omission is unobservable: an endpoint whose
 * ring holds no TRB the controller owns has nothing to fetch, so ringing it
 * makes the xHC look once and stop. The next `SubmitTransfer` on that endpoint
 * rings the doorbell itself, which is the point at which there is something to
 * fetch. This driver also keeps no record of which endpoints were Running, so
 * performing the step literally would mean adding one for a write with no
 * effect. Stated as a deviation rather than as a discharge.
 *
 * Step 9 - the PORTSC walk - is the caller's XhciRootHubInit.
 *
 * Returns 1 on success. IRQL: PASSIVE_LEVEL.
 */
static ULONG xhciRestoreState(PXHCI_EXTENSION ext)
{
    const XHCI_HC_LAYOUT *layout;
    XHCI_HC_INFO recheck;
    ULONG usbcmd;
    ULONG usbsts;
    ULONG command;
    ULONG fatalEvent;

    if (!ext->SavedStateValid || ext->HcInfoStatus != XHCI_HC_OK) {
        return 0;
    }
    /*
     * Consumed on the attempt, whatever happens. "Undefined behaviour if
     * started while SSS = 1" and a restore performed twice from one save is the
     * same class of thing: the saved state is a one-shot, and a second attempt
     * would be a restore with no save behind it.
     */
    ext->SavedStateValid = 0;

    usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
    if (usbsts == 0xFFFFFFFFUL || (usbsts & XHCI_USBSTS_HCH) == 0) {
        /* "When set to '1' and Run/Stop (R/S) = '1' or HCHalted (HCH) = '0'
         * ... no Restore State operation shall be performed" (Table 5-20). */
        return 0;
    }

    /*
     * **Three things the reinitialisation does that a restore must not skip**,
     * and all three were missing from the first draft - which meant a resume
     * could report every device preserved on a controller that could not
     * possibly serve one.
     *
     * Bus mastering first, because it is the one with no symptom. The suspend's
     * quiesce clears PCI Bus Master Enable as its fallback when the controller
     * will not halt (`XhciQuiesceController`), and the reinitialisation puts it
     * back at XHCI_INIT_STEP_BUS_MASTER_RESTORE - a step a restore never
     * reaches. A restored controller without it runs, accepts doorbells, and
     * delivers nothing at all: no command completion, no transfer event, no
     * port change. Every device would be *claimed* preserved and none would
     * work.
     */
    if (!xhciRestoreBusMaster(ext, &command)) {
        XHCI_DBG_VALUE("restore: bus mastering could not be restored, PCI "
                       "command", command);
        return 0;
    }

    /*
     * Then CNR. "Software shall not write any Doorbell or Operational register
     * of the xHC, other than the USBSTS register, until CNR = '0'" (5.4.2), and
     * every register this function writes below is one of those. Coming back
     * from a power transition is precisely when the bit is set, so a restore is
     * the *most* likely place to write through it - and the writes are silently
     * dropped rather than refused.
     */
    if (!XhciWaitForBits(ext, ext->HcInfo.OperationalOffset + XHCI_OP_USBSTS,
                         XHCI_USBSTS_CNR, 0, XHCI_RESET_TIMEOUT_MS, &usbsts)) {
        XHCI_DBG_VALUE("restore: CNR never cleared, USBSTS", usbsts);
        return 0;
    }

    /*
     * And "is this still the same controller", which the reinitialisation asks
     * twice and a restore asked never. The pointer registers below are written
     * from a layout carved for the controller that went down; if the far end of
     * the mapping came back as something else, they describe somebody else's
     * memory map. One implementation, so the two paths cannot disagree about
     * what "the same controller" means.
     */
    if (XhciDeriveControllerInfo(ext, &recheck) != XHCI_HC_OK ||
        !XhciHcInfoEqual(&ext->HcInfo, &recheck)) {
        XHCI_DBG_TEXT("restore: the controller is not the one that was saved");
        return 0;
    }

    ext->RestoreAttempts++;
    layout = &ext->Layout;

    /*
     * Through the preserving writes, all four of them. This site has even less
     * licence to clear a reserved field than the init path does: the init path
     * at least follows an HCRST, and the argument that a reset image is not a
     * blank cheque had to be made there anyway. Here there is no reset at all -
     * whatever a controller carries in DNCTRL 31:16, CONFIG 31:10, ERSTSZ 31:16
     * or ERSTBA 5:0 is what it carried into the power transition.
     *
     * A refusal fails the restore, for the same reason it does in the IMAN write
     * below: every register in this sequence is one whose value this path is
     * responsible for, and a half-programmed one is worse than a resume that
     * reinitialises.
     *
     * DCBAAP and ERDP stay plain writes - DCBAAP's low six bits are RsvdZ (Table
     * 5-25, p.369) and ERDP has no reserved field (Table 5-42, p.394).
     */
    if (!xhciWriteDnctrl(ext, 0)) {
        ext->RestoreFailures++;
        return 0;
    }
    XhciWrite64(ext, ext->HcInfo.OperationalOffset + XHCI_OP_DCBAAP,
                XhciCommonPA(ext, layout->DcbaaOffset));
    /* Masked, to match the init path at xhciProgramDcbaa. `MaxSlotsEn` is
     * already constrained to the same low-eight-bit field, so this is
     * defence-in-depth - but two writes of the same register disagreeing about
     * whether the field needs masking is how the constraint gets lost. */
    if (!xhciWriteConfig(ext, layout->MaxSlotsEn)) {
        ext->RestoreFailures++;
        return 0;
    }
    if (!xhciWriteErstsz(ext, layout->ErstEntries)) {
        ext->RestoreFailures++;
        return 0;
    }
    if (!xhciWriteErstba(ext, XhciCommonPA(ext, layout->ErstOffset))) {
        ext->RestoreFailures++;
        return 0;
    }
    XhciWrite64(ext, ext->HcInfo.RuntimeOffset + XHCI_RT_IR0 + XHCI_IR_ERDP,
                XhciEventRingErdpValue(&ext->EventRing, 0));
    /*
     * IE clear: the enables are usbport's to ask for, and this is a restore of
     * the pointer state rather than of the interrupt policy. IMOD 0 is the
     * value this driver programs everywhere.
     *
     * Through the read-modify-write, not the literal this used to be, and
     * unlike the acknowledge in xhciProgramEventRing this site **cannot** argue
     * from a reset register image: it follows a controller state restore, not
     * HCRST, so the RsvdP field it must carry back is whatever the restore
     * produced. A refused operand fails the restore, because every register
     * written above it is one whose value this path is responsible for.
     */
    if (!xhciWriteIman(ext, 0)) {
        ext->RestoreFailures++;
        return 0;
    }
    XhciWriteIr0(ext, XHCI_IR_IMOD, 0);

    XhciWriteOp(ext, XHCI_OP_USBSTS, XHCI_USBSTS_SRE);

    usbcmd = XhciReadOp(ext, XHCI_OP_USBCMD);
    if (usbcmd == 0xFFFFFFFFUL) {
        ext->RestoreFailures++;
        return 0;
    }
    xhciWriteUsbCmdFrom(ext, usbcmd, XHCI_USBCMD_CRS);

    if (!xhciSaveRestoreWait(ext, XHCI_USBSTS_RSS)) {
        ext->RestoreFailures++;
        /*
         * **The reading both target VMs produce**, and it is predicted rather
         * than surprising: QEMU 11.0.0 implements a CRS write as `usbsts |= SRE`
         * and nothing else (batch 6-0), so every restore there reports an error.
         * The caller falls back to a full reinitialisation and tells usbport its
         * devices are gone, which is the path the Phase 6 checkpoint exercises.
         */
        XHCI_DBG_VALUE("restore: failed, USBSTS", ext->LastSaveRestoreStatus);
        return 0;
    }

    /*
     * Steps 6 and 7, between CRS and R/S. See the header for the specification
     * text and for why an omitted CRCR write is not the same as leaving the
     * pointer alone.
     *
     * `XhciRingInit` *is* the reinitialisation the step asks for: it zeroes every
     * TRB, puts the enqueue and dequeue back at index 0, sets the producer cycle
     * to 1 and rewrites the Link TRB with the opposite Toggle - so afterwards the
     * ring's Cycle bits are consistent with the RCS this then writes, by
     * construction rather than by agreement.
     *
     * **Two things make discarding the ring's contents safe, and neither is
     * "nothing was on it".** The suspend abandons an outstanding command rather
     * than completing it (see xhciSaveState), so a TRB the controller once owned
     * may well still be sitting there. What matters is that the controller is
     * **halted** - the suspend cleared R/S and this function has not set it, so
     * nothing is executing or fetching while the ring is rewritten - and that the
     * CRCR write below is what **re-synchronises** the xHC's internal dequeue
     * pointer and CCS with the rebuilt ring, so the abandoned TRB cannot be
     * re-executed whatever the restored internal state said about it. That is the
     * same property step 7 exists for, applied to a ring this driver gave up on
     * rather than to one it finished with.
     *
     * CRR is '0' for the same reason - R/S is clear - which is the state in which
     * the pointer and RCS are latched at all ("Writes to this field are ignored
     * when Command Ring Running (CRR) = '1'", p.368). Ordering is therefore the
     * specification's own: reinitialise, write, and only then run.
     */
    if (XhciRingInit(&ext->CommandRing,
                     (volatile XHCI_TRB *)
                         XhciCommonAt(ext, layout->CommandRingOffset),
                     XhciCommonPA(ext, layout->CommandRingOffset),
                     layout->CommandRingTrbs,
                     XHCI_RING_KIND_COMMAND) != XHCI_RING_OK) {
        ext->RestoreFailures++;
        XHCI_DBG_TEXT("restore: the command ring could not be reinitialized");
        return 0;
    }
    if (!XhciWriteCrcr(ext,
                       ext->CommandRing.BasePA,
                       ext->CommandRing.Cycle ? XHCI_CRCR_RCS : 0,
                       NULL)) {
        ext->RestoreFailures++;
        return 0;
    }

    /*
     * **Save step 2, discharged here rather than at the save.** The rebuild
     * above is exactly what makes this necessary: the command ring restarts at
     * TRB zero, so the next command the resume issues can be handed the same
     * physical address an abandoned one had, and command completions are matched
     * by address alone. A stale event left on the ring would then retire the
     * wrong command. `XhciEventDiscardStale` receives them - the word step 2
     * uses - and drops them; its body has the argument for why dropping is
     * right.
     *
     * Before R/S, so nothing is producing events while the ring is walked, and
     * before the caller's `XhciSlotResumeSweep`, which is the first thing that
     * can issue a command.
     *
     * **Some of what it can find are not droppable**, and three rounds have now
     * widened that set, each finding its material inside the previous round's
     * fix. A Host Controller Event on the ring is a controller-level fault the
     * save gate never excluded, reported nowhere in `USBSTS` - not in `HCE`, not
     * in `HSE` (round 6). A Transfer Event carrying `Event Lost` is the same loss
     * reported per endpoint, on an endpoint the xHC has halted for it (4.10.1,
     * p.173), and round 6's prose had Event Lost arriving only as the former,
     * which is what hid it (round 7). **Round 8 then found that counting them was
     * the wrong shape**: Table 6-90 marks Undefined Error fatal in as many words
     * and hands the vendor error range the same reading, and Incompatible Device
     * Error owes a Disable Slot that a successful restore would preserve the slot
     * past. The drain now asks `XhciXferCodeInfo` instead of carrying a list, so
     * this comment does not carry one either.
     *
     * Restoring on top of any of them would restart the controller that raised it
     * and consume the only notice of it there will be. The restore fails instead,
     * which is not a report but the strongest repair this path has: the caller
     * falls through to the reinitialisation, which drops every device, resets the
     * controller and builds a fresh event ring.
     *
     * `RestoreEventsFatal` counts them; `RestoreFatalKind` and `RestoreFatalCode`
     * say which kind ended it and which completion code it carried, both readable
     * from a release build. `LastHostControllerCode` is the DPC's field and is
     * written here only by the Host Controller Event arm - audit round 8 found
     * three places implying a machine could read the kind out of it, which is why
     * the kind now has a field of its own.
     */
    fatalEvent = XHCI_RESTORE_FATAL_NONE;
    (VOID)XhciEventDiscardStale(ext, &fatalEvent);
    if (fatalEvent != XHCI_RESTORE_FATAL_NONE) {
        ext->RestoreFailures++;
        XHCI_DBG_VALUE("restore: refused - a fatal event was waiting on the "
                       "ring, XHCI_RESTORE_FATAL_*", fatalEvent);
        XHCI_DBG_VALUE("restore: refused - the completion code it carried",
                       ext->RestoreFatalCode);
        return 0;
    }

    XHCI_DBG_VALUE("restore: state restored, USBSTS",
                   ext->LastSaveRestoreStatus);
    return 1;
}

VOID XhciSuspendController(PXHCI_EXTENSION ext)
{
    if (ext == NULL) {
        return;
    }

    ext->SuspendCount++;

    /*
     * **Task 11-V.9's "never per-event" tier, and this is the case that names
     * it.** Windows 98's usbport idle-suspends this controller within about
     * half a second of the last transfer and did it **29 times in a single idle
     * run** (the Phase 4 checkpoint runs), so a record per suspend would fill
     * the ring with the machine doing nothing. The first one is worth a record
     * because "this target idle-suspends at all" is a fact about the platform;
     * the rest are `power.suspends` in the counter block at flush.
     *
     * Task 11-V.6's `DisableSelectiveSuspend` removes these on the shipping
     * Win98 install path, which makes this cheaper and does not make it
     * optional: the value is machine-wide and a machine can have it cleared.
     */
    if (ext->SuspendCount == 1) {
        XhciLogNote(ext, "power.suspend.first", ext->Flags);
    }

    if (ext->HcInfoStatus != XHCI_HC_OK) {
        /* Nothing was ever decoded, so there is no register to touch and
         * nothing for the resume to restore. */
        (VOID)XhciControllerUpdateFlags(ext, 0,
                                        XHCI_EXT_FLAG_SUSPENDED);
        return;
    }

    /*
     * Deliver anything still parked on the completion list before the world
     * stops (Phase 7 review, B4). The "next DPC or poll" the held-completion
     * comments lean on does not exist while suspended: usbport gates both the
     * worker and the 500 ms timer on its HC_SUSPEND flag, and the quiesce
     * below clears INITIALIZED so the event DPC declines too - a completion
     * that loses the race to Win98's ~0.5 s idle suspend would otherwise sit
     * until resume or an abort. The depth is provably 0 here: usbport does
     * not call SuspendController from inside a SubmitTransfer, so nothing is
     * held back.
     */
    XhciSlotDeferredWork(ext);

    /* Quiesce masks before closing ISR/DPC admission and before the halt, even
     * when the halt later fails. Win98's unbracketed idle suspend depends on it. */
    if (!XhciQuiesceController(ext)) {
        /*
         * The controller would neither halt nor give up bus mastering. It is
         * about to be powered down while capable of DMA, and there is nothing
         * further this driver can legally do - HCRST is forbidden on a running
         * controller (5.4.1, p.360). Counted, because it is the evidence a later
         * corruption would need.
         *
         * **Deliberately not XhciFailClosedDma**, and the difference is what
         * that call is for rather than a gap in it: a suspend does not reclaim
         * the common buffer, so an xHC still mastering here is writing into
         * pages this driver still owns and nobody else will be given. That is a
         * fault to count, not a reason to bugcheck a machine that is going to
         * sleep. Stop and the two failed-start paths reclaim, and they escalate.
         */
        ext->SuspendFailures++;
        XHCI_DBG_TEXT("SuspendController: controller would not stop - it may "
                      "enter D3 still capable of DMA");
    }

    (VOID)XhciControllerUpdateFlags(ext, 0, XHCI_EXT_FLAG_SUSPENDED);

    /*
     * The poll clock's sync goes with the halt, and this is the half
     * XhciInitController's own drop does not cover: MFINDEX does not count on a
     * halted xHC, and a resume that succeeds through the *restore* never
     * reinitialises, so PollClockFrame would be compared against whatever the
     * counter reads on the other side of the power transition. Dropping it
     * leaves the suspended interval simply uncounted - late, never early.
     */
    ext->PollClockSynced = 0;

    /* Kept for the trace and for the resume's own record. */
    ext->SuspendUsbCmd = XhciReadOp(ext, XHCI_OP_USBCMD);
    XHCI_DBG_VALUE("SuspendController: halted, USBCMD", ext->SuspendUsbCmd);

    /*
     * Task 6-B.6, and it is the last thing the suspend does because it is the
     * only thing that has to happen on a *halted* controller: CSS is ignored
     * outright unless HCH is set (Table 5-20, p.361).
     *
     * The devices are deliberately left standing. A suspend that tore them down
     * would leave nothing to save, which is precisely the reinitialise-and-lose
     * -the-bus behaviour Phase 4 task 5 deferred and this task repeals - Win98
     * idle-suspends within about a second of every start, so on that target the
     * bus would be dismantled and rebuilt continuously.
     */
    (VOID)xhciSaveState(ext);
}

/* IRQL: PASSIVE_LEVEL. */
MPSTATUS XhciResumeController(PXHCI_EXTENSION ext)
{
    MPSTATUS status;

    if (ext == NULL) {
        return MP_STATUS_FAILURE;
    }
    if ((XhciControllerUpdateFlags(ext, XHCI_EXT_FLAG_SUSPENDED, 0) &
         XHCI_EXT_FLAG_SUSPENDED) == 0) {
        return MP_STATUS_SUCCESS;
    }

    /*
     * Nothing was programmed to begin with, so there is nothing to bring back.
     * This is the shape of a suspend that arrived after a start refused.
     */
    if (ext->HcInfoStatus != XHCI_HC_OK) {
        return MP_STATUS_SUCCESS;
    }

    /*
     * **Try the restore first** (task 6-B.6). Its whole reason for existing is
     * what the paragraph below used to end with: a reinitialisation does not
     * preserve enabled Slot IDs or their device contexts, and from Phase 6 there
     * are some. Win98 idle-suspends within about a second of every start, so a
     * resume that reinitialised would take an enumerated bus down and put it
     * back up continuously.
     *
     * A restore that succeeds returns here with every slot, endpoint and ring
     * exactly where the suspend left them, so nothing else has to be done to
     * them - the device records in this extension were never touched.
     */
    if (ext->SavedStateValid) {
        ULONG restored;

        restored = xhciRestoreState(ext);
        if (restored) {
            ULONG usbsts;

            /*
             * R/S last, after every pointer register and after CRS: step 8 of
             * the restore procedure (4.23.2, p.314). Through the same
             * xhciRunController the start uses, so a controller that takes the
             * write and stays halted is caught here exactly as it is there - a
             * restore that reprogrammed every register and then did not start is
             * a failed resume, not a successful one, and the suite found this
             * missing by noticing that a refuseRun controller passed.
             */
            usbsts = 0;
            if (!xhciRunController(ext, &usbsts)) {
                XHCI_DBG_VALUE("ResumeController: restored but would not run, "
                               "USBSTS", usbsts);
                restored = 0;
            }
        }
        if (restored) {
            (VOID)XhciControllerUpdateFlags(ext, 0, XHCI_EXT_FLAG_INITIALIZED);
            /*
             * "The state of a Root Hub port is not covered by a Save or Restore
             * operation" (p.315), which is why step 9 of the procedure -
             * re-initialising every PORTSC - is mandatory rather than optional.
             * XhciRootHubInit is that step: it re-seeds every shadow from a live
             * read, latching whatever changed while the controller was down.
             *
             * `afterRestore` is 1 because this is the one path on which the port
             * registers themselves survived - see finding A5 and the pass the
             * flag suppresses.
             *
             * **And its status is not discarded** (finding A6). The start path
             * fails the whole start on a nonzero, and the asymmetry was latent
             * rather than harmless: a rebuild that refused would leave every
             * status query answering "no such managed port" on a controller that
             * is running, with no path back. Unreachable today - the inputs have
             * not changed since the successful start that built the map - so the
             * treatment is the one that costs nothing when it never fires: give
             * up on the restore and take the reinitialisation, which rebuilds
             * from HCRST.
             */
            if (XhciRootHubInit(ext, 1) != XHCI_RH_OK) {
                XHCI_DBG_VALUE("ResumeController: restored but the root hub "
                               "would not rebuild, status",
                               ext->RootHub.Status);
                (VOID)XhciControllerUpdateFlags(ext, XHCI_EXT_FLAG_INITIALIZED,
                                                0);
                restored = 0;
            }
        }
        if (restored) {
            if ((ext->Flags & XHCI_EXT_FLAG_INTERRUPTS) != 0) {
                XhciEnableInterrupts(ext);
            }
            /* A REMOVE or PAUSE delivered inside the suspend window left its
             * quiesce chain FAILED-by-unavailability, and this is the one
             * path on which the record survives to be taken back (Phase 7
             * review, B3). */
            XhciSlotResumeSweep(ext);
            XhciRootHubDeferredWork(ext);
            XhciSlotDeferredWork(ext);
            return MP_STATUS_SUCCESS;
        }

        /*
         * The restore failed, which in both target VMs is the *expected* reading
         * rather than a defect: QEMU implements CRS as "set SRE" and nothing
         * else. Fall through to the reinitialisation, which drops the devices.
         */
        XHCI_DBG_VALUE_CHANGED("ResumeController: restore failed, "
                               "reinitializing, USBSTS",
                               ext->LastSaveRestoreStatus);
    }

    /*
     * **Every path that reaches the reinitialisation drops every device first**,
     * and it is unconditional rather than the failed-restore path's business: a
     * suspend that never took a save reaches here too, and HCRST takes the
     * contexts either way. Doing it here rather than inside XhciInitController
     * is what makes the difference between dropping a device and *losing* one -
     * this completes the transfers usbport is holding for it, where the start
     * path's XhciSlotInit simply clears a table usbport has already zeroed.
     *
     * A failed restore must never present as a successful one, and after this
     * line it cannot: the address map is empty, so a later EP0 open at an
     * address nothing holds is refused rather than served against a slot the xHC
     * no longer has.
     */
    {
        KIRQL oldIrql;
        ULONG usbsts;
        ULONG halted;

        /*
         * **Read the controller rather than trust the flags.** The suspend
         * halted it, or tried to; a failed restore may have left it part-way
         * through a CRS. Whether its slots can still be released is a question
         * about the hardware, and the reinitialisation below has not halted or
         * reset anything yet - so a record released here on the strength of
         * "we are about to reset it" would be released while the xHC still had
         * that slot enabled, and a preflight refusal would leave it that way
         * for good.
         */
        usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
        halted = (usbsts != 0xFFFFFFFFUL &&
                  (usbsts & XHCI_USBSTS_HCH) != 0) ? 1UL : 0UL;

        XhciControllerLockAcquire(&oldIrql);
        XhciSlotInvalidateAll(ext, halted);
        XhciControllerLockRelease(oldIrql);
        XhciSlotDeferredWork(ext);
    }

    /*
     * The full bounded reinitialization roadmap task 8 asks for, and the reason
     * it is affordable here rather than a last resort: it re-derives every
     * capability register from the controller and reprograms every pointer
     * register from the common buffer, which usbport does not reclaim across a
     * suspend - so it is correct whether the controller kept its state, lost it
     * in D3cold, or came back as something that has to be refused. NULL is what
     * says usbport is not handing resources over this time; the extension's own
     * copies are the source.
     */
    ext->ResumeReinits++;
    /* The other half of the pair above, on the same rule: one record, then a
     * count. A reinitialisation is more interesting than a suspend - it takes
     * the whole bus down and puts it back up - which is why the *first* one is
     * recorded rather than none, and why `power.resumes` is in the counter
     * block beside `power.suspends` so a gap between them is readable. */
    if (ext->ResumeReinits == 1) {
        XhciLogNote(ext, "power.resume.first", ext->Flags);
    }
    status = XhciInitController(ext, NULL);
    if (status != MP_STATUS_SUCCESS) {
        ext->ResumeFailures++;
        XHCI_DBG_VALUE("ResumeController: reinitialization refused at step",
                       ext->InitStep);
        return status;
    }

    /*
     * Put the interrupt enables back if usbport had them on when the suspend
     * arrived. The reinitialization deliberately leaves them masked - that is
     * EnableInterrupts' job on the ordinary start path - so without this a
     * resume would return a controller that never interrupts again.
     *
     * Through XhciEnableInterrupts rather than XhciUnmaskInterrupts, because
     * this path has the identical hazard: the reinitialization above runs the
     * controller and powers the ports, so a device that is still attached
     * produces its Port Status Change Event before this line is reached, and
     * the interrupter is already holding IP with Event Handler Busy set. Note
     * that usbport does *not* call EnableInterrupts after a successful resume -
     * only after it restarts a controller whose resume failed (ReactOS
     * power.c:192-212) - so this is the only place that release can happen.
     */
    if ((ext->Flags & XHCI_EXT_FLAG_INTERRUPTS) != 0) {
        XhciEnableInterrupts(ext);
    }

    /*
     * **Announce what the reinitialization's seed found** (Phase 5 tasks 5
     * and 6). This is the announcement's load-bearing case rather than an extra:
     * after a resume the root hub PDO already exists and nothing rescans it, so
     * a device plugged in while the controller was suspended is latched by the
     * seed, acknowledged in hardware - which stops the controller from ever
     * mentioning it again - and then simply missing until something else happens
     * to that port. On a *start* the same call is a no-op, because the
     * notification gate has not been opened yet and the announcement is gated on
     * it; usbhub scans every port when it loads there anyway.
     *
     * After EnableInterrupts, so that if usbport does come back and poll, it
     * polls a controller whose interrupts are already live.
     */
    XhciRootHubDeferredWork(ext);

    return MP_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Task 13-R.1: recovery in place                                      */
/* ------------------------------------------------------------------ */

/* See the contract in src/xhci_hw.h.
 *
 * This is `XhciResumeController`'s reinitialization branch with the power IRP
 * taken out of it, and the three differences from that branch are all forced by
 * where it runs:
 *
 *   - `InitBelowPassive` is set across the whole sequence, which is what keeps
 *     every bounded wait a stall and keeps the PASSIVE-only configuration-space
 *     service out of it;
 *   - the interrupt enables are put back unconditionally when usbport had them
 *     on, because the latch masked them and nothing else will;
 *   - a refusal is not reported to anybody. There is no caller waiting for a
 *     status: the controller stays latched, `RecoveryLastStep`/`RecoveryLastStatus`
 *     say where the sequence gave up, and the health poll decides whether to
 *     arm another attempt.
 *
 * **What is deliberately not attempted is preserving anything.** HCRST returns
 * every slot and every port to its default state, so the devices on the bus go
 * away and come back; `XhciSlotInvalidateAll` is what makes that a *drop* rather
 * than a *loss*, completing the transfers usbport is holding before the contexts
 * behind them stop existing. That is the same thing a hub being unplugged and
 * replugged does to usbport, which is why it needs no cooperation from it.
 *
 * IRQL: DISPATCH_LEVEL, no usbport lock held.
 */
ULONG XhciRecoverController(PXHCI_EXTENSION ext)
{
    MPSTATUS status;
    ULONG usbsts;
    ULONG halted;
    KIRQL oldIrql;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return 0;
    }
    /*
     * Nothing was ever decoded, so there is no register to reprogram and no
     * mapping to reprogram it through - the shape of a controller whose start
     * refused in the preflight. A recovery here would be the first thing this
     * driver ever wrote to it.
     *
     * **Counted as a consecutive failure rather than declined silently**, which
     * is what keeps task 13-R.1's "bounded, then left latched" true. The
     * callback re-requests whenever `RecoveryFailuresConsecutive` is under the
     * cap, and `XhciInitController` sets `HcInfoStatus = XHCI_HC_BAD_PARAM` at
     * its top - so an attempt that refuses *before* the caps re-decode restores
     * it (a dead MMIO window, realistic in a genuine wedge) leaves this guard
     * declining every later attempt. Uncounted, that is an unbounded
     * arm-and-decline loop at one iteration per health poll, for ever.
     *
     * `RecoveryLastStep` is XHCI_INIT_STEP_NONE here on purpose: no step of the
     * sequence ran, and the status beside it is the `HcInfoStatus` that stopped
     * it rather than an `InitStatus` from a sequence that was never entered.
     */
    if (ext->HcInfoStatus != XHCI_HC_OK) {
        ext->RecoveryAttempts++;
        ext->RecoveryFailures++;
        ext->RecoveryFailuresConsecutive++;
        ext->RecoveryLastStep = XHCI_INIT_STEP_NONE;
        ext->RecoveryLastStatus = ext->HcInfoStatus;
        XhciLogNote(ext, "ctrl.recover.nohw", ext->HcInfoStatus);
        return 0;
    }

    ext->RecoveryAttempts++;
    XhciLogNote(ext, "ctrl.recover.begin", ext->RecoveryAttempts);

    /*
     * **The transition a stop/start would have provided, and the recovery is
     * incomplete without it.** `XhciInitController` reprograms the *hardware*;
     * it does not retire this driver's own command engine, because on every
     * other path something already has - `StartController` calls
     * `XhciCommandInit` on an extension usbport has just zeroed, and the resume
     * arrives through a suspend whose quiesce ran this. The recovery arrives
     * through neither, and the state it arrives with is precisely the wedged
     * one: a command outstanding on a ring that would not stop. Without this,
     * the reinitialization's own No Op self-test is answered `XHCI_CMD_BUSY` by
     * an engine still holding a command the HCRST has just abolished, and the
     * whole sequence refuses at its last step.
     *
     * It retires the command generation (telling the owning device record its
     * command is gone), advances every armed root-hub operation's generation so
     * the uncancellable port timers decline, masks the enables again, and drops
     * `XHCI_EXT_FLAG_INITIALIZED` - all under the lock, with no wait, which is
     * what makes it legal here.
     */
    XhciControllerBeginQuiesce(ext);

    /*
     * **Read the controller rather than trust the flags**, the same rule the
     * resume's reinitialization follows and for the same reason: whether the
     * records may be released is a question about whether the xHC still holds
     * their slots, and nothing has halted or reset it yet. All ones is not a
     * halt; it is a window that has stopped decoding, which says nothing about
     * what the device is still doing on the bus.
     */
    usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
    halted = (usbsts != 0xFFFFFFFFUL &&
              (usbsts & XHCI_USBSTS_HCH) != 0) ? 1UL : 0UL;

    XhciControllerLockAcquire(&oldIrql);
    XhciSlotInvalidateAll(ext, halted);
    XhciControllerLockRelease(oldIrql);
    XhciSlotDeferredWork(ext);

    ext->InitBelowPassive = 1;
    status = XhciInitController(ext, NULL);
    ext->InitBelowPassive = 0;

    ext->RecoveryLastStep = ext->InitStep;
    ext->RecoveryLastStatus = ext->InitStatus;

    if (status != MP_STATUS_SUCCESS) {
        /*
         * `XhciInitController` has already put `XHCI_EXT_FLAG_INITIALIZED` down
         * and left the latch alone on every refusal before the HCRST, and its
         * two late exits stop the controller. So the state here is the one the
         * latch describes and there is nothing to undo - only to record.
         */
        ext->RecoveryFailures++;
        ext->RecoveryFailuresConsecutive++;
        /*
         * **Re-latch, and this is not bookkeeping.** `XhciInitController`
         * releases `ControllerFailed` once HCRST has completed, because from
         * there on the hardware is no longer in the unknown state the latch
         * describes - but a sequence that then refuses at the run, the root hub
         * or the self-test has left a controller that is *not in service*, and
         * leaving the word clear would say the opposite to every admission gate
         * in the driver. The flag `XhciInitController` does clear -
         * `XHCI_EXT_FLAG_INITIALIZED` - already refuses that work, so this is
         * the two saying the same thing rather than one covering for the other;
         * what it adds is that the health poll's arming predicate reads this
         * word, so without it a failed attempt would never be retried.
         */
        XhciControllerLockAcquire(&oldIrql);
        ext->ControllerFailed = 1;
        XhciControllerLockRelease(oldIrql);
        XhciLogNote(ext, "ctrl.recover.refused", ext->InitStep);
        XHCI_DBG_VALUE_CHANGED("recover: reinitialization refused at step",
                               ext->InitStep);
        return 0;
    }

    /*
     * The enables the latch masked. Through `XhciEnableInterrupts` rather than
     * `XhciUnmaskInterrupts` for the reason the resume path gives: the sequence
     * above ran the controller and powered the ports, so a device still attached
     * has already produced its Port Status Change Event and the interrupter is
     * holding IP with Event Handler Busy set - an unmask alone would arm an
     * interrupter that never asserts again.
     *
     * Gated on usbport having asked for interrupts at all, which is the same
     * gate the resume uses: `XHCI_EXT_FLAG_INTERRUPTS` records whether
     * `EnableInterrupts` was ever called, and usbport does not call it after a
     * recovery any more than after a resume.
     *
     * **And gated on SUSPENDED, which is the tail of the window design record 07
     * section 8.1 records.** The callback checked that flag under the lock and
     * then released it, so on SMP Windows 2000 a suspend can begin on another
     * CPU while this sequence runs. Most of what that costs is unavoidable here
     * - two writers to one controller - but *this* step is avoidable and is the
     * damaging one: arming an interrupter on a controller usbport believes is
     * asleep delivers into an ISR whose extension is about to be powered down.
     *
     * **Nothing is lost by declining, and on THIS path the resume is what gives
     * them back either way.** Reaching this line means the sequence succeeded,
     * so `XhciInitController` has already cleared `ControllerFailed` - and a
     * clear latch is the one thing `XhciEnableInterrupts` needs. A
     * reinitialising resume re-enables through its own call; a restoring one
     * re-enables through the call `XhciResumeController` makes once
     * `xhciRestoreState` has succeeded - the helper itself touches no enable.
     * Neither is refused.
     *
     * **Do not import the declined-callback account here: it is a different
     * window.** When the callback declines *before* entering this function, the
     * latch is still set, so a restoring resume's enable IS refused and the
     * enables wait for the recovery that the handback keeps owed
     * (`xhciRecoveryCallback`). That story is true there and false here, and
     * this comment told it in the wrong place until the pre-cut audit.
     */
    if ((ext->Flags & (XHCI_EXT_FLAG_INTERRUPTS | XHCI_EXT_FLAG_SUSPENDED)) ==
        XHCI_EXT_FLAG_INTERRUPTS) {
        XhciEnableInterrupts(ext);
    }

    /*
     * Announce what the fresh root-hub seed found. This is the load-bearing step
     * rather than a courtesy: the root hub PDO already exists and nothing
     * rescans it, so the connect the seed latched - and acknowledged in
     * hardware, which stops the controller ever mentioning it again - would
     * otherwise be simply missing. It is the exact failure Finding 3 ends in,
     * arrived at from the other side.
     */
    XhciRootHubDeferredWork(ext);

    ext->RecoveryCompletions++;
    /*
     * **The budget belongs to a run of failures, not to the machine's lifetime**
     * (Finding T). Cleared here, on the success, which is what makes the cap
     * mean "this controller will not come back" instead of "this controller has
     * come back three times already, so the next fault is terminal".
     */
    ext->RecoveryFailuresConsecutive = 0;
    XhciLogNote(ext, "ctrl.recovered", ext->RecoveryAttempts);
    return 1;
}

/*
 * See the contract in src/xhci_hw.h: this is the minimum that keeps a running
 * controller from writing into memory usbport has reclaimed, not the lifecycle
 * task 8 owns.
 *
 * **Returns whether the controller is provably no longer able to do DMA**, and
 * only clears the two running flags when it is. An earlier version cleared the
 * flag on entry and returned void, which got both halves wrong: a failed halt
 * left the flag down, so a second attempt - or a later stop - saw nothing to do
 * and silently agreed the controller was safe.
 *
 * There are exactly two proofs available, and the ordinary one is the halt.
 * The interrupt sources are masked before R/S is cleared, and USBCMD.INTE
 * before IMAN.IE - the order docs/contributing/implementation-invariants.md, "Interrupt
 * Ordering" gives, so that an ISR racing the disable costs a spurious interrupt
 * rather than a delivered one. R/S is cleared in a later write, after the
 * controller lock is released, because the halt wait must not hold a spin lock.
 *
 * The fallback is clearing PCI Bus Master Enable, and it covers the two cases
 * the halt cannot. **All-ones MMIO is not a proof of anything**: it says the
 * memory window is not decoding, which a cleared Memory Space Enable produces
 * on a device that is still perfectly capable of mastering the bus - MSE and
 * BME are different bits. And a controller that will not halt is still running
 * by definition. Both end at the same place: take the bit away, confirm it
 * stayed away, and only then say the buffer is safe.
 *
 * What is deliberately not here: HCRST, which "software shall not set ... when
 * the HCHalted (HCH) bit in the USBSTS register is a '0'" (5.4.1, p.360), so it
 * is not an escalation for a controller that refused to halt. And clearing port
 * power, which 4.19.4 (p.296) asks for at unload but which requires a *running*
 * controller to write PORTSC at all (5.4.8, p.370); it belongs before the halt,
 * in the ordered teardown task 8 owns.
 *
 * IRQL: PASSIVE_LEVEL.
 */
ULONG XhciQuiesceController(PXHCI_EXTENSION ext)
{
    ULONG usbcmd;
    ULONG usbsts;

    if (ext == NULL) {
        return 1;
    }
    if ((ext->Flags &
         (XHCI_EXT_FLAG_RUNNING | XHCI_EXT_FLAG_HW_RUNNING |
          XHCI_EXT_FLAG_INITIALIZED)) == 0) {
        /* Never initialized by this driver, or already fully closed. Writing
         * anything here would undo a preflight refusal's no-touch guarantee. */
        return 1;
    }

    /*
     * After the initialized-or-running gate so a refused start remains untouched.
     * Under the controller lock this retires the command generation, masks both
     * interrupt enables, then clears INITIALIZED. A DPC or EnableInterrupts that
     * was already waiting on the lock rechecks admission and returns; a timeout
     * sees the retired generation. The lock is dropped before a halt wait begins.
     */
    XhciControllerBeginQuiesce(ext);

    if ((ext->Flags &
         (XHCI_EXT_FLAG_RUNNING | XHCI_EXT_FLAG_HW_RUNNING)) == 0) {
        /* Suspend still needed the atomic mask/admission close above, even if
         * there was no running controller for the halt half to stop. */
        return 1;
    }
    /*
     * Either bit admits the halt below: RUNNING says this driver wrote R/S,
     * HW_RUNNING says the hardware was read executing with no R/S of ours. The
     * obligation - stop it before usbport reclaims the buffer its pointer
     * registers name - is identical, and only `xhciUnpowerPorts` distinguishes
     * them.
     */

    usbcmd = XhciReadOp(ext, XHCI_OP_USBCMD);
    if (usbcmd == 0xFFFFFFFFUL) {
        XHCI_DBG_TEXT("quiesce: MMIO is not decoding - falling back to Bus "
                      "Master Enable");
        if (!xhciClearBusMaster(ext)) {
            ext->QuiesceFailures++;
            return 0;
        }
        (VOID)XhciControllerUpdateFlags(
            ext, XHCI_EXT_FLAG_RUNNING | XHCI_EXT_FLAG_HW_RUNNING, 0);
        return 1;
    }

    /*
     * The atomic transition above masked INTE before IE. R/S is cleared only
     * after its lock is dropped, so no bounded wait can hold a spin lock.
     * Closing DPC admission freezes ERDP while the controller can still post
     * events during this short halt window. EHB may therefore remain set and
     * the bounded ring may fill, which is harmless in teardown: no event from
     * this generation will be consumed, and stop reclaims the ring only after
     * halt/BME proof while resume rebuilds and reprograms it.
     *
     * From the validated read above rather than a fresh one: the all-ones check
     * is worth nothing if the write then re-reads and takes RsvdP from a window
     * that died in between.
     */
    xhciWriteUsbCmdFrom(ext, usbcmd, 0);

    if (!XhciWaitForBits(ext, ext->HcInfo.OperationalOffset + XHCI_OP_USBSTS,
                         XHCI_USBSTS_HCH, XHCI_USBSTS_HCH,
                         XHCI_HALT_TIMEOUT_MS, &usbsts)) {
        /*
         * The spec's 16 ms halt bound is unconditional ("irrespective of any
         * queued Transfer or Command Ring activity", 5.4.1.1), so a controller
         * still running here is broken - and may keep writing into the common
         * buffer after usbport frees it. That is what the BME fallback is for;
         * if even that cannot be confirmed, the flag stays up and the caller is
         * told, because a false "it is safe" is the worst answer available.
         */
        XHCI_DBG_VALUE("quiesce: HCHalted never set, USBSTS", usbsts);
        if (!xhciClearBusMaster(ext)) {
            ext->QuiesceFailures++;
            XHCI_DBG_TEXT("quiesce: DMA NOT PROVEN STOPPED - the common buffer "
                          "is not safe to reclaim");
            return 0;
        }
        (VOID)XhciControllerUpdateFlags(
            ext, XHCI_EXT_FLAG_RUNNING | XHCI_EXT_FLAG_HW_RUNNING, 0);
        return 1;
    }

    (VOID)XhciControllerUpdateFlags(
        ext, XHCI_EXT_FLAG_RUNNING | XHCI_EXT_FLAG_HW_RUNNING, 0);
    XHCI_DBG_VALUE("quiesce: halted, USBSTS", usbsts);
    return 1;
}

/*
 * The ordered teardown - see the contract in src/xhci_hw.h.
 *
 * Two steps, and the order between them is forced by the hardware rather than
 * chosen: port power can only be written while the controller runs (5.4.8,
 * p.370) and the quiesce is what stops it running, so the port pass goes first
 * or never. Everything else the teardown owes - blocking new work, retiring the
 * command generation and its uncancellable watchdog, masking both interrupt
 * enables, closing ISR and DPC admission, and proving DMA has stopped - is
 * inside XhciQuiesceController, which is also why suspend can share it: suspend
 * wants every one of those and none of this one.
 *
 * **Suspend deliberately does not unpower the ports.** An idle suspend/resume
 * pair - which is what Win98's NUSB usbport issues, repeatedly - would then drop
 * VBus on every connector and force every attached device to re-enumerate on
 * each one. 4.19.4's note is about a driver being *unloaded*, not about a
 * controller taking a nap.
 *
 * **The consequence of that, stated rather than discovered later**: usbport's
 * measured clean shutdown is SuspendController -> DisableInterrupts ->
 * StopController(TRUE) (docs/contributing/lessons.md, Phase 3 task 8), so on the ordinary
 * shutdown the stop arrives on a controller the suspend has already halted and
 * the port pass cannot run at all. It is skipped and *counted*
 * (PortTeardownSkippedSuspended), not silently passed over. Restarting the
 * controller here to reach the write is refused: it would set R/S on a
 * controller whose common buffer usbport reclaims the moment this returns, put
 * the most safety-critical path in the driver behind a second halt that could
 * fail, and risk a bugcheck on a machine that was shutting down cleanly - all
 * to honour a "should" about power consumption on the one path where the
 * machine is about to lose power anyway. The case 4.19.4 is actually written
 * about is a driver disabled or unloaded while the machine keeps running, and
 * there the controller is still running when the stop arrives, so the pass does
 * run.
 *
 * **Two things this function deliberately does not do**, both of which look
 * like obvious additions and are refused on the spec text:
 *
 *   *Tearing the pointer registers down.* Zeroing DCBAAP, CRCR, ERSTBA and
 *   ERDP after the halt would leave the controller naming physical address 0
 *   instead of a common buffer usbport is about to reclaim, which is not an
 *   improvement - page 0 is real memory - and the register that would make it
 *   coherent cannot be written: "For the Primary Interrupter: Writing a value of
 *   `0` to this field shall result in undefined behavior of the Event Ring. The
 *   Primary Event Ring cannot be disabled" (ERSTSZ, 5.5.2.3.1, p.393). So there
 *   is no way to say "this controller points at nothing"; the halt is the
 *   statement, and the next start's HCRST is what actually clears them.
 *
 *   *An HCRST after the halt.* It is legal there (unlike after a halt that timed
 *   out, 5.4.1, p.360), it is the first of the two proofs
 *   docs/contributing/implementation-invariants.md, "DMA Teardown" accepts, and it would zero
 *   every pointer register in one write. It also undoes the step above it: an
 *   xHC "shall automatically enable VBus on all Root Hub ports after a Chip
 *   Hardware Reset or HCRST" (4.19.4, p.295), so a reset here re-powers every
 *   port - including the USB 3.x ports the port strategy requires unpowered, on
 *   a machine that no longer has a driver to serve whatever trains onto them.
 *   The halt is already a sufficient proof, so the reset buys a cleaner register
 *   image at the cost of the only user-visible thing this teardown achieves.
 *
 * Returns what XhciQuiesceController returns: 1 only when the xHC is provably
 * unable to do DMA. **A caller after which usbport reclaims the common buffer
 * owes XhciFailClosedDma on a 0** - the stop callback and both reclaiming exits
 * of XhciInitController.
 *
 * IRQL: PASSIVE_LEVEL.
 */
ULONG XhciStopController(PXHCI_EXTENSION ext)
{
    if (ext == NULL) {
        return 1;
    }

    ext->TeardownCount++;

    /*
     * **No admission gate here, deliberately.** An earlier version restated the
     * quiesce's `(RUNNING | INITIALIZED) == 0` check at the top of this
     * function, on the reasoning that a start refused in the preflight must not
     * be read from. Both steps below already refuse that case on their own -
     * xhciUnpowerPorts reads nothing before its own RUNNING gate, and the
     * quiesce carries the original - so all the restatement did was **swallow
     * the case it was not written for**: a stop arriving on a *suspended*
     * controller has RUNNING and INITIALIZED both clear, so it returned here,
     * and the port pass never recorded that it had been skipped. On the
     * measured shutdown ordering that is every ordinary unload.
     *
     * One gate per concern, applied where the concern is.
     */
    /*
     * **Root-hub admission is closed for the port pass** (audit finding A4).
     * `xhciUnpowerPorts` writes PORTSC raw and unlocked, and `xhciRhAdmitted`
     * asks only for `INITIALIZED`, which the quiesce below does not clear until
     * afterwards - so without this a concurrent `RH_GetPortStatus` on SMP could
     * ack a change bit with a neutral value composed from a pre-teardown read,
     * carrying PP back up and undoing the pass, as a second unserialized writer.
     *
     * A flag of its own rather than clearing `INITIALIZED` early: the quiesce's
     * own admission gate reads that bit too, and on a *suspended* controller -
     * the measured ordinary Win98 shutdown - clearing it here would make the
     * quiesce return before `XhciControllerBeginQuiesce` had masked anything.
     *
     * It is never cleared again. A stop is terminal for this driver's ownership
     * of the controller, and the quiesce that follows closes admission for good
     * by the route every other caller reads; a start rebuilds the extension from
     * the zero usbport writes over it.
     */
    (VOID)XhciControllerUpdateFlags(ext, 0, XHCI_EXT_FLAG_RH_CLOSED);
    xhciUnpowerPorts(ext);
    {
        ULONG quiesced;
        KIRQL oldIrql;

        quiesced = XhciQuiesceController(ext);

        /*
         * The devices go with the controller (task 6-B.5). **After the quiesce,
         * not before it**, and the order is the point: every transfer completed
         * here is one usbport is waiting on, and completing them while the xHC
         * could still be executing their TRBs would answer for memory the
         * controller has not finished with. The quiesce is what proves it has.
         *
         * And its answer is passed on rather than assumed. A quiesce that could
         * prove neither a halt nor a lost bus master is a controller that may
         * still be following its DCBAA, so the records are abandoned in place
         * instead of released - the caller's escalation to XhciFailClosedDma is
         * about the *buffer*, and says nothing about whether the slots inside it
         * are still the hardware's.
         */
        XhciControllerLockAcquire(&oldIrql);
        XhciSlotInvalidateAll(ext, quiesced);
        XhciControllerLockRelease(oldIrql);
        XhciSlotDeferredWork(ext);

        return quiesced;
    }
}

/* ------------------------------------------------------------------ */
/* The sequence                                                        */
/* ------------------------------------------------------------------ */

static MPSTATUS xhciInitFailed(PXHCI_EXTENSION ext,
                               ULONG step,
                               ULONG status,
                               MPSTATUS mpStatus)
{
    ext->InitStep = step;
    ext->InitStatus = status;
    XHCI_DBG_VALUE("init REFUSED at step", step);
    XHCI_DBG_VALUE("init refusal status", status);
    /*
     * Task 11-V.9's first tier: `InitStep`/`InitStatus` at **every** refusal,
     * and this is every refusal because the whole sequence funnels here. It is
     * the one record that answers "the device has a yellow bang and I cannot
     * attach a debugger" - the step names where, the status names why - and it
     * is unbounded for the same reason the rest of the tier is: it happens once
     * per start, and a start that refuses does not have a second.
     */
    XhciLogNote(ext, "init.refused.step", step);
    XhciLogNote(ext, "init.refused.status", status);
    return mpStatus;
}

MPSTATUS XhciInitController(PXHCI_EXTENSION ext, PUSBPORT_RESOURCES resources)
{
    XHCI_HC_INFO recheck;
    ULONG status;
    ULONG value;
    ULONG pageSizeMask;
    UCHAR interruptPin;

    /* Logged at the bus-master gate on a path where the config-space read can
     * refuse before writing it. */
    value = 0;
    ext->InitStep = XHCI_INIT_STEP_RESOURCES;
    ext->InitStatus = 0;
    ext->HcInfoStatus = XHCI_HC_BAD_PARAM;
    ext->PortMapStatus = XHCI_CAPS_BAD_PARAM;
    (VOID)XhciControllerUpdateFlags(ext, XHCI_EXT_FLAG_INITIALIZED, 0);

    /* The frame number is a delta against a running MFINDEX, and HCRST restarts
     * that counter from zero. Dropping the sync here rather than trusting the
     * stall path to notice is the same rule the port shadows follow: a
     * reinitialisation invalidates every reading taken from the old run. */
    ext->FrameSynced = 0;
    ext->FrameCongruent = 0;
    /* And the health poll's own clock, for the same reason and with a sharper
     * consequence: PollClockFrame holds a frame number taken from the run HCRST
     * is about to abolish, so a first poll that still believed the sync would
     * add `(newFrame - oldFrame) & 0x7FF` - up to 2,047 ms of time that did not
     * pass - to every threshold measured on PollClockMs. Dropping it here costs
     * only the interval nobody measured, which is the direction
     * XhciPollClockAdvance is documented to fail in. */
    ext->PollClockSynced = 0;
    /* Stale until a poll proves otherwise: a Frame ID may not be claimed on an
     * axis no reading has refreshed since the controller restarted. */
    ext->FrameSampleStale = 1;
    ext->SavedStateValid = 0;

    /*
     * Step 0a: the resource packet. Phase 3 logged a missing resource bit and
     * carried on, because the value of that spike was the log. From here a
     * miniport without a memory window or without an interrupt has nothing to
     * do but decline: usbport connected the interrupt before calling this, so
     * a missing INTERRUPT bit means there is no interrupt object to deliver
     * anything through.
     *
     * `resources` is NULL on the reinitialization path (XhciResumeController),
     * where usbport is not handing anything over - it is the same controller,
     * the same mapping and the same common buffer, all of which the extension
     * already holds. The check then runs against the copy StartController made,
     * which is what the rest of the sequence uses anyway.
     */
    if (((resources != NULL ? resources->ResourcesTypes
                            : ext->ResourcesTypes) &
         (USBPORT_RESOURCES_MEMORY | USBPORT_RESOURCES_INTERRUPT)) !=
        (USBPORT_RESOURCES_MEMORY | USBPORT_RESOURCES_INTERRUPT)) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_RESOURCES,
                              ext->ResourcesTypes, MP_STATUS_NO_RESOURCES);
    }
    if (ext->ResourceBase == 0 ||
        ext->IoSpaceLength < XHCI_CAP_REGISTERS_BYTES) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_RESOURCES,
                              ext->IoSpaceLength, MP_STATUS_NO_RESOURCES);
    }

    /*
     * Step 0b, and the first departure from the roadmap's order: the INTx gate
     * runs before anything else, rather than after CONFIG.MaxSlotsEn.
     *
     * It is a PCI config read that needs no MMIO at all, and its answer is
     * final - "PCI Interrupt Pins are optional" (spec 4.17.3), and neither
     * target's usbport has an MSI path, so a controller reporting pin 0 can
     * never deliver an event to either one. Refusing it here means refusing it
     * without having taken ownership from the firmware or halted a controller
     * that was working for somebody else. Doing it seven steps later would
     * leave a machine whose xHCI is claimed, reset and dead.
     */
    ext->InitStep = XHCI_INIT_STEP_INTERRUPT;
    /*
     * **Task 13-R.1: the three configuration-space steps are skipped when this
     * sequence is the in-place recovery**, because `UsbPortReadWriteConfigSpace`
     * goes out to the bus driver and this project's contract for it is
     * PASSIVE_LEVEL, while the recovery runs from a DPC.
     *
     * What that costs is stated rather than waved past: the vendor/device
     * identification, the INTx gate and the bus-master gate are all questions
     * about a *device* rather than about a controller's current state, and every
     * one of them was answered on this same PCI function by the start that
     * succeeded - the recovery is reached only from a controller this driver
     * started. Nothing between then and now re-enumerated the bus; a PnP restart
     * would have gone through `StopController`/`StartController` and taken this
     * path with `InitBelowPassive` clear. The values the skipped reads would
     * have produced are still in the extension and are what the rest of the
     * sequence uses.
     */
    if (!ext->InitBelowPassive) {
        ext->PciVendorDevice = 0;
    }
    if (!ext->InitBelowPassive &&
        XhciReadPciConfig(ext, XHCI_PCI_VENDOR_DEVICE, &ext->PciVendorDevice,
                          sizeof(ULONG)) == MP_STATUS_SUCCESS) {
        XHCI_DBG_VALUE("PCI vendor/device", ext->PciVendorDevice);
    }

    interruptPin = 0;
    if (ext->InitBelowPassive) {
        /* Answered at the start this controller is recovering from. */
    } else if (XhciReadPciConfig(ext, XHCI_PCI_INTERRUPT_PIN, &interruptPin,
                                 sizeof(UCHAR)) != MP_STATUS_SUCCESS) {
        /*
         * Not fatal, deliberately. A pin of 0 is a refusal; a config read that
         * did not happen is not the same statement, and treating it as one
         * would refuse a working controller over a service failure. The weaker
         * gate is already behind us: PCI derives a device's interrupt resource
         * from this very register, so a controller with no pin does not get the
         * USBPORT_RESOURCES_INTERRUPT bit that the step above required.
         */
        ext->InterruptPin = 0xFFFFFFFFUL;
        XHCI_DBG_TEXT("PCI interrupt pin unreadable - falling back to "
                      "usbport's interrupt resource bit");
    } else {
        ext->InterruptPin = (ULONG)interruptPin;
        XHCI_DBG_VALUE("PCI interrupt pin", ext->InterruptPin);
        /* First tier: the INTx gate's own reading. A pin of 0 refuses below and
         * `init.refused.*` records that; this is what says the gate *ran* and
         * what it saw, which is the difference between a controller that has no
         * pin and a config space that would not answer. */
        XhciLogNote(ext, "gate.intx.pin", ext->InterruptPin);
        if (interruptPin == 0) {
            XHCI_DBG_TEXT("controller is MSI/MSI-X only - no INTx on either "
                          "target's usbport; refusing");
            return xhciInitFailed(ext, XHCI_INIT_STEP_INTERRUPT, 0,
                                  MP_STATUS_NOT_SUPPORTED);
        }
    }

    /*
     * Step 0b's other half, and the same argument as the INTx gate above: a
     * controller that cannot master the bus can never deliver anything either,
     * the question is answered by one config read, and refusing here costs the
     * machine nothing. Reading it on every start rather than only when this
     * driver's own quiesce cleared the bit is the point - see xhciCheckBusMaster.
     */
    ext->InitStep = XHCI_INIT_STEP_BUS_MASTER;
    if (!ext->InitBelowPassive && !xhciCheckBusMaster(ext, &value)) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_BUS_MASTER, value,
                              MP_STATUS_HW_ERROR);
    }
    /* First tier: the PCI Command register the bus-master gate accepted. Every
     * DMA this driver programs depends on bit 2 of it, and a machine whose
     * firmware or power management clears it later is a controller that goes
     * silent with no other symptom. */
    XhciLogNote(ext, "gate.busmaster.command", value);

    /* Step 0c: MMIO sanity, before any register that has a side effect. */
    ext->InitStep = XHCI_INIT_STEP_CAP_SANITY;
    value = XhciRead32(ext, XHCI_CAP_CAPLENGTH);
    status = XhciCheckCapDword0(value, ext->IoSpaceLength, NULL, NULL);
    XHCI_DBG_VALUE("CAPLENGTH/HCIVERSION dword", value);
    if (status != XHCI_HC_OK) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_CAP_SANITY, status,
                              MP_STATUS_HW_ERROR);
    }

    /* Step 5, early: the handoff needs HCCPARAMS1.xECP, so the whole
     * capability set is decoded now and re-decoded after the reset. */
    ext->InitStep = XHCI_INIT_STEP_CAP_DECODE;
    status = XhciDeriveControllerInfo(ext, &ext->HcInfo);
    if (status != XHCI_HC_OK) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_CAP_DECODE, status,
                              MP_STATUS_HW_ERROR);
    }
    ext->HcInfoStatus = XHCI_HC_OK;
    XHCI_DBG_VALUE("HCIVERSION", ext->HcInfo.HciVersion);
    XHCI_DBG_VALUE("CAPLENGTH", ext->HcInfo.CapLength);
    XHCI_DBG_VALUE("MaxSlots", ext->HcInfo.MaxSlots);
    XHCI_DBG_VALUE("MaxPorts", ext->HcInfo.MaxPorts);
    XHCI_DBG_VALUE("MaxIntrs", ext->HcInfo.MaxIntrs);
    XHCI_DBG_VALUE("scratchpad buffers", ext->HcInfo.ScratchpadCount);
    XHCI_DBG_VALUE("context size", ext->HcInfo.ContextSize);
    XHCI_DBG_VALUE("PPC", ext->HcInfo.Ppc);
    XHCI_DBG_VALUE("AC64 (recorded, unused)", ext->HcInfo.Ac64);
    XHCI_DBG_VALUE("RTSOFF", ext->HcInfo.RuntimeOffset);
    XHCI_DBG_VALUE("DBOFF", ext->HcInfo.DoorbellOffset);
    XHCI_DBG_VALUE("xECP dwords", ext->HcInfo.XecpDwords);

    /*
     * Task 11-V.9's first tier: what this controller *is*. Every one of these
     * happens once per start and every one of them changes what the rest of the
     * log means - a slot count that refuses a device, a context size that
     * decides every stride, a scratchpad demand this driver's fixed carve may
     * not be able to meet. A log without them describes a controller the reader
     * has to guess at.
     */
    XhciLogNote(ext, "hc.version", ext->HcInfo.HciVersion);
    XhciLogNote(ext, "hc.maxslots", ext->HcInfo.MaxSlots);
    XhciLogNote(ext, "hc.maxports", ext->HcInfo.MaxPorts);
    XhciLogNote(ext, "hc.maxintrs", ext->HcInfo.MaxIntrs);
    XhciLogNote(ext, "hc.scratchpad", ext->HcInfo.ScratchpadCount);
    XhciLogNote(ext, "hc.contextsize", ext->HcInfo.ContextSize);
    XhciLogNote(ext, "hc.pci", ext->PciVendorDevice);

    /*
     * Step 12, moved into the preflight - the third departure from the
     * roadmap's order, and the same argument as the first two. Classifying
     * ports reads the capability chain and nothing else, and its refusals are
     * final. Running it here means a controller whose topology this driver
     * cannot serve is declined while it is still exactly as its firmware left
     * it. At its listed position the same refusal would arrive after the
     * ownership claim, the halt and the reset - a working controller turned
     * into a dead one by a check that could have run before any of it.
     *
     * This is the last step that only reads. Everything below writes.
     */
    ext->InitStep = XHCI_INIT_STEP_PORT_MAP;
    status = xhciBuildPortMap(ext, 0);
    if (status != XHCI_CAPS_OK) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_PORT_MAP, status,
                              MP_STATUS_HW_ERROR);
    }

    /* Step 1. Only a malformed chain refuses here - see xhciBiosHandoff. */
    ext->InitStep = XHCI_INIT_STEP_HANDOFF;
    status = xhciBiosHandoff(ext);
    if (status != XHCI_CAPS_OK) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_HANDOFF, status,
                              MP_STATUS_HW_ERROR);
    }
    /* usbport records this in the registry as DetectedLegacyBIOS
     * (docs/usb-xhci-info/usbport-miniport-abi.md section 5). */
    if (resources != NULL) {
        resources->LegacySupport =
            (UCHAR)(ext->LegacyCapOffset != 0 ? 1 : 0);
    }
    /*
     * The handoff's outcome, first tier. Zero here is not a failure - it is a
     * controller whose firmware never claimed it - and that distinction is what
     * separates "there was nothing to take" from "there was, and it was taken".
     * The USBLEGSUP value itself is noted inside xhciBiosHandoff, where the
     * BIOS-owned bit is still readable.
     */
    XhciLogNote(ext, "handoff.cap", ext->LegacyCapOffset);

    /* Step 2. */
    ext->InitStep = XHCI_INIT_STEP_HALT;
    if (!xhciHalt(ext)) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_HALT,
                              XhciReadOp(ext, XHCI_OP_USBSTS),
                              MP_STATUS_HW_ERROR);
    }

    /* Step 3. */
    ext->InitStep = XHCI_INIT_STEP_RESET;
    if (!xhciReset(ext)) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_RESET,
                              XhciReadOp(ext, XHCI_OP_USBSTS),
                              MP_STATUS_HW_ERROR);
    }

    /*
     * Step 4, widened. The roadmap asks for HCIVERSION to be re-checked after
     * the reset; the question behind that is whether the thing on the other
     * end of the mapping is still the controller that was decoded before it.
     * A full re-derivation answers it for every field the rest of the sequence
     * uses, and costs six reads.
     */
    ext->InitStep = XHCI_INIT_STEP_RECHECK;
    status = XhciDeriveControllerInfo(ext, &recheck);
    if (status != XHCI_HC_OK) {
        ext->HcInfoStatus = status;
        return xhciInitFailed(ext, XHCI_INIT_STEP_RECHECK, status,
                              MP_STATUS_HW_ERROR);
    }
    if (!XhciHcInfoEqual(&ext->HcInfo, &recheck)) {
        XHCI_DBG_TEXT("capability registers changed across reset - refusing");
        return xhciInitFailed(ext, XHCI_INIT_STEP_RECHECK, XHCI_HC_NOT_DECODING,
                              MP_STATUS_HW_ERROR);
    }

    /*
     * Acknowledge whatever status the reset and the firmware left behind, now
     * that nothing has looked at it and nothing can yet. Written as the bit
     * mask, never as the read value ORed back in: USBSTS is RW1C, and a
     * read-modify-write of it acknowledges changes its caller never saw
     * (docs/usb-xhci-info/xhci-data-structures.md section 3).
     */
    XhciWriteOp(ext, XHCI_OP_USBSTS, XHCI_USBSTS_RW1C_MASK);

    /*
     * The device table, at its start-of-day state (task 6-B.2), and **here
     * rather than at the top of this function** - which is where a first draft
     * put it, and that was a claim that hardware state had been released before
     * anything had released it. HCRST has now completed: "all of the Operational
     * and Runtime Registers shall be at their default values" (4.23.1, p.312),
     * so no slot the previous run enabled survives and no DCBAA entry is being
     * followed. Clearing the table in the preflight instead would have made a
     * *refusal* - which by design writes nothing at all - drop every record
     * while the controller kept its slots, and the next start would then have
     * re-carved the same common buffer underneath contexts the xHC was still
     * reading.
     *
     * On an ordinary start this changes nothing, because usbport has already
     * zeroed the whole extension. On a reinitialisation the caller has already
     * completed the transfers those records were holding - see the invalidation
     * in XhciResumeController, which is deliberately not here because this
     * function has no lock and no way to answer usbport.
     */
    XhciSlotInit(ext);

    /*
     * **The `ControllerFailed` latch is released here, and this is task 13-R.1's
     * other half.** It is released at this point rather than at the top of the
     * sequence or the bottom of it, and both alternatives are wrong for the same
     * reason: the latch is a statement about *hardware in an unknown state*, and
     * this is the line at which that stops being true. HCRST has completed -
     * "all of the Operational and Runtime Registers shall be at their default
     * values" (4.23.1, p.312) - and `XHCI_EXT_FLAG_INITIALIZED` is still down, so
     * nothing is admitted between here and the flag transition below whatever
     * this word says.
     *
     * At the top it would clear on a sequence that then refused at a preflight
     * gate, leaving a failed controller reported as merely uninitialized. At the
     * bottom it would be too late for the two steps that need it: `XhciRootHubInit`
     * passes `xhciRhAdmitted`, whose fourth clause is this word, and the No Op
     * self-test passes `XhciCommandSubmit`, whose first gate is this word.
     *
     * This also fixes the resume path, which had the same gap and reached it by
     * the same route: `XhciCommandInit` is the only other place that clears this
     * word and it is called from `StartController`, so a controller that failed
     * and was then suspended and resumed came back reinitialized and still
     * latched.
     */
    {
        KIRQL failedIrql;

        XhciControllerLockAcquire(&failedIrql);
        ext->ControllerFailed = 0;
        XhciControllerLockRelease(failedIrql);
    }

    /*
     * The pooled non-EP0 rings, reset **in the same breath as the device table**
     * and for the same reason: `Owner[]` holds device *references* - indices
     * into `ext->Devices` plus one - so a pool that outlived the table it names
     * would hand a live ring's entry to whatever record next landed at that
     * index (design doc 04 section 3.6).
     *
     * Called explicitly rather than left to usbport zeroing the miniport
     * extension, even though a zeroed pool happens to be a valid empty one.
     * "usbport `RtlZeroMemory`s the extension before every `StartController`" is
     * a *discovered* fact (Phase 4 task 7), not a term of the ABI, and a pool
     * that is only correct because of it would be correct by coincidence - and
     * the coincidence is exactly why the omission would never fail a test.
     */
    XhciPoolInit(&ext->RingPool);

    /*
     * Step 4's other half. The re-derivation above compared the capability
     * registers; this re-parses the list that hangs off xECP, which they say
     * nothing about. This pass is what writes ext->PortMap - everything
     * downstream reads that structure, and a pre-reset reading of the
     * controller's memory map is precisely the stale data this step exists to
     * rule out. Agreement with the preflight parse is then required field for
     * field (XhciPortMapEqual), against the map the preflight left in
     * ext->PreflightPortMap.
     */
    ext->InitStep = XHCI_INIT_STEP_PORT_MAP_RECHECK;
    status = xhciBuildPortMap(ext, 1);
    if (status != XHCI_CAPS_OK) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_PORT_MAP_RECHECK, status,
                              MP_STATUS_HW_ERROR);
    }

    /* Step 8's precondition: PAGESIZE is a capability bitmap, and the carve
     * assumes 4 KB pages throughout. */
    ext->InitStep = XHCI_INIT_STEP_PAGESIZE;
    pageSizeMask = XhciReadOp(ext, XHCI_OP_PAGESIZE);
    XHCI_DBG_VALUE("PAGESIZE", pageSizeMask);
    XhciLogNote(ext, "hc.pagesize", pageSizeMask);
    if (pageSizeMask == 0xFFFFFFFFUL) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_PAGESIZE, pageSizeMask,
                              MP_STATUS_HW_ERROR);
    }

    /* Steps 6-8: what this controller needs, against what DriverEntry already
     * committed to. Everything past here is arithmetic that has host tests. */
    ext->InitStep = XHCI_INIT_STEP_LAYOUT;
    status = XhciCheckResourceBase(ext->StartVA, ext->StartPA);
    if (status != XHCI_LAYOUT_OK) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_LAYOUT, status,
                              MP_STATUS_NO_RESOURCES);
    }
    ext->LayoutStatus = XhciComputeLayout(ext->HcInfo.ContextSize,
                                          ext->HcInfo.MaxSlots,
                                          ext->HcInfo.ScratchpadCount,
                                          pageSizeMask,
                                          &ext->Layout);
    if (ext->LayoutStatus != XHCI_LAYOUT_OK) {
        /* XHCI_LAYOUT_TOO_MANY_SCRATCHPAD is the one a real controller can
         * plausibly produce, and it is the declared limit refusing rather than
         * silently under-serving it (docs/contributing/design/04-controller-common-
         * buffer.md). The count is in the trace above. */
        return xhciInitFailed(ext, XHCI_INIT_STEP_LAYOUT, ext->LayoutStatus,
                              MP_STATUS_NO_RESOURCES);
    }
    /*
     * The carve is computed from the same constants DriverEntry declared, so
     * these agree in every normal build. They do not in a probe build, whose
     * whole purpose is to ask usbport for a different size than the layout
     * needs (XHCI_PROBE_RESOURCES_SIZE in xhci_dispatch.c) - and carving 400 KiB
     * of structures out of a 4 KB buffer would be the last thing that ever
     * worked on that machine.
     */
    if (ext->Layout.TotalBytes > XhciRegPacket.MiniPortResourcesSize) {
        XHCI_DBG_VALUE("layout does not fit the declared buffer, need",
                       ext->Layout.TotalBytes);
        return xhciInitFailed(ext, XHCI_INIT_STEP_LAYOUT, XHCI_LAYOUT_OVERFLOW,
                              MP_STATUS_NO_RESOURCES);
    }
    XHCI_DBG_VALUE("MaxSlotsEn", ext->Layout.MaxSlotsEn);
    XHCI_DBG_VALUE("layout total bytes", ext->Layout.TotalBytes);

    xhciZeroCommonBuffer(ext, ext->Layout.TotalBytes);

    /* Steps 6-8, the writes. */
    ext->InitStep = XHCI_INIT_STEP_DCBAA;
    status = xhciProgramDcbaa(ext);
    if (status != XHCI_LAYOUT_OK) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_DCBAA, status,
                              MP_STATUS_NO_RESOURCES);
    }

    /* Step 9. */
    ext->InitStep = XHCI_INIT_STEP_COMMAND_RING;
    status = xhciProgramCommandRing(ext);
    if (status != XHCI_RING_OK) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_COMMAND_RING, status,
                              MP_STATUS_NO_RESOURCES);
    }

    /* Step 10. */
    ext->InitStep = XHCI_INIT_STEP_EVENT_RING;
    status = xhciProgramEventRing(ext);
    if (status != XHCI_RING_OK) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_EVENT_RING, status,
                              MP_STATUS_NO_RESOURCES);
    }

    /*
     * A no-op unless a previous quiesce had to clear Bus Master Enable to prove
     * the controller had stopped - the *gate* on bus mastering is step 3, in the
     * preflight, and it has already run on every path that reaches here. This
     * step is only the write, which is why it cannot be up there with it.
     *
     * Before the INITIALIZED transition below, so that a refusal here leaves
     * nothing published: a controller that never ran must not be admitted to
     * the ISR gate or to the callbacks keyed on that flag.
     */
    ext->InitStep = XHCI_INIT_STEP_BUS_MASTER_RESTORE;
    if (!xhciRestoreBusMaster(ext, &value)) {
        return xhciInitFailed(ext, XHCI_INIT_STEP_BUS_MASTER_RESTORE, value,
                              MP_STATUS_HW_ERROR);
    }

    /*
     * Step 11. Nothing above set IMAN.IE or USBCMD.INTE, and nothing here may:
     * usbport calls EnableInterrupts immediately after StartController returns
     * success, and that callback (roadmap Phase 4 task 6) is where they belong.
     * The interrupt path itself already exists - task 4 - which is what makes
     * running the controller in the next step safe to do here.
     *
     * INITIALIZED goes up before R/S rather than at the end, because it is the
     * gate the ISR and the DPC test: from the moment the controller is running
     * it can post events, and the two paths that consume them have to be
     * allowed to. Nothing can deliver an interrupt yet, but a shared line means
     * usbport's ISR can call into this miniport for a neighbour's, and it should
     * find a driver that can read its own registers. All callback-side Flags
     * RMWs use this lock too, so none can lose this admission transition.
     */
    /* `RH_CLOSED` goes down in the same write: a teardown's port pass set it
     * (finding A4) and this is the transition that says the root hub may reach
     * hardware again. Paired here rather than left to usbport's zeroing of the
     * extension, so the pairing is a property of this driver's own sequence. */
    (VOID)XhciControllerUpdateFlags(ext, XHCI_EXT_FLAG_RH_CLOSED,
                                    XHCI_EXT_FLAG_INITIALIZED);

    /* Step 15. */
    ext->InitStep = XHCI_INIT_STEP_RUN;
    if (!xhciRunController(ext, &value)) {
        /*
         * Undo the half-start rather than leaving it: R/S may be set on a
         * controller whose start is about to be reported as failed, and usbport
         * reclaims the common buffer after that.
         *
         * **Not because the quiesce is a no-op when R/S was never written** -
         * this comment said that until the second-reader review, and it
         * is wrong. `INITIALIZED` is still set here (it is cleared two lines
         * below, after this call), so the quiesce is admitted and runs
         * `XhciControllerBeginQuiesce` - retiring the command generation,
         * masking both interrupt enables and closing DPC admission - before it
         * looks at `RUNNING` at all. Only the *halt* half is conditional. That
         * is what makes calling it on every failure of this step correct: the
         * interrupt and command teardown is owed whether or not the controller
         * ever ran, and a start that refused at R/S has already programmed the
         * command ring and the interrupter.
         *
         * And the result is no longer discarded. A failed start reclaims that
         * buffer exactly as a stop does, so the same rule applies: a controller
         * this driver ran and then could not stop is not something to report a
         * refusal about and walk away from (src/xhci_hw.h, XhciFailClosedDma).
         *
         * Through XhciStopController rather than the quiesce alone, so that a
         * failed start leaves the same port state a stop does. Its port pass is
         * self-refusing here, and that holds for **each** of this step's four
         * exits rather than only the one this comment used to name: nothing has
         * been powered yet, so there is no port state to take down whether the
         * step refused an all-ones USBSTS, refused a controller it observed
         * *not* halted, could not form a USBCMD operand, or wrote R/S and never
         * saw HCHalted clear. Only the third of those leaves the controller
         * provably still halted; the second publishes RUNNING precisely because
         * it does not.
         */
        if (!XhciStopController(ext)) {
            XhciFailClosedDma(ext);
        }
        (VOID)XhciControllerUpdateFlags(ext, XHCI_EXT_FLAG_INITIALIZED, 0);
        return xhciInitFailed(ext, XHCI_INIT_STEP_RUN, value,
                              MP_STATUS_HW_ERROR);
    }

    /*
     * Step 16, and it has to be after step 15 rather than beside it: "software
     * shall ensure that the xHC is running (HCHalted (HCH) = '0') before
     * attempting to write to this register" (5.4.8, p.371). It is also when the
     * connects arrive - a port with a device already attached asserts PSCEG as
     * HCH transitions to '0', "generating a respective Port Status Change
     * Event" (4.19.4, p.296), and a halted controller cannot generate one at
     * all.
     */
    ext->InitStep = XHCI_INIT_STEP_PORT_POWER;
    xhciPowerPorts(ext);

    /*
     * Step 17 (roadmap Phase 5 task 2): the root hub usbport is about to build a
     * PDO for, and the first reading of every port in it.
     *
     * After the port power pass, not beside it, because the shadow this seeds
     * records PP - and the ports it has entries for are the managed ones, which
     * is what the port-power pass has just finished driving. It is also after
     * the run, so a device attached at boot has already produced its Port Status
     * Change Event and left CSC set on its port: this reading is what latches
     * that connect, and nothing else ever will, because the controller announces
     * it exactly once.
     *
     * It fails the start, which is a stronger answer than the port-power step's.
     * A port that will not take power is one port; a root hub that cannot be
     * built is a controller with no USB 2.0 port to serve - and reporting zero
     * ports to usbport is the case that asks it for about 1 GB of nonpaged pool
     * (docs/usb-xhci-info/usbport-miniport-abi.md section 4). The port-map step already
     * refuses that controller, so this is the second gate rather than the first.
     */
    ext->InitStep = XHCI_INIT_STEP_ROOT_HUB;
    /* 0: this path has just been through HCRST, so no port survives in U3 and
     * the U3 -> U0 pass runs on its own premise. */
    status = XhciRootHubInit(ext, 0);
    if (status != XHCI_RH_OK) {
        if (!XhciStopController(ext)) {
            XhciFailClosedDma(ext);
        }
        (VOID)XhciControllerUpdateFlags(ext, XHCI_EXT_FLAG_INITIALIZED, 0);
        return xhciInitFailed(ext, XHCI_INIT_STEP_ROOT_HUB, status,
                              MP_STATUS_NO_RESOURCES);
    }

    /*
     * Step 18, and the one step of this sequence whose *answer* arrives after it
     * returns. A No Op Command "can be issued by software to exercise the TRB
     * Ring mechanism of the xHC without affecting any xHC or USB Device state"
     * (4.6.2, p.94) - so it costs the machine nothing and it is the only thing
     * that exercises the command ring, the doorbell, the event ring, the ISR and
     * the DPC as one path. Its completion cannot gate the start: interrupts are
     * still masked here and usbport does not enable them until StartController
     * has returned, which is precisely why the event has to be picked up by the
     * DPC rather than waited for.
     *
     * The submit failing is a different matter and does fail the start. Every
     * way it can fail at this point - not running, already busy, a full ring, a
     * TRB the ring layer refuses - is a broken invariant of this driver rather
     * than a statement about the hardware, and starting anyway would leave a
     * controller whose command path has never worked and nothing to say so.
     */
    ext->InitStep = XHCI_INIT_STEP_NOOP;
    status = XhciCommandNoOpSelfTest(ext);
    if (status != XHCI_CMD_OK) {
        /* Same reclamation rule as the failed run above - and this is the exit
         * where the port pass has real work: step 16 powered the managed ports
         * a moment ago, and this refusal is the driver walking away from them. */
        if (!XhciStopController(ext)) {
            XhciFailClosedDma(ext);
        }
        (VOID)XhciControllerUpdateFlags(ext, XHCI_EXT_FLAG_INITIALIZED, 0);
        return xhciInitFailed(ext, XHCI_INIT_STEP_NOOP, status,
                              MP_STATUS_HW_ERROR);
    }

#ifdef XHCI_FAIL_START_CONTROLLER
    /*
     * Roadmap task 12.3's failed-start artifact, and it is here rather than at
     * the top of the sequence on purpose. What has never been exercised on
     * either target is recovery from a driver that installs, **loads**, and then
     * fails inside StartController - so the injected refusal has to happen after
     * the start has really done its work: the controller is running, the ports
     * are powered, the rings are built and the No Op self-test has passed. That
     * makes this exit the *most* start-time cleanup any refusal in this file
     * performs, which is the property the artifact exists to observe.
     *
     * It takes the No Op step's exit *shape* - stop the controller, fail closed
     * if the stop cannot be proved, drop INITIALIZED, then record the refusal -
     * so what a target observes is this driver's own refusal path and not a
     * special case written for a test.
     *
     * **It is not that exit verbatim, and the difference is observable**
     * (a review's finding 3). The No Op step stops the controller only
     * when the *submit* failed, i.e. with nothing outstanding. Here the submit
     * succeeded, so a No Op is on the command ring with its watchdog armed, and
     * `XhciStopController`'s quiesce abandons it: `CommandsAbandoned` reads 1,
     * the log carries `cmd.abandoned`, and `NoOpWitnessFired` stays 0 because
     * the completion the DPC would have counted never arrives. That is correct
     * behaviour - the quiesce retires the generation, so the watchdog's
     * mismatch branch returns without touching hardware - but a run sheet that
     * did not predict it would read those three values as a defect the artifact
     * had found. Task 12.3's runs did predict them; roadmap task 12.3's
     * result box records what was actually observable, which was less than
     * this: `CommandsAbandoned` and `NoOpWitnessFired` are printed by
     * `CheckController`, and usbport never calls it once a start has failed.
     * The whole of the evidence is the `command: abandoned outstanding TRB`
     * trace line, whose address matched the No Op's submit address.
     *
     * Injecting *before* the submit would avoid it and was rejected: the
     * artifact exists to exercise the most complete start-time cleanup this
     * driver performs, and a start whose command ring was never exercised is
     * not that.
     *
     * Never present in a shipping build: the define comes only from
     * XHCI_EXTRA_DEFINES, which also embeds the do-not-deploy marker (see
     * src/sources and src/xhci_dispatch.c). scripts\package\make-package.ps1
     * stages this artifact only under -FailStartArtifact, and only for an image
     * carrying the artifact's own second marker.
     */
    if (!XhciStopController(ext)) {
        XhciFailClosedDma(ext);
    }
    (VOID)XhciControllerUpdateFlags(ext, XHCI_EXT_FLAG_INITIALIZED, 0);
    return xhciInitFailed(ext, XHCI_INIT_STEP_FAIL_INJECT, 0,
                          MP_STATUS_HW_ERROR);
#endif

    ext->InitStep = XHCI_INIT_STEP_DONE;
    ext->InitStatus = 0;

    XHCI_DBG_VALUE("init complete, USBSTS", XhciReadOp(ext, XHCI_OP_USBSTS));
    XHCI_DBG_VALUE("command ring PA", ext->CommandRing.BasePA);
    XHCI_DBG_VALUE("event ring PA", ext->EventRing.BasePA);
    return MP_STATUS_SUCCESS;
}
