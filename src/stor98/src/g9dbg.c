// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
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
// g9dbg.c - gate G9 instrumentation, Windows 9x test builds only (g9dbg.h).

#include "nvme2k.h"
#include "g9dbg.h"

#if defined(STOR98_G9) && defined(NVME2K_W9X)

#include <stdarg.h>

static ULONG G9Seq;

static VOID G9OutB(USHORT Port, UCHAR Value)
{
    __asm {
        mov dx, Port
        mov al, Value
        out dx, al
    }
}

static VOID G9OutD(USHORT Port, ULONG Value)
{
    __asm {
        mov dx, Port
        mov eax, Value
        out dx, eax
    }
}

static VOID G9Rdtsc(PULONG Lo, PULONG Hi)
{
    ULONG lo, hi;
    __asm {
        _emit 0x0F
        _emit 0x31
        mov lo, eax
        mov hi, edx
    }
    *Lo = lo;
    *Hi = hi;
}

// Coarse tick for deadlines: TSC bits 20..51 (about 1 ms at 1 GHz; only
// differences of it are used, and only in the test build).
ULONG G9Tick(VOID)
{
    ULONG lo, hi;
    G9Rdtsc(&lo, &hi);
    return (lo >> 20) | (hi << 12);
}

static VOID G9PutC(char c)
{
    G9OutB(G9_DEBUGCON_PORT, (UCHAR)c);
}

static VOID G9PutS(const char *s, int prec)
{
    if (!s) {
        s = "(null)";
    }
    while (*s && prec != 0) {
        G9PutC(*s++);
        if (prec > 0) {
            prec--;
        }
    }
}

static VOID G9PutNum(ULONG v, ULONG base, int width, BOOLEAN zero, BOOLEAN upper, BOOLEAN neg)
{
    char buf[12];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    do {
        buf[n++] = digits[v % base];
        v /= base;
    } while (v && n < (int)sizeof(buf));
    if (neg) {
        width--;
    }
    if (neg && zero) {
        G9PutC('-');
    }
    while (width > n) {
        G9PutC((char)(zero ? '0' : ' '));
        width--;
    }
    if (neg && !zero) {
        G9PutC('-');
    }
    while (n) {
        G9PutC(buf[--n]);
    }
}

static VOID G9VFormat(const char *f, va_list ap)
{
    while (*f) {
        int width = 0, prec = -1;
        BOOLEAN zero = FALSE;

        if (*f != '%') {
            G9PutC(*f++);
            continue;
        }
        f++;
        if (*f == '%') {
            G9PutC(*f++);
            continue;
        }
        if (*f == '-') {
            f++;
        }
        if (*f == '0') {
            zero = TRUE;
            f++;
        }
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f++ - '0');
        }
        if (*f == '.') {
            f++;
            prec = 0;
            while (*f >= '0' && *f <= '9') {
                prec = prec * 10 + (*f++ - '0');
            }
        }
        while (*f == 'l' || *f == 'h') {
            f++;
        }
        switch (*f) {
        case 'd':
        case 'i': {
            LONG v = va_arg(ap, LONG);
            G9PutNum(v < 0 ? (ULONG)-v : (ULONG)v, 10, width, zero, FALSE, (BOOLEAN)(v < 0 ? TRUE : FALSE));
            break;
        }
        case 'u':
            G9PutNum(va_arg(ap, ULONG), 10, width, zero, FALSE, FALSE);
            break;
        case 'x':
            G9PutNum(va_arg(ap, ULONG), 16, width, zero, FALSE, FALSE);
            break;
        case 'X':
        case 'p':
            G9PutNum(va_arg(ap, ULONG), 16, *f == 'p' ? 8 : width, (BOOLEAN)(zero || *f == 'p'), TRUE, FALSE);
            break;
        case 'c':
            G9PutC((char)va_arg(ap, int));
            break;
        case 's':
            G9PutS(va_arg(ap, const char *), prec);
            break;
        case 0:
            return;
        default:
            G9PutC('%');
            G9PutC(*f);
            break;
        }
        f++;
    }
}

