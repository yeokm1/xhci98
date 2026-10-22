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
//
// stor98cm.h: the interface between xhci98's connection manager (CM) and
// stor98 for the PCIe link state of a tunnelled NVMe function.
//
// LICENSING OF THIS FILE. This header is an interface definition, licensed
// on its own under the BSD-3-Clause licence above, independently of the code
// that implements either side of it. stor98 (BSD-3-Clause) implements the
// receiving side; xhci98 (GPL-2.0) implements the sending side in its CM and
// may include this file as is. Including it in a GPL-2.0 work does not change
// the licence of this file, and keep it that way: this header must stay a
// permissive, separately licensed definition. Do not copy GPL-licensed code,
// structure definitions or text into it, and do not move stor98 or xhci98
// implementation code into it. Changes to the interface are made here, in
// the stor98 repository, and copied unchanged into xhci98.
//
// Status (stor98 docs/PLAN.md, maintainer decision MD-6, 2026-10-15): the
// link-state table ("interface 2" below) is the adopted NT interface. It
// passed gate G3 qualification on Windows 2000 and XP x64 in the prototype
// (docs/gates/G3.md; prototype branch phase0-g3g6, commit 4e1e55d). The
// IOCTL_SCSI_MINIPORT route ("interface 1") is rejected as the carrier of
// the link-down statement: under a saturated queue it is delivered only
// after the gone deadline. It is kept here as a diagnostic query only.
// Windows 9x (98 SE, Me; maintainer decisions MD-5, D1, D2, 2026-10-15)
// uses "interface 3" below: the interface 2 table found through a registry
// locator with a current-boot proof, plus the MD-5 removal transaction. The
// first Q-9x prototype of interface 3 was found not ready by a Codex review
// and is withdrawn (nothing produces or reads it). Status of this revision:
// revised after that review, with xhci98's locator and REMOVE_PENDING layout
// (maintainer, 2026-10-15), and requalified (docs/gates/Q-9x.md, "Interface
// 3 revision"; branch qual-9x); not adopted until the maintainer records it.
//
// Interface 3 is a Windows 9x, 32-bit x86 ABI only. NT systems (2000, XP,
// 7, 32- and 64-bit) use interface 2 with its native-width pointer transport.
//
// Names and versions (the stor98 rename, maintainer, 2026-10-16). This
// header was include/nvme98cm.h while the driver was named nvme98. The
// rename gave every interface new names and new signatures and set every
// version field to 4: the callback object \Callback\Stor98LinkState, the
// locator key Services\stor98, the proof key HKEY_DYN_DATA\Stor98LinkState,
// the table and locator signature 'S98L' (was 'N98L'), the IOCTL signature
// "STOR98CM" (was "NVME98CM"), and STOR98LS_VERSION,
// STOR98LS9X_LOCATOR_VERSION and STOR98CM_VERSION 4 (all were 1). The value
// 4 is above every number the nvme98 interface used, including its
// "interface 3" (the 9x variant's name, not a version field), so no number
// built against nvme98cm.h can be read as this one. Layouts, sizes,
// offsets, flags and rules are unchanged from version 1. There is no
// compatibility with the old names in either direction: stor98 neither
// reads nor writes \Callback\Nvme98LinkState, Services\nvme98 "LinkState"
// or HKEY_DYN_DATA\Nvme98LinkState, and a receiver of the old names
// never finds a stor98 CM. A CM built against nvme98cm.h with a stor98
// receiver, or the reverse, is the "no channel" case of R2 (MD-1 applies;
// the CM never reports a clean eject).
//
// The types used (ULONG, PVOID, ULONG_PTR) are the Windows DDK base types;
// include the kit's headers before this one.
//

#ifndef _STOR98CM_H_
#define _STOR98CM_H_

//
// The statement both interfaces carry (stor98 PLAN 0.10, evidence 2;
// MD-2): "the PCIe link to function (bus, slot) went down, the tunnel's
// PCIe path is closed and the endpoint reset has completed". The CM may
// make it only after completed quiescence, never as the teardown starts.
//

