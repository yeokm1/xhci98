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
// utils.h - Utility functions for NVMe2K driver
//

#ifndef _NVME2K_UTILS_H_
#define _NVME2K_UTILS_H_

// Forward declare to avoid circular dependency if nvme2k.h includes utils.h first
struct _NVME_SMART_INFO;
struct _ATA_SMART_DATA;


//
// Memory helper macros (not available in miniport.h)
//
#define RtlZeroMemory(Destination, Length) do { \
    PUCHAR _Dest = (PUCHAR)(Destination); \
    ULONG _Len = (Length); \
    while (_Len--) *_Dest++ = 0; \
} while (0)

#define RtlCopyMemory(Destination, Source, Length) do { \
    PUCHAR _Dest = (PUCHAR)(Destination); \
    PUCHAR _Src = (PUCHAR)(Source); \
    ULONG _Len = (Length); \
    while (_Len--) *_Dest++ = *_Src++; \
} while (0)

//
// Byte manipulation helper macros for handling unaligned little-endian data.
// These are safe for architectures like Alpha AXP that fault on unaligned access.
//
#define READ_USHORT(p) ((USHORT)((p)[0] | ((USHORT)(p)[1] << 8)))
#define READ_ULONG(p)  ((ULONG)((p)[0] | ((ULONG)(p)[1] << 8) | ((ULONG)(p)[2] << 16) | ((ULONG)(p)[3] << 24)))
#define READ_ULONGLONG(p) ((ULONGLONG)READ_ULONG(p) | ((ULONGLONG)READ_ULONG((p)+4) << 32))

#define WRITE_USHORT(p, val) do { (p)[0] = (UCHAR)(val); (p)[1] = (UCHAR)((val) >> 8); } while(0)
#define WRITE_ULONG(p, val) do { (p)[0] = (UCHAR)(val); (p)[1] = (UCHAR)((val) >> 8); \
                                  (p)[2] = (UCHAR)((val) >> 16); (p)[3] = (UCHAR)((val) >> 24); } while(0)


ULONG RtlCompareMemory(IN CONST VOID *Source1, IN CONST VOID *Source2, IN ULONG Length);
VOID NvmeSmartToAtaSmart(IN struct _NVME_SMART_INFO *NvmeSmart, OUT struct _ATA_SMART_DATA *AtaSmart);
ULONG log2(ULONG n);

VOID BSWAPCopy(OUT PULARGE_INTEGER Target, IN PULARGE_INTEGER Source);

#ifdef i386

ULONG static __inline BSWAP(ULONG Value)
{
    __asm mov eax, Value
    __asm bswap eax
}

#else

ULONG BSWAP(ULONG Value);

#endif

#ifdef NVME2K_W9X

#define VXDINLINE static __inline

#define	VMM_DEVICE_ID 0x0001

#define	MPL_NonCached			0x00000000
#define	MPL_HardwareCoherentCached	0x00000001
#define	MPL_FrameBufferCached		0x00000002
#define	MPL_Cached			0x00000004

PVOID _cdecl _MapPhysToLinear(ULONG PhysAddr, ULONG nBytes, ULONG flags);

#define PTE_BASE 0xFF800000
#define PDE_BASE 0xFFBFE000

/* Convert an address to a corresponding PTE */
#define MiAddressToPTE(x) \
   ((PULONG)(((((ULONG)(x)) >> 12) << 2) + PTE_BASE))

/* Convert an address to a corresponding PDE */
#define MiAddressToPDE(x) \
   ((PULONG)(((((ULONG)(x)) >> 22) << 2) + PDE_BASE))

#define GetPTE(x) (*MiAddressToPTE(x))

#define GetPDE(x) (*MiAddressToPDE(x))

