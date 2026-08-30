/*
 * xhci_log.c - the in-memory log ring, roadmap tasks 11-V.7 (the ring),
 * 11-V.9 (the producer set and the per-code budget) and 13-L.2 (the three
 * switches, and the removal of the file sink and all of its path handling).
 *
 * See src/xhci_log.h for why a ring is the only shape that survives the two
 * constraints (no `Zw*` without Win98 evidence, and no PASSIVE-level worker
 * available to this driver at all), and for the split between this file and the
 * DDK half in src/xhci_dispatch.c.
 *
 * **The rule task 13-L.2 added, and it is the one to keep in mind reading the
 * rest: RECORDING IS NOT EMISSION.** `Enabled` is a function of the verbosity
 * level and of nothing else. It used to be `FileEnabled || DebugViewEnabled`,
 * so naming a sink was what switched recording on - and on Windows 98 neither
 * sink can honestly be selected, which left the ring empty on the one operating
 * system it was built for.
 *
 * Three rules hold throughout, and they are the same three every pure file in
 * this driver keeps:
 *
 *   **It decides nothing about hardware.** Nothing here reads or writes a
 *   register, and nothing the driver does branches on anything here. The log is
 *   diagnosis; a driver whose behaviour changed when the switch was on would be
 *   a driver whose logs describe a different driver.
 *
 *   **It never holds a pointer the caller owns.** `label` is copied out during
 *   the call and never retained; the drain writes into a buffer the caller
 *   supplies and sized.
 *
 *   **It refuses rather than grows.** The ring is fixed, the record length is
 *   capped, and both overruns are counted. There is no allocation anywhere in
 *   this driver (AGENTS.md) and this file is not the place to introduce one.
 *
 * ## The lock, and why there is none here
 *
 * The ring is mutated from two kinds of context - the DISPATCH-level callbacks,
 * the deferred completion path among them, and the PASSIVE lifecycle callbacks
 * - so it needs the same discipline as
 * every other shared field in the extension: **the controller lock**
 * (docs/contributing/design/05-locking-model.md section 2).
 *
 * **The trace macros are not a third context, and an earlier draft of this
 * paragraph said they were.** `XHCI_DBG_*` reaches `XhciDbgEmit`
 * (src/xhci_dbg.c), which writes port `0xE9` and `DbgPrint` and touches no ring
 * at all. The two channels are disjoint in every flavour, so no `XHCI_DBG_*`
 * site can put anything here and none of them needs this lock. *(Since task
 * 13-L.1 those macros compile to nothing outside `qemu` anyway, which makes the
 * point moot in both published builds rather than changing it.)*
 *
 * The lock is taken by the
 * caller, exactly as src/xhci_topo.c's caller does, and for the same reason:
 * taking it here would make the file impure and would nest a second acquisition
 * inside every path that already holds it.
 *
 * The one context that must never reach this file is the **ISR**, which runs at
 * DIRQL and takes no lock (section 4 of the same document). No append site is
 * on the ISR path, and that is a review property rather than something the ring
 * can enforce.
 *
 * ## The IRQL derivation this file does NOT rest on
 *
 * Task 11-V.7 says to derive which lifecycle callbacks are PASSIVE from the
 * shipping binaries rather than assuming it. That derivation was **attempted
 * and left unfinished**, and saying so is worth more than a
 * confident sentence: usbport copies the registration packet into its own
 * storage, so a miniport callback is reached through `[<usbport's copy> +
 * slot]` and a plain search for `call dword ptr [reg+3Ch]` also matches other
 * three-argument vtables in the image, which is what the first pass found. What
 * *was* established is the shape to resume from - usbport's device extension
 * holds the miniport extension at `+0x100` and a pointer to its packet copy at
 * `+0x104` (SP4 `0x254A2`, `0x27543`), and `ResumeController` is visibly called
 * as `push [fdo+0x100]; call [pkt+0x44]`.
 *
 * Until that is finished, the driver **measures** instead of claiming: the
 * flush site reads `KeGetCurrentIrql()` and refuses above PASSIVE_LEVEL. That
 * is strictly stronger than the derivation would have been, because it cannot
 * be wrong about the other side.
 *
 * C89 only. No MMIO, no DDK, no lock, no allocation.
 */