//
// Interface 2 (adopted, MD-6): link-state table.
//
// Owner and lifetime. The CM allocates one STOR98LS_TABLE in non-paged
// memory and never frees it, not even at its own unload, so a pointer taken
// by stor98 never dangles. stor98 registers nothing with the CM: on NT a
// miniport gets no callback before its image is unloaded after a surprise
// removal.
//
// CM lifetime (NT). The CM that created the callback object stays loaded for
// the rest of the boot: it has no DriverUnload routine (or one that is never
// reached), so its registered callback routine and the code that updates the
// table stay resident, and it creates exactly one table per boot. OBJ_PERMANENT
// keeps only the object, not the callback routine. A CM that does unload
// anyway is outside this contract; the only defined rundown is:
// ExUnregisterCallback on its registration before its image goes away (the
// object then answers no query, so a receiver that loads later finds no
// table: "no CM", MD-1 applies), the table left allocated and never freed
// (receivers that already hold the pointer keep polling it; it stops
// changing, so they get no further statement), and no reloaded CM may
// publish a second table or resume writing the old one in that boot.
// stor98 calls the callback only once per HwFindAdapter and never after.
// This is a clarification; interface 2's behaviour is unchanged.
//
// Rendezvous (NT). The CM creates the callback object
// \Callback\Stor98LinkState (ExCreateCallback, OBJ_PERMANENT) and registers
// one callback that, for Argument1 == STOR98LS_QUERY, stores the table
// address in *(PSTOR98LS_TABLE *)Argument2. stor98 opens the object without
// creating it, once, in HwFindAdapter (PASSIVE_LEVEL), calls
// ExNotifyCallback once, keeps the pointer and polls its entry from its own
// timer and HwStartIo. A missing object means no CM on this system, so no
// link-down statement (the gone deadline, MD-1, applies). stor98 does not
// use the table in dump mode.
//
// Entry protocol. The CM owns every field except AckGeneration (and, on 9x
// only, RemoveAck).
//  1. Tunnel established: Sequence odd; LinkGeneration++;
//     Flags = IN_USE | UP; Sequence even. Done before the OS can enumerate
//     the endpoint (before IoInvalidateDeviceRelations on the bridge). Only
//     a new tunnel increments LinkGeneration: UP published again for a
//     tunnel that stayed up keeps its generation (R8b).
//  2. Link down, only after the tunnel's PCIe path is closed and the
//     endpoint reset has completed: Sequence odd;
//     Flags = IN_USE | DOWN_QUIESCED; Sequence even.
//  3. Then the CM waits for AckGeneration == LinkGeneration, for at most
//     the gone deadline, and only then starts the OS removal path.
//  4. The CM never re-establishes a tunnel to an endpoint that was not
//     reset since the last tunnel (MD-2).
// stor98 records LinkGeneration at HwFindAdapter (the generation it is
// bound to). It applies DOWN_QUIESCED for that generation or a newer one
// (held requests completed, held DMA memory released, gone deadline
// cancelled, BusChangeDetected), then writes AckGeneration = LinkGeneration
// only when LinkGeneration is the bound generation. A newer generation
// belongs to a newer instance (R8c): the bound instance applies a
// DOWN_QUIESCED for it but never acknowledges it, so the CM's wait in step
// 3 runs to its end. On NT, UP with a newer generation changes nothing (the
// instance keeps polling); on 9x it is evidence only once the instance has
// lost presence, and while its controller still answers the instance stops
// acting on the entry (R8d). DOWN without QUIESCED, or an older generation,
// is ignored. An entry read with an odd Sequence, or whose Sequence changed
// during the read, is read again later.
//
#define STOR98LS_CALLBACK_NAME      L"\\Callback\\Stor98LinkState"
#define STOR98LS_QUERY              ((PVOID)(ULONG_PTR)0x4C383953)  // 'S98L'
#define STOR98LS_SIGNATURE          0x4C383953
#define STOR98LS_VERSION            4
#define STOR98LS_ENTRIES            8

#define STOR98LS_IN_USE             0x00000001
#define STOR98LS_UP                 0x00000002
#define STOR98LS_DOWN_QUIESCED      0x00000004

// Layout shared by interface 2 (NT) and interface 3 (9x). On NT the two
// removal fields and Cookie are zero and unused (they were Reserved); on 9x
// they carry the interface 3 removal transaction and the per-boot cookie.
typedef struct _STOR98LS_ENTRY {                // 32 bytes
    volatile ULONG Sequence;        // 0x00 CM: odd while the CM is updating the entry
    volatile ULONG BusNumber;       // 0x04 CM: PCI bus of the NVMe function (the tunnel bridge's secondary bus)
    volatile ULONG SlotNumber;      // 0x08 CM: PCI_SLOT_NUMBER.u.AsULONG (device 4:0, function 7:5)
    volatile ULONG LinkGeneration;  // 0x0C CM: count of tunnel establishments for this slot
    volatile ULONG Flags;           // 0x10 CM: STOR98LS_*
    volatile ULONG AckGeneration;   // 0x14 stor98 only
    volatile ULONG RemoveSequence;  // 0x18 CM only (9x): 24-bit removal request sequence, 0 none (NT: 0)
    volatile ULONG RemoveAck;       // 0x1C stor98 only (9x): RemoveSequence << 8 | outcome (NT: 0)
} STOR98LS_ENTRY, *PSTOR98LS_ENTRY;

typedef struct _STOR98LS_TABLE {                // 272 bytes
    ULONG Signature;                // 0x00 STOR98LS_SIGNATURE
    ULONG Version;                  // 0x04 STOR98LS_VERSION
    ULONG Count;                    // 0x08 STOR98LS_ENTRIES
    ULONG Cookie;                   // 0x0C 9x: per-boot cookie, nonzero (was Reserved; NT: 0)
    STOR98LS_ENTRY Entry[STOR98LS_ENTRIES];     // 0x10
} STOR98LS_TABLE, *PSTOR98LS_TABLE;

