// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// prprun.h - reuse of one physically contiguous run the port stated, for
// the PRP entries after it (engine.c, EngMapCaller; design record 30
// section 13.6, C2; xhci98 3.4.2.0). Pure arithmetic with no port or OS
// call, so the host unit test (tests/unit/prprun_ut.c) includes it
// unchanged.
//
// ScsiPortGetPhysicalAddress answers with the physical address of a byte
// of the caller's buffer and, in Length, how many bytes from there are
// physically contiguous. EngMapCaller asks for the rest of the child and
// clamps the run it keeps to that (PrpRunClamp), whatever the port answers,
// so the run lies inside the child. A later PRP entry whose span lies wholly inside that run has its
// physical address by addition, without another call; anything else (the
// span leaves the run, the run is shorter than the span, the addition would
// overflow) calls the port as before. The caller checks a derived entry
// exactly as a called one (PrpLaterOk, EngSpanOk with the run bytes left).
//

#ifndef _STOR98_PRPRUN_H_
#define _STOR98_PRPRUN_H_

// The run last stated by the port: RunOff the child offset asked for,
// RunPa its physical address, RunLen the contiguous bytes from there
// (already clamped by the caller to the rest of the child). Off and Need
// describe the next span (child offset, bytes). TRUE: the span lies wholly
// inside the run; *Pa is its physical address and *Avail the run's bytes
// from it (at least Need). FALSE: call the port.
static BOOLEAN PrpRunDerive(ULONG RunOff, ULONGLONG RunPa, ULONG RunLen, ULONG Off, ULONG Need,
                            ULONGLONG *Pa, ULONG *Avail)
{
    ULONG delta;

    if (RunLen == 0 || RunPa == 0 || Need == 0 || Off < RunOff) {
        return FALSE;
    }
    delta = Off - RunOff;
    if (delta >= RunLen || RunLen - delta < Need) {
        return FALSE;
    }
    if (RunPa > (ULONGLONG)-1 - delta) {
        return FALSE;
    }
    *Pa = RunPa + delta;
    *Avail = RunLen - delta;
    return TRUE;
}

// The run to remember after a call at child offset Off that returned Len
// bytes of contiguous memory, for a child of ChildBytes: the length clamped
// to the rest of the child (a port answering more than was asked is not
// trusted beyond it).
static ULONG PrpRunClamp(ULONG Off, ULONG Len, ULONG ChildBytes)
{
    ULONG rest = (Off < ChildBytes) ? ChildBytes - Off : 0;

    return Len < rest ? Len : rest;
}

#endif // _STOR98_PRPRUN_H_
