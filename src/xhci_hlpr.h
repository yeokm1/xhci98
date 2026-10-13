/*
 * xhci_hlpr.h - the channel between xhci98.sys and the x86 helper,
 * xhcihlpr.sys (design record 18 sections 3 and 15.3; roadmap-hcd.md task
 * 38.3).
 *
 * The helper hands the HCD's x86 image the addresses of kernel routines it
 * cannot import, because the same binary loads on Windows 98: on Vista and
 * 7 the IoConnectInterruptEx pair (the MSI service), on Windows 2000 and
 * later ExSetTimerResolution (the timer service). The HCD opens the
 * helper's named device with IoGetDeviceObjectPointer and sends one
 * internal device control, METHOD_NEITHER, whose Type3InputBuffer is the
 * block below, in and out. This header is the one declaration both
 * binaries compile.
 *
 * Version 2 (record 18 section 15.3) replaced xhcimsi.sys's version 1 on a
 * new device name: no 2.3.x HCD opens the new device and no 2.4 HCD opens
 * the old one, so an old and a new helper loaded in one boot never collide
 * and neither end speaks the other's version.
 *
 * The pure halves of both ends live in xhci_hlpr.c: the helper's
 * validation and fill (XhciHlprAnswer) and the HCD's two-level acceptance
 * (XhciHlprAccept), host-tested in test\test_msi.c.
 *
 * Built three ways: into xhci98.sys (MSVC 6 and the Windows 2000 DDK, or
 * WDK 7.1 for amd64, where nothing calls it), into xhcihlpr.sys (WDK 7.1
 * x86, XHCI_HLPR_HELPER defined, ntddk.h included first), and into the
 * host suite (XHCI_HOST_TEST). The block's layout is x86's; the size and
 * offset asserts below hold for x86 only and are compiled there.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_HLPR_H
#define XHCI_HLPR_H

#ifdef XHCI_HLPR_HELPER
#include <ntddk.h>
#define XHCI_HLPR_ASSERT(name, cond) \
    typedef char xhci_hlpr_assert_##name[(cond) ? 1 : -1]
#else
#include "xhci_compat.h"
#define XHCI_HLPR_ASSERT(name, cond) XHCI_C_ASSERT(name, cond)
#endif

/* \Device\Xhci98Helper (owner, 2026-10-13, decision H1); the helper's
 * class GUID for IoCreateDeviceSecure is in hlpr\hlpr_entry.c. */
#define XHCI_HLPR_DEVICE_NAME       L"\\Device\\Xhci98Helper"

/* 'XHLP'. */
#define XHCI_HLPR_MAGIC             0x504C4858UL
#define XHCI_HLPR_MAJOR             2
#define XHCI_HLPR_MINOR             0

/* The whole V2.0 block, and the least any later minor may send. */
#define XHCI_HLPR_V2_MIN            0x34UL

/* CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900 + major, METHOD_NEITHER,
 * FILE_ANY_ACCESS): (0x22 << 16) | (0 << 14) | (0x902 << 2) | 3. A new
 * major takes a new function number, so an older helper refuses it. */
#define XHCI_HLPR_IOCTL_QUERY_V2    0x0022240BUL

/* CallerWants and Services. */
#define XHCI_HLPR_SVC_MSI           0x00000001UL    /* the MSI pair        */
#define XHCI_HLPR_SVC_TIMER         0x00000002UL    /* the timer routine   */

/* Flags the helper sets. */
#define XHCI_HLPR_F_INJECTED        0x00000001UL    /* qemu injection on   */

typedef struct _XHCI_HLPR_QUERY {
    ULONG Size;             /* 0x00 caller: the block size it declares     */
    USHORT CallerMajor;     /* 0x04 caller                                 */
    USHORT CallerMinor;     /* 0x06 caller                                 */
    ULONG CallerVersion;    /* 0x08 caller: XHCI_VER packed a.b.c.d        */
    ULONG CallerWants;      /* 0x0C caller: XHCI_HLPR_SVC_*, the record's  */
    ULONG Magic;            /* 0x10 helper: XHCI_HLPR_MAGIC                */
    USHORT HelperMajor;     /* 0x14 helper                                 */
    USHORT HelperMinor;     /* 0x16 helper                                 */
    ULONG HelperVersion;    /* 0x18 helper: XHCI_VER packed                */
    ULONG Returned;         /* 0x1C helper: bytes it filled, from 0        */
    ULONG Services;         /* 0x20 helper: XHCI_HLPR_SVC_* it answers     */
    ULONG Flags;            /* 0x24 helper: XHCI_HLPR_F_*                  */
    PVOID ConnectEx;        /* 0x28 helper: IoConnectInterruptEx           */
    PVOID DisconnectEx;     /* 0x2C helper: IoDisconnectInterruptEx        */
    PVOID SetTimerResolution; /* 0x30 helper: ExSetTimerResolution         */
} XHCI_HLPR_QUERY, *PXHCI_HLPR_QUERY;

/* The helper writes from here on, never the caller's own fields. */
#define XHCI_HLPR_HELPER_FIELDS     0x10UL

#if !defined(_WIN64)
XHCI_HLPR_ASSERT(hlpr_query_size,
                 sizeof(XHCI_HLPR_QUERY) == XHCI_HLPR_V2_MIN);
XHCI_HLPR_ASSERT(hlpr_query_wants,
                 (ULONG)(ULONG_PTR)&((XHCI_HLPR_QUERY *)0)->CallerWants ==
                     0x0C);
XHCI_HLPR_ASSERT(hlpr_query_magic,
                 (ULONG)(ULONG_PTR)&((XHCI_HLPR_QUERY *)0)->Magic ==
                     XHCI_HLPR_HELPER_FIELDS);
