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
// Debug instrumentation; see dbgport.h. Compiled to nothing without
// STOR98_DBGPORT.
//

#include "nvme2k.h"

#ifdef STOR98_DBGPORT

static BOOLEAN DbgPortPresent = FALSE;
static BOOLEAN DbgPortProbed = FALSE;

#ifdef NVME2K_W9X
// The 9x port's I/O accessors are not relied on; plain IN/OUT (the .mpd is
// ring-0 x86 code).
static ULONG DbgIn32(USHORT Port)
{
    ULONG v;

    __asm {
        mov dx, Port
        in eax, dx
        mov v, eax
    }
    return v;
}

static VOID DbgOut32(USHORT Port, ULONG Value)
{
    __asm {
        mov dx, Port
        mov eax, Value
        out dx, eax
    }
}

static VOID DbgOut8(USHORT Port, UCHAR Value)
{
    __asm {
        mov dx, Port
        mov al, Value
        out dx, al
    }
}
#else
// On x86 and amd64 NT the HAL treats the pointer value as the port number.
static ULONG DbgIn32(USHORT Port)
{
    return ScsiPortReadPortUlong((PULONG)(ULONG_PTR)Port);
}

static VOID DbgOut32(USHORT Port, ULONG Value)
{
    ScsiPortWritePortUlong((PULONG)(ULONG_PTR)Port, Value);
}

static VOID DbgOut8(USHORT Port, UCHAR Value)
{
    ScsiPortWritePortUchar((PUCHAR)(ULONG_PTR)Port, Value);
}
#endif

VOID DbgPortInit(VOID)
{
    if (DbgPortProbed) {
        return;
    }
    DbgPortProbed = TRUE;
    DbgPortPresent = (BOOLEAN)(DbgIn32(STOR98_DBGPORT_BASE) == STOR98_DBGPORT_SIG);
}

static VOID DbgConHex(ULONG Value, ULONG Digits)
{
    static const char hex[] = "0123456789abcdef";
    ULONG shift = Digits * 4;

    while (shift) {
        shift -= 4;
        DbgOut8(STOR98_DEBUGCON_PORT, (UCHAR)hex[(Value >> shift) & 0xF]);
    }
}

static VOID DbgConLine(ULONG TscLo, ULONG TscHi, ULONG Code, ULONG Arg)
{
    DbgOut8(STOR98_DEBUGCON_PORT, 'n');
    DbgOut8(STOR98_DEBUGCON_PORT, '9');
    DbgOut8(STOR98_DEBUGCON_PORT, '8');
    DbgOut8(STOR98_DEBUGCON_PORT, ' ');
    DbgConHex(TscHi, 8);
    DbgConHex(TscLo, 8);
    DbgOut8(STOR98_DEBUGCON_PORT, ' ');
    DbgConHex(Code, Code > 0xFF ? 3 : 2);   // 32-bit codes above 0xFF (pool9x, int-0.2)
    DbgOut8(STOR98_DEBUGCON_PORT, ' ');
    DbgConHex(Arg, 8);
    DbgOut8(STOR98_DEBUGCON_PORT, '\n');
}

VOID DbgStage(IN ULONG Code, IN ULONG Arg)
{
    ULONG lo, hi;

    if (!DbgPortProbed) {
        DbgPortInit();
    }
    if (!DbgPortPresent) {
        return;
    }
    ClockReadTsc(&lo, &hi);
    DbgOut32(STOR98_DBGPORT_BASE + 4, Arg);
    DbgOut32(STOR98_DBGPORT_BASE, Code);
    DbgConLine(lo, hi, Code, Arg);
}

VOID DbgMark(IN ULONG Code, IN ULONG Arg)
{
    ULONG lo, hi;

    if (!DbgPortProbed) {
        DbgPortInit();
    }
    if (!DbgPortPresent) {
        return;
    }
    ClockReadTsc(&lo, &hi);
    DbgOut32(STOR98_DBGPORT_BASE + 4, Arg);
    DbgOut32(STOR98_DBGPORT_BASE, Code);
    DbgOut32(STOR98_DBGPORT_BASE + 8, lo);
    DbgOut32(STOR98_DBGPORT_BASE + 0xC, hi);
    DbgConLine(lo, hi, Code, Arg);
}

#endif // STOR98_DBGPORT
