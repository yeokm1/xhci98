/*
 * cm_oscos.c - the platform's USB4 handover on 98 SE and ME, the OS half
 * (roadmap-usb4.md task 46.12; design record 32 section 12.8): on an
 * ASM4242 host whose firmware hands USB4 over only through ACPI _OSC,
 * which these systems never run, the command the firmware's _OSC sends
 * is sent from the start, before ring 0, once per boot, only when the
 * firmware's own tables and code are a build read and name the upstream
 * port this file finds by its bus (section 12.8.4). The firmware is known
 * by its exact code, or by its canonical form (cm_oscc.c, section 12.8.10:
 * sent only with XhciCmAsm4242Structural 1); the tier is reported in the
 * status record (CmOscReport, section 12.8.11). cm_osc.c and cm_oscc.c are
 * the pure half.
 *
 * Reads: physical memory and ECAM through MmMapIoSpace, mapped for each
 * read and unmapped after, in dwords; configuration space through
 * HalGetBusData. Writes: the mailbox's two dwords alone, through ECAM.
 *
 * THE TABLES KEPT (task 46.21; record 32 section 12.8.14). What discovery
 * reads - the root pointer, the root table, each entry's header in the
 * MCFG pass, MCFG and the named SSDTs whole - is copied, as it is read,
 * from the buffer it was read into (cmOscKeep), and the copies made the
 * query device's firmware tables answer once the start's handover is
 * done (cmOscKept, cm_qacpi.c). No read is added for it, and nothing kept
 * changes what discovery decides, logs or sends.
 *
 * ON NT (record 32 section 12.8.15.17) the discovery, its reader and the
 * table checks are shared with the read path, B, C and D: cm_oscnt.c
 * calls CmOscNtScan and CmOscNtDiscover with the system's snapshot of the
 * memory it manages, every read whose pages that snapshot holds refused,
 * the BIOS data area and the EBDA never read, and nothing kept for
 * XHCITR. The root pointer from the BIOS data area, the inventory, the
 * served table, ECAM and the exchange through it, and the tables kept
 * stay 98 SE's and ME's (x86 only).
 *
 * IRQL: PASSIVE_LEVEL, the NHI's thread (CmNhiStartWork). The 98 SE and
 * ME half is x86 only: the x64 build has no 98 SE or ME, and
 * CmOscHandover does nothing there.
 */

#include "cm.h"
#include "cm_osc.h"
#include "cm_oscp.h"

#define CM_OSC_ASM_NHI      0x24251B21UL    /* vendor 15:0, device 31:16 */
#define CM_OSC_ASM_USP      0x24211B21UL
#define CM_OSC_ASM_DSP      0x24231B21UL
#define CM_OSC_HOSTS        4UL
#define CM_OSC_MAX_TABLES   64UL
#define CM_OSC_MAX_BYTES    0x100000UL
#define CM_OSC_SSDT_CAP     0x10000UL
#define CM_OSC_MCFG_CAP     0x1000UL
#define CM_OSC_WAIT_MS      1000UL
#define CM_OSC_SETTLE_MS    200UL
#define CM_OSC_REPLY_OK     0x00000100UL

#if defined(_X86_)

/* The upstream ports served this boot (12.8.2), reserved before any
 * mailbox access and never released. */
static KSPIN_LOCK cmOscLock;
static ULONG cmOscLockReady;
static ULONG cmOscServed[CM_OSC_HOSTS];     /* bdf + 1; 0 free           */
static ULONG cmOscSent[CM_OSC_HOSTS];       /* the handover written      */

VOID CmOscInit(VOID)
{
    if (!cmOscLockReady) {
        KeInitializeSpinLock(&cmOscLock);
        cmOscLockReady = 1;
    }
}

/* Reserves `bdf`'s entry: FALSE when it was served or no entry is free;
 * *slot the entry reserved. */
static BOOLEAN cmOscReserve(ULONG bdf, PULONG slot)
{
    KIRQL irql;
    ULONG i;
    ULONG free;
    BOOLEAN ok;

    KeAcquireSpinLock(&cmOscLock, &irql);
    free = CM_OSC_HOSTS;
    ok = TRUE;
    for (i = 0; i < CM_OSC_HOSTS; i++) {
        if (cmOscServed[i] == bdf + 1UL) {
            ok = FALSE;
        }
        if (cmOscServed[i] == 0 && free == CM_OSC_HOSTS) {
            free = i;
        }
    }
    if (ok && free < CM_OSC_HOSTS) {
        cmOscServed[free] = bdf + 1UL;
        *slot = free;
    } else {
        ok = FALSE;
    }
    KeReleaseSpinLock(&cmOscLock, irql);
    return ok;
}

/* The status record's handover word (record 32 section 12.8.11): the
 * tier of this start in bits 2:0, bit 3 the handover written this boot to
 * this host's upstream port (kept with its once-per-boot entry). */
ULONG CmOscReport(PCM_NHI nhi)
{
    KIRQL irql;
    ULONG v;
    ULONG i;

    if (IoIsWdmVersionAvailable(1, 0x10)) {
        return CmOscntReport(nhi);
    }
    v = nhi->OscTier & 7UL;
    /* 0 (the extension as allocated) or no port: none found yet. */
    if (!cmOscLockReady || nhi->OscUspBdf == 0 ||
        nhi->OscUspBdf == XHCI_TBT_NO_BDF) {
        return v;
    }
    KeAcquireSpinLock(&cmOscLock, &irql);
    for (i = 0; i < CM_OSC_HOSTS; i++) {
        if (cmOscServed[i] == nhi->OscUspBdf + 1UL && cmOscSent[i]) {
            v |= 8UL;
        }
    }
    KeReleaseSpinLock(&cmOscLock, irql);
    return v;
}

/* The written mark of entry `slot`. */
static VOID cmOscMarkSent(ULONG slot)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmOscLock, &irql);
    cmOscSent[slot] = 1;
    KeReleaseSpinLock(&cmOscLock, irql);
}

#endif /* _X86_ */

/* ---- the tables kept (task 46.21) ------------------------------------- */

/* The copies of one start's discovery: entry 0 the root pointer, 1 the
 * root table, 2 + n the root table's entry n. State and Outcome what the
 * start's handover ended with (CM_QA_S_*, the cm.osc.refused value). */
typedef struct _CM_OSC_CAP {
    ULONG Count;
    ULONG Flags;            /* CM_QA_F_CUT, CM_QA_F_NOMEM                 */
    ULONG Bytes;            /* bytes kept                                 */
    ULONG State;
    ULONG Outcome;
    CM_QA_ITEM Item[CM_QA_MAX_ENTRIES];
    UCHAR *Own[CM_QA_MAX_ENTRIES];
} CM_OSC_CAP;

/* `held` bytes at `bytes` kept as entry `idx` (`how` CM_QA_H_WHOLE or
 * CM_QA_H_HEADER), replacing what it held: a whole table past
 * CM_QA_DATA_MAX kept as its header, a copy with no memory as nothing.
 * A NULL cap keeps nothing. */