/* PageCommit flags */
#define PC_FIXED    0x00000008	/* pages are permanently locked */
#define PC_LOCKED   0x00000080	/* pages are made present and locked*/
#define PC_LOCKEDIFDP	0x00000100  /* pages are locked if swap via DOS */
#define PC_WRITEABLE	0x00020000  /* make the pages writeable */
#define PC_USER     0x00040000	/* make the pages ring 3 accessible */
#define PC_INCR     0x40000000	/* increment "pagerdata" each page */
#define PC_PRESENT  0x80000000	/* make pages initially present */
#define PC_STATIC   0x20000000	/* allow commit in PR_STATIC object */
#define PC_DIRTY    0x08000000	/* make pages initially dirty */
#define PC_CACHEDIS 0x00100000  /* Allocate uncached pages - new for WDM */
#define PC_CACHEWT  0x00080000  /* Allocate write through cache pages - new for WDM */
#define PC_PAGEFLUSH 0x00008000 /* Touch device mapped pages on alloc - new for WDM */

ULONG _cdecl _PageModifyPermissions(ULONG page, ULONG npages,
    ULONG permand, ULONG permor);

//Present bit
#define P_PRESBIT            0
#define P_PRES               (1 << P_PRESBIT)

//Cache bits
#define P_WRITETHROUGHBIT    3
#define P_WRITETHROUGH       (1 << P_WRITETHROUGHBIT)
#define P_CACHEDISABLEDBIT   4
#define P_CACHEDISABLED      (1 << P_CACHEDISABLEDBIT)
//4K pages only (PTE)
#define P_PATBIT             7
#define P_PAT                (1 << P_PATBIT)

#define	PTE_CACHE_WB 0
#define	PTE_CACHE_WT P_WRITETHROUGH            //if PAT unsupported or MTRR.VXD is not loaded
#define	PTE_CACHE_WC P_WRITETHROUGH            //if PAT supported and MTRR.VXD loaded
#define	PTE_CACHE_UC_MINUS P_CACHEDISABLED
#define	PTE_CACHE_UC (P_WRITETHROUGH | P_CACHEDISABLED)

#define	PTE_CACHE_MASK (P_WRITETHROUGH | P_CACHEDISABLED | P_PAT)

//PTEs applicable ONLY
//Return value is processed pages count (i.e. present pages)
ULONG ApplyCacheMode(PUCHAR Addr, ULONG nPages, ULONG CacheMode);

#define POINTER_STR "%08X"
#define INT64_STR "(%u + %u * 4Gi)"

#else //NVME2K_W9X

#define POINTER_STR "%p"
#define INT64_STR "%I64u"

#endif //NVME2K_W9X

#ifdef NVME2K_DBG

#ifdef NVME2K_W9X

#define	_Debug_Printf_Service_Ordinal 0x012D

VOID VXDINLINE _cdecl ScsiDebugPrint_W9x(ULONG DebugPrintLevel,char *pszfmt, ...)
{
    __asm lea  eax,(pszfmt + 4)
    __asm push eax
    __asm push pszfmt

    //VxDCall(_Debug_Printf_Service)
    __asm int 0x20
    //_Debug_Printf_Service_Ordinal
    __asm _emit (_Debug_Printf_Service_Ordinal >> 0) & 0xFF
    __asm _emit (_Debug_Printf_Service_Ordinal >> 8) & 0xFF
    //VMM_DEVICE_ID
    __asm _emit (VMM_DEVICE_ID >> 0) & 0xFF
    __asm _emit (VMM_DEVICE_ID >> 8) & 0xFF

    __asm add esp, 2*4
}

#if defined(STOR98_G9)
// Gate G9 test build: to the QEMU debug console instead (g9dbg.h)
#define ScsiDebugPrint G9DebugPrint
#else
#define ScsiDebugPrint ScsiDebugPrint_W9x
#endif

#endif //NVME2K_W9X

#else //NVME2K_DBG

//Fail if not within #ifdef NVME2K_DBG* #endif
#define ScsiDebugPrint ScsiDebugPrint_DisallowedHere

#endif //NVME2K_DBG

#endif // _NVME2K_UTILS_H_