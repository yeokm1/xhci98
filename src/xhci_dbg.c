/*
 * xhci_dbg.c - the `qemu` flavour's trace channel (see xhci_dbg.h for the
 * contract and for the three imports this file adds to a qemu binary; it adds
 * none to either published one, and said "the debug binary" until the post-Phase 13 review rounds).
 *
 * Formatting is done by hand rather than by DbgPrint's own %-expansion for one
 * reason: the same finished line has to go to two sinks, and only one of them
 * takes a format string. Doing it this way also keeps the file free of any
 * CRT dependency, which kernel mode has none of anyway.
 *
 * C89 only. IRQL: any.
 */

#include "xhci_dbg.h"

/*
 * `XHCI_DBG_E9` without `XHCI_DBG_LIVE` would compile this whole file away and
 * take the import with it, which is the one outcome defect 2b's discriminating
 * binary must not have: an absent WRITE_PORT_UCHAR means the build, not the
 * machine, decided the outcome. `src/sources` defines the two together for the
 * qemu flavour, so this only fires for a hand-built file.
 */
#if defined(XHCI_DBG_E9) && !defined(XHCI_DBG_LIVE)
#error XHCI_DBG_E9 without XHCI_DBG_LIVE compiles this file away and drops the WRITE_PORT_UCHAR import with it, so the define would silently do nothing. They are set together by src/sources for the qemu flavour.
#endif

/*
 * The header's own condition under its own name: this file is the `qemu`
 * flavour's live trace, and `debug` must carry neither its port write nor its
 * per-line `DbgPrint`.
 */
#ifdef XHCI_DBG_TRACE

/* One line, built on the stack. Long enough for a name plus three hex words
 * and the IRQL; XhciDbgWords wraps rather than growing it. */
#define XHCI_DBG_LINE_MAX 128

static const char xhciDbgHexDigits[] = "0123456789ABCDEF";

/* Append `s`, stopping at the buffer end. Returns the new length. */
static ULONG xhciDbgAppend(char *buf, ULONG len, const char *s)
{
    while (*s != '\0' && len < (XHCI_DBG_LINE_MAX - 1)) {
        buf[len] = *s;
        len++;
        s++;
    }
    return len;
}

/* Append `digits` hex digits of `value`, most significant first. */
static ULONG xhciDbgAppendHex(char *buf, ULONG len, ULONG value, ULONG digits)
{
    ULONG shift;

    shift = digits * 4;
    while (shift > 0 && len < (XHCI_DBG_LINE_MAX - 1)) {
        shift -= 4;
        buf[len] = xhciDbgHexDigits[(value >> shift) & 0xF];
        len++;
    }
    return len;
}

/*
 * Emit a completed, NUL-terminated line to both sinks.
 *
 * **The port-0xE9 write is opt-IN and only the `qemu` flavour asks for it**
 * (roadmap task 13-L.1, design record 08 section 5.1). It is a QEMU/Bochs
 * debug console, and this file used to say that "on real hardware nothing
 * decodes that port and the byte is discarded, so this is safe to leave in
 * every debug build". **That sentence was never measured and the published
 * 0.0.0.4 debug flavour is what disproved the conclusion drawn from it**: the
 * import it needs - HAL.dll!WRITE_PORT_UCHAR - is the sole delta against the
 * release flavour, and that binary gives the ThinkPad E460 a Code 2 and loads
 * nothing under Windows 98 SE, while a build without the pair loads clean and
 * drives real devices. The control that was taken - restoring the
 * previous binary, which cleared the bang - implicates the BUILD and not the
 * import; and the build that loaded was itself a diagnostic one, so it carried
 * the do-not-deploy marker too. The matched control (xhci98-diagcontrol.sys,
 * run-13e.md P6) has never been booted.
 *
 * Whether the cause is the import failing to resolve or the port being decoded
 * is **still open** - that is defect 2b, `docs/contributing/runs/run-13e.md`
 * P6, and XHCI_DBG_E9_NOEXEC below is the discriminator that answers it. What
 * the polarity change settles is only which configuration is the default: the
 * shipping `debug` flavour, whose defining requirement is that it loads on real
 * Windows 98 and Windows 2000 metal, no longer carries the import at all.
 */
#if defined(XHCI_DBG_E9_NOEXEC) && !defined(XHCI_DBG_E9)
#error XHCI_DBG_E9_NOEXEC exists to keep the WRITE_PORT_UCHAR import while never executing the write, so it is meaningless without XHCI_DBG_E9. Build defect 2b's discriminating binary from the qemu flavour, which is the one that defines XHCI_DBG_E9.
#endif

