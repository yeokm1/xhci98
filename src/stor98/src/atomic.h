// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2025, Dominik Behr (nvme2k)
// Windows 9x/NT4 port and changes (nvme9x/nvment): Copyright (c) SweetLow
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
// atomic.h - Atomic operations and synchronization primitives
// Windows 2000 ScsiPort miniport drivers cannot use kernel InterlockedXxx functions
// so we implement them using inline assembly for x86 and Alpha AXP, and
// compiler intrinsics for amd64
//

#ifndef _ATOMIC_H_
#define _ATOMIC_H_

//
// ATOMIC type - Use this for variables that need atomic access
// This is just volatile ULONG, but makes code more self-documenting
//
typedef volatile ULONG ATOMIC;

//
// Atomic operations for SMP safety
//
#if defined(_M_IX86)

// x86 inline assembly for atomic increment
// Returns the NEW value (after increment)
static __inline ULONG AtomicIncrement(volatile ULONG *Addend)
{
    ULONG result;
    __asm {
        mov ecx, Addend
        mov eax, 1
        lock xadd dword ptr [ecx], eax
        inc eax
        mov result, eax
    }
    return result;
}

// x86 atomic read: a plain aligned 32-bit load (atomic on x86), with no
// memory barrier; ordering needs a locked operation (AtomicSet)
static __inline ULONG AtomicRead(volatile ULONG *Value)
{
    ULONG result;
    __asm {
        mov ecx, Value
        mov eax, dword ptr [ecx]
        mov result, eax
    }
    return result;
}

// x86 inline assembly for atomic compare-exchange
// Returns TRUE if exchange succeeded, FALSE otherwise
static __inline BOOLEAN AtomicCompareExchange(volatile ULONG *Destination, ULONG Exchange, ULONG Comparand)
{
    ULONG result;
    __asm {
        mov ecx, Destination
        mov edx, Exchange
        mov eax, Comparand
        lock cmpxchg dword ptr [ecx], edx
        setz al
        movzx eax, al
        mov result, eax
    }
    return (BOOLEAN)result;
}

// x86 inline assembly for atomic write (with memory barrier)
// return previous value of *Destination
static __inline ULONG AtomicSet(volatile ULONG *Destination, ULONG Value)
{
    __asm {
        mov ecx, Destination
        mov eax, Value
        lock xchg dword ptr [ecx], eax
    }
}

#elif defined(_M_AMD64)

// amd64: the compiler has no inline assembler here, so use the interlocked
// compiler intrinsics that miniport.h declares (#pragma intrinsic) for this
// architecture. They compile to the same locked instructions as the x86
// branch above and import nothing from the kernel. Semantics match the x86
// versions exactly (stor98 phase 5 / docs/PLAN.md 0.1).

// Returns the NEW value (after increment)
static __inline ULONG AtomicIncrement(volatile ULONG *Addend)
{
    return (ULONG)_InterlockedIncrement((volatile LONG *)Addend);
}

// Plain volatile read, as on x86 (aligned 32-bit loads are atomic), with no
// memory barrier
static __inline ULONG AtomicRead(volatile ULONG *Value)
{
    return *Value;
}

// Returns TRUE if exchange succeeded, FALSE otherwise
static __inline BOOLEAN AtomicCompareExchange(volatile ULONG *Destination, ULONG Exchange, ULONG Comparand)
{
    return (BOOLEAN)((ULONG)_InterlockedCompareExchange((volatile LONG *)Destination,
                                                       (LONG)Exchange, (LONG)Comparand) == Comparand);
}

// return previous value of *Destination
static __inline ULONG AtomicSet(volatile ULONG *Destination, ULONG Value)
{
    return (ULONG)_InterlockedExchange((volatile LONG *)Destination, (LONG)Value);
}

#elif defined(_M_ALPHA)

// Alpha AXP atomic operations implemented in assembly (atomic_alpha.s)
// MSVC for Alpha doesn't support __asm blocks, so we use external assembly

#ifdef __cplusplus
extern "C" {
#endif

// Atomic operations implemented in atomic_alpha.s
ULONG AtomicIncrement(volatile ULONG *Addend);
ULONG AtomicRead(volatile ULONG *Value);
BOOLEAN AtomicCompareExchange(volatile ULONG *Destination, ULONG Exchange, ULONG Comparand);
VOID AtomicSet(volatile ULONG *Destination, ULONG Value);

#ifdef __cplusplus
}
#endif

#else
#error "Unsupported architecture - need atomic operations for x86, amd64 or Alpha AXP"
#endif

#endif // _ATOMIC_H_
