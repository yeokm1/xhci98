/*
 * xhci_dbg.c - the `qemu` flavour's trace channel (see xhci_dbg.h for the
 * contract and for the three imports this file adds to a qemu binary; it adds
 * none to either published one, and said "the debug binary" until after Phase 13).
 *
 * Formatting is done by hand rather than by DbgPrint's own %-expansion for one
 * reason: the same finished line has to go to two sinks, and only one of them
 * takes a format string. Doing it this way also keeps the file free of any
 * CRT dependency, which kernel mode has none of anyway.
 *
 * Each line reaches the debug console whole: it is built on the caller's
 * stack and written under one flag with interrupts held off (xhciDbgPortLine
 * below; docs/contributing/build-and-test.md, "Whole lines on the debug
 * console").
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
 * flavour's live trace, and `release` (and `debug`, until task 45.45 retired
 * it) must carry neither its port write nor its per-line `DbgPrint`.
 */
#ifdef XHCI_DBG_TRACE

/*
 * One physical line is built on the stack: the prefix, then at most
 * XHCI_DBG_TEXT_MAX characters in all (the prefix counted), then the
 * newline and a terminator. A longer line is not cut short: the builder
 * emits all but its last character with XHCI_DBG_CONT and a newline, and
 * carries on in a new line that begins with the prefix and XHCI_DBG_CONT,
 * so no physical line is longer than 127 characters with its newline, as
 * before, and nothing is dropped. A line that fits comes out byte for byte
 * as it did before the builder existed.
 */
#define XHCI_DBG_LINE_MAX 128
#define XHCI_DBG_TEXT_MAX (XHCI_DBG_LINE_MAX - 2)
#define XHCI_DBG_CONT     '\\'

static const char xhciDbgHexDigits[] = "0123456789ABCDEF";
static const char xhciDbgPrefixText[] = "xhci98: ";

typedef struct _XHCI_DBG_LINE {
    ULONG Len;
    char Text[XHCI_DBG_LINE_MAX];
} XHCI_DBG_LINE;

/*
 * The port-0xE9 write is opt-IN and only the `qemu` flavour asks for it
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
 * flavours that ship (`release`, and `debug` until task 45.45 retired it),
 * whose defining requirement is that they load on real Windows 98 and Windows
 * 2000 metal, carry no such import at all.
 */
#if defined(XHCI_DBG_E9_NOEXEC) && !defined(XHCI_DBG_E9)
#error XHCI_DBG_E9_NOEXEC exists to keep the WRITE_PORT_UCHAR import while never executing the write, so it is meaningless without XHCI_DBG_E9. Build defect 2b's discriminating binary from the qemu flavour, which is the one that defines XHCI_DBG_E9.
#endif

#ifdef XHCI_DBG_E9_NOEXEC
/*
 * **No supported build defines XHCI_DBG_E9_NOEXEC.** It was built through
 * `XHCI_EXTRA_DEFINES`, which `src\sources` and `build-driver.cmd` now refuse
 * (the miniport's diagnostic hatch, retired with it), so building this
 * discriminator means adding the define to the qemu flavour's `C_DEFINES`
 * line in `src\sources` by hand, in a tree that is never published. It is
 * kept because defect 2b is still open and this is the binary that answers it.
 *
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

#ifdef XHCI_DBG_E9
/*
 * Whole lines on the debug console (docs/contributing/build-and-test.md,
 * "Whole lines on the debug console"). Each physical line goes out under
 * one flag with every interrupt of this processor held off, so a line from
 * another thread, a DPC, the ISR or another processor can only come before
 * or after it, never inside it.
 *
 * Held off how: on x86 the processor's own interrupt flag (pushfd, cli,
 * then popfd, which puts back whatever the caller had), because the one
 * x86 binary also runs on 98 SE and ME, whose emulation of the levels above
 * DISPATCH_LEVEL this project has not read, and IF clear is what holds an
 * interrupt off on every x86 kernel alike; on amd64 KeRaiseIrql to
 * HIGH_LEVEL, an intrinsic there (no import) and the documented level that
 * masks every interrupt on NT. The flag is taken with xchg (on x86 by hand,
 * as msi4nt.h's MsgIrqpXchg, so nothing is called while IF is clear; on
 * amd64 the intrinsic). Nothing is called under the flag but the port
 * writes, so while a processor holds the flag an emit can start on it only
 * from an NMI or a fault; an ordinary interrupt can start one while it
 * waits, which is safe, since a waiter holds nothing. Interrupts are held
 * off for one look at the flag at a time and for the writing of the line,
 * not across the wait: between two looks the caller's own state is put
 * back, so a processor waiting for another's line still takes its
 * interrupts if its caller allowed them. The flag is not fair, so under
 * contention one wait can outlast several lines; against that, an NMI path
 * and a holder that never lets go, the wait is bounded: after
 * XHCI_DBG_LOCK_SPINS looks the line is written without the flag and a
 * notice line says so, with the number of such lines since the load,
 * counted by an interlocked increment. No call out, no allocation.
 *
 * DbgPrint stays outside, at the caller's IRQL, as before.
 */