XHCI_HLPR_ASSERT(hlpr_query_returned,
                 (ULONG)(ULONG_PTR)&((XHCI_HLPR_QUERY *)0)->Returned ==
                     0x1C);
XHCI_HLPR_ASSERT(hlpr_query_services,
                 (ULONG)(ULONG_PTR)&((XHCI_HLPR_QUERY *)0)->Services ==
                     0x20);
XHCI_HLPR_ASSERT(hlpr_query_flags,
                 (ULONG)(ULONG_PTR)&((XHCI_HLPR_QUERY *)0)->Flags == 0x24);
XHCI_HLPR_ASSERT(hlpr_query_connect,
                 (ULONG)(ULONG_PTR)&((XHCI_HLPR_QUERY *)0)->ConnectEx ==
                     0x28);
XHCI_HLPR_ASSERT(hlpr_query_disconnect,
                 (ULONG)(ULONG_PTR)&((XHCI_HLPR_QUERY *)0)->DisconnectEx ==
                     0x2C);
XHCI_HLPR_ASSERT(hlpr_query_timer,
                 (ULONG)(ULONG_PTR)&((XHCI_HLPR_QUERY *)0)
                         ->SetTimerResolution == 0x30);
#endif

/* The helper's answers, which it maps to an NTSTATUS. */
#define XHCI_HLPR_ANS_OK            0UL     /* STATUS_SUCCESS              */
#define XHCI_HLPR_ANS_TOO_SMALL     1UL     /* STATUS_BUFFER_TOO_SMALL     */
#define XHCI_HLPR_ANS_INVALID       2UL     /* STATUS_INVALID_PARAMETER    */
#define XHCI_HLPR_ANS_REVISION      3UL     /* STATUS_REVISION_MISMATCH    */

/* The qemu flavour's injections (record 18 sections 2.1 and 15.12, N3)
 * that change the answer; 1 (DriverEntry fails) and 2 (the query refused)
 * are the helper's own. 3 answers major 3, the next major, which a V2
 * caller refuses; 4 fills 0x30 bytes, leaving the timer field uncovered. */
#define XHCI_HLPR_INJ_NONE          0UL
#define XHCI_HLPR_INJ_MAJOR         3UL
#define XHCI_HLPR_INJ_SHORT         4UL
#define XHCI_HLPR_INJ_NULL_DISC     5UL     /* MSI bit set, DisconnectEx 0 */
#define XHCI_HLPR_INJ_MAGIC         6UL
#define XHCI_HLPR_INJ_TIMER_OFF     7UL     /* timer bit clear, pointer 0  */
#define XHCI_HLPR_INJ_TIMER_NULL    8UL     /* timer bit set, pointer 0    */
#define XHCI_HLPR_INJ_MSI_OFF       9UL     /* MSI bit clear, both 0       */
#define XHCI_HLPR_INJ_LAST          9UL

/*
 * The helper's half (record 18 sections 3.2 and 15.3): validates the
 * request before it reads any byte of the block - the buffer non-NULL, the
 * two lengths equal and at least V2_MIN - then the block's Size against the
 * length and the caller's major; fills its own fields, from 0x10, for
 * min(Size, its block) bytes and returns that count through *information
 * (0 on a refusal). A service is answered when its routines were found at
 * DriverEntry: the MSI pair when both are non-NULL, the timer when its
 * pointer is. `inject` is an XHCI_HLPR_INJ_* value, XHCI_HLPR_INJ_NONE
 * outside the qemu flavour. Returns an XHCI_HLPR_ANS_*. IRQL: any.
 */
ULONG XhciHlprAnswer(PVOID buffer, ULONG inputLength, ULONG outputLength,
                     PVOID connectEx, PVOID disconnectEx, PVOID setTimer,
                     ULONG helperVersion, ULONG inject, PULONG information);

/* The HCD's channel verdict (record 18 section 15.3): accepted, or the
 * first test that failed - the answer then refused as a whole. */
#define XHCI_HLPR_ACCEPTED          0UL
#define XHCI_HLPR_REFUSE_STATUS     1UL     /* the IRP failed              */
#define XHCI_HLPR_REFUSE_LENGTH     2UL     /* Returned/Information/capacity */
#define XHCI_HLPR_REFUSE_IDENTITY   3UL     /* Magic or HelperMajor        */

/* One verdict per service. NONE: the channel was refused or never asked,
 * nothing judged; NOT_ASKED: the MSI service below NT 6, whatever the
 * answer says. */
#define XHCI_HLPR_SVC_NONE          0UL
#define XHCI_HLPR_SVC_ACCEPTED      1UL
#define XHCI_HLPR_SVC_NOT_OFFERED   2UL
#define XHCI_HLPR_SVC_MALFORMED     3UL
#define XHCI_HLPR_SVC_NOT_ASKED     4UL

/*
 * The HCD's half: `succeeded` is NT_SUCCESS of the IRP's status,
 * `information` its IoStatus.Information, `capacity` the Size the HCD sent,
 * `nt6` the HCD's own IoIsWdmVersionAvailable(6, 0). Returns the channel's
 * XHCI_HLPR_ACCEPTED or REFUSE_*, and through the two pointers one
 * XHCI_HLPR_SVC_* each (NONE on a refusal). Set Services bits above bit 1
 * are ignored. IRQL: any.
 */
ULONG XhciHlprAccept(ULONG succeeded, ULONG information, ULONG capacity,
                     const XHCI_HLPR_QUERY *query, ULONG nt6,
                     PULONG msiVerdict, PULONG timerVerdict);

/* "a.b.c.d" packed one byte a part (each part held to 255), for the two
 * ends' version fields. IRQL: any. */
ULONG XhciHlprPackVersion(const char *text);

#endif /* XHCI_HLPR_H */
