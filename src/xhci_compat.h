/*
 * xhci_compat.h - Win98/Win2000 DDK compatibility shims for xhci98.sys.
 *
 * Two jobs:
 *   1. Undo the Win2K DDK's ExAllocatePool -> ExAllocatePoolWithTag rewrite.
 *      **This is policy, not a missing export.** The reason recorded here used
 *      to be "ExAllocatePoolWithTag does not exist on Win98", and the repo
 *      measured the opposite, from Win98 SE's own usbd.sys
 *      (AGENTS.md; docs/usb-xhci-info/win98-wdm.md). The ban stands on another
 *      ground: Option A needs no private pool at all - usbport supplies every
 *      buffer, so a pool call site is a design regression before it is an
 *      import risk. The mechanism below is unchanged; only its justification
 *      was wrong, and a maintainer relaxing the gate should know they are
 *      overturning a policy rather than a fact.
 *   2. Let the pure hardware-logic core compile on the build host under
 *      XHCI_HOST_TEST with no DDK at all (docs/contributing/design/03-host-unit-tests.md).
 *
 * C89 only.
 */

#ifndef XHCI_COMPAT_H
#define XHCI_COMPAT_H

#ifdef XHCI_HOST_TEST

/*
 * Host-test build: no ntddk.h. These must match the DDK's x86 widths, which
 * is what the layout asserts in xhci.h check.
 */
typedef unsigned long   ULONG;
typedef unsigned short  USHORT;
typedef unsigned char   UCHAR;
typedef unsigned char   BOOLEAN;
typedef unsigned long   ULONG_PTR;
typedef long            LONG;
typedef void            VOID;

/*
 * Enough of the DDK's pointer types and calling-convention macro for
 * xhci_usbport.h - the miniport ABI declaration - to compile here. Widths and
 * conventions must match the DDK's x86 definitions exactly; the size and
 * offset asserts in that header are what proves they do.
 */
typedef void *          PVOID;
typedef char *          PCHAR;
typedef unsigned char * PUCHAR;
typedef unsigned short *PUSHORT;
typedef unsigned long * PULONG;

/*
 * The wide character. The DDK's WCHAR is `wchar_t`, which MSVC 6.0 makes an
 * unsigned short, so this matches its width and its signedness.
 *
 * **What still needs it is the registry value NAME**, not a value:
 * `UsbPortGetMiniportRegistryKeyValue` takes a wide name, so
 * `XHCI_LOG_VERBOSITY_VALUE_NAME` and `XHCI_LOG_DBGVIEW_VALUE_NAME` are `L""`
 * literals with their byte lengths beside them, and `test/test_init.c`'s
 * stand-in service checks both the names and those lengths - which is what
 * decides which key a value comes out of.
 *
 * *(This block was written for task 11-V.9's `XhciLogFile`, a REG_SZ value with
 * path validation in src/xhci_log.c. Task 13-L.2 retired that value and its
 * validation; the typedef stays because the value names do, and
 * this comment said otherwise until the post-Phase 13 review rounds.)*
 */
typedef unsigned short  WCHAR;
typedef WCHAR *         PWSTR;
typedef const WCHAR *   PCWSTR;

/*
 * The DDK's x86 spin-lock types. KSPIN_LOCK is ULONG_PTR wide there, which is
 * what XHCI_EXTENSION's layout depends on; KIRQL is a UCHAR.
 */
typedef ULONG_PTR       KSPIN_LOCK;
typedef KSPIN_LOCK *    PKSPIN_LOCK;
typedef UCHAR           KIRQL;
typedef KIRQL *         PKIRQL;

#define NTAPI __stdcall

#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif

/*
 * The three DDK primitives src/xhci_pci.c is built out of, redirected to hooks
 * a test provides. This is a deliberate widening of what XHCI_HOST_TEST covers:
 * the rest of this header exists so the *pure* core needs no DDK, and these
 * three exist so the MMIO layer and the init sequence written against it can be
 * driven against a synthetic controller (test/test_init.c).
 *
 * The distinction that keeps the split meaningful is what such a suite can
 * prove. It sees the sequence of accesses this driver's source performs, so it
 * can check ordering and that a refusal wrote nothing at all - neither of which
 * a suite over finished bytes can see. It cannot say anything about what the
 * bus does with those accesses, so the QEMU register trace stays owed
 * (docs/contributing/design/03-host-unit-tests.md).
 */