static VOID cmOscKeep(CM_OSC_CAP *cap, ULONG idx, ULONG sig, ULONG kind,
                      ULONG length, const UCHAR *bytes, ULONG held, ULONG how)
{
    CM_QA_ITEM *it;
    UCHAR *copy;
    ULONG i;

    if (cap == NULL || idx >= CM_QA_MAX_ENTRIES) {
        return;
    }
    it = &cap->Item[idx];
    if (cap->Own[idx] != NULL) {
        cap->Bytes -= it->Held;
        CmFree(cap->Own[idx]);
        cap->Own[idx] = NULL;
    }
    if (how == CM_QA_H_WHOLE && held > CM_QA_DATA_MAX - cap->Bytes &&
        (kind & 0xFFUL) != CM_QA_K_RSDP) {
        cap->Flags |= CM_QA_F_CUT;
        how = CM_QA_H_HEADER;
        held = CM_QA_TBL_HDR;
    }
    if (held > CM_QA_DATA_MAX - cap->Bytes) {
        cap->Flags |= CM_QA_F_CUT;
        how = CM_QA_H_NONE;
        held = 0;
    }
    copy = NULL;
    if (held != 0) {
        copy = (UCHAR *)CmAlloc(held);
        if (copy == NULL) {
            cap->Flags |= CM_QA_F_NOMEM;
            how = CM_QA_H_NONE;
            held = 0;
        } else {
            for (i = 0; i < held; i++) {
                copy[i] = bytes[i];
            }
        }
    }
    cap->Own[idx] = copy;
    cap->Bytes += held;
    it->Sig = sig;
    it->Kind = kind | (how << 8);
    it->Length = length;
    it->Data = copy;
    it->Held = held;
    if (idx + 1UL > cap->Count) {
        cap->Count = idx + 1UL;
    }
}

/* A root entry's header (36 bytes as read) kept as entry n. */
static VOID cmOscKeepHeader(CM_OSC_CAP *cap, ULONG n, const UCHAR *hdr,
                            const CM_OSC_HDR *h)
{
    cmOscKeep(cap, 2UL + n, h->Signature, CM_QA_K_ENTRY | (n << 16),
              h->Length, hdr, CM_QA_TBL_HDR, CM_QA_H_HEADER);
}

/* A root entry read whole and checked, kept as entry n. */
static VOID cmOscKeepWhole(CM_OSC_CAP *cap, ULONG n, const UCHAR *t,
                           const CM_OSC_HDR *h)
{
    cmOscKeep(cap, 2UL + n, h->Signature, CM_QA_K_ENTRY | (n << 16),
              h->Length, t, h->Length, CM_QA_H_WHOLE);
}

#if defined(_X86_)
/* The start's copies made the body the query device will serve: the
 * NHI's last body freed, this one in its place with its state. */
static VOID cmOscKept(PCM_NHI nhi, CM_OSC_CAP *cap)
{
    UCHAR *body;
    ULONG bytes;
    ULONG i;

    CmQueryForgetTables(nhi);
    if (cap == NULL) {
        nhi->QaState = CM_QA_S_NOMEM;
        return;
    }
    nhi->QaState = cap->State;
    nhi->QaOutcome = cap->Outcome;
    nhi->QaFlags = cap->Flags;
    /* Nothing held (every copy failed) is no table kept. */
    if (cap->State == CM_QA_S_KEPT && cap->Bytes == 0) {
        nhi->QaState = CM_QA_S_NOMEM;
    }
    if (nhi->QaState == CM_QA_S_KEPT) {
        bytes = CmQaBodyBytes(cap->Item, cap->Count);
        body = bytes != 0 ? (UCHAR *)CmAlloc(bytes) : NULL;
        if (body == NULL) {
            nhi->QaState = CM_QA_S_NOMEM;
        } else {
            nhi->QaBodyBytes = CmQaBody(body, bytes, cap->Item, cap->Count);
            nhi->QaCount = cap->Count;
            nhi->QaBody = body;
        }
    }
    for (i = 0; i < CM_QA_MAX_ENTRIES; i++) {
        if (cap->Own[i] != NULL) {
            CmFree(cap->Own[i]);
        }
    }
    CmFree(cap);
}

#endif /* _X86_ */

/* ---- physical memory -------------------------------------------------- */

/* On NT (record 32 section 12.8.15.17) the reader also refuses every read
 * whose pages the system's snapshot holds (Ranges, Count, counted in
 * Refused), and the BIOS areas are read only for the scan of E0000h to
 * FFFFFh. */
typedef struct _CM_OSC_RD {
    ULONG EcamLo;       /* ECAM's window, never read as memory          */
    ULONG EcamHi;
    ULONG Bytes;        /* table bytes read so far                       */
    ULONG Nt;
    const CM_OSCP_RANGE *Ranges;
    ULONG Count;
    ULONG Refused;
    ULONG McfgOnly;     /* NT's ECAM base (12.8.15.18): stop after MCFG   */
} CM_OSC_RD;

/* Whether `bytes` at `phys` lie within 12.8.3 step 2's bounds. */
static BOOLEAN cmOscBounds(const CM_OSC_RD *rd, ULONG phys, ULONG bytes)
{
    ULONG end;

    if (bytes == 0 || phys > 0xFFFFFFFFUL - bytes) {
        return FALSE;
    }
    end = phys + bytes;
    if (phys < 0x100000UL || end > 0xFEC00000UL) {
        return FALSE;
    }
    if (rd->EcamHi != 0 && phys < rd->EcamHi && end > rd->EcamLo) {
        return FALSE;
    }
    return TRUE;
}

/* `bytes` of physical memory at `phys` into `out`, byte by byte (each
 * byte the one asked for, no neighbour touched) through a mapping of the
 * pages it spans. `bios` admits the BIOS areas below 1 MB instead of
 * step 2's bounds. */
static BOOLEAN cmOscPhys(CM_OSC_RD *rd, ULONG phys, UCHAR *out, ULONG bytes,
                         BOOLEAN bios)
{
    PHYSICAL_ADDRESS pa;
    volatile UCHAR *va;
    ULONG base;
    ULONG span;
    ULONG i;

    if (bios) {
        if (bytes == 0 || phys > 0x100000UL || bytes > 0x100000UL - phys) {
            return FALSE;
        }
    } else if (!cmOscBounds(rd, phys, bytes)) {
        return FALSE;
    }
    /* On NT a refusal is terminal (12.8.15.17): no read after it. */
    if (rd->Nt && rd->Refused != 0) {
        return FALSE;
    }
    if (rd->Nt && ((bios && phys < 0xE0000UL) ||
                   CmOscpRangeHit(rd->Ranges, rd->Count, phys, bytes))) {
        rd->Refused++;
        return FALSE;
    }
    base = phys & ~0xFFFUL;
    span = ((phys - base) + bytes + 0xFFFUL) & ~0xFFFUL;
    pa.QuadPart = (LONGLONG)base;
    va = (volatile UCHAR *)MmMapIoSpace(pa, span, MmNonCached);
    if (va == NULL) {
        /* On NT a mapping that fails is a refusal too, terminal. */
        if (rd->Nt) {
            rd->Refused++;
        }
        return FALSE;
    }
    for (i = 0; i < bytes; i++) {
        out[i] = va[(phys - base) + i];
    }
    MmUnmapIoSpace((PVOID)va, span);
    return TRUE;
}

/* `base` + `off` without wrapping: FALSE when it would. */
static BOOLEAN cmOscAdd(ULONG base, ULONG off, PULONG out)
{
    if (base > 0xFFFFFFFFUL - off) {
        return FALSE;
    }
    *out = base + off;
    return TRUE;
}

/* A byte, a dword at `base` + `off` (checked), in the order the firmware
 * reads them. */