//
// Interface 1 (diagnostic only; rejected as the statement carrier, MD-6):
// IOCTL_SCSI_MINIPORT to the SCSI port device of the stor98 adapter
// (\Device\ScsiPortN), SRB_IO_CONTROL header first, METHOD_BUFFERED: the
// request and the reply share the buffer that follows the header.
//
// Measured limits (G3): the port routes it to LU 0 and starts it only when
// that LU has a free slot, so with the queue full of held requests it
// arrives after the gone deadline; once the disk is gone it fails with
// STATUS_DEVICE_DOES_NOT_EXIST. STOR98CM_CODE_QUERY is the supported use.
// STOR98CM_CODE_LINK_DOWN is the prototype's statement over this route and
// is not part of the adopted contract.
//
#define STOR98CM_SIGNATURE          "STOR98CM"      // SRB_IO_CONTROL.Signature (8 bytes, no NUL)
#define STOR98CM_VERSION            4

// SRB_IO_CONTROL.ControlCode
#define STOR98CM_CODE_QUERY         0x00980001      // reply only: who is this adapter
#define STOR98CM_CODE_LINK_DOWN     0x00980002      // prototype only (see above)

// Request flags
#define STOR98CM_FLAG_QUIESCED      0x00000001      // path closed and endpoint reset completed (required)

// SRB_IO_CONTROL.ReturnCode
#define STOR98CM_RC_ACCEPTED        0   // statement applied (held requests completed, memory released)
#define STOR98CM_RC_BAD_REQUEST     1   // length, version or code wrong
#define STOR98CM_RC_NOT_THIS_FUNC   2   // bus/slot do not name this adapter's function
#define STOR98CM_RC_CONTRADICTED    3   // the controller still answers with an enabled controller
#define STOR98CM_RC_NOT_QUIESCED    4   // STOR98CM_FLAG_QUIESCED missing: not evidence
#define STOR98CM_RC_ALREADY         5   // the same link generation was already applied

// Payload after SRB_IO_CONTROL (SRB_IO_CONTROL.Length = sizeof(STOR98CM_MSG)).
typedef struct _STOR98CM_MSG {
    // Request (LINK_DOWN); ignored for QUERY
    ULONG Version;          // STOR98CM_VERSION
    ULONG Flags;            // STOR98CM_FLAG_*
    ULONG BusNumber;        // PCI bus of the NVMe function (the tunnel bridge's secondary bus)
    ULONG SlotNumber;       // PCI_SLOT_NUMBER.u.AsULONG (device 4:0, function 7:5)
    ULONG LinkGeneration;   // CM's count of tunnel establishments for that slot
    // Reply (both codes)
    ULONG ReplyVersion;
    ULONG MyBusNumber;      // the adapter's own location (ConfigInfo)
    ULONG MySlotNumber;
    ULONG VendorDevice;     // device ID << 16 | vendor ID
    ULONG StateBefore;      // driver state | bring-up state << 8 (implementation-defined values)
    ULONG StateAfter;
    ULONG HeldCompleted;    // held requests completed on this statement
    ULONG PortRetired;      // held requests the port had already retired
    ULONG Csts;             // CSTS read when the statement arrived
} STOR98CM_MSG, *PSTOR98CM_MSG;

