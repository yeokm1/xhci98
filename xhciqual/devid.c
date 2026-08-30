/*
 * devid.c - C8 device identification (informational, DOSUSB-style device
 * listing): for each USB2 port that passed the C6 reset, Enable Slot ->
 * Address Device (BSR=0) -> GET_DESCRIPTOR over EP0, and report VID/PID,
 * class, and strings. Exercises the full command + control-transfer path
 * the driver will rely on, but never affects the section 8 go/no-go
 * verdict (design doc 01, C8).
 *
 * Context and TRB layouts are transcribed from docs/usb-xhci-info/xhci-data-structures.md
 * (contexts spec 6.2, TRBs spec 6.4). Do not edit a bit position here
 * without checking that document.
 */

#include <stdio.h>
#include <string.h>
#include "qual.h"

#define EP0_TRBS  16
#define DESC_BUF  256

/* USB standard requests / descriptor types used here */
#define REQ_GET_DESCRIPTOR  6
#define DT_DEVICE           1
#define DT_CONFIG           2
#define DT_STRING           3
#define DT_INTERFACE        4

typedef struct {
    int  slot;
    int  port;
    u32  speed;              /* PORTSC PSIV */
    int  speed_class;        /* USB_SPEED_* decoded from this port's PSI */
    u32 *inctx;   u32 inctx_phys;    /* Input Context (33 entries) */
    u32 *devctx;  u32 devctx_phys;   /* Output Device Context */
    TRB *ring;    u32 ring_phys;     /* EP0 transfer ring */
    int  enq, pcs;
    u8  *buf;     u32 buf_phys;      /* descriptor DMA buffer */
} EP0DEV;

/* context index -> dword pointer, honoring CSZ (32- or 64-byte stride) */
static u32 *ctx_at(CTRL *c, u32 *base, int idx)
{
    return base + ((u32)idx * (u32)c->csz) / 4;
}

/* ------------------------------------------------------------------ */
/* EP0 transfer ring                                                  */
/* ------------------------------------------------------------------ */

static u32 ep0_put(EP0DEV *d, u32 p0, u32 p1, u32 st, u32 ctl)
{
    TRB *t = &d->ring[d->enq];
    u32 phys = d->ring_phys + (u32)d->enq * 16;

    t->p0 = p0;
    t->p1 = p1;
    t->st = st;
    t->ctl = ctl | (d->pcs ? TRB_C : 0);

    d->enq++;
    if (d->enq == EP0_TRBS - 1) {       /* hand the Link TRB to the HC */
        TRB *l = &d->ring[EP0_TRBS - 1];
        l->p0 = d->ring_phys;
        l->p1 = 0;
        l->st = 0;
        l->ctl = TRB_TYPE(TRB_T_LINK) | TRB_LINK_TC |
                 (d->pcs ? TRB_C : 0);
        d->pcs ^= 1;
        d->enq = 0;
    }
    return phys;
}

/*
 * IN (or no-data) control transfer on EP0. Setup/Data/Status are each
 * their own TD (spec 4.11.2.2); IOC only on Status, ISP on Data so a
 * short IN packet is seen and its residual credited. Returns 1 and the
 * byte count in *got, or 0 with a note in err.
 */