#ifdef XHCI_DBG_E9_NOEXEC
/*
 * The discriminating test for the 0.0.0.4 debug flavour's Code 2 on the E460
 * (defect 2b). That binary does not load there, the sole import
 * delta against the standard flavour is HAL.dll!WRITE_PORT_UCHAR, and a rebuild
 * without the pair loads clean. Two readings survive that and they contradict
 * each other: either the import does not resolve on Win98 metal, or port 0xE9
 * IS decoded on that chipset and the write faults during init, which would
 * refute the never-measured comment this file used to carry.
 *
 * This define separates them by keeping the import and never executing the
 * write. Loads => the write is the problem. Code 2 => the import is.
 *
 * The gate is file-scope and **volatile** so that MSVC 6 must emit the read,
 * the branch and therefore the call - a plain `static ULONG` initialised to 0
 * would be constant-folded, the call site deleted, and the import dropped,
 * which would silently turn this binary into a no-E9 build wearing a different
 * name and make the test answer a question nobody asked. **Verify the import
 * survives with DUMPBIN before deploying**: an absent WRITE_PORT_UCHAR means
 * the build, not the machine, decided the outcome.
 *
 * Nothing writes this variable. That is the point of it.
 */
static volatile ULONG xhciDbgE9Gate = 0;
#endif

static VOID xhciDbgEmit(const char *line)
{
#ifdef XHCI_DBG_E9
    const char *p;

    p = line;
    while (*p != '\0') {
#ifdef XHCI_DBG_E9_NOEXEC
        if (xhciDbgE9Gate != 0)
#endif
        {
            WRITE_PORT_UCHAR((PUCHAR)0xE9, (UCHAR)*p);
        }
        p++;
    }
#endif
    DbgPrint("%s", line);
}

static ULONG xhciDbgIrql(VOID)
{
#ifdef XHCI_DBG_NO_IRQL
    return 0xFF;
#else
    return (ULONG)KeGetCurrentIrql();
#endif
}

static ULONG xhciDbgPrefix(char *buf)
{
    return xhciDbgAppend(buf, 0, "xhci98: ");
}

VOID XhciDbgText(const char *msg)
{
    char line[XHCI_DBG_LINE_MAX];
    ULONG len;

    len = xhciDbgPrefix(line);
    len = xhciDbgAppend(line, len, msg);
    len = xhciDbgAppend(line, len, "\n");
    line[len] = '\0';
    xhciDbgEmit(line);
}

VOID XhciDbgValue(const char *msg, ULONG value)
{
    char line[XHCI_DBG_LINE_MAX];
    ULONG len;

    len = xhciDbgPrefix(line);
    len = xhciDbgAppend(line, len, msg);
    len = xhciDbgAppend(line, len, "=");
    len = xhciDbgAppendHex(line, len, value, 8);
    len = xhciDbgAppend(line, len, "\n");
    line[len] = '\0';
    xhciDbgEmit(line);
}

VOID XhciDbgCallback(const char *name, ULONG a, ULONG b, ULONG c)
{
    char line[XHCI_DBG_LINE_MAX];
    ULONG len;

    len = xhciDbgPrefix(line);
    len = xhciDbgAppend(line, len, "cb ");
    len = xhciDbgAppend(line, len, name);
    len = xhciDbgAppend(line, len, " irql=");
    len = xhciDbgAppendHex(line, len, xhciDbgIrql(), 2);
    len = xhciDbgAppend(line, len, " a=");
    len = xhciDbgAppendHex(line, len, a, 8);
    len = xhciDbgAppend(line, len, " b=");
    len = xhciDbgAppendHex(line, len, b, 8);
    len = xhciDbgAppend(line, len, " c=");
    len = xhciDbgAppendHex(line, len, c, 8);
    len = xhciDbgAppend(line, len, "\n");
    line[len] = '\0';
    xhciDbgEmit(line);
}

/*
 * Dump `count` ULONGs, four per line, each line labelled with the word index
 * so a structure can be reassembled from the log without counting columns.
 * The caller is responsible for the pointer being readable - this is used for
 * usbport-owned structures whose size the ABI record pins.
 */
VOID XhciDbgWords(const char *msg, const ULONG *words, ULONG count)
{
    char line[XHCI_DBG_LINE_MAX];
    ULONG len;
    ULONG i;

    if (words == NULL) {
        XhciDbgText("(null word dump)");
        return;
    }

    i = 0;
    while (i < count) {
        ULONG n;

        len = xhciDbgPrefix(line);
        len = xhciDbgAppend(line, len, msg);
        len = xhciDbgAppend(line, len, "+");
        len = xhciDbgAppendHex(line, len, i * 4, 2);
        len = xhciDbgAppend(line, len, ":");

        for (n = 0; n < 4 && i < count; n++) {
            len = xhciDbgAppend(line, len, " ");
            len = xhciDbgAppendHex(line, len, words[i], 8);
            i++;
        }

        len = xhciDbgAppend(line, len, "\n");
        line[len] = '\0';
        xhciDbgEmit(line);
    }
}

#else /* release build: keep the translation unit non-empty */

typedef int xhciDbgFreeBuildPlaceholder;

#endif /* XHCI_DBG_TRACE */