#define XHCI_DBG_LOCK_SPINS 0x04000000UL

static volatile LONG xhciDbgLineFlag;
static volatile LONG xhciDbgLineForced;

#if defined(_M_IX86)
typedef ULONG XHCI_DBG_MASK;

static ULONG xhciDbgMaskOn(VOID)
{
    ULONG f;

    __asm {
        pushfd
        pop eax
        mov f, eax
        cli
    }
    return f;
}

static VOID xhciDbgMaskOff(ULONG f)
{
    __asm {
        mov eax, f
        push eax
        popfd
    }
}

static LONG xhciDbgXchg(volatile LONG *target, LONG value)
{
    LONG r;

    __asm {
        mov ecx, target
        mov eax, value
        xchg [ecx], eax
        mov r, eax
    }
    return r;
}

/* The new value; lock xadd, by hand like the exchange. */
static LONG xhciDbgIncrement(volatile LONG *target)
{
    LONG r;

    __asm {
        mov ecx, target
        mov eax, 1
        lock xadd [ecx], eax
        inc eax
        mov r, eax
    }
    return r;
}
#elif defined(_M_AMD64)
typedef KIRQL XHCI_DBG_MASK;

static KIRQL xhciDbgMaskOn(VOID)
{
    KIRQL old;

    KeRaiseIrql(HIGH_LEVEL, &old);
    return old;
}

static VOID xhciDbgMaskOff(KIRQL old)
{
    KeLowerIrql(old);
}

#define xhciDbgXchg(t, v) InterlockedExchange((t), (v))
#define xhciDbgIncrement(t) InterlockedIncrement((t))
#else
#error the qemu flavour's line flag knows x86 and amd64 only
#endif

static VOID xhciDbgPortByte(UCHAR c)
{
#ifdef XHCI_DBG_E9_NOEXEC
    if (xhciDbgE9Gate != 0)
#endif
    {
        WRITE_PORT_UCHAR((PUCHAR)0xE9, c);
    }
}

static VOID xhciDbgPortLine(const char *line, ULONG len)
{
    XHCI_DBG_MASK mask;
    ULONG spins;
    ULONG i;
    ULONG forced;
    BOOLEAN held;

    held = FALSE;
    mask = xhciDbgMaskOn();
    for (spins = 0; spins < XHCI_DBG_LOCK_SPINS; spins++) {
        if (xhciDbgLineFlag == 0 && xhciDbgXchg(&xhciDbgLineFlag, 1) == 0) {
            held = TRUE;
            break;
        }
        xhciDbgMaskOff(mask);
        mask = xhciDbgMaskOn();
    }
    for (i = 0; i < len; i++) {
        xhciDbgPortByte((UCHAR)line[i]);
    }
    if (held) {
        (VOID)xhciDbgXchg(&xhciDbgLineFlag, 0);
    } else {
        const char *p;

        forced = (ULONG)xhciDbgIncrement(&xhciDbgLineForced);
        for (p = xhciDbgPrefixText; *p != '\0'; p++) {
            xhciDbgPortByte((UCHAR)*p);
        }
        for (p = "dbg: line written without the line flag, lines="; *p != '\0'; p++) {
            xhciDbgPortByte((UCHAR)*p);
        }
        for (i = 8; i > 0; i--) {
            xhciDbgPortByte((UCHAR)xhciDbgHexDigits[(forced >> ((i - 1) * 4)) & 0xF]);
        }
        xhciDbgPortByte((UCHAR)'\n');
    }
    xhciDbgMaskOff(mask);
}
#endif /* XHCI_DBG_E9 */

#ifdef XHCI_HOST_TEST
/* The host suite's sink (test/test_dbg.c): each finished physical line. */
VOID XhciDbgHostSink(const char *line, ULONG len);
#endif

/* Emit one finished physical line, NUL-terminated, `len` bytes with its
 * newline. */
static VOID xhciDbgEmit(const char *line, ULONG len)
{
#ifdef XHCI_DBG_E9
    xhciDbgPortLine(line, len);
#endif
#ifdef XHCI_HOST_TEST
    XhciDbgHostSink(line, len);
#else
    UNREFERENCED_PARAMETER(len);
    DbgPrint("%s", line);
#endif
}

