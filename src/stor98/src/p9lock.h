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
// p9lock.h - the 9x DMA pool's boot-block lock attempt and the stall budget
// it is charged against (pool9x.c; known issue K2 of 0.3.0.0, PLAN 0.10
// stall budget). Header-only and free of VMM calls, so the host unit test
// (tests/unit/p9lock_ut.c) runs the same code against a mocked lock. The
// includer defines
//   P9LOCK_CAS(p, cmp, val)   locked compare-exchange of *p, returning the old value
//   P9LOCK_STALL(us)          a busy-wait of us microseconds (ScsiPortStallExecution)
// before including it.
//
// Rules (K2):
//   - Every lock attempt is one compare-exchange; a failed one stalls only
//     when the caller passes a stall budget with stalls left, and each stall
//     is charged to that budget. One budget is shared by every attempt in
//     one callback, so the callback's total never exceeds it.
//   - Runtime callbacks (timer, HwStartIo, ISR, HwAdapterControl stop) pass
//     NULL: one non-blocking attempt; the caller defers unfinished work to a
//     later timer tick (P9Work) or leaves it to the next load (an ended
//     owner token, P9C_DEAD).
//   - On Windows 9x (uniprocessor) a busy-wait cannot let the holder run:
//     the holder is the context this callback interrupted, which resumes
//     only after the callback returns. A stall therefore never helps against
//     a held lock, and the budget is only ever what the callback has left
//     (P9_FIND_STALLS, HwFindAdapter after the clock calibration).
//

#ifndef _STOR98_P9LOCK_H_
#define _STOR98_P9LOCK_H_

#if !defined(P9LOCK_CAS) || !defined(P9LOCK_STALL)
#error p9lock.h needs P9LOCK_CAS and P9LOCK_STALL
#endif

#define P9_LOCK_STALL_US    100     // one stall (PLAN 0.10: at most 100 us per call)

// HwFindAdapter's pool work (P9Acquire) shares that callback's budget with
// the clock calibration: the stalls the calibration leaves (currently none).
#define P9_FIND_STALLS      (ENG_STALL_BUDGET - CLOCK_CAL_STALLS)

typedef struct _P9_STALLS {
    ULONG Left;                 // stalls this callback may still spend
    ULONG Used;                 // stalls spent so far
} P9_STALLS, *PP9_STALLS;

static VOID P9StallsInit(OUT PP9_STALLS Budget, IN ULONG Stalls)
{
    Budget->Left = Stalls;
    Budget->Used = 0;
}

// TRUE if the lock word went from 0 to Me. Budget NULL: exactly one attempt.
static BOOLEAN P9LockAttempt(IN volatile ULONG *Lock, IN ULONG Me, IN OUT PP9_STALLS Budget)
{
    for (;;) {
        if (P9LOCK_CAS(Lock, 0, Me) == 0) {
            return TRUE;
        }
        if (Budget == NULL || Budget->Left == 0) {
            return FALSE;
        }
        Budget->Left--;
        Budget->Used++;
        P9LOCK_STALL(P9_LOCK_STALL_US);
    }
}

#endif // _STOR98_P9LOCK_H_
