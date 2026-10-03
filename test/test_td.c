/*
 * test_td.c - the transfer engine's TD builders over the HCD's own buffer
 * mapping (src\xhci_xfer.c with src\xhci_pipe.c; roadmap-hcd.md task 26-A.9).
 *
 * test_xfer and test_iso check the builders against hand-made SG lists and
 * isochronous blocks, the shapes usbport handed the miniport. The HCD hands
 * the same builders lists it made itself, and the property that matters is
 * the seam: that what hcd_io.c and hcd_dma.c produce is something the engine
 * turns into the TRBs the controller must see. Neither half can be run on the
 * host as it stands (both are ntddk.h code), so this suite carries a model of
 * the three steps, each written from the source it imitates and kept to the
 * arithmetic:
 *
 *   plan_chunk   hcd_io.c hcdPlanChunk: a chunk is what the granted map
 *                registers, capped at HCD_SG_ELEMENTS pages, can reach from
 *                the buffer's position in its first page; a control or an
 *                isochronous transfer that does not fit is refused, and a
 *                bulk or interrupt chunk that is not the last ends on a
 *                packet boundary.
 *   map_chunk    hcd_dma.c hcdMapExecute: MapTransfer gives physically
 *                contiguous runs, and each run is cut at every page bound, so
 *                no element crosses a page and none can cross a 64 KB line.
 *   iso_fill     hcd_io.c hcdIsoFill and the stamping in hcdIsoPublish: each
 *                packet one or two page-bounded fragments, found in the
 *                chunk's SG list, and stamped StartFrame + XhciPipeIsoFrameOf.
 *
 * What is asserted of every built TD is computed here from the physical page
 * table, not read back from the SG list: the address of byte k of the buffer
 * is phys[page] + offset, so an element the model or the engine put in the
 * wrong place shows up as a wrong address rather than as a self-consistent
 * one. The TD Size expectation is spec 4.11.2.4's formula, written out again.
 *
 * Several physical layouts are swept, including runs that are contiguous
 * across a 64 KB line (which the page cut is what splits) and a buffer that
 * ends on the last page below 4 GB (where "distance to the next 64 KB line"
 * wraps).
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass). C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci_xfer.h"
#include "../src/xhci_pipe.h"
#include "test_harness.h"

#define PAGE            4096UL
#define BUF_PAGES       80UL
/* hcd.h: data elements per chunk. Not included (hcd.h is ntddk.h code); the
 * check below pins it to the pure header's number it is documented as. */
#define HCD_SG_ELEMENTS 32UL

#define KIND_NORMAL     0UL
#define KIND_CONTROL    1UL
#define KIND_ISOCH      2UL

#define RING_PA         0x0F001000UL
#define EV_SLOT         3UL
#define EV_DCI          4UL

/* The physical page behind each page of the buffer's virtual range. */
static ULONG phys[BUF_PAGES];

/* hcd.h HCD_XFER.Sg: the declared element plus HCD_SG_ELEMENTS more. */
typedef struct _TD_SG {
    USBPORT_SCATTER_GATHER_LIST List;
    USBPORT_SCATTER_GATHER_ELEMENT More[HCD_SG_ELEMENTS];
} TD_SG;

/* hcd.h HCD_ISO_BLOCK. */
typedef struct _TD_ISO_BLOCK {
    USBPORT_ISO_TRANSFER Block;
    USBPORT_ISO_PACKET More[XHCI_XFER_MAX_ISO_PACKETS - 1];
} TD_ISO_BLOCK;

static const char *layout_name;

static void layout_contiguous(ULONG base, const char *name)
{
    ULONG k;

    for (k = 0; k < BUF_PAGES; k++) {
        phys[k] = base + k * PAGE;
    }
    layout_name = name;
}

/* Every page somewhere else, descending: no two elements merge. */
static void layout_scattered(void)
{
    ULONG k;

    for (k = 0; k < BUF_PAGES; k++) {
        phys[k] = 0x0F000000UL - k * 0x3000UL;
    }
    layout_name = "scattered";
}

/* Pairs of pages contiguous across a 64 KB line: 0x..F000 then 0x..0000. */
static void layout_straddling(void)
{
    ULONG k;

    for (k = 0; k < BUF_PAGES; k++) {
        phys[k] = 0x0040F000UL + (k / 2) * 0x00020000UL + (k % 2) * PAGE;
    }
    layout_name = "pairs straddling 64 KB lines";
}

static void layout_select(ULONG which)
{
    switch (which) {
    case 0:
        layout_contiguous(0x0001E000UL, "contiguous from 0x0001E000");
        break;
    case 1:
        layout_scattered();
        break;
    case 2:
        layout_straddling();
        break;
    default:
        layout_contiguous(0xFFFFF000UL - (BUF_PAGES - 1) * PAGE,
                          "contiguous to 0xFFFFFFFF");
        break;
    }
}
#define LAYOUTS 4UL

/* The physical address of virtual byte `va` (counted from the first page). */
static ULONG pa_of(ULONG va)
{
    return phys[va / PAGE] + (va % PAGE);
}

/* ------------------------------------------------------------------ */
/* The model of hcd_io.c and hcd_dma.c                                 */
/* ------------------------------------------------------------------ */

/* hcdPlanChunk. `va` is the chunk's first byte; `mapRegisters` the grant. */
static ULONG plan_chunk(ULONG kind, ULONG va, ULONG remaining,
                        ULONG mapRegisters, ULONG mps, ULONG *chunk,
                        ULONG *mapCount)
{
    ULONG pageOffset;
    ULONG pages;
    ULONG room;

    pageOffset = va % PAGE;
    pages = mapRegisters;
    if (pages > HCD_SG_ELEMENTS) {
        pages = HCD_SG_ELEMENTS;
    }
    if (pages == 0) {
        return 0;
    }
    room = pages * PAGE - pageOffset;
    if (remaining <= room) {
        *chunk = remaining;
    } else {
        if (kind != KIND_NORMAL) {
            return 0;
        }
        if (mps == 0 || room < mps) {
            return 0;
        }
        *chunk = room - (room % mps);
    }
    /* ADDRESS_AND_SIZE_TO_SPAN_PAGES */
    *mapCount = (*chunk == 0) ? 0
                              : ((pageOffset + *chunk + PAGE - 1) / PAGE);
    return 1;
}