static BOOLEAN cmOscByte(CM_OSC_RD *rd, ULONG base, ULONG off, PUCHAR out)
{
    ULONG at;

    return (BOOLEAN)(cmOscAdd(base, off, &at) &&
                     cmOscPhys(rd, at, out, 1, FALSE));
}

static BOOLEAN cmOscDword(CM_OSC_RD *rd, ULONG base, ULONG off, PULONG out)
{
    UCHAR b[4];
    ULONG at;

    if (!cmOscAdd(base, off, &at) || !cmOscPhys(rd, at, b, 4, FALSE)) {
        return FALSE;
    }
    *out = (ULONG)b[0] | ((ULONG)b[1] << 8) | ((ULONG)b[2] << 16) |
           ((ULONG)b[3] << 24);
    return TRUE;
}

#if defined(_X86_)

/* ---- configuration space ---------------------------------------------- */

#define CM_OSC_CFG_OK     0UL
#define CM_OSC_CFG_ABSENT 1UL   /* no function: two bytes, all ones     */
#define CM_OSC_CFG_NO_BUS 2UL   /* the HAL knows no such bus            */
#define CM_OSC_CFG_FAIL   3UL   /* a short read                         */

static ULONG cmOscCfgEx(ULONG bdf, ULONG offset, PULONG value)
{
    UCHAR buffer[256];
    ULONG got;

    if ((offset & 3UL) != 0 || offset > 252UL) {
        return CM_OSC_CFG_FAIL;
    }
    got = HalGetBusData(PCIConfiguration, XHCI_TBT_BDF_BUS(bdf),
                        ((bdf >> 3) & 0x1FUL) | ((bdf & 7UL) << 5), buffer,
                        offset + 4UL);
    if (got == 0) {
        return CM_OSC_CFG_NO_BUS;
    }
    if (got == 2) {
        return CM_OSC_CFG_ABSENT;
    }
    if (got < offset + 4UL) {
        return CM_OSC_CFG_FAIL;
    }
    *value = (ULONG)buffer[offset] | ((ULONG)buffer[offset + 1] << 8) |
             ((ULONG)buffer[offset + 2] << 16) |
             ((ULONG)buffer[offset + 3] << 24);
    return CM_OSC_CFG_OK;
}

static BOOLEAN cmOscCfg(ULONG bdf, ULONG offset, PULONG value)
{
    return (BOOLEAN)(cmOscCfgEx(bdf, offset, value) == CM_OSC_CFG_OK);
}

/*
 * One pass over every bus, device and function (12.8.3 step 1), calling
 * `visit` for each function present with its id and header dword: FALSE
 * when a read failed (the inventory is incomplete and decides nothing).
 */
typedef VOID (*CM_OSC_VISIT)(PVOID ctx, ULONG bdf, ULONG id);

static BOOLEAN cmOscInventory(CM_OSC_VISIT visit, PVOID ctx)
{
    ULONG b;
    ULONG dev;
    ULONG fn;
    ULONG v;
    ULONG hdr;
    ULONG r;

    for (b = 0; b < 256UL; b++) {
        for (dev = 0; dev < 32UL; dev++) {
            for (fn = 0; fn < 8UL; fn++) {
                r = cmOscCfgEx(XHCI_TBT_BDF(b, dev, fn), 0, &v);
                if (r == CM_OSC_CFG_NO_BUS) {
                    dev = 32UL;
                    break;
                }
                if (r == CM_OSC_CFG_FAIL) {
                    return FALSE;
                }
                if (r == CM_OSC_CFG_ABSENT || v == 0xFFFFFFFFUL ||
                    (v & 0xFFFFUL) == 0xFFFFUL) {
                    if (fn == 0) {
                        break;      /* no function 0: no device */
                    }
                    continue;
                }
                visit(ctx, XHCI_TBT_BDF(b, dev, fn), v);
                if (fn == 0) {
                    if (!cmOscCfg(XHCI_TBT_BDF(b, dev, 0), 0x0C, &hdr)) {
                        return FALSE;
                    }
                    if (((hdr >> 16) & 0x80UL) == 0) {
                        break;      /* not multi-function */
                    }
                }
            }
        }
    }
    return TRUE;
}

/* The inventory's state for the parent of a bus, and for an id. */
typedef struct _CM_OSC_FIND {
    ULONG Bus;          /* the secondary bus wanted (parent search)     */
    ULONG Id;           /* the id wanted (id search)                     */
    ULONG Found;
    ULONG At;
    ULONG AtId;
    BOOLEAN Bad;
} CM_OSC_FIND;

static VOID cmOscVisitParent(PVOID ctx, ULONG bdf, ULONG id)
{
    CM_OSC_FIND *f;
    ULONG hdr;
    ULONG buses;

    f = (CM_OSC_FIND *)ctx;
    if (!cmOscCfg(bdf, 0x0C, &hdr)) {
        f->Bad = TRUE;
        return;
    }
    if (((hdr >> 16) & 0x7FUL) != 1UL) {
        return;
    }
    if (!cmOscCfg(bdf, 0x18, &buses)) {
        f->Bad = TRUE;
        return;
    }
    if (((buses >> 8) & 0xFFUL) == f->Bus) {
        f->Found++;
        f->At = bdf;
        f->AtId = id;
        if (((buses >> 16) & 0xFFUL) < f->Bus) {
            f->Bad = TRUE;
        }
    }
}

static VOID cmOscVisitId(PVOID ctx, ULONG bdf, ULONG id)
{
    CM_OSC_FIND *f;

    f = (CM_OSC_FIND *)ctx;
    if (id == f->Id) {
        f->Found++;
        f->At = bdf;
    }
}

/* The one type 1 function whose secondary bus is `bus`, its id in *id
 * (12.8.3 step 1): XHCI_TBT_NO_BDF for none, two, or an incomplete
 * inventory. */
static ULONG cmOscParentOf(ULONG bus, PULONG id)
{
    CM_OSC_FIND f;

    RtlZeroMemory(&f, sizeof(f));
    f.Bus = bus;
    if (!cmOscInventory(cmOscVisitParent, &f) || f.Bad || f.Found != 1UL) {
        return XHCI_TBT_NO_BDF;
    }
    *id = f.AtId;
    return f.At;
}

/* A capability of `bdf` by id, in configuration space: its offset or 0. */
static ULONG cmOscCap(ULONG bdf, ULONG want)
{
    ULONG v;
    ULONG at;
    ULONG i;

    if (!cmOscCfg(bdf, 0x34, &v)) {
        return 0;
    }
    at = v & 0xFFUL;
    for (i = 0; i < 48UL && at >= 0x40UL && (at & 3UL) == 0; i++) {
        if (!cmOscCfg(bdf, at, &v)) {
            return 0;
        }
        if ((v & 0xFFUL) == want) {
            return at;
        }
        at = (v >> 8) & 0xFFUL;
    }
    return 0;
}

/* ---- ECAM --------------------------------------------------------------- */

typedef struct _CM_OSC_PAGE {
    PUCHAR Va;
} CM_OSC_PAGE;

static BOOLEAN cmOscMapPage(ULONG base, ULONG busEnd, ULONG bdf,
                            CM_OSC_PAGE *pg)
{
    PHYSICAL_ADDRESS pa;
    ULONG phys;

    if (!CmOscEcam(base, busEnd, bdf, 0, &phys)) {
        return FALSE;
    }
    pa.QuadPart = (LONGLONG)phys;
    pg->Va = (PUCHAR)MmMapIoSpace(pa, 0x1000, MmNonCached);
    return (BOOLEAN)(pg->Va != NULL);
}

