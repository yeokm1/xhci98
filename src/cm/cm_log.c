/*
 * cm_log.c - xhcicm.sys's use of the shared log ring (src\xhci_log.c,
 * compiled into this binary by path; record 21 section 8).
 *
 * Recording follows the shared ladder: XhciLogVerbosity 2 and up fills
 * the ring (xhci_log.h). Emission is decision D12's default: the release
 * flavour emits nothing and imports no DbgPrint, as xhciuas.sys prints
 * nothing; the debug and qemu flavours hand the ring to DbgPrint at each
 * stop, under #if DBG, so a reading needs a checked flavour and DebugView;
 * the qemu flavour also mirrors the same bytes to port 0xE9, the
 * emulator's debug console. This file is the one DbgPrint site and the one
 * port-0xE9 site of the binary (SITES=cm_log.obj in
 * the allowlists, debug and qemu rows only), and AGENTS.md's two-site
 * rule for an unguarded DbgPrint is untouched.
 *
 * IRQL: CmLogInit, CmLogApply and CmLogNote at <= DISPATCH_LEVEL;
 * CmLogEmit at PASSIVE_LEVEL only (the PnP path calls it).
 */

#include "cm.h"

/* The drain's stack buffer: small, because a large local costs __chkstk,
 * which the Windows 2000 DDK's driver libraries do not provide. */
#define CM_LOG_CHUNK 128

#if defined(XHCI_FLAVOUR_QEMU)
/*
 * The qemu flavour's port-0xE9 mirror (decision D12, record 21 section 8):
 * the emulator's debug console takes the bytes as they are emitted. Its
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
    sink = 0;
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

VOID CmLogEmit(PCM_NHI nhi, ULONG reason)
{
#if DBG
    UCHAR staging[CM_LOG_CHUNK + 1];
    KIRQL irql;
    ULONG decision;
    ULONG chunk;
    ULONG emitted;

    KeAcquireSpinLock(&nhi->LogLock, &irql);
    decision = XhciLogFlushBegin(&nhi->Log, reason);
    KeReleaseSpinLock(&nhi->LogLock, irql);
    if (decision != XHCI_LOG_FLUSH_GO) {
        return;
    }
    emitted = 0;
    for (;;) {
        KeAcquireSpinLock(&nhi->LogLock, &irql);
        chunk = XhciLogDrain(&nhi->Log, staging, CM_LOG_CHUNK);
        KeReleaseSpinLock(&nhi->LogLock, irql);
        if (chunk == 0) {
            break;
        }
        staging[chunk] = 0;
        DbgPrint("%s", (char *)staging);
#if defined(XHCI_FLAVOUR_QEMU)
        cmLogMirrorE9(staging, chunk);
#endif
        emitted += chunk;
    }
    KeAcquireSpinLock(&nhi->LogLock, &irql);
    if (emitted != 0) {
        nhi->Log.DebugViewEmits++;
        nhi->Log.DebugViewBytes += emitted;
    }
    XhciLogFlushEnd(&nhi->Log, emitted, 1);
    KeReleaseSpinLock(&nhi->LogLock, irql);
#else
    UNREFERENCED_PARAMETER(nhi);
    UNREFERENCED_PARAMETER(reason);
#endif
}