//
// ===========================================================================
// Interface 3 (Windows 98 SE and Me only; MD-5, MD-4, D1, D2): the
// interface 2 table, found through a registry locator with a current-boot
// proof, plus the removal transaction. Revised after the Codex review of the
// first prototype and with xhci98's locator and REMOVE_PENDING layout
// (maintainer, 2026-10-15); docs/gates/Q-9x.md, "Interface 3 revision".
// Rules are numbered (R*) so that code can cite them.
// ===========================================================================
//
// R1  ABI. 32-bit x86, little-endian. The table is STOR98LS_TABLE above (272
//     bytes: header 16, Entry[8] of 32 bytes at 0x10) and the locator is
//     STOR98LS9X_LOCATOR (20 bytes); every field is a 32-bit ULONG at a
//     4-byte-aligned offset, read and written as one aligned access (no
//     64-bit shared field). Structures are declared under 4-byte packing and
//     sizes and offsets are checked at compile time below. Linear addresses
//     are 9x system-arena addresses, not NT pointers. NT keeps interface 2.
//
// R2  Versions. Locator version 4 and table version 4 (STOR98LS_VERSION),
//     layout and meaning frozen as written here (version 4 is version 1 of
//     nvme98cm.h under the stor98 names and signatures, nothing else; see
//     "Names and versions" above). Producers write zero to
//     anything not defined; readers require exact signatures, versions,
//     sizes and Count. An incompatible change gets a new locator value name
//     ("LinkState<n>") and a new table, never a reinterpretation of this one,
//     so a reader keeps its old decoder and a CM may publish both. The first
//     Q-9x prototype ("interface 3 v1": HKEY_DYN_DATA\Nvme98Cm "LinkState",
//     32-byte record, 1056-byte table) is withdrawn: nothing produces or reads
//     it. Mixed peers: CM without this interface (none, or the prototype) +
//     stor98: no channel (MD-1 applies, pools under stor98's own budget);
//     this CM + a receiver without it: no acknowledgement ever comes, so the
//     CM never reports a clean eject (R11 O4). A missing or unverifiable
//     channel, or a missing acknowledgement, is never taken as removal
//     support or as evidence.
//
// R3  Locator and current-boot proof (no VxD, documented services only, D1).
//     Exactly one publisher per boot: the CM, a WDM driver. It allocates one
//     STOR98LS_TABLE from NTKERN non-paged pool (VMM system arena, fixed
//     pages), zeroes it, fills Signature, Version, Count and Cookie (a
//     nonzero per-boot value; it is not a secret), then:
//       a. creates the current-boot proof: the dynamic key
//          HKEY_DYN_DATA\STOR98LS9X_PROOF_KEY ("Stor98LinkState") with two
//          values, both answered by its own resident provider (VMM
//          _RegCreateDynKey) from the CM's own memory:
//            STOR98LS9X_PROOF_VALUE ("BootCookie"), REG_DWORD = Cookie;
//            STOR98LS9X_PROOF_TABLE_VALUE ("TableLinear"), REG_BINARY 12 bytes
//            STOR98LS9X_PROOF_TABLE {TableLinear, 272, Cookie}.
//          The provider value is the authority for the table address (the
//          maintainer's choice for re-review finding P1-2, option A). Before
//          creating the key the CM opens it: if it already exists another
//          publisher owns it and this CM publishes nothing (duplicates
//          refused);
//       b. writes the locator, REG_BINARY STOR98LS9X_LOCATOR_VALUE
//          ("LinkState") of HKLM\STOR98LS9X_LOCATOR_KEY
//          ("System\CurrentControlSet\Services\stor98"): {'S98L', 4,
//          TableLinear, 272, Cookie}.
//     The locator persists across boots and is only a locator; the proof
//     exists only while its provider is loaded in this boot, so a table is
//     accepted only when the proof's cookie and table address equal the
//     locator's and the table's cookie matches (R5). The table is never moved, replaced or re-initialised
//     during the boot. stor98 reads the locator and the proof once per load,
//     in HwFindAdapter (VMM _RegOpenKey, _RegQueryValueEx, _RegCloseKey).
//
// R4  CM lifetime. The table is never freed. The CM is meant to stay loaded
//     for the boot. It publishes the proof and the table only after it has
//     made its own residency independent of its PnP devices (the stand-in:
//     no DriverUnload, a non-PnP device object created in DriverEntry, and
//     DriverEntry fails if that object cannot be created); it vetoes
//     IRP_MN_QUERY_REMOVE of its own devices. If a CM unloads anyway it
//     first deletes the proof key (later loads then find no channel), stops
//     writing the table and leaves it allocated (receivers holding the
//     pointer get no further statement: MD-1 covers them). A CM loaded again
//     in the same boot publishes a new table with a new cookie and must
//     remove (retire, R8c) every endpoint devnode under its bridges before
//     its first UP.
//
// R5  Validation by stor98 before use; failing any check means "CM channel
//     unavailable" for that load, and nothing more is read from what failed.
//     Trust boundary: the CM is a trusted kernel component. These checks
//     reject stale (another boot's), foreign, malformed or unreachable data;
//     they are not authentication, and data the CM itself publishes wrongly
//     while carrying the right cookie is outside what stor98 can detect.
//     stor98 cannot verify that the CM's statements (DOWN_QUIESCED, R11/R12
//     order) are true; it relies on the CM's obligations (MD-2), and the
//     Q-9x negative test (a statement with the path still open) shows the
//     limit.
//     R5a Locator: rc 0, type REG_BINARY, length exactly 20; Signature
//         STOR98LS_SIGNATURE, Version 4, TableSize 272, Cookie nonzero.
//     R5b Proof: the dynamic key opens; BootCookie is REG_DWORD, length 4,
//         nonzero, == locator Cookie; TableLinear is REG_BINARY, length 12,
//         with TableLinear, TableSize (272) and Cookie each equal to the
//         locator's. Any difference: no channel.
//     R5c Table address: 4-byte aligned; [TableLinear, TableLinear + 272)
//         inside the system arena C0000000-FFBFFFFF, no overflow; every page
//         present (VMM _CopyPageTable, P bit) before the first dereference.
//         Then stor98 establishes write access and residency itself: every
//         page present and writable (R/W bit), and the pages locked with VMM
//         _LinPageLock once per boot and table (never unlocked; the table is
//         never freed, R4), so later paging cannot remove them.
//     R5d Header: Signature, Version (4), Count (8), Cookie == locator Cookie.
//         The header is immutable after publication.
//     R5e Entry (each snapshot, R6): only known Flags bits; IN_USE set; UP
//         and DOWN_QUIESCED not both; REMOVE_PENDING only with UP and with
//         RemoveSequence in 1..0xFFFFFF; LinkGeneration nonzero when UP or
//         DOWN_QUIESCED; BusNumber and SlotNumber equal the values bound at
//         HwFindAdapter. A snapshot that fails is ignored and counted;
//         stor98 then stops acting on the entry for that load (no evidence,
//         no acknowledgement).
//
// R6  Synchronization.
//     R6a Ownership: header and entry fields Sequence, BusNumber, SlotNumber,
//         LinkGeneration, Flags and RemoveSequence are written by the CM
//         only; AckGeneration and RemoveAck by the bound stor98 instance only.
//     R6b One CM writer: all entry writes come from one CM thread or under
//         one CM lock. An update is InterlockedIncrement(Sequence) (odd),
//         the payload stores, InterlockedIncrement(Sequence) (even); the
//         locked instructions are the CPU and compiler barriers.
//     R6c stor98 snapshot: s1 = Sequence; if odd, retry; read the payload;
//         s2 = Sequence; if s1 != s2, retry; at most 2 attempts per poll,
//         else the poll is skipped (next tick). No spinning. Reads at these
//         points are compiler barriers (inline assembly in stor98).
//     R6d stor98 publishes each acknowledgement as one aligned 32-bit store
//         by an interlocked exchange, after all the work it reports:
//         AckGeneration (R12) and RemoveAck (R9). The CM reads it, checks
//         it, and reads it once more (unchanged) before acting.
//     R6e A CM serving several ports keeps every wait (link acknowledgement,
//         removal acknowledgement, the delay before Remove_SubTree, removal
//         retries, teardown) as per-port state advanced by its one polling
//         loop, so its single writer never blocks; no port's wait may delay
//         another port's statements (S2) by more than one CM poll period.
//         CONFIGMG calls that can wait on the OS or on other drivers
//         (Query_Remove_SubTree, Remove_SubTree, Reenumerate_DevNode) are
//         made off the writer, from a per-port worker, and their result is
//         taken by the port's state when it arrives.
//         Residual (9x, observed on Me): while the system-modal "Disk Write
//         Error" message for a volume whose device went away with unwritten
//         data was shown, the stand-in CM's writer and worker threads did
//         not run (no heartbeat, no log) and published nothing until it was
//         dismissed (one 14 s timeout was recorded at 282 s). stor98's own
//         timer kept running: its polling, all-ones detection and the MD-1
//         gone deadline do not depend on the CM.
//     R6f Worker handoff (a CM that runs CONFIGMG calls on a worker, R6e):
//         one shared state word per worker, each transition made by one
//         side with a locked instruction: IDLE -> SUBMITTED (writer, after
//         storing operation, devnode, flags and a new operation id);
//         SUBMITTED -> RUNNING (worker, locked compare-exchange; it runs
//         only operations it moved to RUNNING itself); RUNNING -> DONE
//         (worker, after storing the result and the id it ran); DONE ->
//         IDLE (writer, after taking or discarding the result). The writer
//         accepts a result only when its id is the id it submitted for that
//         operation and devnode. Separate busy and done flags are not
//         allowed: clearing them in two steps lets the worker run an
//         operation twice and the writer take one call's result for another.
//         An operation still running when its state is abandoned (O6) stays
//         owned by its port, whose next step for that devnode takes its
//         result before submitting anything else.
//
// R7  stor98's own records (not part of this interface; Q-9x documents
//     them): the MD-4 pool ownership records (OWNED / ISOLATED / RELEASED /
//     QUARANTINED, owner tokens, claims and releases under stor98's own lock,
//     never spinning from the timer or HwStartIo) and the boot-wide pool
//     budget live in an stor98 boot-resident block validated with documented
//     VMM services. The CM never sees or writes them. For these records
//     stor98 takes from this table only DOWN_QUIESCED for a generation (or
//     a later one) on the same entry as evidence; a larger LinkGeneration
//     alone is not (for a bound instance it counts only as R8d allows). Its
//     other evidence is its own: a later instance's acknowledged disable
//     (CC.EN=0, RDY=0) of the same function.
//
// R8  Entries and identity.
//     R8a One entry per tunnel port (one NVMe function behind one tunnel
//         bridge), published by the CM before the first UP on that port and
//         kept for the whole boot: its BusNumber and SlotNumber never change
//         while IN_USE, and the entry is never reused for another function
//         in that boot. Two IN_USE entries never name the same (BusNumber,
//         SlotNumber); stor98 rejects a duplicate (no channel for that
//         function). If a port's bus number changes, the CM clears IN_USE on
//         the old entry (only after its devnode is retired, R8c) and uses a
//         free entry; with none free, that port runs without the interface
//         (stor98 finds no entry; the CM never reports a clean eject there).
//     R8b LinkGeneration: per entry, nonzero once UP, incremented for every
//         tunnel establishment and for nothing else (UP published again for
//         a tunnel that stayed up keeps it), never reset or wrapped in a
//         boot (the CM stops establishing tunnels on that port first).
//         Tunnels on one port are sequential: the CM publishes DOWN_QUIESCED
//         for generation G before UP of G + 1, so on the same entry every
//         generation below the current one, and the current one when
//         DOWN_QUIESCED, is quiesced. That is evidence for that entry only.
//     R8c Devnode retirement: the CM publishes UP for a new tunnel, or
//         re-enumerates the bridge, only after CONFIGMG Remove_SubTree of the
//         port's previous endpoint devnode has succeeded (or it never
//         existed); a veto delays the new tunnel and is never forced. So at
//         most one stor98 instance serves an entry at a time; a new
//         HwFindAdapter for a function means the previous instance for it is
//         gone (9x binds one adapter per PCI function; observed in Q-9x).
//     R8d stor98 binds once, in HwFindAdapter, to the one IN_USE entry naming
//         its (BusNumber, SlotNumber), recording LinkGeneration and the
//         RemoveSequence present at that time (a request older than the load
//         is never its own). An instance that later sees a different
//         BusNumber/SlotNumber, IN_USE cleared, or a LinkGeneration below its
//         own has lost its identity: it stops acting on the entry. A
//         LinkGeneration above its own is R8b evidence that its own
//         generation is quiesced when the entry is DOWN_QUIESCED, and in any
//         other state (UP, IN_USE alone) only once the instance has lost
//         presence (it is gone, or its controller reads all-ones). While the
//         controller still answers, such an entry is identity loss: no
//         evidence, nothing completed, no acknowledgement; the drive keeps
//         running, and a later presence loss ends at the MD-1 deadline.
//         Evidence is applied once (the S3 cleanup) without AckGeneration,
//         which is written only for the generation the instance is bound to;
//         then it stops acting on the entry. While it holds an accepted
//         removal request (R9), any other LinkGeneration is identity loss.
//
// R9  The removal transaction (REMOVE_PENDING, Flags bit 3).
//     R9a Request: one Sequence update sets RemoveSequence = the next value
//         of the CM's per-entry 24-bit counter (1..0xFFFFFF, skipping 0 on
//         wrap) and sets REMOVE_PENDING; LinkGeneration is not changed while
//         REMOVE_PENDING is set. The request names (entry, LinkGeneration,
//         RemoveSequence). The CM issues at most one request per endpoint
//         devnode (per instance); a retry is the same request left in place.
//     R9b stor98 accepts the first request it sees after binding whose
//         RemoveSequence differs from the one recorded at binding and whose
//         snapshot shows its own LinkGeneration. Any later RemoveSequence is
//         an overlapping request and is ignored: the first acknowledgement
//         stays.
//     R9c Acknowledgement: RemoveAck = RemoveSequence << 8 | outcome, written
//         once by an interlocked exchange after the work (R6d) and never
//         changed (terminal results are immutable). Outcomes:
//         STOR98RM_CLEAN (1): drained, an ordered Flush completed
//         successfully, CC.SHN set and SHST complete, CC.EN=0 and RDY=0
//         acknowledged. STOR98RM_ISOLATED_NOT_CLEAN (2): CC.EN=0 and RDY=0
//         acknowledged (or DOWN_QUIESCED applied) but drain, Flush or shutdown
//         did not complete; no DMA is possible any more. STOR98RM_FAILED (3):
//         no acknowledged disable (RDY stayed 1 or presence was lost); the
//         controller may still DMA. The CM accepts the acknowledgement only if
//         RemoveAck >> 8 equals the RemoveSequence it wrote and the entry
//         still shows the request's LinkGeneration; anything else is "no
//         acknowledgement".
//     R9d Accepting a request makes the stor98 instance terminal: it never
//         re-enables the controller and never serves I/O again. Until the
//         acknowledgement new SRBs get BUSY; after it every new request fails
//         (SRB_FUNCTION_SHUTDOWN completes with success, everything else
//         SRB_STATUS_NO_DEVICE). Requests still outstanding when isolation is
//         acknowledged are completed SRB_STATUS_NO_DEVICE; on FAILED they
//         stay held until DOWN_QUIESCED or the MD-1 deadline. The only way
//         back to a usable disk is D2: Remove_SubTree and a fresh devnode.
//
// R10 Bounds (VMM elapsed time). stor98 requests a timer callback every
//     STOR98LS9X_POLL_MS (the requested polling interval) whenever it is not
//     stopped, idle or not, and also polls from HwStartIo; callbacks can run
//     up to STOR98LS9X_LATE_MS late (measured on Me). It acknowledges a
//     request within STOR98LS9X_ACK_BOUND_MS of its publication, enforced as
//     one overall transaction budget: it sees the request at most POLL + LATE
//     after publication, and a decision taken at a threshold is executed by
//     a timer check that can itself come POLL + LATE after it, so measured
//     from first sight every decision is due by ACK_BOUND - 2 * (POLL +
//     LATE) (8500 ms) and the acknowledgement is written within ACK_BOUND of
//     publication; the disable is requested from the first check at or after
//     8500 - 3000 = 5500 ms whatever phase is running (drain, Flush and
//     shutdown are each also capped at 2 s); a disable that is not
//     acknowledged by 8500 ms is FAILED. Its gone deadline (MD-1) is STOR98LS9X_GONE_MS by default (a
//     tunable). The CM polls acknowledgements at least every 50 ms while it
//     waits; its bounds below include the lateness.
//
// R11 Orderly (CM-controlled) eject.
//     O1 CM: file-system quiescence, then Query_Remove_SubTree(endpoint). A
//        veto is final for this eject: no request, nothing changed.
//     O2 CM: the request (R9a).
//     O3 stor98: within STOR98LS9X_POLL_MS: BUSY for new SRBs; drain, ordered
//        Flush, CC.SHN and SHST, CC.EN=0 and RDY=0; acknowledgement (R9c).
//     O4 CM: waits T_ack >= STOR98LS9X_ACK_BOUND_MS + 2 * STOR98LS9X_POLL_MS
//        + 1500 ms (STOR98LS9X_CM_ACK_WAIT_MS), validating it (R9c).
//     O5 CLEAN: Remove_SubTree; on success clear REMOVE_PENDING; clean eject;
//        then the path teardown and, after it, DOWN_QUIESCED for the
//        generation (no acknowledgement to wait for: no instance remains).
//        ISOLATED_NOT_CLEAN: not clean; the controller is isolated:
//        Remove_SubTree, clear REMOVE_PENDING, Reenumerate_DevNode(bridge)
//        (D2: the disk returns as a fresh device on the same tunnel).
//        FAILED, no acknowledgement in T_ack, or a cancel (the CM abandons
//        the clean-eject claim): not clean; the controller may still DMA, so
//        the CM isolates it first (path teardown, endpoint reset,
//        DOWN_QUIESCED and the R12 S4 acknowledgement wait), then
//        Remove_SubTree; the returning tunnel gets a new generation (D2:
//        fresh device).
//     O6 Remove_SubTree failing after a successful query, whichever outcome
//        led to it (CLEAN, ISOLATED_NOT_CLEAN or a cancel; one common path):
//        retry every 2 s until 30 s have elapsed since the first attempt
//        (an elapsed deadline, also when a call is still running; a call
//        that completes after the deadline counts for the O6 retries, never
//        as a clean removal); then keep the devnode, keep ownership of a
//        call still running (R6f),
//        tear the tunnel down (DOWN_QUIESCED releases what the terminal
//        instance holds), report the eject failed, keep retrying the removal
//        (vetoes honoured), and publish no UP on that port until it succeeds
//        (R8c); the returning tunnel then gets a new generation.
//        A CM with no per-port teardown (the Thunderbolt 3 firmware CM,
//        whose only teardown drops every tunnel of the host) tears nothing
//        down: after the deadline it keeps the devnode and the request in
//        place (the instance stays terminal, R9d), reports the eject
//        failed, keeps retrying the removal every 2 s (vetoes honoured) and
//        publishes no UP on that port until the removal succeeds or the
//        devnode is gone; the unplug's link down is then R12's (maintainer,
//        2026-10-22).
//     Late acknowledgements (after T_ack) are logged and never change the
//     verdict: the CM checks its deadline before it reads the
//     acknowledgement, so one first seen after T_ack is late (also when the
//     CM itself ran late; the table carries no timestamp). The same holds
//     for the S4 link-down wait. There is no in-place resume after O1 succeeded (D2).
//
// R12 Surprise removal (link lost without an eject).
//     S1 stor98: all-ones reads mean GONE: new SRBs fast-fail, issued ones
//        stay held (no evidence yet).
//     S2 CM: after the tunnel's PCIe path is closed and the endpoint reset or
//        drain has completed (MD-2), one update: Flags = IN_USE |
//        DOWN_QUIESCED (UP and REMOVE_PENDING cleared). Never earlier.
//     S3 stor98: within STOR98LS9X_POLL_MS, whether or not anything is held:
//        cleanup first (held requests completed SRB_STATUS_NO_DEVICE, pool
//        isolated and released, gone deadline cancelled, an open request
//        acknowledged ISOLATED_NOT_CLEAN), then AckGeneration =
//        LinkGeneration last (R6d), only for the generation the instance is
//        bound to (R8d). On 9x stor98 does not report
//        BusChangeDetected: the removal is the CM's CONFIGMG call (S4).
//     S4 CM: waits for AckGeneration == LinkGeneration for at most the gone
//        deadline; a shorter bound is allowed but not below
//        STOR98LS9X_CM_LINK_WAIT_MS (2 * STOR98LS9X_POLL_MS + LATE). Then
//        Query_Remove_SubTree; a veto is retried every 2 s and never forced;
//        on success Remove_SubTree (the devnode is retired, R8c).
//     S5 CM: a returning link gets UP with LinkGeneration + 1 only after S4
//        retired the devnode, then Reenumerate_DevNode(bridge).
//     Deadline: without DOWN_QUIESCED within the gone deadline of presence
//     loss, stor98 applies MD-1 (held SRBs completed, the pool block stays
//     owned and is never reused without later evidence).
//