static ULONG cmOscRd(CM_OSC_PAGE *pg, ULONG off)
{
    return READ_REGISTER_ULONG((PULONG)(pg->Va + off));
}

static VOID cmOscWr(CM_OSC_PAGE *pg, ULONG off, ULONG v)
{
    WRITE_REGISTER_ULONG((PULONG)(pg->Va + off), v);
}

/* One exchange (12.8.5 steps 1 to 3): the command written, its
 * acceptance and the reply waited for, each within CM_OSC_WAIT_MS by the
 * thread's clock and by a count of 1 ms stalls. Returns the reply, or
 * 0xFFFFFFFF (gone), 0xFFFFFFFE (not accepted), 0xFFFFFFFD (no reply). */
static ULONG cmOscExchange(PCM_NHI nhi, CM_OSC_PAGE *pg, ULONG cmd)
{
    ULONG start;
    ULONG v;
    ULONG i;

    cmOscWr(pg, CM_OSC_MB_CMD, cmd);
    start = nhi->Host.NowMs(nhi->Host.Context);
    for (i = 0;; i++) {
        v = cmOscRd(pg, CM_OSC_MB_CMD);
        if (v == 0xFFFFFFFFUL) {
            return 0xFFFFFFFFUL;
        }
        if (!CM_OSC_MB_BUSY(v)) {
            break;
        }
        if (i >= CM_OSC_WAIT_MS ||
            nhi->Host.NowMs(nhi->Host.Context) - start >= CM_OSC_WAIT_MS) {
            CmLogNote(nhi, "cm.osc.not-accepted", v);
            return 0xFFFFFFFEUL;
        }
        KeStallExecutionProcessor(1000);
    }
    CmLogNote(nhi, "cm.osc.accepted-ms", i);
    start = nhi->Host.NowMs(nhi->Host.Context);
    for (i = 0;; i++) {
        v = cmOscRd(pg, CM_OSC_MB_REPLY);
        if (v == 0xFFFFFFFFUL) {
            return 0xFFFFFFFFUL;
        }
        if (CM_OSC_MB_READY(v)) {
            break;
        }
        if (i >= CM_OSC_WAIT_MS ||
            nhi->Host.NowMs(nhi->Host.Context) - start >= CM_OSC_WAIT_MS) {
            CmLogNote(nhi, "cm.osc.no-reply", v);
            return 0xFFFFFFFDUL;
        }
        KeStallExecutionProcessor(1000);
    }
    cmOscWr(pg, CM_OSC_MB_REPLY, 0);
    CmLogNote(nhi, "cm.osc.reply-ms", i);
    return v;
}

#endif /* _X86_ */

/* ---- discovery -------------------------------------------------------- */

/* The root pointer found at `p` (CmOscFindRsdp checked it and its
 * length) kept at its whole length: 20, or its length from revision 2. */
static VOID cmOscKeepRsdp(CM_OSC_CAP *cap, const UCHAR *p)
{
    ULONG len;

    len = 20UL;
    if (p[15] >= 2) {
        len = (ULONG)p[20] | ((ULONG)p[21] << 8) | ((ULONG)p[22] << 16) |
              ((ULONG)p[23] << 24);
    }
    cmOscKeep(cap, 0, CM_OSC_SIG('R', 'S', 'D', 'P'), CM_QA_K_RSDP, len, p,
              len, CM_QA_H_WHOLE);
}

/* The root pointer (12.8.3 step 2) into rsdp[36]: TRUE when found, and
 * kept in cap from the area it was found in. */
static BOOLEAN cmOscRsdp(CM_OSC_RD *rd, UCHAR *rsdp, CM_OSC_CAP *cap)
{
    UCHAR *area;
    UCHAR seg[2];
    ULONG ebda;
    ULONG at;
    BOOLEAN found;

    area = (UCHAR *)CmAlloc(0x20000);
    if (area == NULL) {
        return FALSE;
    }
    found = FALSE;
    if (!rd->Nt && cmOscPhys(rd, 0x40E, seg, 2, TRUE)) {
        ebda = ((ULONG)seg[0] | ((ULONG)seg[1] << 8)) << 4;
        if (ebda >= 0x80000UL && ebda < 0xA0000UL &&
            cmOscPhys(rd, ebda, area, 1024, TRUE)) {
            at = CmOscFindRsdp(area, 1024);
            if (at != 0xFFFFFFFFUL && at + 36UL <= 1024UL) {
                RtlCopyMemory(rsdp, area + at, 36);
                cmOscKeepRsdp(cap, area + at);
                found = TRUE;
            }
        }
    }
    if (!found && cmOscPhys(rd, 0xE0000UL, area, 0x20000, TRUE)) {
        at = CmOscFindRsdp(area, 0x20000);
        if (at != 0xFFFFFFFFUL && at + 36UL <= 0x20000UL) {
            RtlCopyMemory(rsdp, area + at, 36);
            cmOscKeepRsdp(cap, area + at);
            found = TRUE;
        }
    }
    CmFree(area);
    return found;
}

/* A whole table at `phys` into a new allocation (its header read first,
 * its length within [36, cap], its checksum zero): NULL on any failure,
 * *over TRUE when it is the discovery's byte budget that refused it. */
static UCHAR *cmOscTable(CM_OSC_RD *rd, ULONG phys, ULONG cap,
                         CM_OSC_HDR *h, BOOLEAN *over)
{
    UCHAR hdr[36];
    UCHAR *t;

    if (!cmOscPhys(rd, phys, hdr, 36, FALSE)) {
        return NULL;
    }
    CmOscHeader(hdr, h);
    if (h->Length < 36UL || h->Length > cap) {
        return NULL;
    }
    if (rd->Bytes > CM_OSC_MAX_BYTES - h->Length) {
        *over = TRUE;
        return NULL;
    }
    t = (UCHAR *)CmAlloc(h->Length);
    if (t == NULL) {
        *over = TRUE;
        return NULL;
    }
    rd->Bytes += h->Length;
    if (!cmOscPhys(rd, phys, t, h->Length, FALSE) ||
        !CmOscSumZero(t, h->Length)) {
        CmFree(t);
        return NULL;
    }
    return t;
}

/* OEM ID AMD, and OEM table id `table` (NULL: any), padded with NUL or
 * space. */
static BOOLEAN cmOscOem(const CM_OSC_HDR *h, const char *table)
{
    ULONG i;
    UCHAR c;

    if (h->OemId[0] != 'A' || h->OemId[1] != 'M' || h->OemId[2] != 'D') {
        return FALSE;
    }
    for (i = 3; i < 6UL; i++) {
        if (h->OemId[i] != 0 && h->OemId[i] != ' ') {
            return FALSE;
        }
    }
    if (table == NULL) {
        return TRUE;
    }
    for (i = 0; i < 8UL; i++) {
        c = (UCHAR)table[i];
        if (c == 0) {
            break;
        }
        if (h->OemTableId[i] != c) {
            return FALSE;
        }
    }
    for (; i < 8UL; i++) {
        if (h->OemTableId[i] != 0 && h->OemTableId[i] != ' ') {
            return FALSE;
        }
    }
    return TRUE;
}

typedef struct _CM_OSC_FOUND {
    ULONG RootBdf;
    ULONG UspBdf;
    ULONG DspBdf;
    ULONG EcamBase;
    ULONG EcamEnd;
    ULONG Data;         /* the command's data byte                     */
} CM_OSC_FOUND;