static VOID G9Prefix(VOID)
{
    ULONG lo, hi;

    G9Rdtsc(&lo, &hi);
    G9PutS("[stor98 ", -1);
    G9PutNum(hi, 16, 8, TRUE, FALSE, FALSE);
    G9PutNum(lo, 16, 8, TRUE, FALSE, FALSE);
    G9PutC(' ');
    G9PutNum(G9Seq++, 10, 5, TRUE, FALSE, FALSE);
    G9PutS("] ", -1);
}

VOID G9Log(const char *Fmt, ...)
{
    va_list ap;

    G9Prefix();
    va_start(ap, Fmt);
    G9VFormat(Fmt, ap);
    va_end(ap);
    G9PutC('\n');
}

// ScsiDebugPrint replacement for the inherited NVME2K_DBG messages; they
// already end in a newline.
VOID G9DebugPrint(ULONG Level, const char *Fmt, ...)
{
    va_list ap;

    UNREFERENCED_PARAMETER(Level);
    G9Prefix();
    G9PutS("dbg: ", -1);
    va_start(ap, Fmt);
    G9VFormat(Fmt, ap);
    va_end(ap);
}

// usb4-debug-port: the argument register first, then the stage code. The
// write of the stage code is what usb4-emu traces (and fires at=stage rules on).
VOID G9Stage(ULONG Code, ULONG Arg)
{
    G9OutD(G9_DBGPORT_ARG, Arg);
    G9OutD(G9_DBGPORT_STAGE, Code);
}

// Clock correlation: the TSC as a usb4_dbg_marker plus a log line with the
// same value (docs/TESTING.md, Measurement definitions).
VOID G9Marker(VOID)
{
    ULONG lo, hi;

    G9Rdtsc(&lo, &hi);
    G9OutD(G9_DBGPORT_MARK_LO, lo);
    G9OutD(G9_DBGPORT_MARK_HI, hi);
    G9Log("marker tsc=%08X%08X", hi, lo);
}


// ---------------------------------------------------------------------------
// Observation hooks (STOR98_G9)
// ---------------------------------------------------------------------------

static ULONG G9Instances;

VOID G9NewInstance(PHW_DEVICE_EXTENSION DevExt)
{
    DevExt->G9Instance = ++G9Instances;
}

VOID G9Summary(PHW_DEVICE_EXTENSION DevExt, const char *Why)
{
    G9Log("summary(%s) inst=%u srbs=%u rw=%u tagged=%u untagged=%u maxdepth=%u depth=%u "
          "nontagged=%08X nullbuf=%u lowbuf=%u completions=%u timer=%u gone=%u fastfail=%u",
          Why, DevExt->G9Instance, DevExt->G9SrbCount, DevExt->G9RwCount, DevExt->G9Tagged,
          DevExt->G9Untagged, DevExt->MaxQueueDepthReached, DevExt->CurrentQueueDepth,
          (ULONG)DevExt->NonTaggedInFlight, DevExt->G9NullBuffer, DevExt->G9LowBuffer,
          DevExt->G9Completions, DevExt->G9TimerCalls, (ULONG)DevExt->G9Gone, DevExt->G9FastFail);
    G9Log("summary(%s) tags %08X %08X %08X %08X %08X %08X %08X %08X", Why,
          DevExt->G9TagMask[0], DevExt->G9TagMask[1], DevExt->G9TagMask[2], DevExt->G9TagMask[3],
          DevExt->G9TagMask[4], DevExt->G9TagMask[5], DevExt->G9TagMask[6], DevExt->G9TagMask[7]);
    G9Stage(G9S_SUMMARY, DevExt->G9RwCount);
}

