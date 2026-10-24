// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// prpchk.h - the PRP entry rules the engine holds a caller buffer's
// translation to before it lets the controller DMA into it (engine.c,
// EngMapCaller; PLAN 0.4). NVM Express Base 1.4c, "Physical Region Page Entry and
// List": PRP entry 1 may carry
// a page offset but its bits 1:0 are zero (DWORD aligned); every later entry
// is page aligned. A child whose translation breaks either rule is bounced,
// never sent. Pure arithmetic with no port or OS call, so the host unit test
// (tests/unit/sg9x_ut.c) includes it unchanged.
//
// A buffer at an odd or word offset (+1, +2, +3, +511, a page straddle at
// +4094) therefore always bounces; xhci98's roadmap 43.79 found the 9x
// corruption at such offsets above Stor98, in a list that named a linear
// address as a physical one, which no PRP rule can see.
//

#ifndef _STOR98_PRPCHK_H_
#define _STOR98_PRPCHK_H_

// Entry 1: Pa's low dword from the port, CallerVa the linear address it is
// for. DWORD aligned, and the same page offset as the linear address (a
// translation that moved the offset is not one of this buffer).
static BOOLEAN PrpFirstOk(ULONG PaLow, ULONG CallerVa)
{
    return (BOOLEAN)((PaLow & 3) == 0 && (PaLow & 0xFFF) == (CallerVa & 0xFFF));
}

// Entries 2 onward, and PRP2 when it is a second data page: page aligned.
static BOOLEAN PrpLaterOk(ULONG PaLow)
{
    return (BOOLEAN)((PaLow & 0xFFF) == 0);
}

#endif // _STOR98_PRPCHK_H_