/* MapTransfer: the physically contiguous run from `va`, at most `*run`. */
static ULONG map_transfer(ULONG va, ULONG *run)
{
    ULONG want;
    ULONG got;
    ULONG page;

    want = *run;
    got = PAGE - (va % PAGE);
    page = va / PAGE;
    while (got < want && page + 1 < BUF_PAGES &&
           phys[page + 1] == phys[page] + PAGE) {
        got += PAGE;
        page++;
    }
    if (got > want) {
        got = want;
    }
    *run = got;
    return pa_of(va);
}

static void sg_clear(TD_SG *sg)
{
    ULONG i;
    USBPORT_SCATTER_GATHER_ELEMENT *e;

    sg->List.Flags = 0;
    sg->List.CurrentVa = 0;
    sg->List.MappedSystemVa = NULL;
    sg->List.SgElementCount = 0;
    e = &sg->List.SgElement[0];
    for (i = 0; i <= HCD_SG_ELEMENTS; i++) {
        e[i].SgPhysicalAddressLo = 0xDEADBEEFUL;
        e[i].SgPhysicalAddressHi = 0;
        e[i].Reserved1 = 0;
        e[i].SgTransferLength = 0;
        e[i].SgOffset = 0;
        e[i].Reserved2 = 0;
    }
}

/* hcdMapExecute: runs, each cut at every page bound. 0 when the chunk needs
 * more elements than a record carries. */
static ULONG map_chunk(ULONG va, ULONG chunk, TD_SG *sg)
{
    USBPORT_SCATTER_GATHER_ELEMENT *e;
    ULONG done;
    ULONG run;
    ULONG piece;
    ULONG pa;
    ULONG n;

    sg_clear(sg);
    e = &sg->List.SgElement[0];
    n = 0;
    done = 0;
    while (done < chunk) {
        run = chunk - done;
        pa = map_transfer(va + done, &run);
        if (run == 0) {
            return 0;
        }
        while (run > 0) {
            piece = PAGE - (pa & (PAGE - 1));
            if (piece > run) {
                piece = run;
            }
            if (n >= HCD_SG_ELEMENTS) {
                return 0;
            }
            e[n].SgPhysicalAddressLo = pa;
            e[n].SgPhysicalAddressHi = 0;
            e[n].SgTransferLength = piece;
            e[n].SgOffset = done;
            n++;
            pa += piece;
            done += piece;
            run -= piece;
        }
    }
    sg->List.SgElementCount = n;
    return 1;
}