// R4-2: does ScsiPortGetBusData work from this (runtime) callback on 9x?
VOID G9ProbeConfig(PHW_DEVICE_EXTENSION DevExt, const char *Where)
{
    ULONG buf[2];
    ULONG got;

    buf[0] = buf[1] = 0xDEADBEEF;
    got = ScsiPortGetBusData(DevExt, PCIConfiguration, DevExt->BusNumber, DevExt->SlotNumber,
                             buf, 8);
    G9Log("config probe in %s: ScsiPortGetBusData(bus %u slot %u, 8) = %u, id=%08X cmdsts=%08X",
          Where, DevExt->BusNumber, DevExt->SlotNumber, got, buf[0], buf[1]);
}

#if defined(STOR98_G9_DDBPOKE)
static VOID G9DdbPoke(PHW_DEVICE_EXTENSION DevExt);
#endif

// Called by HwStartIo for every SRB addressed to our LUN.
VOID G9StartIoLog(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR op = Srb->Cdb[0];
    BOOLEAN rw = (BOOLEAN)((Srb->Function == SRB_FUNCTION_EXECUTE_SCSI &&
        (op == 0x08 || op == 0x28 || op == 0x88 || op == 0x0A || op == 0x2A || op == 0x8A)) ? TRUE : FALSE);
    BOOLEAN tagged = (BOOLEAN)(((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) &&
        Srb->QueueTag != SP_UNTAGGED) ? TRUE : FALSE);

    DevExt->G9SrbCount++;
    G9Stage(G9S_STARTIO, ((ULONG)Srb->Function << 24) | ((ULONG)op << 16) |
            ((ULONG)Srb->QueueTag << 8) | (Srb->SrbFlags & 0xFF));
    if (rw) {
        DevExt->G9RwCount++;
        if (tagged) {
            DevExt->G9Tagged++;
            DevExt->G9TagMask[(Srb->QueueTag >> 5) & 7] |= 1UL << (Srb->QueueTag & 31);
        } else {
            DevExt->G9Untagged++;
        }
        if (Srb->DataBuffer == NULL) {
            DevExt->G9NullBuffer++;
        } else if ((ULONG)Srb->DataBuffer < 0x80000000UL) {
            DevExt->G9LowBuffer++;
        }
#if defined(STOR98_G9_DDBPOKE)
        G9DdbPoke(DevExt);
#endif
        if (DevExt->G9RwCount <= 40 || DevExt->G9Gone) {
            G9Log("startio rw op=%02X tag=%02X qa=%02X flags=%08X buf=%08X len=%u depth=%u cdb=%02X%02X%02X%02X%02X%02X",
                  op, Srb->QueueTag, Srb->QueueAction, Srb->SrbFlags, (ULONG)Srb->DataBuffer,
                  Srb->DataTransferLength, DevExt->CurrentQueueDepth,
                  Srb->Cdb[2], Srb->Cdb[3], Srb->Cdb[4], Srb->Cdb[5], Srb->Cdb[7], Srb->Cdb[8]);
        }
        if ((DevExt->G9RwCount & 1023) == 0) {
            G9Summary(DevExt, "periodic");
        }
    } else {
        G9Log("startio func=%02X op=%02X tag=%02X qa=%02X flags=%08X buf=%08X len=%u timeout=%u depth=%u",
              Srb->Function, op, Srb->QueueTag, Srb->QueueAction, Srb->SrbFlags,
              (ULONG)Srb->DataBuffer, Srb->DataTransferLength, Srb->TimeOutValue,
              DevExt->CurrentQueueDepth);
        if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI && op == 0x12 && DevExt->G9SrbCount < 8) {
            G9ProbeConfig(DevExt, "HwStartIo");
        }
    }
}