ULONG XhciHostReadRegister(PULONG address);
VOID XhciHostWriteRegister(PULONG address, ULONG value);
VOID XhciHostStall(ULONG microseconds);

#define READ_REGISTER_ULONG(a)          XhciHostReadRegister(a)
#define WRITE_REGISTER_ULONG(a, v)      XhciHostWriteRegister((a), (v))
#define KeStallExecutionProcessor(us)   XhciHostStall(us)

/*
 * The miniport's own interior lock (Phase 4 task 7, src/xhci_cmd.c), redirected
 * for the same reason the three above are: the property worth testing is not
 * what the lock *does* on a single-threaded host - nothing - but *where* it is
 * taken. Three unsynchronized contexts reach the command engine (a submit under
 * usbport's MiniportSpinLock, the DPC under MiniportInterruptsSpinLock, and an
 * async timer callback under neither), so "the claim happened inside the lock",
 * "no usbport service is called while holding it", and "every acquire has a
 * release" are the checks, and all three need a hook to be visible at all.
 */
VOID XhciHostInitSpinLock(PKSPIN_LOCK lock);
VOID XhciHostAcquireSpinLock(PKSPIN_LOCK lock, PKIRQL oldIrql);
VOID XhciHostReleaseSpinLock(PKSPIN_LOCK lock, KIRQL oldIrql);

#define KeInitializeSpinLock(l)         XhciHostInitSpinLock(l)
#define KeAcquireSpinLock(l, i)         XhciHostAcquireSpinLock((l), (i))
#define KeReleaseSpinLock(l, i)         XhciHostReleaseSpinLock((l), (i))

/*
 * A single-threaded host cannot make this atomic mean anything, so the shim is
 * the plain increment the DDK primitive performs. It exists only so the file
 * that needs the real one compiles here.
 */
LONG XhciHostInterlockedIncrement(LONG *addend);

#define InterlockedIncrement(a)         XhciHostInterlockedIncrement(a)

#else /* driver build */

#include <ntddk.h>

/*
 * The installed Win2K DDK defines POOL_TAGGING unconditionally and rewrites
 * ExAllocatePool(a,b) into ExAllocatePoolWithTag(a,b,tag) - ' mdW' via wdm.h,
 * ' kdD' via ntddk.h. Undo it here, after the DDK include and before any use,
 * and let the post-link import gate (Phase 3 task 5) confirm the binary really
 * imports the two-argument entry point.
 *
 * Under Option A the miniport should not need pool at all: usbport.sys owns
 * the miniport extension, the endpoint extensions, and the common buffer.
 * This shim exists so that a later accidental use fails loudly on the host
 * rather than silently at load time on the guest.
 */
#ifdef ExAllocatePool
#undef ExAllocatePool
#endif

#endif /* XHCI_HOST_TEST */

/*
 * C89 compile-time assertion (docs/usb-xhci-info/win98-wdm.md, "MSVC 6.0 / C89
 * Language Pitfalls", the compile-time size checks bullet). A false condition
 * declares an array of negative size, which
 * MSVC 6.0 rejects with "C2118: negative subscript or subscript is too
 * large". The diagnostic carries the file and line but *not* the assert name,
 * so name each one after the property it states and keep it on its own line.
 */
#define XHCI_C_ASSERT(name, cond) \
    typedef char xhci_assert_##name[(cond) ? 1 : -1]

/*
 * The byte offset of a member within a structure (task 13-L.2).
 *
 * Spelled here rather than taken from the DDK's `FIELD_OFFSET` or from
 * `<stddef.h>`'s `offsetof`, because this header's whole job is to give the two
 * builds the same vocabulary: `ntddk.h` has the first and the host suite has
 * neither, and a driver header that compiled only one way would be exactly the
 * split this file exists to prevent.
 *
 * Its one use is the snapshot header's `RingOffset`, which travels on the wire
 * so that `XHCISNAP` can print the note ring out of a raw extension dump with
 * no offset table. Deriving it from the struct is what keeps it from becoming
 * a second copy of the layout that drifts.
 */
#define XHCI_FIELD_OFFSET(type, member) \
    ((ULONG)(ULONG_PTR)&(((type *)0)->member))

#endif /* XHCI_COMPAT_H */