static VOID xhciDbgPut(XHCI_DBG_LINE *l, char c)
{
    ULONG i;
    char carry;

    if (l->Len == XHCI_DBG_TEXT_MAX) {
        carry = l->Text[XHCI_DBG_TEXT_MAX - 1];
        l->Text[XHCI_DBG_TEXT_MAX - 1] = XHCI_DBG_CONT;
        l->Text[XHCI_DBG_TEXT_MAX] = '\n';
        l->Text[XHCI_DBG_TEXT_MAX + 1] = '\0';
        xhciDbgEmit(l->Text, XHCI_DBG_TEXT_MAX + 1);
        l->Len = 0;
        for (i = 0; xhciDbgPrefixText[i] != '\0'; i++) {
            l->Text[l->Len++] = xhciDbgPrefixText[i];
        }
        l->Text[l->Len++] = XHCI_DBG_CONT;
        l->Text[l->Len++] = carry;
    }
    l->Text[l->Len++] = c;
}

static VOID xhciDbgAppend(XHCI_DBG_LINE *l, const char *s)
{
    while (*s != '\0') {
        xhciDbgPut(l, *s);
        s++;
    }
}

/* Append `digits` hex digits of `value`, most significant first. */
static VOID xhciDbgAppendHex(XHCI_DBG_LINE *l, ULONG value, ULONG digits)
{
    ULONG shift;

    shift = digits * 4;
    while (shift > 0) {
        shift -= 4;
        xhciDbgPut(l, xhciDbgHexDigits[(value >> shift) & 0xF]);
    }
}

static VOID xhciDbgStart(XHCI_DBG_LINE *l)
{
    l->Len = 0;
    xhciDbgAppend(l, xhciDbgPrefixText);
}

/* After any put Len is at most XHCI_DBG_TEXT_MAX, so the newline and the
 * terminator fit. */
static VOID xhciDbgFinish(XHCI_DBG_LINE *l)
{
    l->Text[l->Len++] = '\n';
    l->Text[l->Len] = '\0';
    xhciDbgEmit(l->Text, l->Len);
}

static ULONG xhciDbgIrql(VOID)
{
#if defined(XHCI_DBG_NO_IRQL) || defined(XHCI_HOST_TEST)
    return 0xFF;
#else
    return (ULONG)KeGetCurrentIrql();
#endif
}

VOID XhciDbgText(const char *msg)
{
    XHCI_DBG_LINE line;

    xhciDbgStart(&line);
    xhciDbgAppend(&line, msg);
    xhciDbgFinish(&line);
}

VOID XhciDbgValue(const char *msg, ULONG value)
{
    XHCI_DBG_LINE line;

    xhciDbgStart(&line);
    xhciDbgAppend(&line, msg);
    xhciDbgAppend(&line, "=");
    xhciDbgAppendHex(&line, value, 8);
    xhciDbgFinish(&line);
}

VOID XhciDbgCallback(const char *name, ULONG a, ULONG b, ULONG c)
{
    XHCI_DBG_LINE line;

    xhciDbgStart(&line);
    xhciDbgAppend(&line, "cb ");
    xhciDbgAppend(&line, name);
    xhciDbgAppend(&line, " irql=");
    xhciDbgAppendHex(&line, xhciDbgIrql(), 2);
    xhciDbgAppend(&line, " a=");
    xhciDbgAppendHex(&line, a, 8);
    xhciDbgAppend(&line, " b=");
    xhciDbgAppendHex(&line, b, 8);
    xhciDbgAppend(&line, " c=");
    xhciDbgAppendHex(&line, c, 8);
    xhciDbgFinish(&line);
}

/*
 * Dump `count` ULONGs, four per line, each line labelled with the word index
 * so a structure can be reassembled from the log without counting columns -
 * which is also why each line goes out on its own under the line flag rather
 * than the whole dump under one hold: a line of another context may fall
 * between two of them, and the label still places each.
 * The caller is responsible for the pointer being readable - this is used for
 * usbport-owned structures whose size the ABI record pins.
 */
VOID XhciDbgWords(const char *msg, const ULONG *words, ULONG count)
{
    XHCI_DBG_LINE line;
    ULONG i;

    if (words == NULL) {
        XhciDbgText("(null word dump)");
        return;
    }

    i = 0;
    while (i < count) {
        ULONG n;

        xhciDbgStart(&line);
        xhciDbgAppend(&line, msg);
        xhciDbgAppend(&line, "+");
        xhciDbgAppendHex(&line, i * 4, 2);
        xhciDbgAppend(&line, ":");

        for (n = 0; n < 4 && i < count; n++) {
            xhciDbgAppend(&line, " ");
            xhciDbgAppendHex(&line, words[i], 8);
            i++;
        }

        xhciDbgFinish(&line);
    }
}

#else /* release build: keep the translation unit non-empty */

typedef int xhciDbgFreeBuildPlaceholder;

#endif /* XHCI_DBG_TRACE */