#pragma pack(push, 4)

#define STOR98LS9X_LOCATOR_KEY      "System\\CurrentControlSet\\Services\\stor98"   // under HKLM
#define STOR98LS9X_LOCATOR_VALUE    "LinkState"         // REG_BINARY STOR98LS9X_LOCATOR
#define STOR98LS9X_LOCATOR_VERSION  4
#define STOR98LS9X_PROOF_KEY        "Stor98LinkState"   // dynamic key under HKEY_DYN_DATA
#define STOR98LS9X_PROOF_VALUE      "BootCookie"        // REG_DWORD == locator and table Cookie
#define STOR98LS9X_PROOF_TABLE_VALUE "TableLinear"      // REG_BINARY STOR98LS9X_PROOF_TABLE (R3a, R5b)
#define STOR98LS9X_ARENA_LO         0xC0000000UL        // R5c: VMM system arena
#define STOR98LS9X_ARENA_HI         0xFFC00000UL        // exclusive

#define STOR98LS_REMOVE_PENDING     0x00000008          // Flags bit 3 (9x)
#define STOR98LS9X_FLAGS_KNOWN      0x0000000F
#define STOR98LS9X_SEQ_MASK         0x00FFFFFF          // RemoveSequence

// RemoveAck outcome (low byte)
#define STOR98RM_CLEAN              1
#define STOR98RM_ISOLATED_NOT_CLEAN 2
#define STOR98RM_FAILED             3