#include "xhci_compat.h"
#include "xhci_log.h"

/* ------------------------------------------------------------------ */
/* Formatting                                                          */
/* ------------------------------------------------------------------ */

static const char xhciLogHexDigits[] = "0123456789ABCDEF";

/* See the contract in src/xhci_log.h. IRQL: any. */
VOID XhciLogHex32(UCHAR *out, ULONG value)
{
    ULONG i;

    for (i = 0; i < 8; i++) {
        out[7 - i] = (UCHAR)xhciLogHexDigits[value & 0xF];
        value >>= 4;
    }
}

/* ------------------------------------------------------------------ */
/* The ring                                                            */
/* ------------------------------------------------------------------ */

/*
 * One byte in. The wrap is what makes this a ring rather than a buffer that
 * fills and stops, and the choice of *which* byte to lose is the whole design
 * question: the oldest is dropped, so a log that overflows keeps the events
 * nearest the fault. A driver that kept the oldest would answer "what happened
 * at start" when the user is asking "what happened just now".
 *
 * IRQL: any.
 */
static VOID xhciLogPut(PXHCI_LOG log, UCHAR b)
{
    log->Ring[log->Head] = b;
    log->Head = (log->Head + 1) & XHCI_LOG_RING_MASK;

    if (log->Used < XHCI_LOG_RING_BYTES) {
        log->Used++;
    } else {
        /*
         * Full: this byte overwrote the oldest one. Counted rather than
         * silently absorbed, because "the log is complete" and "the log is the
         * last XHCI_LOG_RING_BYTES of a much longer story" are different
         * readings of the same file and nothing in the file itself
         * distinguishes them.
         */
        log->BytesDropped++;
    }
}

/* See the contract in src/xhci_log.h. IRQL: any. */
VOID XhciLogAppend(PXHCI_LOG log,
                   const char *label,
                   ULONG value,
                   ULONG hasValue)
{
    UCHAR hex[8];
    ULONG written;
    ULONG i;

    if (log == NULL || label == NULL) {
        return;
    }

    /*
     * The switch gates the *append*, not just the flush. Two reasons, and the
     * second is the one that matters: a ring filled by a driver whose owner
     * never asked for a log is XHCI_LOG_RING_BYTES of the extension doing
     * nothing, and the producer set reaches the DPC - `ep.halted`,
     * `ep.recovery` and `xfer.error` are on the deferred completion path - so
     * an unswitched machine would otherwise pay the formatting and the copy at
     * every one of them. (**Not the ISR**: `src/xhci_evt.c` contains no
     * producer at all, which is deliberate and worth keeping that way.)
     * `Suppressed` counts what the gate turned away, so an empty log is
     * distinguishable from an unswitched one.
     *
     * **`Publishing` is the one bypass and it belongs to the flush** (task
     * 13-L.2). The ladder says level 0 publishes the counter block, so the
     * counter block has to reach the ring on a machine whose ordinary producers
     * are off - and it is composed in one short locked PASSIVE-level pass with
     * this flag raised, rather than through a second append entry point that
     * would be a second spelling of the record format.
     */
    if (!log->Enabled && !log->Publishing) {
        log->Suppressed++;
        return;
    }

    written = 0;
    for (i = 0; label[i] != '\0'; i++) {
        if (written >= XHCI_LOG_MAX_RECORD) {
            break;
        }
        xhciLogPut(log, (UCHAR)label[i]);
        written++;
    }

    /*
     * A label that hit the cap loses its value too, and that is deliberate: a
     * truncated label followed by a number reads as a *complete* record naming
     * something else. Better a visibly short line.
     */
    if (label[i] != '\0') {
        log->Truncated++;
    } else if (hasValue) {
        xhciLogPut(log, (UCHAR)'=');
        XhciLogHex32(hex, value);
        for (i = 0; i < 8; i++) {
            xhciLogPut(log, hex[i]);
        }
    }

    xhciLogPut(log, (UCHAR)'\r');
    xhciLogPut(log, (UCHAR)'\n');

    log->Appends++;
}