/* The three SSDTs walked (12.8.3 step 4), by OEM table id: each must be
 * there once and walk whole. A name defined twice in a firmware's
 * namespace fails its load (ACPI), so no other table can hold a second
 * CPNV, M620 or method of these names in a firmware that works. */
static const char *const cmOscSsdt[3] = { "CPMCMN", "DISCUSB4", "CPMMSOSC" };

/* 12.8.3 steps 2 to 6 and the match of 12.8.4 but the mailbox's state:
 * CM_OSC_OK with *f, else the reason. */
/* `n` characters (at most 4) as a dword, the first in bits 7:0, as a
 * table signature is read (the log's form of a table's ids). */
static ULONG cmOscChars(const UCHAR *p, ULONG n)
{
    ULONG v;
    ULONG i;

    v = 0;
    for (i = 0; i < n; i++) {
        v |= (ULONG)p[i] << (8UL * i);
    }
    return v;
}

/* The second tier (record 32 section 12.8.10): the five methods' canonical
 * forms, each logged, compared with the forms read. CM_OSC_OK or
 * CM_OSC_UNKNOWN_CODE. */
static ULONG cmOscForms(PCM_NHI nhi, const CM_OSC_TBL *tbl,
                        const CM_OSC_WALK *w)
{
    CM_OSCC *c;
    ULONG fnv[CM_OSC_METHODS];
    ULONG crc[CM_OSC_METHODS];
    ULONG why;
    ULONG m;
    ULONG reason;

    c = (CM_OSCC *)CmAlloc(sizeof(CM_OSCC));
    if (c == NULL) {
        CmLogNote(nhi, "cm.osc.form.nomem", sizeof(CM_OSCC));
        return CM_OSC_UNKNOWN_CODE;
    }
    reason = CM_OSC_OK;
    for (m = 0; m < CM_OSC_METHODS; m++) {
        why = CmOscCanon(c, tbl, 3, w, m, &fnv[m], &crc[m]);
        if (why != 0) {
            CmLogNote(nhi, "cm.osc.form.refused",
                      (m << 24) | ((why & 0xFFUL) << 16) |
                          (c->WhyAt & 0xFFFFUL));
            reason = CM_OSC_UNKNOWN_CODE;
            break;
        }
        CmLogNote(nhi, "cm.osc.form.fnv", fnv[m]);
        CmLogNote(nhi, "cm.osc.form.crc", crc[m]);
    }
    CmFree(c);
    if (reason == CM_OSC_OK) {
        reason = CmOscFormKnown(fnv, crc);
    }
    return reason;
}