// Bounds (R10-R12), milliseconds of VMM elapsed time
#define STOR98LS9X_POLL_MS          250     // requested polling interval (the prototype: 100)
#define STOR98LS9X_LATE_MS          1500    // timer callback lateness allowed for (measured on Me)
#define STOR98LS9X_ACK_BOUND_MS     12000   // stor98 acknowledges a request within this
#define STOR98LS9X_GONE_MS          5000    // stor98's default gone deadline (MD-1)
#define STOR98LS9X_CM_ACK_WAIT_MS   (STOR98LS9X_ACK_BOUND_MS + 2 * STOR98LS9X_POLL_MS + STOR98LS9X_LATE_MS)
#define STOR98LS9X_CM_LINK_WAIT_MS  (2 * STOR98LS9X_POLL_MS + STOR98LS9X_LATE_MS)

typedef struct _STOR98LS9X_LOCATOR {            // 20 bytes
    ULONG Signature;                // 0x00 STOR98LS_SIGNATURE ('S98L')
    ULONG Version;                  // 0x04 STOR98LS9X_LOCATOR_VERSION
    ULONG TableLinear;              // 0x08 linear address (system arena) of the STOR98LS_TABLE
    ULONG TableSize;                // 0x0C sizeof(STOR98LS_TABLE) = 272
    ULONG Cookie;                   // 0x10 == STOR98LS_TABLE.Cookie == the proof's BootCookie
} STOR98LS9X_LOCATOR, *PSTOR98LS9X_LOCATOR;

