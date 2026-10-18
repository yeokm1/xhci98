// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2026, Yeo Kheng Meng
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// cmlinknt.c - the kernel half of the NT receiver of the CM link-state table
// (include/stor98cm.h, interface 2; MD-6, PLAN 0.10 evidence 2). The
// decisions are in cmlink.h; this file opens the callback object, asks it
// once for the table, and writes the acknowledgement.
//
// Rendezvous (stor98cm.h, "Rendezvous (NT)"): in the first HwFindAdapter of
// a device extension (PASSIVE_LEVEL), and never in a dump or hibernation
// instance, \Callback\Stor98LinkState is opened without being created
// (ExCreateCallback, Create FALSE), ExNotifyCallback is called once with
// STOR98LS_QUERY, and the object is dereferenced again. The table is a
// never-freed non-paged CM allocation, so the pointer is kept for the life
// of the device extension. A resume (HwFindAdapter on the same extension,
// G7) does not call the callback again: it re-binds to the table it already
// has, with a snapshot only (no object access, whatever the IRQL of the
// port's resume path).
//
// Lifetimes:
//   - No CM (no object, or xhci98 not installed): no channel; the engine's
//     MD-1 gone deadline applies, exactly as before this receiver.
//   - The CM unloads: it unregisters its callback and leaves the table
//     allocated (stor98cm.h, "CM lifetime (NT)"). An instance already bound
//     keeps polling a table that no longer changes (no statement: MD-1);
//     a later find gets no answer (CML_RC_NO_ANSWER).
//   - The CM reloads in the same boot: it finds the object and publishes
//     nothing (no second table); a later find again gets no answer. A CM
//     that answered with a second table anyway is refused
//     (CML_RC_SECOND_TABLE): the first table this image accepted stays the
//     only one for its life.
//   - A table of another interface version is refused (CML_RC_VERSION).
//
// The ntoskrnl imports (ExCreateCallback, ExNotifyCallback,
// ObfDereferenceObject, MmIsAddressValid and the data export
// MmSystemRangeStart) are MD-6's accepted step outside the SCSIport
// miniport contract; they are declared here because a miniport build has
// no ntddk.h. NT builds only.
//

#include "nvme2k.h"
#include "utils.h"
#include "dpkeys.h"

#if !defined(NVME2K_W9X)

typedef struct _S98_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} S98_UNICODE_STRING;

typedef struct _S98_OBJECT_ATTRIBUTES {
    ULONG Length;
    PVOID RootDirectory;
    S98_UNICODE_STRING *ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} S98_OBJECT_ATTRIBUTES;

#define S98_OBJ_CASE_INSENSITIVE    0x00000040L

__declspec(dllimport) LONG __stdcall ExCreateCallback(PVOID *CallbackObject, S98_OBJECT_ATTRIBUTES *ObjectAttributes,
                                                      BOOLEAN Create, BOOLEAN AllowMultipleCallbacks);
__declspec(dllimport) VOID __stdcall ExNotifyCallback(PVOID CallbackObject, PVOID Argument1, PVOID Argument2);
__declspec(dllimport) LONG __fastcall ObfDereferenceObject(PVOID Object);
__declspec(dllimport) BOOLEAN __stdcall MmIsAddressValid(PVOID VirtualAddress);
__declspec(dllimport) extern PVOID MmSystemRangeStart;     // MM_SYSTEM_RANGE_START

// The image is a crash dump or hibernation instance (DriverEntry(NULL, NULL),
// G6): the table is never used there.
static BOOLEAN CmlDumpImage;

// One table per boot (cmlink.h): the first table this image accepted.
static PSTOR98LS_TABLE CmlBoot;

VOID PlatCmLinkDriverEntry(IN PVOID DriverObject, IN PVOID Argument2)
{
    CmlDumpImage = (BOOLEAN)(DriverObject == NULL && Argument2 == NULL);
}