/* hcdSgPa */
static ULONG sg_pa(const TD_SG *sg, ULONG offset, ULONG *pa)
{
    const USBPORT_SCATTER_GATHER_ELEMENT *e;
    ULONG i;

    for (i = 0; i < sg->List.SgElementCount; i++) {
        e = &sg->List.SgElement[i];
        if (offset >= e->SgOffset &&
            offset - e->SgOffset < e->SgTransferLength) {
            *pa = e->SgPhysicalAddressLo + (offset - e->SgOffset);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* What a built data TD must be                                        */
/* ------------------------------------------------------------------ */

static ULONG ceil_div(ULONG a, ULONG b)
{
    return (a + b - 1) / b;
}

/*
 * The data TRBs of one TD: `count` TRBs from `out` carrying `total` bytes
 * from virtual byte `va`. `controlData` says the first is a Data Stage TRB
 * and the TD has no IOC of its own (the Status Stage carries it). Returns
 * NULL, or what was wrong.
 */
static const char *data_td_wrong(const XHCI_TRB *out, ULONG count, ULONG va,
                                 ULONG total, ULONG mps, ULONG in,
                                 ULONG controlData)
{
    ULONG packets;
    ULONG pos;
    ULONG len;
    ULONG want;
    ULONG i;

    packets = ceil_div(total, mps);
    pos = 0;
    for (i = 0; i < count; i++) {
        len = XHCI_TRB_GET_LENGTH(out[i].Status);
        if (total != 0 && len == 0) {
            return "an empty TRB inside a TD with data";
        }
        if (out[i].Param0 != (total == 0 ? 0UL : pa_of(va + pos))) {
            return "a TRB's buffer address is not where its bytes are";
        }
        if (out[i].Param1 != 0) {
            return "a TRB's high address is not 0";
        }
        if ((out[i].Param0 & 0xFFFFUL) + len > 0x10000UL) {
            return "a TRB crosses a 64 KB line (spec 6.4.1)";
        }
        if ((out[i].Param0 & (PAGE - 1)) + len > PAGE) {
            return "a TRB crosses a page: the HCD's mapping has none that do";
        }
        pos += len;
        want = (i + 1 == count) ? 0UL : packets - pos / mps;
        if (want > 31) {
            want = 31;
        }
        if (XHCI_TRB_GET_TD_SIZE(out[i].Status) != want) {
            return "TD Size is not 4.11.2.4's packets remaining";
        }
        if (((out[i].Control & XHCI_TRB_CH) != 0) != (i + 1 != count)) {
            return "Chain is not set on exactly the non-last TRBs";
        }
        if (((out[i].Control & XHCI_TRB_ISP) != 0) != (in != 0)) {
            return "ISP is not set on exactly the IN TRBs";
        }
        if (controlData) {
            if ((out[i].Control & XHCI_TRB_IOC) != 0) {
                return "a Data Stage TRB carries IOC";
            }
        } else if (((out[i].Control & XHCI_TRB_IOC) != 0) !=
                   (i + 1 == count)) {
            return "IOC is not on the last TRB alone";
        }
        if (controlData && i == 0) {
            if (XHCI_TRB_GET_TYPE(out[i].Control) !=
                XHCI_TRB_TYPE_DATA_STAGE) {
                return "the first data TRB is not a Data Stage TRB";
            }
            if (((out[i].Control & XHCI_TRB_DIR_IN) != 0) != (in != 0)) {
                return "the Data Stage DIR is not the transfer's";
            }
        } else if (XHCI_TRB_GET_TYPE(out[i].Control) != XHCI_TRB_TYPE_NORMAL) {
            return "a data TRB that is not first is not a Normal TRB";
        }
        if ((out[i].Control & XHCI_TRB_CYCLE) != 0) {
            return "the builder set a Cycle bit (the ring's to set)";
        }
    }
    if (pos != total) {
        return "the TRB lengths do not add up to the chunk";
    }
    return NULL;
}

static void report(const char *wrong, const char *what, ULONG vo, ULONG length,
                   ULONG mps, ULONG regs)
{
    if (wrong != NULL) {
        printf("  [%s; page offset 0x%lX, length %lu, MPS %lu, %lu map "
               "registers] %s\n", layout_name, vo, length, mps, regs, wrong);
    }
    CHECK(wrong == NULL, what);
}

/* ------------------------------------------------------------------ */
/* 1. Bulk and interrupt: every chunk of a transfer                    */
/* ------------------------------------------------------------------ */

static const ULONG sweep_lengths[] = {
    1, 63, 64, 65, 512, 4095, 4096, 4097, 8191, 8192, 65535, 65536, 65537,
    0x1F000, 0x1FFFF, 0x20000, 0x20001, 0x30007, 300000
};
static const ULONG sweep_offsets[] = { 0, 1, 0x200, 0xFFF };
static const ULONG sweep_mps[] = { 8, 64, 512, 1024 };
static const ULONG sweep_regs[] = { 32, 64, 17, 2, 1 };

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/*
 * One whole bulk or interrupt transfer, chunk by chunk as hcd_io.c moves it:
 * each chunk planned, mapped, built and checked, and the chunks between them
 * the whole buffer, each non-final one a whole number of packets.
 */
static void normal_transfer(ULONG vo, ULONG length, ULONG mps, ULONG regs,
                            ULONG in)
{
    XHCI_NORMAL_REQUEST req;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];
    TD_SG sg;
    const char *wrong;
    ULONG offset;
    ULONG chunk;
    ULONG mapCount;
    ULONG count;
    ULONG answer;
    ULONG refused;

    wrong = NULL;
    refused = 0;
    offset = 0;
    while (offset < length && wrong == NULL) {
        if (!plan_chunk(KIND_NORMAL, vo + offset, length - offset, regs, mps,
                        &chunk, &mapCount)) {
            /* Refused only where a packet does not fit the grant at all -
             * hcd_io.c then fails the URB, which is the refusal's meaning
             * and not a coverage gap. */
            if (regs * PAGE - (vo + offset) % PAGE >= mps) {
                wrong = "a chunk the grant can hold was refused";
            }
            refused = 1;
            break;
        }
        if (chunk == 0) {
            wrong = "a planned chunk of nothing";
        } else if (mapCount > regs || mapCount > HCD_SG_ELEMENTS) {
            wrong = "a chunk spans more pages than were granted";
        } else if (chunk > XHCI_PIPE_TD_MAX_BYTES) {
            wrong = "a chunk is larger than a TD may be";
        } else if (offset + chunk < length && chunk % mps != 0) {
            wrong = "a non-final chunk ends mid-packet (USB 2.0 5.8.3)";
        } else if (!map_chunk(vo + offset, chunk, &sg)) {
            wrong = "the mapping ran out of SG elements";
        } else if (sg.List.SgElementCount != mapCount) {
            wrong = "elements != pages spanned: an element crosses a page";
        } else {
            req.TransferLength = chunk;
            req.DirectionIn = in;
            req.MaxPacketSize = mps;
            req.SgList = &sg.List;
            count = 0;
            answer = XhciXferBuildNormal(&req, in, out,
                                         XHCI_XFER_MAX_CONTROL_TRBS, &count);
            if (answer != XHCI_XFER_OK) {
                wrong = "the engine refused a chunk the HCD planned";
            } else if (count != sg.List.SgElementCount) {
                wrong = "not one TRB per page element";
            } else {
                wrong = data_td_wrong(out, count, vo + offset, chunk, mps,
                                      in, 0);
            }
        }
        offset += chunk;
    }
    if (wrong == NULL && !refused && offset != length) {
        wrong = "the chunks do not cover the transfer";
    }
    report(wrong, "bulk/interrupt transfer over the HCD's mapping", vo,
           length, mps, regs);
}

static void test_normal_sweep(void)
{
    ULONG l;
    ULONG o;
    ULONG m;
    ULONG r;
    ULONG y;

    for (y = 0; y < LAYOUTS; y++) {
        layout_select(y);
        for (o = 0; o < COUNT_OF(sweep_offsets); o++) {
            for (l = 0; l < COUNT_OF(sweep_lengths); l++) {
                for (m = 0; m < COUNT_OF(sweep_mps); m++) {
                    for (r = 0; r < COUNT_OF(sweep_regs); r++) {
                        normal_transfer(sweep_offsets[o], sweep_lengths[l],
                                        sweep_mps[m], sweep_regs[r],
                                        (l + m) & 1UL);
                    }
                }
            }
        }
    }
}

/* The numbers the sweep rests on, pinned one by one. */
static void test_normal_pinned(void)
{
    XHCI_NORMAL_REQUEST req;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];
    TD_SG sg;
    ULONG chunk;
    ULONG mapCount;
    ULONG count;

    CHECK_EQ(HCD_SG_ELEMENTS, XHCI_PIPE_CHUNK_ELEMENTS,
             "hcd.h's elements per chunk is xhci_pipe.h's");
    CHECK_EQ(HCD_SG_ELEMENTS, XHCI_XFER_MAX_DATA_TRBS,
             "and the engine's data TRB cap: a page-bounded chunk always "
             "fits the scratch hcd_io.c gives the builders");
    CHECK_EQ(HCD_SG_ELEMENTS * PAGE, XHCI_PIPE_TD_MAX_BYTES,
             "32 pages is the 128 KB TD cap");

    /* 300000 bytes at page offset 0x200, MPS 512, 32 registers: the first
     * chunk is 32 pages less the offset, 130560 bytes, already a multiple of
     * 512, so the second starts page-aligned and is the full 128 KB; the
     * last is the 38368 left. */
    CHECK_EQ(plan_chunk(KIND_NORMAL, 0x200, 300000, 32, 512, &chunk,
                        &mapCount), 1, "first chunk planned");
    CHECK_EQ(chunk, 32UL * 4096 - 0x200, "to the end of the 32nd page");
    CHECK_EQ(mapCount, 32, "spanning all 32 pages");
    CHECK_EQ(plan_chunk(KIND_NORMAL, 0x200 + 130560, 300000 - 130560, 32, 512,
                        &chunk, &mapCount), 1, "second chunk planned");
    CHECK_EQ(chunk, 131072, "page-aligned now: all 32 pages");
    CHECK_EQ(plan_chunk(KIND_NORMAL, 0x200 + 130560 + 131072,
                        300000 - 130560 - 131072, 32, 512, &chunk,
                        &mapCount), 1, "last chunk planned");
    CHECK_EQ(chunk, 38368, "the rest");
    CHECK_EQ(mapCount, 10, "page-aligned again: ten pages");
    /* MPS 1024 from 0x200: 127.5 packets of room, so 127, and the next
     * chunk starts at page offset 0xE00 with 124.5 packets of room. */
    CHECK_EQ(plan_chunk(KIND_NORMAL, 0x200, 300000, 32, 1024, &chunk,
                        &mapCount), 1, "MPS 1024");
    CHECK_EQ(chunk, 127UL * 1024, "rounded down to 127 packets");
    CHECK_EQ(plan_chunk(KIND_NORMAL, 0x200 + 127UL * 1024,
                        300000 - 127UL * 1024, 32, 1024, &chunk, &mapCount),
             1, "the next");
    CHECK_EQ(chunk, 124UL * 1024, "124 packets from page offset 0xE00");
    /* MPS 1023 does not divide the room: rounded down. */
    CHECK_EQ(plan_chunk(KIND_NORMAL, 0, 300000, 32, 1023, &chunk, &mapCount),
             1, "an odd packet size");
    CHECK_EQ(chunk, 131072UL - (131072UL % 1023), "ends on a packet bound");
    CHECK_EQ(plan_chunk(KIND_NORMAL, 0xFFF, 4096, 1, 512, &chunk, &mapCount),
             0, "one register, one byte of it reachable: refused");

    /* A full 32-page chunk contiguous across 0x20000 and 0x30000: one TRB per
     * page, none across the lines, the last one 0xFFF bytes short. */
    layout_contiguous(0x0001E000UL, "contiguous from 0x0001E000");
    CHECK_EQ(map_chunk(0x0, 32UL * 4096, &sg), 1, "mapped");
    CHECK_EQ(sg.List.SgElementCount, 32, "32 page elements");
    req.TransferLength = 32UL * 4096;
    req.DirectionIn = 1;
    req.MaxPacketSize = 512;
    req.SgList = &sg.List;
    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                 &count), XHCI_XFER_OK, "built");
    CHECK_EQ(count, 32, "32 TRBs");
    CHECK_EQ(out[1].Param0, 0x0001F000UL, "TRB 1 ends on 0x20000");
    CHECK_EQ(out[2].Param0, 0x00020000UL, "TRB 2 starts on it");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[0].Status), 31,
             "256 packets remaining saturates at 31");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[29].Status), 16,
             "after 30 pages, 16 packets remain");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[30].Status), 8, "after 31, 8");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[31].Status), 0, "the last TRB 0");

    /* The last page below 4 GB: the engine's "distance to the next 64 KB
     * line" wraps to 0 there and must still pass the page whole. */
    layout_contiguous(0xFFFFF000UL - (BUF_PAGES - 1) * PAGE,
                      "contiguous to 0xFFFFFFFF");
    CHECK_EQ(map_chunk((BUF_PAGES - 2) * PAGE + 0x800, 0x1800, &sg), 1,
             "mapped up to the top byte");
    CHECK_EQ(sg.List.SgElementCount, 2, "two elements");
    req.TransferLength = 0x1800;
    req.DirectionIn = 0;
    req.MaxPacketSize = 64;
    CHECK_EQ(XhciXferBuildNormal(&req, 0, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                 &count), XHCI_XFER_OK, "built");
    CHECK_EQ(count, 2, "two TRBs");
    CHECK_EQ(out[1].Param0, 0xFFFFF000UL, "the last page");
    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[1].Status), 0x1000, "all of it");

    /* hcd_io.c's zero-length transfer: Chunk 0, nothing mapped. */
    sg_clear(&sg);
    req.TransferLength = 0;
    req.DirectionIn = 0;
    req.MaxPacketSize = 512;
    CHECK_EQ(XhciXferBuildNormal(&req, 0, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                 &count), XHCI_XFER_OK, "a ZLP built");
    CHECK_EQ(count, 1, "as one TRB");
    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[0].Status), 0, "of length 0");
    CHECK_EQ(out[0].Control & (XHCI_TRB_IOC | XHCI_TRB_CH), XHCI_TRB_IOC,
             "that completes and does not chain");
}