typedef struct _STOR98LS9X_PROOF_TABLE {        // 12 bytes, proof value "TableLinear"
    ULONG TableLinear;              // 0x00 the table's linear address (authority, R3a)
    ULONG TableSize;                // 0x04 sizeof(STOR98LS_TABLE) = 272
    ULONG Cookie;                   // 0x08 == BootCookie == locator Cookie == table Cookie
} STOR98LS9X_PROOF_TABLE, *PSTOR98LS9X_PROOF_TABLE;

#pragma pack(pop)

// R1: layout checks (compile-time; every target that includes this header).
#define STOR98_CASSERT(n, c)        typedef char stor98_cassert_##n[(c) ? 1 : -1]
#define STOR98_OFS(t, f)            ((ULONG)(ULONG_PTR)&(((t *)0)->f))
STOR98_CASSERT(ent_size, sizeof(STOR98LS_ENTRY) == 32);
STOR98_CASSERT(ent_ack, STOR98_OFS(STOR98LS_ENTRY, AckGeneration) == 0x14);
STOR98_CASSERT(ent_rseq, STOR98_OFS(STOR98LS_ENTRY, RemoveSequence) == 0x18);
STOR98_CASSERT(ent_rack, STOR98_OFS(STOR98LS_ENTRY, RemoveAck) == 0x1C);
STOR98_CASSERT(tbl_cookie, STOR98_OFS(STOR98LS_TABLE, Cookie) == 0x0C);
STOR98_CASSERT(tbl_entry, STOR98_OFS(STOR98LS_TABLE, Entry) == 0x10);
STOR98_CASSERT(tbl_size, sizeof(STOR98LS_TABLE) == 272);
STOR98_CASSERT(loc_size, sizeof(STOR98LS9X_LOCATOR) == 20);
STOR98_CASSERT(loc_cookie, STOR98_OFS(STOR98LS9X_LOCATOR, Cookie) == 0x10);
STOR98_CASSERT(prf_size, sizeof(STOR98LS9X_PROOF_TABLE) == 12);

#endif // _STOR98CM_H_