VOID G9CompleteLog(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb, USHORT Cid, USHORT Status)
{
    DevExt->G9Completions++;
    G9Stage(G9S_COMPLETE, ((ULONG)Srb->SrbStatus << 24) | ((ULONG)Srb->Cdb[0] << 16) |
            ((ULONG)Srb->QueueTag << 8));
    if (DevExt->G9Completions <= 40 || Srb->SrbStatus != SRB_STATUS_SUCCESS || DevExt->G9Gone) {
        G9Log("complete cid=%04X nvme=%04X srb=%08X status=%02X op=%02X tag=%02X depth=%u",
              Cid, Status, (ULONG)Srb, Srb->SrbStatus, Srb->Cdb[0], Srb->QueueTag,
              DevExt->CurrentQueueDepth);
    }
}

#if defined(STOR98_G9_GONE)
// ---------------------------------------------------------------------------
// Minimal gone-state prototype (gate only; PLAN 0.10, 2.2, MD-1)
// ---------------------------------------------------------------------------

#define G9_GONE_DEADLINE_TICKS  (5000000 / FALLBACK_TIMER_PERIOD)   // 5 s

BOOLEAN G9CheckGone(PHW_DEVICE_EXTENSION DevExt, const char *Where)
{
    ULONG csts;

    if (DevExt->G9Gone) {
        return TRUE;
    }
    if (!DevExt->ControllerRegisters) {
        return FALSE;
    }
    csts = NvmeReadReg32(DevExt, NVME_REG_CSTS);
    if (csts != 0xFFFFFFFF) {
        return FALSE;
    }
    DevExt->G9Gone = TRUE;
    DevExt->G9GoneTicks = 0;
    G9Stage(G9S_GONE, DevExt->CurrentQueueDepth);
    G9Marker();
    G9Log("GONE detected in %s: CSTS all-ones, depth=%u nontagged=%08X admin=%u",
          Where, DevExt->CurrentQueueDepth, (ULONG)DevExt->NonTaggedInFlight,
          DevExt->CurrentAdminQueueDepth);
    G9ProbeConfig(DevExt, Where);
    return TRUE;
}

// HwStartIo: TRUE when the SRB was fast-failed and completed here.
BOOLEAN G9StartIoGone(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb)
{
    if (!DevExt->InitComplete) {
        return FALSE;
    }
    if (!G9CheckGone(DevExt, "HwStartIo")) {
        return FALSE;
    }
    DevExt->G9FastFail++;
    G9Stage(G9S_FASTFAIL, ((ULONG)Srb->Function << 24) | ((ULONG)Srb->Cdb[0] << 16));
    if (DevExt->G9FastFail <= 50 || (DevExt->G9FastFail & 255) == 0) {
        G9Log("fast-fail #%u func=%02X op=%02X tag=%02X -> NO_DEVICE",
              DevExt->G9FastFail, Srb->Function, Srb->Cdb[0], Srb->QueueTag);
    }
    Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
    Srb->ScsiStatus = 0;
    Srb->DataTransferLength = 0;
    ScsiPortNotification(RequestComplete, DevExt, Srb);
    ScsiPortNotification(NextRequest, DevExt, NULL);
    return TRUE;
}

// FallbackTimer: TRUE when the timer must stay armed.
BOOLEAN G9TimerGone(PHW_DEVICE_EXTENSION DevExt)
{
    BOOLEAN outstanding = (BOOLEAN)((DevExt->CurrentQueueDepth || DevExt->NonTaggedInFlight) ? TRUE : FALSE);

    if (!outstanding && !DevExt->G9Gone) {
        return FALSE;
    }
    if (!G9CheckGone(DevExt, "FallbackTimer")) {
        return outstanding;     // keep watching outstanding commands
    }
    if (DevExt->G9GoneCompleted || !outstanding) {
        return FALSE;
    }
    DevExt->G9GoneTicks++;
    if (DevExt->G9GoneTicks < G9_GONE_DEADLINE_TICKS) {
        return TRUE;
    }
    // Gone deadline without isolation evidence (MD-1): complete what we hold.
    G9Stage(G9S_GONE_DEADLINE, DevExt->CurrentQueueDepth);
    G9Marker();
    G9Log("GONE deadline: completing depth=%u nontagged=%08X with NO_DEVICE via ScsiPortCompleteRequest",
          DevExt->CurrentQueueDepth, (ULONG)DevExt->NonTaggedInFlight);
    DevExt->G9GoneCompleted = TRUE;
    ScsiPortCompleteRequest(DevExt, 0, SP_UNTAGGED, SP_UNTAGGED, SRB_STATUS_NO_DEVICE);
    DevExt->CurrentQueueDepth = 0;
    DevExt->NonTaggedInFlight = NULL;
    ScsiPortNotification(NextRequest, DevExt, NULL);
    G9Summary(DevExt, "gone-deadline");
    return FALSE;
}
#endif