// "dump=1" in the ArgumentString (2000, XP, 7: the dump and hibernation
// stacks pass it; G6).
static BOOLEAN CmlDumpArgument(IN PCHAR ArgumentString)
{
    static const char key[] = DPK_DUMP;
    ULONG i, k;

    if (ArgumentString == NULL) {
        return FALSE;
    }
    for (i = 0; ArgumentString[i] != 0; i++) {
        for (k = 0; key[k] != 0; k++) {
            UCHAR c = (UCHAR)ArgumentString[i + k];

            if (c >= 'A' && c <= 'Z') {
                c = (UCHAR)(c + ('a' - 'A'));
            }
            if (c != (UCHAR)key[k]) {
                break;
            }
        }
        if (key[k] == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

// Asks \Callback\Stor98LinkState for the table: CML_RC_BOUND with *Table
// set, or why not. PASSIVE_LEVEL (the first HwFindAdapter).
static ULONG CmlQuery(OUT PSTOR98LS_TABLE *Table)
{
    static WCHAR name[] = STOR98LS_CALLBACK_NAME;
    S98_UNICODE_STRING text;
    S98_OBJECT_ATTRIBUTES attributes;
    PVOID object = NULL;
    PSTOR98LS_TABLE t = NULL;
    LONG status;

    *Table = NULL;
    text.Buffer = name;
    text.Length = (USHORT)(sizeof(name) - sizeof(WCHAR));
    text.MaximumLength = (USHORT)sizeof(name);
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = NULL;
    attributes.ObjectName = &text;
    attributes.Attributes = S98_OBJ_CASE_INSENSITIVE;
    attributes.SecurityDescriptor = NULL;
    attributes.SecurityQualityOfService = NULL;
    // Opened, never created: stor98 does not own the object (stor98cm.h).
    status = ExCreateCallback(&object, &attributes, FALSE, TRUE);
    if (status < 0 || object == NULL) {
        return CML_RC_NO_OBJECT;
    }
    ExNotifyCallback(object, STOR98LS_QUERY, (PVOID)&t);
    ObfDereferenceObject(object);
    if (t == NULL) {
        return CML_RC_NO_ANSWER;
    }
    // A pointer from a trusted kernel component (stor98cm.h R5's trust
    // boundary): checked for alignment, for lying wholly in system space
    // (the table is non-paged CM memory, never a user address), and for
    // residency of both ends; not authenticated.
    if (((ULONG_PTR)t & 3) != 0 || (ULONG_PTR)t < (ULONG_PTR)MmSystemRangeStart ||
        (ULONG_PTR)t > ~(ULONG_PTR)0 - (sizeof(STOR98LS_TABLE) - 1) || !MmIsAddressValid((PVOID)t) ||
        !MmIsAddressValid((PVOID)((PUCHAR)t + sizeof(STOR98LS_TABLE) - 1))) {
        return CML_RC_BAD_POINTER;
    }
    *Table = t;
    return CML_RC_BOUND;
}

//
// HwFindAdapter, after the DMA memory (nvme2k.c): the table and this
// function's entry. Any failure means no channel for this find (MD-1).
//
VOID PlatCmLinkOpen(IN PHW_DEVICE_EXTENSION DevExt, IN PCHAR ArgumentString)
{
    CML_STATE *st = &DevExt->CmLink;
    PSTOR98LS_TABLE t = NULL;
    ULONG rc;

    if (!DevExt->Resumed) {
        RtlZeroMemory(st, sizeof(*st));
        st->Rc = CML_RC_NONE;
    }
    st->Entry = NULL;
    st->LinkGen = 0;
    st->Applied = 0;
    st->Lost = 0;
    if (CmlDumpImage || CmlDumpArgument(ArgumentString)) {
        st->Table = NULL;
        rc = CML_RC_DUMP;
    } else if (st->Opened) {
        // A resume: the table this extension already has, or none.
        t = st->Table;
        rc = t != NULL ? CML_RC_BOUND : (ULONG)st->Rc;
    } else {
        st->Opened = TRUE;
        rc = CmlQuery(&t);
        if (rc == CML_RC_BOUND) {
            rc = CmlCheckTable(t);
        }
        if (rc == CML_RC_BOUND) {
            rc = CmlBootTable(&CmlBoot, t);
        }
        st->Table = rc == CML_RC_BOUND ? t : NULL;
    }
    if (rc == CML_RC_BOUND) {
        rc = CmlBind(st, DevExt->BusNumber, DevExt->SlotNumber);
    }
    if (rc != CML_RC_BOUND) {
        st->Entry = NULL;
    }
    st->Rc = (UCHAR)rc;
    DBGSTAGE(STG_CML_OPEN, rc | ((ULONG)DevExt->Resumed << 7) |
             ((st->Entry != NULL ? (ULONG)(st->Entry - st->Table->Entry) : 0xFF) << 8) |
             ((st->Gen & 0xFF) << 16) | ((DevExt->BusNumber & 0xFF) << 24));
}

//
// From the engine's timer, HwStartIo and gone state (lifecycle.c,
// LcLinkStatement): a DOWN_QUIESCED statement for the bound generation or
// a newer one, reported until PlatCmLinkAcknowledge.
//
ULONG PlatCmLinkPoll(IN PHW_DEVICE_EXTENSION DevExt)
{
    CML_STATE *st = &DevExt->CmLink;
    UCHAR lost = st->Lost;
    BOOLEAN reported = (BOOLEAN)(st->LinkGen != 0);
    ULONG r = CmlPoll(st);

    if (r == PLAT_LINK_DOWN_QUIESCED && !reported) {
        DBGMARK(STG_CML_SEEN, (st->LinkGen & 0xFF) | ((st->Gen & 0xFF) << 8) | ((ULONG)DevExt->State << 16) |
                ((DevExt->BusNumber & 0xFF) << 24));
    }
    if (st->Lost != lost) {
        DBGSTAGE(STG_CML_LOST, st->Lost | ((ULONG)DevExt->State << 8));
    }
    UNREFERENCED_PARAMETER(lost);
    UNREFERENCED_PARAMETER(reported);
    return r;
}

//
// The engine applied the statement (held requests completed, children and
// blocks settled with isolation evidence, BusChangeDetected follows):
// AckGeneration last, one interlocked store (R6d), and only for the bound
// generation. On NT the blocks stay ISOLATED in the port's uncached
// extension (MD-3 (a)); the port frees them at the removal the CM starts
// after this acknowledgement.
//
VOID PlatCmLinkAcknowledge(IN PHW_DEVICE_EXTENSION DevExt)
{
    CML_STATE *st = &DevExt->CmLink;
    PSTOR98LS_ENTRY e = st->Entry;
    ULONG gen = CmlAcknowledged(st);

    if (gen != 0 && e != NULL) {
        AtomicSet(&e->AckGeneration, gen);
    }
    DBGMARK(STG_CML_ACK, (st->Applied & 0xFF) | ((ULONG)(gen != 0) << 16) | ((DevExt->BusNumber & 0xFF) << 24));
}

// While a statement can still come, a timer callback at the watchdog
// period in every started state, idle or not (as interface 3 R10 asks of the
// 9x receiver), so an idle adapter acknowledges within about one tick and
// the CM's wait (5 s, xhci98) ends early.
ULONG PlatCmLinkPollUs(IN PHW_DEVICE_EXTENSION DevExt)
{
    return CmlWantsPoll(&DevExt->CmLink) ? CML_POLL_US : 0;
}

#else

// Windows 9x: no NT receiver (pool9x.c has the interface 3 one).
typedef int stor98_cmlinknt_empty;

#endif
