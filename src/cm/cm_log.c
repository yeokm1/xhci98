/*
 * cm_log.c - xhcicm.sys's use of the shared log ring (src\xhci_log.c,
 * compiled into this binary by path; record 21 section 8).
 *
 * Recording follows the shared ladder: XhciCmLogVerbosity (XhciLogVerbosity
 * before 3.0.0.0, read as a fallback for this one release; cm_reg.c) 2 and
 * up fills the ring (xhci_log.h). Emission is the owner's decision of
 * 2026-10-16, which reversed decision D12 for the release flavour, for
 * consistency with xhci98.sys: every flavour hands the ring to DbgPrint
 * when XhciCmLogDebugView is 1, and the checked flavours (debug, qemu)
 * always do, as before; from the connection-manager thread after each of
 * its passes that recorded something and from the PnP path at each stop,
 * at PASSIVE_LEVEL only. The qemu flavour also mirrors the same bytes to
 * port 0xE9, the emulator's debug console. This file is the one DbgPrint
 * site and the one port-0xE9 site of the binary (SITES=cm_log.obj in the
 * allowlists).
 *
 * WHAT IS EMITTED. Whole records, one DbgPrint each: below level 4 the
 * value of a record whose label names an address (cm_qryb.h,
 * CmQueryLogAddress, the rule the query device's answer follows) is
 * written "--------", so no flavour publishes an address below the level
 * that asks for them, as xhci98.sys's ring holds none below it.
 *
 * IRQL: CmLogInit, CmLogApply and CmLogNote at <= DISPATCH_LEVEL;
 * CmLogEmit at PASSIVE_LEVEL only (the thread, and the PnP path once the
 * thread has ended, so the two never emit at once).
 */

#include "cm.h"

/* The drain's stack buffer and the line being assembled: small, because a
 * large local costs __chkstk, which the Windows 2000 DDK's driver
 * libraries do not provide. A record is at most XHCI_LOG_MAX_RECORD + 11
 * bytes, so a line always fits. */
#define CM_LOG_CHUNK 128
#define CM_LOG_LINE  128

#if defined(XHCI_FLAVOUR_QEMU)
/*
 * The qemu flavour's port-0xE9 mirror (record 21 section 8): the
 * emulator's debug console takes the bytes as they are emitted. Its
 * import, HAL.dll!WRITE_PORT_UCHAR, is a qemu row of the x86 allowlist
 * alone (design record 08: the flavour that carries it is never
 * published). IRQL: PASSIVE_LEVEL (CmLogEmit's).
 */
static VOID cmLogMirrorE9(const UCHAR *bytes, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        WRITE_PORT_UCHAR((PUCHAR)0xE9, bytes[i]);
    }
}
#endif

VOID CmLogInit(PCM_NHI nhi)
{
    KeInitializeSpinLock(&nhi->LogLock);
}

VOID CmLogApply(PCM_NHI nhi)
{
    KIRQL irql;
    ULONG sink;

#if DBG
    sink = 1;
#else
    sink = nhi->Values.Value[CM_VAL_LOG_DEBUGVIEW];
#endif
    KeAcquireSpinLock(&nhi->LogLock, &irql);
    nhi->Log.SwitchRead = 1;
    (VOID)XhciLogApplySwitches(&nhi->Log,
                               nhi->Values.Value[CM_VAL_LOG_VERBOSITY], sink);
    KeReleaseSpinLock(&nhi->LogLock, irql);
}

VOID CmLogNote(PCM_NHI nhi, const char *label, ULONG value)
{
    KIRQL irql;

    KeAcquireSpinLock(&nhi->LogLock, &irql);
    XhciLogAppend(&nhi->Log, label, value, 1);
    KeReleaseSpinLock(&nhi->LogLock, irql);
}

/* One record (or the tail of the ring) to the sinks, its address value
 * withheld below level 4. PASSIVE_LEVEL. */
static VOID cmLogLine(UCHAR *line, ULONG n, ULONG verbosity)
{
    (VOID)CmQueryLogLine(line, n, verbosity);
    line[n] = 0;
    DbgPrint("%s", (char *)line);
#if defined(XHCI_FLAVOUR_QEMU)
    cmLogMirrorE9(line, n);
#endif
}

VOID CmLogEmit(PCM_NHI nhi, ULONG reason)
{
    UCHAR staging[CM_LOG_CHUNK];
    UCHAR line[CM_LOG_LINE + 1];
    KIRQL irql;
    ULONG decision;
    ULONG chunk;
    ULONG emitted;
    ULONG verbosity;
    ULONG lineLen;
    ULONG skip;
    ULONG i;

    KeAcquireSpinLock(&nhi->LogLock, &irql);
    decision = XhciLogFlushBegin(&nhi->Log, reason);
    verbosity = nhi->Log.Verbosity;
    /* The wrap overwrote bytes since the last emission: the oldest line can
     * be a record's tail, its label gone, so it is not printed. */
    skip = (ULONG)(nhi->Log.BytesDropped != nhi->LogDroppedEmitted);
    nhi->LogDroppedEmitted = nhi->Log.BytesDropped;
    KeReleaseSpinLock(&nhi->LogLock, irql);
    if (decision != XHCI_LOG_FLUSH_GO) {
        return;
    }
    emitted = 0;
    lineLen = 0;
    for (;;) {
        KeAcquireSpinLock(&nhi->LogLock, &irql);
        chunk = XhciLogDrain(&nhi->Log, staging, CM_LOG_CHUNK);
        KeReleaseSpinLock(&nhi->LogLock, irql);
        if (chunk == 0) {
            break;
        }
        for (i = 0; i < chunk; i++) {
            line[lineLen++] = staging[i];
            if (staging[i] == '\n' || lineLen == CM_LOG_LINE) {
                if (!skip) {
                    cmLogLine(line, lineLen, verbosity);
                }
                skip = 0;
                lineLen = 0;
            }
        }
        emitted += chunk;
    }
    if (lineLen != 0 && !skip) {
        cmLogLine(line, lineLen, verbosity);
    }
    KeAcquireSpinLock(&nhi->LogLock, &irql);
    if (emitted != 0) {
        nhi->Log.DebugViewEmits++;
        nhi->Log.DebugViewBytes += emitted;
    }
    XhciLogFlushEnd(&nhi->Log, emitted, 1);
    KeReleaseSpinLock(&nhi->LogLock, irql);
}