#if defined(STOR98_G9_PROBE)
static VOID G9ProbeTimer(IN PVOID DeviceExtension)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

    G9Stage(G9S_PROBE_TIMER, G9Tick() - DevExt->G9ProbeArmed);
    G9Log("probe: RequestTimerCall callback ran, %u ticks after arming", G9Tick() - DevExt->G9ProbeArmed);
}

VOID G9ArmProbe(PHW_DEVICE_EXTENSION DevExt, const char *Where)
{
    DevExt->G9ProbeArmed = G9Tick();
    G9Log("probe: arming RequestTimerCall(10 ms) in %s", Where);
    ScsiPortNotification(RequestTimerCall, DevExt, G9ProbeTimer, 10000);
    G9Log("probe: RequestTimerCall returned in %s", Where);
}
#endif

#if defined(STOR98_G9_CACHES)
// Windows Me stop investigation (docs/gates/G9.md). SCSIPORT.PDR keeps the
// miniport's device extension inside the IOS DDB of the adapter, at DDB+0x8C
// (from its AEP handlers: HwAdapterState gets DDB+0x8C). Byte 0x11 of the DDB
// is DDB_ios_flags (Windows 98 DDK DDB.H). SCSIPORT calls HwAdapterState(FALSE)
// at AEP_UNINITIALIZE only when bit 0x04 is set there. This logs the DDB
// header read-only; level 5 also sets that bit once, at the first read.
#define G9_DDB_DEVEXT_OFFSET 0x8C

static PUCHAR G9Ddb(PHW_DEVICE_EXTENSION DevExt)
{
    return (PUCHAR)DevExt - G9_DDB_DEVEXT_OFFSET;
}

VOID G9DdbLog(PHW_DEVICE_EXTENSION DevExt, const char *Where)
{
    PULONG d = (PULONG)G9Ddb(DevExt);

    G9Log("ddb in %s: ddb=%08X phys=%08X next=%08X nextinit=%08X dcb=%08X "
          "buses=%02X iosflags=%02X sig=%04X dvt=%08X devnode=%08X flags34=%08X",
          Where, (ULONG)d, d[0], d[1], d[2], d[3], (ULONG)G9Ddb(DevExt)[0x10],
          (ULONG)G9Ddb(DevExt)[0x11], (ULONG)*(PUSHORT)(G9Ddb(DevExt) + 0x12),
          d[5], d[6], *(PULONG)(G9Ddb(DevExt) + 0x34));
}

#if defined(STOR98_G9_DDBPOKE)
static BOOLEAN G9DdbPoked;

static VOID G9DdbPoke(PHW_DEVICE_EXTENSION DevExt)
{
    PUCHAR ddb = G9Ddb(DevExt);

    if (G9DdbPoked) {
        return;
    }
    G9DdbPoked = TRUE;
    G9DdbLog(DevExt, "poke(before)");
    if (*(PULONG)(ddb + 0x0C) < 0xC0000000UL || *(PULONG)(ddb + 0x14) < 0xC0000000UL) {
        G9Log("ddb poke: DDB layout check failed, not poking");
        return;
    }
    ddb[0x11] |= 0x04;
    G9DdbLog(DevExt, "poke(after)");
}
#endif
#endif

#else

// Keeps the translation unit non-empty in builds without STOR98_G9.
typedef int G9DBG_UNUSED;

#endif