static ULONG cmOscDiscover(PCM_NHI nhi, CM_OSC_FOUND *f, CM_OSC_CAP *cap,
                            CM_OSC_RD *rd, const CM_OSC_NTSRC *g)
{
    CM_OSC_HDR h;
    CM_OSC_HDR th;
    CM_OSC_WALK *w;
    CM_OSC_TBL tbl[3];
    UCHAR *keep[3];
    UCHAR rsdp[36];
    UCHAR hdr[36];
    UCHAR dusb4[CM_OSC_DUSB4_BYTES];
    UCHAR entry[8];
    UCHAR *root;
    UCHAR *t;
    ULONG rsdt;
    ULONG xsdt;
    ULONG n;
    ULONG k;
    ULONG phys;
    ULONG reason;
    ULONG mcfg;
    ULONG seen[3];
    ULONG cbase;
    ULONG clen;
    ULONG o620;
    ULONG o251;
    ULONG p620;
    ULONG p251;
    ULONG idx;
    ULONG lo;
    ULONG hi;
    BOOLEAN xs;
    BOOLEAN over;

    over = FALSE;
    root = NULL;
    xs = FALSE;
    rsdt = 0;
    xsdt = 0;
    if (g != NULL && g->Root != NULL) {
        /* C: the root table as the system gave it, checked by
         * CmOscpFwTable; copied, so it is freed as one read is. */
        CmOscHeader(g->Root, &h);
        root = (UCHAR *)CmAlloc(h.Length);
        if (root == NULL) {
            return CM_OSC_NO_ROOT_TABLE;
        }
        RtlCopyMemory(root, g->Root, h.Length);
        xs = (BOOLEAN)(h.Signature == CM_OSC_SIG('X', 'S', 'D', 'T'));
        if ((!xs && h.Signature != CM_OSC_SIG('R', 'S', 'D', 'T')) ||
            ((h.Length - 36UL) % (xs ? 8UL : 4UL)) != 0) {
            CmFree(root);
            root = NULL;
        }
    } else {
        if (g != NULL) {
            RtlCopyMemory(rsdp, g->Rsdp, 36);
        } else if (!cmOscRsdp(rd, rsdp, cap)) {
            return CM_OSC_NO_RSDP;
        }
        CmOscRsdpTables(rsdp, &rsdt, &xsdt);
    }
    /* The XSDT when the pointer gives one, else (or when it fails) the
     * RSDT: each of its own signature, a whole number of entries, at most
     * CM_OSC_MAX_TABLES of them. */
    if (root == NULL && xsdt != 0) {
        root = cmOscTable(rd, xsdt, 36UL + 8UL * CM_OSC_MAX_TABLES, &h,
                          &over);
        if (root != NULL &&
            (h.Signature != CM_OSC_SIG('X', 'S', 'D', 'T') ||
             ((h.Length - 36UL) % 8UL) != 0)) {
            CmFree(root);
            root = NULL;
        }
        xs = (BOOLEAN)(root != NULL);
    }
    if (root == NULL && !over && rsdt != 0) {
        root = cmOscTable(rd, rsdt, 36UL + 4UL * CM_OSC_MAX_TABLES, &h,
                          &over);
        if (root != NULL &&
            (h.Signature != CM_OSC_SIG('R', 'S', 'D', 'T') ||
             ((h.Length - 36UL) % 4UL) != 0)) {
            CmFree(root);
            root = NULL;
        }
    }
    if (root == NULL || over ||
        (h.Length - 36UL) / (xs ? 8UL : 4UL) > CM_OSC_MAX_TABLES) {
        if (root != NULL) {
            CmFree(root);
        }
        return CM_OSC_NO_ROOT_TABLE;
    }
    cmOscKeep(cap, 1, h.Signature, CM_QA_K_ROOT, h.Length, root, h.Length,
              CM_QA_H_WHOLE);
    /* MCFG first: its window bounds every later read. */
    lo = XHCI_TBT_BDF_BUS(f->RootBdf);
    hi = XHCI_TBT_BDF_BUS(f->DspBdf);
    mcfg = 0;
    if (g != NULL && g->Mcfg != NULL) {
        /* C: MCFG by name (12.8.15.5), no physical MCFG pass. */
        CmOscHeader(g->Mcfg, &th);
        mcfg = CmOscMcfg(g->Mcfg, th.Length, lo, hi, &f->EcamBase,
                         &f->EcamEnd) ? 1UL : 2UL;
    }
    for (n = 0; (g == NULL || g->Mcfg == NULL) &&
                CmOscRootEntry(root, h.Length, xs, n, &phys); n++) {
        if (phys == 0 || !cmOscPhys(rd, phys, hdr, 36, FALSE)) {
            continue;
        }
        CmOscHeader(hdr, &th);
        cmOscKeepHeader(cap, n, hdr, &th);
        if (th.Signature != CM_OSC_SIG('M', 'C', 'F', 'G')) {
            continue;
        }
        mcfg++;
        t = cmOscTable(rd, phys, CM_OSC_MCFG_CAP, &th, &over);
        if (t == NULL ||
            !CmOscMcfg(t, th.Length, lo, hi, &f->EcamBase, &f->EcamEnd)) {
            mcfg = 2;
        }
        if (t != NULL) {
            cmOscKeepWhole(cap, n, t, &th);
            CmFree(t);
        }
    }
    if (mcfg != 1 || over) {
        CmFree(root);
        return CM_OSC_NO_MCFG;
    }
    rd->EcamLo = f->EcamBase;
    rd->EcamHi = f->EcamBase + ((f->EcamEnd + 1UL) << 20);
    if (rd->EcamHi < rd->EcamLo || rd->EcamHi == 0) {
        rd->EcamHi = 0xFFFFFFFFUL;
    }
    if (rd->McfgOnly) {
        /* Record 32 section 12.8.15.18: the ECAM base alone, no SSDT. */
        CmFree(root);
        return CM_OSC_OK;
    }
    /* Past MCFG: the code is looked for (tier 1 until it is known). */
    nhi->OscTier = CM_OSC_TIER_UNKNOWN;
    w = (CM_OSC_WALK *)CmAlloc(sizeof(CM_OSC_WALK));
    if (w == NULL) {
        CmFree(root);
        return CM_OSC_NO_CPNV;
    }
    RtlZeroMemory(w, sizeof(*w));
    seen[0] = 0;
    seen[1] = 0;
    seen[2] = 0;
    keep[0] = NULL;
    keep[1] = NULL;
    keep[2] = NULL;
    reason = CM_OSC_OK;
    for (n = 0; reason == CM_OSC_OK &&
                CmOscRootEntry(root, h.Length, xs, n, &phys); n++) {
        if (phys == 0 || !cmOscPhys(rd, phys, hdr, 36, FALSE)) {
            continue;
        }
        CmOscHeader(hdr, &th);
        if (th.Signature != CM_OSC_SIG('S', 'S', 'D', 'T')) {
            continue;
        }
        for (k = 0; k < 3UL && !cmOscOem(&th, cmOscSsdt[k]); k++) {
        }
        if (k == 3UL) {
            continue;
        }
        seen[k]++;
        /* Record 32 section 12.8.12: the table's identity, so a report's
         * hashes name the firmware build they came from. */
        CmLogNote(nhi, "cm.osc.ssdt", k);
        CmLogNote(nhi, "cm.osc.ssdt.oemid", cmOscChars(th.OemId, 4));
        CmLogNote(nhi, "cm.osc.ssdt.oemid2", cmOscChars(th.OemId + 4, 2));
        CmLogNote(nhi, "cm.osc.ssdt.tableid", cmOscChars(th.OemTableId, 4));
        CmLogNote(nhi, "cm.osc.ssdt.tableid2",
                  cmOscChars(th.OemTableId + 4, 4));
        CmLogNote(nhi, "cm.osc.ssdt.oemrev", th.OemRevision);
        CmLogNote(nhi, "cm.osc.ssdt.length", th.Length);
        t = cmOscTable(rd, phys, CM_OSC_SSDT_CAP, &th, &over);
        if (t != NULL) {
            cmOscKeepWhole(cap, n, t, &th);
        }
        w->Table = k;
        if (t == NULL || !CmOscAmlWalk(t + 36, th.Length - 36UL, w)) {
            CmLogNote(nhi, "cm.osc.ssdt.refused", k);
            reason = CM_OSC_NO_CPNV;
        }
        /* Each table is kept for the second tier's name lookups and
         * bodies (12.8.10); a second one of a name fails below. */
        if (t != NULL && keep[k] == NULL && reason == CM_OSC_OK) {
            keep[k] = t;
            tbl[k].Aml = t + 36;
            tbl[k].Length = th.Length - 36UL;
        } else if (t != NULL) {
            CmFree(t);
        }
    }
    CmFree(root);
    if (reason == CM_OSC_OK &&
        (seen[0] != 1 || seen[1] != 1 || seen[2] != 1 || over)) {
        reason = CM_OSC_NO_CPNV;
    }
    if (reason == CM_OSC_OK) {
        reason = CmOscCpnvPlace(w, &cbase, &clen, &o620, &o251);
    }
    if (reason == CM_OSC_OK) {
        for (n = 0; n < CM_OSC_METHODS; n++) {
            CmLogNote(nhi, "cm.osc.method.hash", w->MethodHash[n]);
        }
        /* 12.8.3 step 5: a known build, else the same handover code. */
        reason = CmOscCodeKnown(w);
        if (reason == CM_OSC_OK) {
            nhi->OscTier = CM_OSC_TIER_KNOWN;
        } else if (cmOscForms(nhi, tbl, w) == CM_OSC_OK) {
            nhi->OscTier = CM_OSC_TIER_SAME;
            reason = nhi->Values.HandoverStructural != 0 ? CM_OSC_OK
                                                         : CM_OSC_SAME_OFF;
        }
        CmLogNote(nhi, "cm.osc.tier", nhi->OscTier);
        if (nhi->OscTier == CM_OSC_TIER_KNOWN ||
            nhi->OscTier == CM_OSC_TIER_SAME) {
            /* 12.8.12: the contract a recognised build gives, the values
             * this file implements (12.8.1, 12.8.5). */
            CmLogNote(nhi, "cm.osc.contract.mailbox",
                      (CM_OSC_MB_CMD << 16) | CM_OSC_MB_REPLY);
            CmLogNote(nhi, "cm.osc.contract.commands",
                      (CM_OSC_CMD_HANDOVER << 16) | CM_OSC_CMD_QUERY);
            CmLogNote(nhi, "cm.osc.contract.masks",
                      (0x0F00UL << 16) | 0x0100UL);
            CmLogNote(nhi, "cm.osc.contract.bytes",
                      CM_OSC_D_FITTED | (CM_OSC_D_GRANT << 8) |
                          (CM_OSC_D_DATA << 16) |
                          (CM_OSC_D_ROOT_INDEX << 24));
            CmLogNote(nhi, "cm.osc.contract.bytes2",
                      CM_OSC_D_ROOT_DEV | (CM_OSC_D_ROOT_FN << 8));
            CmLogNote(nhi, "cm.osc.contract.m620", o620);
            CmLogNote(nhi, "cm.osc.contract.m251", o251);
        }
    }
    for (k = 0; k < 3UL; k++) {
        if (keep[k] != NULL) {
            CmFree(keep[k]);
        }
    }
    CmFree(w);
    if (reason != CM_OSC_OK) {
        return reason;
    }
    /* 12.8.3 step 6, in the firmware's order: the discrete USB4 table's
     * byte 10h, then 4Dh (the platform branch), then 4Bh, then 12h to 14h
     * (the method), then the roots entry (its helper); each byte alone. */
    RtlZeroMemory(dusb4, sizeof(dusb4));
    RtlZeroMemory(entry, sizeof(entry));
    if (!cmOscDword(rd, cbase, o620, &p620) || p620 == 0) {
        return CM_OSC_NO_POINTER;
    }
    if (!cmOscByte(rd, p620, CM_OSC_D_FITTED, &dusb4[CM_OSC_D_FITTED])) {
        return CM_OSC_NO_POINTER;
    }
    CmLogNote(nhi, "cm.osc.dusb4.10", dusb4[CM_OSC_D_FITTED]);
    if (dusb4[CM_OSC_D_FITTED] == 0) {
        return CM_OSC_NOT_FITTED;
    }
    if (!cmOscByte(rd, p620, CM_OSC_D_GRANT, &dusb4[CM_OSC_D_GRANT])) {
        return CM_OSC_NO_POINTER;
    }
    CmLogNote(nhi, "cm.osc.dusb4.4d", dusb4[CM_OSC_D_GRANT]);
    if (dusb4[CM_OSC_D_GRANT] == 0) {
        return CM_OSC_NOT_GRANTED;
    }
    if (!cmOscByte(rd, p620, CM_OSC_D_DATA, &dusb4[CM_OSC_D_DATA]) ||
        !cmOscByte(rd, p620, CM_OSC_D_ROOT_INDEX,
                   &dusb4[CM_OSC_D_ROOT_INDEX]) ||
        !cmOscByte(rd, p620, CM_OSC_D_ROOT_DEV, &dusb4[CM_OSC_D_ROOT_DEV]) ||
        !cmOscByte(rd, p620, CM_OSC_D_ROOT_FN, &dusb4[CM_OSC_D_ROOT_FN]) ||
        !cmOscDword(rd, cbase, o251, &p251) || p251 == 0) {
        return CM_OSC_NO_POINTER;
    }
    idx = CM_OSC_ROOT_ENTRY(dusb4[CM_OSC_D_ROOT_INDEX]);
    if (!cmOscByte(rd, p251, idx, &entry[0])) {
        return CM_OSC_NO_POINTER;
    }
    if (entry[0] != 0xFF &&
        (!cmOscByte(rd, p251, idx + 3UL, &entry[3]) ||
         !cmOscByte(rd, p251, idx + 4UL, &entry[4]))) {
        return CM_OSC_NO_POINTER;
    }
    CmLogNote(nhi, "cm.osc.dusb4.4b", dusb4[CM_OSC_D_DATA]);
    CmLogNote(nhi, "cm.osc.root",
              (((ULONG)entry[3] | ((ULONG)entry[4] << 8)) << 16) |
                  ((ULONG)dusb4[CM_OSC_D_ROOT_DEV] << 8) |
                  dusb4[CM_OSC_D_ROOT_FN]);
    return CmOscDecide(dusb4, entry, f->RootBdf, &f->Data);
}