static int ep0_ctrl_in(CTRL *c, EP0DEV *d, u8 breq, u16 wval, u16 widx,
                       u16 wlen, u32 *got, char *err)
{
    u32 setup_p0, setup_p1, data_phys = 0, status_phys;
    unsigned ms;
    int status_seen = 0;
    TRB e;

    if (wlen > DESC_BUF)
        wlen = DESC_BUF;
    *got = wlen;

    setup_p0 = 0x80UL | ((u32)breq << 8) | ((u32)wval << 16);
    setup_p1 = (u32)widx | ((u32)wlen << 16);
    ep0_put(d, setup_p0, setup_p1, 8,
            TRB_TYPE(TRB_T_SETUP) | TRB_IDT |
            (wlen ? TRB_TRT_IN : TRB_TRT_NONE));
    if (wlen) {
        memset(d->buf, 0, wlen);
        data_phys = ep0_put(d, d->buf_phys, 0, wlen,
                            TRB_TYPE(TRB_T_DATA) | TRB_DIR_IN | TRB_ISP);
    }
    /* Status direction is opposite the data stage; IN if no data stage */
    status_phys = ep0_put(d, 0, 0, 0,
                          TRB_TYPE(TRB_T_STATUS) | TRB_IOC |
                          (wlen ? 0 : TRB_DIR_IN));

    WR32(c->db + 4 * d->slot, 1);       /* DB[slot] = DCI 1 (EP0) */

    for (ms = 0; ms < 1000 && !status_seen; ms++) {
        /* Drain to empty rather than breaking out on the Status event: the
         * empty read is what releases EHB (see cmd_wait in bringup.c). */
        while (evt_next(c, &e)) {
            u8 code;

            if (TRB_GET_TYPE(e.ctl) != TRB_T_XFER_EVT ||
                (int)TRB_GET_SLOT(e.ctl) != d->slot)
                continue;
            code = (u8)TRB_GET_CODE(e.st);
            if (e.p0 == data_phys && wlen != 0 &&
                (code == CC_SHORT_PKT || code == CC_SUCCESS)) {
                *got = wlen - TRB_GET_RESID(e.st);
                continue;               /* Status event still to come */
            }
            if (e.p0 == status_phys &&
                (code == CC_SUCCESS || code == CC_SHORT_PKT)) {
                status_seen = 1;
                continue;
            }
            if (status_seen)
                continue;               /* stragglers after completion */
            sprintf(err, "EP0 transfer failed (completion code %u%s)",
                    code, code == CC_STALL ? " = STALL" : "");
            return 0;
        }
        if (!status_seen)
            msleep(1);
    }
    if (!status_seen) {
        strcpy(err, "EP0 transfer timed out");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* commands                                                           */
/* ------------------------------------------------------------------ */

static int do_cmd(CTRL *c, const char *what, u32 p0, u32 ctl,
                  u8 *slotid, char *err)
{
    u32 phys = cmd_submit(c, p0, 0, ctl);
    u8 code;

    if (!cmd_wait(c, phys, 1000, 0, &code, slotid)) {
        sprintf(err, "%s command timed out", what);
        return 0;
    }
    if (code != CC_SUCCESS) {
        sprintf(err, "%s command failed (completion code %u)", what, code);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* descriptor parsing helpers                                         */
/* ------------------------------------------------------------------ */

static u16 get16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

/* UTF-16LE string descriptor -> ASCII, best effort */
static void str_from_desc(const u8 *buf, u32 got, char *out)
{
    u32 i, len = buf[0];
    int o = 0;

    if (len > got)
        len = got;
    if (len < 2 || buf[1] != DT_STRING)
        return;
    for (i = 2; i + 1 < len && o < DEVSTR_LEN - 1; i += 2)
        out[o++] = (buf[i + 1] == 0 && buf[i] >= 32 && buf[i] < 127)
                   ? (char)buf[i] : '?';
    out[o] = 0;
}

const char *usb_class_name(u8 cls)
{
    switch (cls) {
    case 0x00: return "per-interface";
    case 0x01: return "Audio";
    case 0x02: return "CDC";
    case 0x03: return "HID";
    case 0x06: return "Still Image";
    case 0x07: return "Printer";
    case 0x08: return "Mass Storage";
    case 0x09: return "Hub";
    case 0x0A: return "CDC-Data";
    case 0x0B: return "Smart Card";
    case 0x0E: return "Video";
    case 0xE0: return "Wireless";
    case 0xEF: return "Miscellaneous";
    case 0xFF: return "Vendor-specific";
    default:   return "class?";
    }
}

/* ------------------------------------------------------------------ */
/* per-device identification                                          */
/* ------------------------------------------------------------------ */

/* EP0 Max Packet Size before the descriptor is read (spec 4.3) */
static int mps0_initial(int speed_class)
{
    return (speed_class == USB_SPEED_HIGH) ? 64 : 8;
}

static void fill_ep0_input_ctx(CTRL *c, EP0DEV *d, int mps0)
{
    u32 *icc = ctx_at(c, d->inctx, 0);
    u32 *sc  = ctx_at(c, d->inctx, 1);
    u32 *ep  = ctx_at(c, d->inctx, 2);

    memset(d->inctx, 0, 33UL * (u32)c->csz);
    icc[1] = 0x3;                       /* Add flags A0 + A1 */
    /* Slot Context: Route String 0, speed from PORTSC, Context Entries 1 */
    sc[0] = (d->speed << 20) | (1UL << 27);
    sc[1] = ((u32)d->port << 16);       /* Root Hub Port Number */
    /* EP0 Endpoint Context: Control, CErr 3, fresh ring, avg TRB len 8 */
    ep[1] = (3UL << 1) | (4UL << 3) | ((u32)mps0 << 16);
    ep[2] = d->ring_phys | 1;           /* TR Dequeue Pointer | DCS */
    ep[3] = 0;
    ep[4] = 8;
    d->enq = 0;
    d->pcs = 1;
}

static int identify_device(CTRL *c, EP0DEV *d, DEVINFO *dev, u8 slot_type)
{
    u32 got;
    u8 slotid = 0;
    int mps0;
    u16 total, langid;
    u8 iman, iprod;
    int strings_ok = 1;

    if (d->speed_class != USB_SPEED_LOW &&
        d->speed_class != USB_SPEED_FULL &&
        d->speed_class != USB_SPEED_HIGH) {
        sprintf(dev->note, "PSIV %lu has no USB2 speed-class mapping",
                d->speed);
        return 0;
    }
    mps0 = mps0_initial(d->speed_class);

    if (!do_cmd(c, "Enable Slot", 0,
                TRB_TYPE(TRB_T_ENA_SLOT) | TRB_SLOTTYPE(slot_type),
                &slotid, dev->note))
        return 0;
    if (slotid == 0) {
        strcpy(dev->note, "Enable Slot returned slot 0");
        return 0;
    }
    d->slot = slotid;
    dev->slot = slotid;

    memset(d->devctx, 0, 32UL * (u32)c->csz);
    memset(d->ring, 0, EP0_TRBS * 16UL);
    c->dcbaa[(u32)slotid * 2]     = d->devctx_phys;
    c->dcbaa[(u32)slotid * 2 + 1] = 0;

    fill_ep0_input_ctx(c, d, mps0);
    if (!do_cmd(c, "Address Device", d->inctx_phys,
                TRB_TYPE(TRB_T_ADDR_DEV) | TRB_SLOT(slotid),
                0, dev->note))
        return 0;
    dev->addr = (u8)(ctx_at(c, d->devctx, 0)[3] & 0xFF);

    /* device descriptor, first 8 bytes: learn the real bMaxPacketSize0 */
    if (!ep0_ctrl_in(c, d, REQ_GET_DESCRIPTOR, DT_DEVICE << 8, 0, 8,
                     &got, dev->note))
        return 0;
    if (got >= 8 && d->speed_class == USB_SPEED_FULL &&
        d->buf[7] != mps0 &&
        (d->buf[7] == 16 || d->buf[7] == 32 || d->buf[7] == 64)) {
        /* FS with MPS0 16/32/64: fix EP0 via Evaluate Context (A1 only) */
        u32 *icc = ctx_at(c, d->inctx, 0);
        u32 *ep  = ctx_at(c, d->inctx, 2);

        mps0 = d->buf[7];
        icc[1] = 0x2;
        ep[1] = (3UL << 1) | (4UL << 3) | ((u32)mps0 << 16);
        if (!do_cmd(c, "Evaluate Context", d->inctx_phys,
                    TRB_TYPE(TRB_T_EVAL_CTX) | TRB_SLOT(slotid),
                    0, dev->note))
            return 0;
    }

    /* full device descriptor */
    if (!ep0_ctrl_in(c, d, REQ_GET_DESCRIPTOR, DT_DEVICE << 8, 0, 18,
                     &got, dev->note))
        return 0;
    if (got < 18) {
        sprintf(dev->note, "short device descriptor (%lu of 18 bytes)",
                got);
        return 0;
    }
    dev->bcdusb   = get16(d->buf + 2);
    dev->cls      = d->buf[4];
    dev->sub      = d->buf[5];
    dev->protocol = d->buf[6];
    dev->mps0     = d->buf[7];
    dev->vid      = get16(d->buf + 8);
    dev->pid      = get16(d->buf + 10);
    dev->bcddev   = get16(d->buf + 12);
    iman          = d->buf[14];
    iprod         = d->buf[15];
    dev->nconf    = d->buf[17];
    dev->ok = 1;    /* enough for the report even if config/strings fail */

    /* config descriptor: walk interfaces (a class-0 device such as a
     * mouse or flash drive declares its class per interface) */
    if (ep0_ctrl_in(c, d, REQ_GET_DESCRIPTOR, DT_CONFIG << 8, 0, 9,
                    &got, dev->note) && got >= 4) {
        total = get16(d->buf + 2);
        if (total > DESC_BUF)
            total = DESC_BUF;
        if (total >= 9 &&
            ep0_ctrl_in(c, d, REQ_GET_DESCRIPTOR, DT_CONFIG << 8, 0,
                        total, &got, dev->note)) {
            u32 off = 0;

            while (off + 1 < got && d->buf[off] >= 2) {
                if (d->buf[off + 1] == DT_INTERFACE &&
                    off + 8 < got) {
                    if (dev->nifc == 0) {
                        dev->icls   = d->buf[off + 5];
                        dev->isub   = d->buf[off + 6];
                        dev->iproto = d->buf[off + 7];
                    }
                    dev->nifc++;
                }
                off += d->buf[off];
            }
        }
    }
    dev->note[0] = 0;   /* config problems are not identification failures */

    /* string descriptors, best effort - stop at the first failure (a
     * stall halts EP0, and we are about to Disable Slot anyway) */
    if ((iman || iprod) &&
        ep0_ctrl_in(c, d, REQ_GET_DESCRIPTOR, DT_STRING << 8, 0, 255,
                    &got, dev->note) && got >= 4) {
        langid = get16(d->buf + 2);
        if (iman) {
            if (ep0_ctrl_in(c, d, REQ_GET_DESCRIPTOR,
                            (u16)((DT_STRING << 8) | iman), langid, 255,
                            &got, dev->note))
                str_from_desc(d->buf, got, dev->manuf);
            else
                strings_ok = 0;
        }
        if (iprod && strings_ok &&
            ep0_ctrl_in(c, d, REQ_GET_DESCRIPTOR,
                        (u16)((DT_STRING << 8) | iprod), langid, 255,
                        &got, dev->note))
            str_from_desc(d->buf, got, dev->product);
    }
    dev->note[0] = 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* C8 driver                                                          */
/* ------------------------------------------------------------------ */

int qual_devid(CTRL *c)
{
    EP0DEV d;
    int port, tried = 0, idok = 0;
    u8 slot_type = 0;
    int i;

    c->v_dev = V_SKIP;
    for (i = 0; i < c->nproto; i++) {
        if (c->proto[i].major == 2) {
            slot_type = c->proto[i].slottype;
            break;
        }
    }

    memset(&d, 0, sizeof(d));
    d.inctx  = (u32 *)dma_alloc(33UL * (u32)c->csz, 64, 4096,
                                &d.inctx_phys);
    d.devctx = (u32 *)dma_alloc(32UL * (u32)c->csz, 64, 4096,
                                &d.devctx_phys);
    d.ring   = (TRB *)dma_alloc(EP0_TRBS * 16UL, 64, 0x10000UL,
                                &d.ring_phys);
    d.buf    = (u8 *)dma_alloc(DESC_BUF, 64, 0x10000UL, &d.buf_phys);
    if (!d.inctx || !d.devctx || !d.ring || !d.buf) {
        strcpy(c->dev_note, "out of conventional memory");
        c->v_dev = V_WARN;
        return 0;
    }

    for (port = 1; port <= c->maxports && c->ndevs < MAX_DEVS; port++) {
        u32 v;
        DEVINFO *dev;

        if (!port_is_usb2(c, port))
            continue;
        v = portsc_read(c, port);
        if ((v & PSC_CCS) == 0 || (v & PSC_PED) == 0)
            continue;   /* nothing there, or C6 reset did not stick */

        tried++;
        dev = &c->devs[c->ndevs];
        memset(dev, 0, sizeof(*dev));
        dev->port  = (u8)port;
        dev->speed = (u8)PSC_SPEED(v);
        d.port  = port;
        d.speed = PSC_SPEED(v);
        d.speed_class = speed_class_port(c, port, d.speed);
        d.slot  = 0;

        msleep(10);                     /* USB2 reset recovery time */
        if (identify_device(c, &d, dev, slot_type)) {
            idok++;
            qprintf("  C8: port %d slot %u addr %u: %04X:%04X %s%s%s\n",
                    port, dev->slot, dev->addr, dev->vid, dev->pid,
                    dev->product[0] ? "\"" : "",
                    dev->product[0] ? dev->product
                                    : usb_class_name(dev->cls ? dev->cls
                                                              : dev->icls),
                    dev->product[0] ? "\"" : "");
        } else {
            qprintf("  C8: port %d: identification FAILED - %s\n",
                    port, dev->note);
        }

        if (d.slot != 0) {
            char scratch[64];

            do_cmd(c, "Disable Slot", 0,
                   TRB_TYPE(TRB_T_DIS_SLOT) | TRB_SLOT(d.slot),
                   0, scratch);
            c->dcbaa[(u32)d.slot * 2]     = 0;
            c->dcbaa[(u32)d.slot * 2 + 1] = 0;
        }
        c->ndevs++;
    }

    if (tried == 0) {
        strcpy(c->dev_note, "no enabled USB2 port to identify");
        c->v_dev = V_SKIP;
    } else {
        sprintf(c->dev_note, "%d of %d device(s) identified", idok, tried);
        c->v_dev = (idok == tried) ? V_PASS : V_WARN;
    }
    return idok;
}