/* See the contract in src/xhci_log.h. IRQL: any. */
VOID XhciLogAppendAddress(PXHCI_LOG log, const char *label, ULONG value)
{
    if (log == NULL) {
        return;
    }

    /*
     * **The tier boundary, enforced here rather than promised elsewhere.** The
     * ladder's line between 3 and 4 is addresses, and it is a *publication*
     * line: it is about what a maintainer may reasonably ask a stranger to
     * paste into a public issue. A record written at the log level reaches the
     * ring, the ring reaches the plain-text companion, and the companion is the
     * thing that gets pasted - so an address logged below
     * XHCI_LOG_VERBOSITY_FULL defeats the boundary wherever the documentation
     * says it is drawn.
     *
     * `Publishing` does not open this. The counter block is flush-time
     * formatting over counters and holds no address; if it ever needs one, it
     * needs this test too.
     */
    if (log->Verbosity < XHCI_LOG_VERBOSITY_FULL) {
        log->Suppressed++;
        return;
    }

    XhciLogAppend(log, label, value, 1);
}

/* ------------------------------------------------------------------ */
/* Flushing                                                            */
/* ------------------------------------------------------------------ */

/* See the contract in src/xhci_log.h. IRQL: any. */
VOID XhciLogFlushRefusedIrql(PXHCI_LOG log)
{
    if (log == NULL) {
        return;
    }
    log->FlushesRefusedIrql++;
}

/* See the contract in src/xhci_log.h. IRQL: any. */
ULONG XhciLogFlushBegin(PXHCI_LOG log, ULONG reason)
{
    if (log == NULL) {
        return XHCI_LOG_FLUSH_DISABLED;
    }

    /*
     * **The SINK, not `Enabled`** (task 13-L.2). Recording is gated by the
     * verbosity level and publication by whether anything is listening, and
     * this is the publication end: a machine at the counter rung with the
     * DebugView sink on has an empty note ring and a counter block, and that is
     * a real report. Testing `Enabled` here would have made that rung publish
     * nothing at all, which is not what the ladder says it is.
     */
    if (!log->DebugViewEnabled) {
        log->FlushesRefusedState++;
        return XHCI_LOG_FLUSH_DISABLED;
    }

    if (log->Used == 0) {
        log->FlushesRefusedState++;
        return XHCI_LOG_FLUSH_EMPTY;
    }

    /*
     * The reason line goes in *before* the drain, so it is the last record in
     * the output rather than a separate write - which means a torn or partial
     * capture still ends at a record boundary. (That was worth a sentence when
     * the sink was one `ZwWriteFile`; it is worth the same one now that the
     * sink is a loop of `DbgPrint` calls a capture program concatenates.)
     *
     * It also carries the drop count, which is the only place a reader learns
     * that what follows is a window rather than the whole run.
     */
    XhciLogAppend(log, "flush.reason", reason, 1);
    XhciLogAppend(log, "flush.dropped", log->BytesDropped, 1);
    XhciLogAppend(log, "flush.truncated", log->Truncated, 1);

    return XHCI_LOG_FLUSH_GO;
}

/* See the contract in src/xhci_log.h. IRQL: any. */
ULONG XhciLogDrain(PXHCI_LOG log, UCHAR *out, ULONG capacity)
{
    ULONG take;
    ULONG start;
    ULONG i;

    if (log == NULL || out == NULL || capacity == 0) {
        return 0;
    }

    take = log->Used;
    if (take > capacity) {
        take = capacity;
    }

    /*
     * Oldest first, and `Used` is what says how far back that is: `Head` is one
     * past the newest byte, so the oldest byte still held sits `Used` positions
     * behind it, modulo the ring.
     *
     * Nothing is counted as dropped here. A short buffer is a *chunk*, not a
     * loss - the caller comes back for the rest - and counting it would make
     * `BytesDropped` mean two different things, one of which is not a loss at
     * all.
     */
    start = (log->Head + XHCI_LOG_RING_BYTES - log->Used) & XHCI_LOG_RING_MASK;
    for (i = 0; i < take; i++) {
        out[i] = log->Ring[(start + i) & XHCI_LOG_RING_MASK];
    }

    log->Used -= take;

    return take;
}