/* ---- the read path on NT (record 32 section 12.8.15.17) -------------- */

ULONG CmOscNtScan(const CM_OSC_NTSRC *s, UCHAR *rsdp, PULONG refused)
{
    CM_OSC_RD rd;
    UCHAR *area;
    ULONG at;
    ULONG r;

    RtlZeroMemory(&rd, sizeof(rd));
    rd.Nt = 1;
    rd.Ranges = s->Ranges;
    rd.Count = s->Count;
    area = (UCHAR *)CmAlloc(0x20000);
    if (area == NULL) {
        *refused = 0;
        return CM_OSCP_S_REFUSED;
    }
    if (!cmOscPhys(&rd, 0xE0000UL, area, 0x20000, TRUE)) {
        r = CM_OSCP_S_REFUSED;
    } else {
        at = CmOscFindRsdp(area, 0x20000);
        r = CM_OSCP_S_ABSENT;
        if (at != 0xFFFFFFFFUL && at + 36UL <= 0x20000UL) {
            RtlCopyMemory(rsdp, area + at, 36);
            r = CM_OSCP_S_OBTAINED;
        }
    }
    CmFree(area);
    *refused = rd.Refused;
    return r;
}

ULONG CmOscNtMcfg(PCM_NHI nhi, const CM_OSC_NTSRC *s, PULONG base,
                  PULONG busEnd, PULONG refused)
{
    CM_OSC_FOUND f;
    CM_OSC_RD rd;
    ULONG reason;

    RtlZeroMemory(&f, sizeof(f));
    f.RootBdf = s->RootBdf;
    f.UspBdf = s->UspBdf;
    f.DspBdf = s->DspBdf;
    RtlZeroMemory(&rd, sizeof(rd));
    rd.Nt = 1;
    rd.Ranges = s->Ranges;
    rd.Count = s->Count;
    rd.McfgOnly = 1;
    reason = cmOscDiscover(nhi, &f, NULL, &rd, s);
    *refused = rd.Refused;
    if (rd.Refused != 0) {
        reason = 0x27UL;
    }
    *base = (reason == CM_OSC_OK) ? f.EcamBase : 0UL;
    *busEnd = (reason == CM_OSC_OK) ? f.EcamEnd : 0UL;
    return reason;
}

ULONG CmOscNtDiscover(PCM_NHI nhi, const CM_OSC_NTSRC *s, PULONG data,
                      PULONG ecamBase, PULONG ecamEnd, PULONG refused)
{
    CM_OSC_FOUND f;
    CM_OSC_RD rd;
    ULONG reason;

    RtlZeroMemory(&f, sizeof(f));
    f.RootBdf = s->RootBdf;
    f.UspBdf = s->UspBdf;
    f.DspBdf = s->DspBdf;
    RtlZeroMemory(&rd, sizeof(rd));
    rd.Nt = 1;
    rd.Ranges = s->Ranges;
    rd.Count = s->Count;
    reason = cmOscDiscover(nhi, &f, NULL, &rd, s);
    *refused = rd.Refused;
    if (rd.Refused != 0) {
        /* Whatever the discovery made of it: refused (cm_oscnt.c's
         * CM_OSN_RD_PAGE). */
        reason = 0x27UL;
    }
    *data = (reason == CM_OSC_OK) ? f.Data : 0UL;
    *ecamBase = (reason == CM_OSC_OK) ? f.EcamBase : 0UL;
    *ecamEnd = (reason == CM_OSC_OK) ? f.EcamEnd : 0UL;
    return reason;
}

#if defined(_X86_)

/* The NHI's location: its PDO's, checked against its ids, else the one
 * function in the inventory carrying the ASM4242 NHI's ids. */
static ULONG cmOscNhiBdf(PCM_NHI nhi)
{
    CM_OSC_FIND f;
    ULONG bus;
    ULONG address;
    ULONG bytes;
    ULONG bdf;
    ULONG v;

    bus = 0;
    address = 0;
    if (NT_SUCCESS(IoGetDeviceProperty(nhi->Common.Pdo,
                                       DevicePropertyBusNumber, sizeof(bus),
                                       &bus, &bytes)) &&
        bus <= 0xFFUL &&
        NT_SUCCESS(IoGetDeviceProperty(nhi->Common.Pdo,
                                       DevicePropertyAddress,
                                       sizeof(address), &address, &bytes)) &&
        (address >> 16) <= 0x1FUL && (address & 0xFFFFUL) <= 7UL) {
        bdf = XHCI_TBT_BDF(bus, address >> 16, address & 0xFFFFUL);
        if (cmOscCfg(bdf, 0, &v) && v == CM_OSC_ASM_NHI) {
            return bdf;
        }
    }
    RtlZeroMemory(&f, sizeof(f));
    f.Id = CM_OSC_ASM_NHI;
    if (!cmOscInventory(cmOscVisitId, &f) || f.Found != 1UL) {
        return XHCI_TBT_NO_BDF;
    }
    return f.At;
}

/* The start's handover outcome for the tables kept (CM_QA_S_*). */
static VOID cmOscState(CM_OSC_CAP *c, ULONG state, ULONG outcome)
{
    if (c != NULL) {
        c->State = state;
        c->Outcome = outcome;
    }
}