/* ------------------------------------------------------------------ */
/* 2. Control: one TD per stage, never split                           */
/* ------------------------------------------------------------------ */

static void control_transfer(ULONG vo, ULONG length, ULONG mps0, ULONG regs,
                             ULONG in)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_CONTROL_LAYOUT lay;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];
    TD_SG sg;
    const char *wrong;
    const XHCI_TRB *status;
    ULONG chunk;
    ULONG mapCount;
    ULONG answer;
    ULONG wantTrt;

    wrong = NULL;
    if (!plan_chunk(KIND_CONTROL, vo, length, regs, mps0, &chunk,
                    &mapCount)) {
        if (length <= (regs < HCD_SG_ELEMENTS ? regs : HCD_SG_ELEMENTS) *
                          PAGE - vo) {
            wrong = "a control transfer that fits was refused";
        }
        report(wrong, "control transfer refused only when it cannot fit",
               vo, length, mps0, regs);
        return;
    }
    if (chunk != length) {
        wrong = "a control transfer was split";
    } else if (!map_chunk(vo, chunk, &sg)) {
        wrong = "the mapping ran out of SG elements";
    } else {
        req.Setup.bmRequestType = (UCHAR)(in ? 0xC0 : 0x40);
        req.Setup.bRequest = 0x01;
        req.Setup.wValue = 0;
        req.Setup.wIndex = 0;
        req.Setup.wLength = (USHORT)(length > 0xFFFF ? 0xFFFF : length);
        req.TransferLength = chunk;
        req.TransferFlagsIn = in;
        req.MaxPacketSize = mps0;
        req.SgList = &sg.List;
        answer = XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                      &lay);
        wantTrt = (length == 0) ? XHCI_TRB_TRT_NO_DATA
                                : (in ? XHCI_TRB_TRT_IN_DATA
                                      : XHCI_TRB_TRT_OUT_DATA);
        if (answer != XHCI_XFER_OK) {
            wrong = "the engine refused a control transfer the HCD planned";
        } else if (lay.TrbCount != sg.List.SgElementCount + 2) {
            wrong = "not Setup + one TRB per page element + Status";
        } else if (XHCI_TRB_GET_TYPE(out[0].Control) !=
                       XHCI_TRB_TYPE_SETUP_STAGE ||
                   (out[0].Control & XHCI_TRB_IDT) == 0 ||
                   (out[0].Control & XHCI_TRB_TRT(3)) !=
                       XHCI_TRB_TRT(wantTrt)) {
            wrong = "the Setup Stage TRB's type, IDT or TRT";
        } else if (length != 0 &&
                   (lay.DataFirst != 1 ||
                    lay.DataCount != sg.List.SgElementCount)) {
            wrong = "the layout does not place the data where it is";
        } else if (length != 0 &&
                   (wrong = data_td_wrong(&out[1], lay.DataCount, vo, chunk,
                                          mps0, in, 1)) != NULL) {
            /* wrong is set */
        } else {
            status = &out[lay.TrbCount - 1];
            if (XHCI_TRB_GET_TYPE(status->Control) !=
                    XHCI_TRB_TYPE_STATUS_STAGE ||
                (status->Control & XHCI_TRB_IOC) == 0 ||
                ((status->Control & XHCI_TRB_DIR_IN) != 0) !=
                    (length == 0 || !in)) {
                wrong = "the Status Stage TRB's type, IOC or direction";
            }
        }
    }
    report(wrong, "control transfer over the HCD's mapping", vo, length, mps0,
           regs);
}