/* See the contract in src/xhci_log.h. IRQL: any. */
VOID XhciLogFlushEnd(PXHCI_LOG log, ULONG bytes, ULONG ok)
{
    if (log == NULL) {
        return;
    }

    /*
     * Unconditional, and separate from the verdict: `bytes` is what the sink
     * reported taking, so a short delivery contributes the part that really
     * left while still being counted as a failure.
     */
    log->FlushBytes += bytes;

    if (ok) {
        log->Flushes++;
    } else {
        log->FlushFailures++;
    }
}

/*
 * ------------------------------------------------------------------
 * The two switches (roadmap task 13-L.2)
 * ------------------------------------------------------------------
 *
 * *(Task 11-V.9's path form, path probe, path validation, path composition and
 * sink selection stood here - about 300 lines of it, plus two transcribed
 * NTSTATUS values and three measured root strings. All of it left with the file
 * sink. The reason is in src/xhci_log.h and in design record 08 section 13.0.1,
 * and it is evidential rather than load-time: the sink was never observed to
 * write a byte outside a virtual machine, on either target.)*
 */

/* See the contract in src/xhci_log.h. IRQL: any. */
ULONG XhciLogApplySwitches(PXHCI_LOG log,
                           ULONG verbosityValue,
                           ULONG debugViewValue)
{
    if (log == NULL) {
        return 0;
    }

    log->DebugViewEnabled = (debugViewValue != 0) ? 1UL : 0UL;

    /*
     * **Refused, not clamped.** A value of 7 is not "the highest level" - it is
     * a value this driver does not understand, and treating it as 4 would put
     * kernel addresses in a dump on the strength of a typo. The fallback is the
     * default and the refusal is recorded, so a reader can always tell which of
     * the two happened. Both numbers travel in every snapshot header.
     *
     * **Since the snapshot-value merge the fallback also shuts the read channel**, because
     * the default is `XHCI_LOG_VERBOSITY_OFF` and the channel's consent is now
     * rung 0 of this same value. That is the right direction for a refusal to
     * fail in, and it is the reverse of what a clamp would have done.
     */
    log->VerbosityRead = verbosityValue;
    if (verbosityValue > XHCI_LOG_VERBOSITY_MAX) {
        log->VerbosityRefused = 1;
        log->Verbosity = XHCI_LOG_VERBOSITY_DEFAULT;
    } else {
        log->VerbosityRefused = 0;
        log->Verbosity = verbosityValue;
    }

    /*
     * **The whole of the switch rework is this line.** It is a function of the
     * level and of nothing else: no sink appears in it, so the ring fills on a
     * machine with no reachable sink at all - which is every Windows 98
     * machine, and is the property the task exists to create.
     *
     * The threshold is XHCI_LOG_VERBOSITY_RING and not "nonzero", which is the
     * whole of why the merged ladder has five rungs: rung 1 engages the read
     * channel with recording still off, and that configuration is the one a
     * bench wants while the append sites' cost on Windows 98 metal is
     * unmeasured.
     */
    log->Enabled = (log->Verbosity >= XHCI_LOG_VERBOSITY_RING) ? 1UL : 0UL;

    return log->Enabled;
}

/* See the contract in src/xhci_log.h. IRQL: any. */
ULONG XhciLogErrorBudget(PXHCI_LOG log, ULONG completionCode)
{
    ULONG slot;

    if (log == NULL) {
        return 0;
    }

    slot = completionCode;
    if (slot >= XHCI_LOG_ERROR_SLOTS) {
        /* The vendor-defined ranges, folded onto one slot. A controller
         * inventing codes is one class of surprise, not sixty-four. */
        slot = XHCI_LOG_ERROR_SLOTS - 1;
    }

    if (log->ErrorRecords[slot] >= XHCI_LOG_ERROR_BUDGET) {
        return 0;
    }
    log->ErrorRecords[slot]++;
    return 1;
}