static VOID cmOscRun(PCM_NHI nhi, CM_OSC_CAP *c)
{
    CM_OSC_FOUND f;
    CM_OSC_RD rd;
    CM_OSC_PAGE pg;
    ULONG nhiBdf;
    ULONG id;
    ULONG v;
    ULONG pm;
    ULONG cap;
    ULONG reason;
    ULONG reply;
    ULONG slot;

    /* The tier is this start's; the upstream port found by an earlier
     * start is kept, so its written mark stays reported (12.8.11). */
    nhi->OscTier = CM_OSC_TIER_NONE;
    if (IoIsWdmVersionAvailable(1, 0x10) ||
        ((nhi->Vendor & 0xFFFFUL) | (nhi->Device << 16)) != CM_OSC_ASM_NHI) {
        return;
    }
    if (nhi->Values.PlatformHandover == 0) {
        CmLogNote(nhi, "cm.osc.off", 0);
        cmOscState(c, CM_QA_S_OFF, 0);
        return;
    }
    RtlZeroMemory(&f, sizeof(f));
    /* 12.8.3 step 1: the host by its bus. */
    nhiBdf = cmOscNhiBdf(nhi);
    if (nhiBdf == XHCI_TBT_NO_BDF) {
        CmLogNote(nhi, "cm.osc.refused", 0x100UL);
        cmOscState(c, CM_QA_S_NO_HOST, 0x100UL);
        return;
    }
    f.DspBdf = cmOscParentOf(XHCI_TBT_BDF_BUS(nhiBdf), &id);
    if (f.DspBdf == XHCI_TBT_NO_BDF || id != CM_OSC_ASM_DSP) {
        CmLogNote(nhi, "cm.osc.refused", 0x101UL);
        cmOscState(c, CM_QA_S_NO_HOST, 0x101UL);
        return;
    }
    f.UspBdf = cmOscParentOf(XHCI_TBT_BDF_BUS(f.DspBdf), &id);
    if (f.UspBdf == XHCI_TBT_NO_BDF || id != CM_OSC_ASM_USP ||
        (f.UspBdf & 0xFFUL) != 0) {
        CmLogNote(nhi, "cm.osc.refused", 0x102UL);
        cmOscState(c, CM_QA_S_NO_HOST, 0x102UL);
        return;
    }
    f.RootBdf = cmOscParentOf(XHCI_TBT_BDF_BUS(f.UspBdf), &id);
    cap = f.RootBdf == XHCI_TBT_NO_BDF ? 0 : cmOscCap(f.RootBdf, 0x10);
    if (cap == 0 || !cmOscCfg(f.RootBdf, cap, &v) ||
        ((v >> 20) & 0xFUL) != 4UL) {
        CmLogNote(nhi, "cm.osc.refused", 0x103UL);
        cmOscState(c, CM_QA_S_NO_HOST, 0x103UL);
        return;
    }
    CmLogNote(nhi, "cm.osc.usp", f.UspBdf);
    nhi->OscUspBdf = f.UspBdf;
    /* 12.8.3 steps 2 to 6. */
    RtlZeroMemory(&rd, sizeof(rd));
    reason = cmOscDiscover(nhi, &f, c, &rd, NULL);
    cmOscState(c, reason == CM_OSC_NO_RSDP ? CM_QA_S_NO_RSDP : CM_QA_S_KEPT,
               reason);
    if (reason != CM_OSC_OK) {
        CmLogNote(nhi, "cm.osc.refused", reason);
        return;
    }
    /* The upstream port in D0, through configuration space. */
    pm = cmOscCap(f.UspBdf, 0x01);
    if (pm == 0 || !cmOscCfg(f.UspBdf, pm + 4UL, &v) || (v & 3UL) != 0) {
        CmLogNote(nhi, "cm.osc.refused", 0x104UL);
        cmOscState(c, CM_QA_S_KEPT, 0x104UL);
        return;
    }
    if (!cmOscMapPage(f.EcamBase, f.EcamEnd, f.UspBdf, &pg)) {
        CmLogNote(nhi, "cm.osc.refused", 0x105UL);
        cmOscState(c, CM_QA_S_KEPT, 0x105UL);
        return;
    }
    /* ECAM read back against configuration space (12.8.3 step 3). */
    if (!cmOscCfg(f.UspBdf, 0, &v) || cmOscRd(&pg, 0) != v ||
        !cmOscCfg(f.UspBdf, 0x18, &v) || cmOscRd(&pg, 0x18) != v ||
        ((v >> 8) & 0xFFUL) != XHCI_TBT_BDF_BUS(f.DspBdf)) {
        MmUnmapIoSpace(pg.Va, 0x1000);
        CmLogNote(nhi, "cm.osc.refused", 0x106UL);
        cmOscState(c, CM_QA_S_KEPT, 0x106UL);
        return;
    }
    v = cmOscRd(&pg, CM_OSC_MB_CMD);
    reply = cmOscRd(&pg, CM_OSC_MB_REPLY);
    CmLogNote(nhi, "cm.osc.mb.before", v);
    if (v == 0xFFFFFFFFUL || CM_OSC_MB_BUSY(v) || reply != 0) {
        MmUnmapIoSpace(pg.Va, 0x1000);
        CmLogNote(nhi, "cm.osc.refused", 0x107UL);
        cmOscState(c, CM_QA_S_KEPT, 0x107UL);
        return;
    }
    if (!cmOscReserve(f.UspBdf, &slot)) {
        MmUnmapIoSpace(pg.Va, 0x1000);
        CmLogNote(nhi, "cm.osc.already", f.UspBdf);
        return;
    }
    /* 12.8.5; the first write marks the entry (12.8.11's bit 19). */
    cmOscMarkSent(slot);
    reply = cmOscExchange(nhi, &pg,
                          CM_OSC_CMD_HANDOVER | ((f.Data & 0xFFUL) << 16));
    CmLogNote(nhi, "cm.osc.handover", reply);
    if (reply == CM_OSC_REPLY_OK) {
        reply = cmOscExchange(nhi, &pg, CM_OSC_CMD_QUERY);
        CmLogNote(nhi, "cm.osc.query", reply);
    }
    MmUnmapIoSpace(pg.Va, 0x1000);
    for (v = 0; v < CM_OSC_SETTLE_MS; v++) {
        KeStallExecutionProcessor(1000);
    }
}

/* The handover of 12.8.2 to 12.8.5, and the tables its discovery read
 * kept for the query device (record 32 section 12.8.14): built whatever
 * the outcome, its state CM_QA_S_NOT_LOOKED where nothing was looked for. */
VOID CmOscHandover(PCM_NHI nhi)
{
    CM_OSC_CAP *cap;

    cap = (CM_OSC_CAP *)CmAlloc(sizeof(CM_OSC_CAP));
    if (cap != NULL) {
        RtlZeroMemory(cap, sizeof(CM_OSC_CAP));
        cap->State = CM_QA_S_NOT_LOOKED;
    }
    cmOscRun(nhi, cap);
    cmOscKept(nhi, cap);
}

#endif /* _X86_ */

#if !defined(_X86_) /* no 98 SE or ME */

VOID CmOscInit(VOID)
{
}

VOID CmOscHandover(PCM_NHI nhi)
{
    UNREFERENCED_PARAMETER(nhi);
}

ULONG CmOscReport(PCM_NHI nhi)
{
    return CmOscntReport(nhi);
}

#endif