static void test_control_sweep(void)
{
    static const ULONG lengths[] = {
        0, 1, 8, 18, 64, 255, 4095, 4096, 4097, 65535, 0x1F000, 0x20000
    };
    static const ULONG mps0[] = { 8, 64 };
    ULONG l;
    ULONG o;
    ULONG m;
    ULONG r;
    ULONG y;

    for (y = 0; y < LAYOUTS; y++) {
        layout_select(y);
        for (o = 0; o < COUNT_OF(sweep_offsets); o++) {
            for (l = 0; l < COUNT_OF(lengths); l++) {
                for (m = 0; m < COUNT_OF(mps0); m++) {
                    for (r = 0; r < COUNT_OF(sweep_regs); r++) {
                        control_transfer(sweep_offsets[o], lengths[l],
                                         mps0[m], sweep_regs[r],
                                         (l + o) & 1UL);
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* 3. Published and answered: the residual over page-sized TRBs        */
/* ------------------------------------------------------------------ */

static ULONG event_dw2(ULONG completionCode, ULONG residual)
{
    return (residual & 0x00FFFFFFUL) | ((completionCode & 0xFFUL) << 24);
}

static ULONG event_dw3(void)
{
    return (32UL << 10) | (EV_DCI << 16) | (EV_SLOT << 24);
}

/*
 * A 32-page IN chunk at page offset 0x300, published onto a pool-sized ring,
 * and a short packet landing on each TRB in turn: the bytes reported are the
 * pages before it plus what that TRB moved, whichever TRB it is, and the
 * conforming controller's tail on the last TRB ends it.
 */
static void test_submit_and_short(void)
{
    static XHCI_TRB mem[XHCI_POOL_RING_TRBS];
    XHCI_RING ring;
    XHCI_TRANSFER_QUEUE queue;
    XHCI_TRANSFER transfer;
    XHCI_NORMAL_REQUEST req;
    XHCI_XFER_EVENT_RESULT result;
    XHCI_TRB scratch[XHCI_XFER_MAX_CONTROL_TRBS];
    TD_SG sg;
    ULONG chunk;
    ULONG mapCount;
    ULONG before;
    ULONG len;
    ULONG bad;
    ULONG k;
    ULONG i;

    layout_scattered();
    CHECK_EQ(plan_chunk(KIND_NORMAL, 0x300, 1000000, 32, 512, &chunk,
                        &mapCount), 1, "a full chunk planned");
    CHECK_EQ(map_chunk(0x300, chunk, &sg), 1, "and mapped");
    CHECK_EQ(sg.List.SgElementCount, 32, "into 32 elements");
    req.TransferLength = chunk;
    req.DirectionIn = 1;
    req.MaxPacketSize = 512;
    req.SgList = &sg.List;

    bad = 0;
    for (k = 0; k < 32; k++) {
        for (i = 0; i < XHCI_POOL_RING_TRBS; i++) {
            mem[i].Param0 = 0;
            mem[i].Param1 = 0;
            mem[i].Status = 0;
            mem[i].Control = 0;
        }
        if (XhciRingInit(&ring, mem, RING_PA, XHCI_POOL_RING_TRBS,
                         XHCI_RING_KIND_ENDPOINT) != XHCI_RING_OK) {
            bad++;
            continue;
        }
        XhciXferQueueInit(&queue);
        if (XhciXferSubmitNormal(&queue, &ring, &req, 1, &transfer,
                                 (PVOID)0x1234, scratch,
                                 XHCI_XFER_MAX_CONTROL_TRBS) !=
                XHCI_XFER_OK ||
            transfer.TrbCount != 32) {
            bad++;
            continue;
        }
        len = XHCI_TRB_GET_LENGTH(mem[k].Status);
        before = 0;
        for (i = 0; i < k; i++) {
            before += XHCI_TRB_GET_LENGTH(mem[i].Status);
        }
        if (k + 1 < 32) {
            /* Half of TRB k arrives, then the tail on the IOC TRB. */
            (VOID)XhciXferEvent(&queue, &ring, EV_SLOT, EV_DCI,
                                XhciRingTrbPA(&ring, k),
                                event_dw2(XHCI_CC_SHORT_PACKET, len / 2),
                                event_dw3(), &result);
            if (result.Action != XHCI_XFER_ACTION_NONE) {
                bad++;
                continue;
            }
            (VOID)XhciXferEvent(&queue, &ring, EV_SLOT, EV_DCI,
                                XhciRingTrbPA(&ring, 31),
                                event_dw2(XHCI_CC_SHORT_PACKET, 0),
                                event_dw3(), &result);
        } else {
            (VOID)XhciXferEvent(&queue, &ring, EV_SLOT, EV_DCI,
                                XhciRingTrbPA(&ring, k),
                                event_dw2(XHCI_CC_SHORT_PACKET, len / 2),
                                event_dw3(), &result);
        }
        if (result.Action != XHCI_XFER_ACTION_COMPLETE ||
            result.Completed != &transfer ||
            transfer.BytesTransferred != before + (len - len / 2) ||
            XhciRingFree(&ring) != XhciRingCapacity(&ring)) {
            printf("  short packet on TRB %lu: action %lu, bytes %lu, want "
                   "%lu\n", k, result.Action, transfer.BytesTransferred,
                   before + (len - len / 2));
            bad++;
        }
    }
    CHECK_EQ(bad, 0, "a short packet on any of 32 page TRBs reports the bytes "
                     "before it plus its own, and frees the TD");

    /* And the whole chunk, answered on its last TRB. */
    XhciRingInit(&ring, mem, RING_PA, XHCI_POOL_RING_TRBS,
                 XHCI_RING_KIND_ENDPOINT);
    XhciXferQueueInit(&queue);
    CHECK_EQ(XhciXferSubmitNormal(&queue, &ring, &req, 1, &transfer,
                                  (PVOID)0x1234, scratch,
                                  XHCI_XFER_MAX_CONTROL_TRBS), XHCI_XFER_OK,
             "published");
    CHECK_EQ(XhciXferEvent(&queue, &ring, EV_SLOT, EV_DCI,
                           XhciRingTrbPA(&ring, 31),
                           event_dw2(XHCI_CC_SUCCESS, 0), event_dw3(),
                           &result), XHCI_XFER_OK, "answered");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completed");
    CHECK_EQ(transfer.BytesTransferred, chunk, "every byte of the chunk");
}

/* ------------------------------------------------------------------ */
/* 4. Isochronous: hcdIsoFill's block, built                           */
/* ------------------------------------------------------------------ */

static TD_ISO_BLOCK iso_block;
static XHCI_TRB iso_out[XHCI_XFER_MAX_ISO_TRBS];
static XHCI_ISO_LAYOUT iso_layout;

/* hcdIsoFill over a URB table of `n` packets, all `size` bytes but packet
 * `empty` (0 bytes; n for none), and hcdIsoPublish's stamping. */
static ULONG iso_fill(ULONG vo, ULONG n, ULONG size, ULONG empty,
                      ULONG start, ULONG interval, TD_SG *sg,
                      ULONG *bufferLength)
{
    XHCI_PIPE_ISO_PACKET urb[XHCI_XFER_MAX_ISO_PACKETS];
    USBPORT_ISO_PACKET *p;
    ULONG lengths[2];
    ULONG off;
    ULONG pa;
    ULONG i;
    ULONG b;

    off = 0;
    for (i = 0; i < n; i++) {
        urb[i].Offset = off;
        urb[i].Length = 0;
        urb[i].Status = 0;
        off += (i == empty) ? 0 : size;
    }
    *bufferLength = off;
    if (XhciPipeIsoCheck(urb, n, off, XHCI_XFER_MAX_ISO_PACKETS, size) !=
            XHCI_PIPE_OK ||
        !map_chunk(vo, off, sg)) {
        return 0;
    }
    iso_block.Block.Signature = USBPORT_ISO_SIGNATURE;
    iso_block.Block.NumberOfPackets = n;
    iso_block.Block.SgElementCount = sg->List.SgElementCount;
    iso_block.Block.Reserved = 0;
    for (i = 0; i < n; i++) {
        p = &iso_block.Block.Packet[i];
        for (b = 0; b < sizeof(*p); b++) {
            ((UCHAR *)p)[b] = 0;
        }
        p->Length = XhciPipeIsoLength(urb, n, off, i);
        p->FragmentCount = 1;
        if (p->Length == 0) {
            if (!sg_pa(sg, 0, &pa)) {
                return 0;
            }
            p->Fragment0AddressLo = pa;
        } else {
            p->FragmentCount = XhciPipeIsoFragments(vo, urb[i].Offset,
                                                    p->Length, lengths);
            if (p->FragmentCount == 0 || !sg_pa(sg, urb[i].Offset, &pa)) {
                return 0;
            }
            p->Fragment0Length = lengths[0];
            p->Fragment0AddressLo = pa;
            if (p->FragmentCount == 2) {
                if (!sg_pa(sg, urb[i].Offset + lengths[0], &pa)) {
                    return 0;
                }
                p->Fragment1Length = lengths[1];
                p->Fragment1AddressLo = pa;
            }
        }
        p->FrameNumber = start + XhciPipeIsoFrameOf(i, interval);
    }
    return 1;
}

static void iso_request(XHCI_ISO_REQUEST *req, ULONG interval, ULONG mps,
                        ULONG allowed, ULONG now)
{
    req->Iso = &iso_block.Block;
    req->DirectionIn = 0;
    req->MaxPacketSize = mps;
    req->MaxBurstSize = 0;
    req->MaxEsitPayload = mps;
    req->PacketsPerFrame = (interval <= 3UL) ? (8UL >> interval) : 0UL;
    req->Frames.Allowed = allowed;
    req->Frames.CurrentFrame = now;
    req->Frames.IstFrames = 2;
}

/* The TDs of a built block: one per packet, each carrying its packet's bytes
 * from where they are, scheduled all one way. */
static const char *iso_wrong(ULONG vo, ULONG n, const XHCI_ISO_REQUEST *req)
{
    const USBPORT_ISO_PACKET *p;
    const XHCI_TRB *t;
    ULONG packetVa;
    ULONG index;
    ULONG pos;
    ULONG len;
    ULONG i;
    ULONG j;

    if (iso_layout.TdCount != n) {
        return "not one TD per packet";
    }
    if (iso_layout.FrameIdsUsed != XhciXferIsoUsesFrameIds(req)) {
        return "the build and XhciXferIsoUsesFrameIds disagree";
    }
    if (iso_layout.CadenceMismatch != (req->PacketsPerFrame == 0 ? 1UL : 0UL)) {
        return "XhciPipeIsoFrameOf's stamps disagree with the Interval";
    }
    index = 0;
    packetVa = vo;
    for (i = 0; i < n; i++) {
        p = &iso_block.Block.Packet[i];
        pos = 0;
        for (j = 0; j < iso_layout.TdLengths[i]; j++) {
            t = &iso_out[index + j];
            len = XHCI_TRB_GET_LENGTH(t->Status);
            if (XHCI_TRB_GET_TYPE(t->Control) !=
                (ULONG)(j == 0 ? XHCI_TRB_TYPE_ISOCH : XHCI_TRB_TYPE_NORMAL)) {
                return "a TD is not one Isoch TRB then Normal TRBs";
            }
            if (p->Length != 0 && t->Param0 != pa_of(packetVa + pos)) {
                return "an isochronous TRB's address is not its bytes'";
            }
            if ((t->Param0 & (PAGE - 1)) + len > PAGE) {
                return "an isochronous TRB crosses a page";
            }
            if (((t->Control & XHCI_TRB_CH) != 0) !=
                    (j + 1 != iso_layout.TdLengths[i]) ||
                ((t->Control & XHCI_TRB_IOC) != 0) !=
                    (j + 1 == iso_layout.TdLengths[i])) {
                return "Chain and IOC are not chain-then-IOC per TD";
            }
            if (j == 0) {
                if (iso_layout.FrameIdsUsed) {
                    if ((t->Control & XHCI_TRB_SIA) != 0 ||
                        XHCI_TRB_GET_FRAME_ID(t->Control) !=
                            (p->FrameNumber & XHCI_FRAME_ID_MASK)) {
                        return "a Frame ID is not the packet's frame";
                    }
                } else if ((t->Control & XHCI_TRB_SIA) == 0) {
                    return "SIA is clear on a request scheduled ASAP";
                }
            }
            pos += len;
        }
        if (pos != p->Length) {
            return "a TD does not carry its packet's bytes";
        }
        if (p->Length > 0 &&
            iso_layout.TdLengths[i] != p->FragmentCount) {
            return "not one TRB per page fragment";
        }
        index += iso_layout.TdLengths[i];
        packetVa += p->Length;
    }
    if (index != iso_layout.TrbCount) {
        return "the TDs do not add up to the TRB count";
    }
    return NULL;
}

static void iso_case(ULONG vo, ULONG n, ULONG size, ULONG empty,
                     ULONG interval, ULONG allowed, ULONG start)
{
    XHCI_ISO_REQUEST req;
    TD_SG sg;
    const char *wrong;
    ULONG length;
    ULONG answer;

    wrong = NULL;
    if (!iso_fill(vo, n, size, empty, start, interval, &sg, &length)) {
        wrong = "hcdIsoFill's model refused the URB";
    } else {
        iso_request(&req, interval, size < 8 ? 8 : size, allowed, 1000);
        answer = XhciXferBuildIso(&req, 0, iso_out, XHCI_XFER_MAX_ISO_TRBS,
                                  &iso_layout);
        if (answer != XHCI_XFER_OK) {
            wrong = "the engine refused a block hcdIsoFill built";
        } else {
            wrong = iso_wrong(vo, n, &req);
        }
    }
    if (wrong != NULL) {
        printf("  [%s; page offset 0x%lX, %lu x %lu bytes, interval %lu, "
               "allowed %lu, start %lu] %s\n", layout_name, vo, n, size,
               interval, allowed, start, wrong);
    }
    CHECK(wrong == NULL, "isochronous block over the HCD's mapping");
}

static void test_iso_sweep(void)
{
    static const ULONG sizes[] = { 1, 176, 192, 1023, 1024 };
    static const ULONG counts[] = { 1, 8, 10 };
    static const ULONG starts[] = { 1003, 1500, 1890, 2900 };
    ULONG y;
    ULONG o;
    ULONG s;
    ULONG c;
    ULONG v;
    ULONG f;

    for (y = 0; y < LAYOUTS; y++) {
        layout_select(y);
        for (o = 0; o < COUNT_OF(sweep_offsets); o++) {
            for (s = 0; s < COUNT_OF(sizes); s++) {
                for (c = 0; c < COUNT_OF(counts); c++) {
                    for (v = 0; v <= 4; v++) {
                        for (f = 0; f < COUNT_OF(starts); f++) {
                            iso_case(sweep_offsets[o], counts[c], sizes[s],
                                     (c == 2) ? 3UL : counts[c], v,
                                     f & 1UL ? 0UL : 1UL, starts[f]);
                            iso_case(sweep_offsets[o], counts[c], sizes[s],
                                     counts[c], v, 1, starts[f]);
                        }
                    }
                }
            }
        }
    }
}

/*
 * The Frame ID decision as hcdIsoPublish asks it before an explicit
 * StartFrame, against blocks built the way it builds them.
 */
static void test_iso_frame_decision(void)
{
    XHCI_ISO_REQUEST req;
    TD_SG sg;
    ULONG length;

    layout_contiguous(0x00300000UL, "contiguous from 0x00300000");

    /* Ten 1 ms packets from IST + 1 frames ahead: every one in the window. */
    CHECK_EQ(iso_fill(0, 10, 192, 10, 1003, 3, &sg, &length), 1, "filled");
    iso_request(&req, 3, 192, 1, 1000);
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 1, "Frame IDs from IST + 1");
    req.Frames.Allowed = 0;
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 0, "not without CFC");
    req.Frames.Allowed = 1;
    req.Frames.CurrentFrame = 1001;
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 0,
             "a first packet inside the IST is refused, not clamped");

    /* The last packet one frame past the 895-frame window end. */
    CHECK_EQ(iso_fill(0, 10, 192, 10, 1000 + 895 - 9, 3, &sg, &length), 1,
             "filled");
    iso_request(&req, 3, 192, 1, 1000);
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 1, "the last packet on 895");
    CHECK_EQ(iso_fill(0, 10, 192, 10, 1000 + 895 - 9, 3, &sg, &length), 1,
             "filled");
    iso_block.Block.Packet[9].FrameNumber++;
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 0, "one past it");

    /* Across the 2048-frame wrap of the Frame ID: 2040 + 3 .. 2040 + 12. */
    CHECK_EQ(iso_fill(0, 10, 192, 10, 2043, 3, &sg, &length), 1, "filled");
    iso_request(&req, 3, 192, 1, 2040);
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 1, "across the Frame ID wrap");
    CHECK_EQ(XhciXferBuildIso(&req, 0, iso_out, XHCI_XFER_MAX_ISO_TRBS,
                              &iso_layout), XHCI_XFER_OK, "built");
    CHECK_EQ(XHCI_TRB_GET_FRAME_ID(iso_out[iso_layout.TrbCount - 1].Control),
             (2043UL + 9) & 0x7FF, "the last packet's Frame ID wrapped to 4");

    /* High Speed, Interval 0..2: 8, 4 and 2 packets a frame. */
    CHECK_EQ(iso_fill(0, 16, 1024, 16, 1010, 0, &sg, &length), 1, "filled");
    iso_request(&req, 0, 1024, 1, 1000);
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 1, "eight a frame");
    CHECK_EQ(iso_block.Block.Packet[15].FrameNumber, 1011, "16 in two frames");
    CHECK_EQ(iso_fill(0, 16, 1024, 16, 1010, 2, &sg, &length), 1, "filled");
    iso_request(&req, 2, 1024, 1, 1000);
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 1, "two a frame");
    req.PacketsPerFrame = 4;
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 0,
             "stamps at another cadence than the Interval's: refused");

    /* Interval 4 (2 ms): the cadence the engine cannot state. */
    CHECK_EQ(iso_fill(0, 4, 192, 4, 1010, 4, &sg, &length), 1, "filled");
    iso_request(&req, 4, 192, 1, 1000);
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 0,
             "PacketsPerFrame 0: no Frame IDs, so hcdIsoPublish refuses an "
             "explicit StartFrame as BAD_START_FRAME");

    /* The request shapes BuildIso refuses are refused here too, before a
     * packet is read (src\xhci_xfer.c, task 26-A.9's fix). */
    CHECK_EQ(XhciXferIsoUsesFrameIds(NULL), 0, "no request");
    CHECK_EQ(iso_fill(0, 4, 192, 4, 1010, 3, &sg, &length), 1, "filled");
    iso_request(&req, 3, 192, 1, 1000);
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 1, "(the good block)");
    iso_block.Block.NumberOfPackets = 0;
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 0,
             "no packets: not a request that uses anything");
    iso_block.Block.NumberOfPackets = XHCI_XFER_MAX_ISO_PACKETS + 1;
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 0,
             "more packets than a block holds: refused unread");
    iso_block.Block.NumberOfPackets = 4;
    iso_block.Block.Signature = 0;
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 0, "no signature");
    iso_block.Block.Signature = USBPORT_ISO_SIGNATURE;
    req.Iso = NULL;
    CHECK_EQ(XhciXferIsoUsesFrameIds(&req), 0, "no block");
}

