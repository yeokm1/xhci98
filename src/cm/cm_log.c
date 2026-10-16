/*
 * cm_log.c - xhcicm.sys's use of the shared log ring (src\xhci_log.c,
 * compiled into this binary by path; record 21 section 8).
 *
 * Recording follows the shared ladder: XhciCmLogVerbosity (cm_reg.c) 2 and
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
 * WHAT IS EMITTED. Whole records, one DbgPrint each, from one drain of the
 * whole ring: below level 4 the
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

/* One printed line: a record is at most XHCI_LOG_MAX_RECORD + 11 bytes,
 * within CM_QL_EMIT_LINE; a stack buffer below a page (a page or more
 * costs __chkstk, which the Windows 2000 DDK's libraries do not provide). */
#define CM_LOG_LINE CM_QL_EMIT_LINE

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

/* One line to the sinks (CmQueryLogEmitLines' callback): NUL-terminated
 * on the stack, printed with a literal format. PASSIVE_LEVEL. */
static VOID cmLogLine(PVOID context, const UCHAR *bytes, ULONG n)
{
    UCHAR line[CM_LOG_LINE + 1];
    ULONG i;

    UNREFERENCED_PARAMETER(context);
    for (i = 0; i < n && i < CM_LOG_LINE; i++) {
        line[i] = bytes[i];
    }
    line[i] = 0;
    DbgPrint("%s", (char *)line);
#if defined(XHCI_FLAVOUR_QEMU)
    cmLogMirrorE9(line, i);
#endif
}

/*
 * The whole ring is drained in one step under LogLock into the NHI's own
 * buffer, so no producer can wrap it while lines are printed, and the
 * drop count read in the same step says whether the first line can be a
 * record's tail (CmQueryLogEmitLines). The count is taken only by a drain,
 * so a wrap while emission was off is still seen at the next one.
 */
VOID CmLogEmit(PCM_NHI nhi, ULONG reason)
{
    KIRQL irql;
    ULONG decision;
    ULONG n;
    ULONG verbosity;
    ULONG partial;

    KeAcquireSpinLock(&nhi->LogLock, &irql);
    decision = XhciLogFlushBegin(&nhi->Log, reason);
    if (decision != XHCI_LOG_FLUSH_GO) {
        KeReleaseSpinLock(&nhi->LogLock, irql);
        return;
    }
    verbosity = nhi->Log.Verbosity;
    partial = (ULONG)(nhi->Log.BytesDropped != nhi->LogDroppedEmitted);
    n = XhciLogDrain(&nhi->Log, nhi->LogEmit, XHCI_LOG_RING_BYTES);
    nhi->LogDroppedEmitted = nhi->Log.BytesDropped;
    KeReleaseSpinLock(&nhi->LogLock, irql);
    (VOID)CmQueryLogEmitLines(nhi->LogEmit, n, partial, verbosity, cmLogLine,
                              NULL);
    KeAcquireSpinLock(&nhi->LogLock, &irql);
    if (n != 0) {
        nhi->Log.DebugViewEmits++;
        nhi->Log.DebugViewBytes += n;
    }
    XhciLogFlushEnd(&nhi->Log, n, 1);
    KeReleaseSpinLock(&nhi->LogLock, irql);
}
