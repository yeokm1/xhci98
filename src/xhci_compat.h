/*
 * xhci_compat.h - Win98/Win2000 DDK compatibility shims for xhci98.sys.
 *
 * Two jobs:
 *   1. Undo the Win2K DDK's ExAllocatePool -> ExAllocatePoolWithTag rewrite.
 *      **This is policy, not a missing export.** The reason recorded here used
 *      to be "ExAllocatePoolWithTag does not exist on Win98", and the repo
 *      measured the opposite, from Win98 SE's own usbd.sys
 *      (AGENTS.md; docs/usb-xhci-info/win98-wdm.md). The rule is the HCD's
 *      named-site pool policy (design record 13 section 7.5): the pair
 *      ExAllocatePoolWithTag + ExFreePool, in hcd_pool.obj alone. Undoing the
 *      rewrite makes a stray untagged ExAllocatePool resolve to the name the
 *      import gate refuses, rather than silently becoming a tagged call at an
 *      unnamed site. (The 1.2.0.0 miniport allocated no pool at all, under
 *      Option A; that was the rule's first ground.)
 *   2. Let the pure hardware-logic core compile on the build host under
 *      XHCI_HOST_TEST with no DDK at all (docs/contributing/design/03-host-unit-tests.md).
 *
 * C89 only.
 */

#ifndef XHCI_COMPAT_H
#define XHCI_COMPAT_H

#ifdef XHCI_HOST_TEST

/*
 * Host-test build: no ntddk.h. These must match the DDK's widths for whichever
 * architecture the host suite is being compiled for, which is what the layout
 * asserts in xhci.h check.
 */
typedef unsigned long   ULONG;
typedef unsigned short  USHORT;
typedef unsigned char   UCHAR;
typedef unsigned char   BOOLEAN;
/*
 * Pointer-sized, and on Windows that is not `unsigned long`. Windows is LLP64:
 * `long` stays 32 bits on amd64 while a pointer becomes 64, so the old
 * unconditional `unsigned long` was accidentally correct rather than correct -
 * it agreed with the DDK only because the host suite has always been built
 * x86. It is what `test/test_packet.c` needs in order to check the amd64
 * layout of the miniport ABI on the build host, and getting it wrong there
 * would make that test agree with a header that no driver build produces.
 */
#ifdef _WIN64
typedef unsigned __int64 ULONG_PTR;
#else
typedef unsigned long   ULONG_PTR;
#endif
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
 * this comment said otherwise until after Phase 13.)*
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

#else /* driver build */

#include <ntddk.h>

/*
 * The installed Win2K DDK defines POOL_TAGGING unconditionally and rewrites
 * ExAllocatePool(a,b) into ExAllocatePoolWithTag(a,b,tag) - ' mdW' via wdm.h,
 * ' kdD' via ntddk.h. Undo it here, after the DDK include and before any use,
 * and let the post-link import gate (Phase 3 task 5) confirm the binary really
 * imports the two-argument entry point.
 *
 * The HCD allocates pool through ExAllocatePoolWithTag at named sites only
 * (design record 13 section 7.5); this shim keeps an accidental untagged call
 * resolving to the name the import gate refuses.
 */
#ifdef ExAllocatePool
#undef ExAllocatePool
#endif

/*
 * WDK 7.1's ntddk.h, under the same POOL_TAGGING, rewrites ExFreePool(a) into
 * ExFreePoolWithTag(a, 0), which the amd64 build then imports. Undone for the
 * same reason as the rewrite above: one spelling, ExFreePool, on both
 * architectures, and the HCD's pool rule (design record 13 section 7.5) names
 * ExFreePool as its free entry point.
 */
#ifdef ExFreePool
#undef ExFreePool
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