/*
 * The block's ceiling. hcd_io.c admits up to XHCI_XFER_MAX_ISO_PACKETS
 * packets and builds into XHCI_XFER_MAX_ISO_TRBS TRBs of scratch, the same
 * number, so a URB at the packet cap fits only while no packet crosses a
 * page. One that does is refused whole as ISO_TOO_LARGE - which hcdIsoPublish
 * counts as IsoRefusalsTooLarge and fails the URB - and never half-built.
 */
static void test_iso_ceiling(void)
{
    XHCI_ISO_REQUEST req;
    TD_SG sg;
    ULONG length;

    layout_contiguous(0x00300000UL, "contiguous from 0x00300000");
    CHECK_EQ(XHCI_XFER_MAX_ISO_PACKETS, XHCI_XFER_MAX_ISO_TRBS,
             "the packet cap is the TRB cap");

    /* 62 x 64 bytes from page offset 0: 3968 bytes, one page, 62 TRBs. */
    CHECK_EQ(iso_fill(0, XHCI_XFER_MAX_ISO_PACKETS, 64,
                      XHCI_XFER_MAX_ISO_PACKETS, 1003, 3, &sg, &length), 1,
             "filled");
    iso_request(&req, 3, 64, 0, 1000);
    CHECK_EQ(XhciXferBuildIso(&req, 0, iso_out, XHCI_XFER_MAX_ISO_TRBS,
                              &iso_layout), XHCI_XFER_OK,
             "62 packets inside one page: built");
    CHECK_EQ(iso_layout.TrbCount, XHCI_XFER_MAX_ISO_TRBS, "62 TRBs exactly");

    /* 62 x 192 bytes: 11904 bytes, two page crossings, 64 TRBs. */
    CHECK_EQ(iso_fill(0, XHCI_XFER_MAX_ISO_PACKETS, 192,
                      XHCI_XFER_MAX_ISO_PACKETS, 1003, 3, &sg, &length), 1,
             "filled");
    iso_request(&req, 3, 192, 0, 1000);
    CHECK_EQ(XhciXferBuildIso(&req, 0, iso_out, XHCI_XFER_MAX_ISO_TRBS,
                              &iso_layout), XHCI_XFER_ISO_TOO_LARGE,
             "62 FS audio packets that cross two pages: refused as too "
             "large, not truncated");
}

int main(void)
{
    test_normal_pinned();
    test_normal_sweep();
    test_control_sweep();
    test_submit_and_short();
    test_iso_frame_decision();
    test_iso_sweep();
    test_iso_ceiling();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
