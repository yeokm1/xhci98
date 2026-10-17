/*
 * u4cm-crosscheck.c - run xhci98's USB4 software connection manager (the
 * pure core in src/cm/, tbt_ring.c and tbt_u4cm.c) against usb4-emu's
 * fabric model on the build host (roadmap-usb4.md task 40-A.5; design
 * record 22 section 14.1, "The usb4-emu fabric cross-check").
 *
 * Adapted from usb4-emu's tools/usb4cm/host/cm-host-test.c and
 * src/qemu/tests/usb4/fabric-test.c (usb4-emu main 3c74189 and the
 * working tree's QEMU fork at 178237c, read 2026-10-15), whose notices
 * are:
 *
 *   Copyright (c) 2026 usb4-emu contributors
 *   SPDX-License-Identifier: GPL-2.0-only
 *
 * What is kept: the fakes for the fabric's neighbours, the virtual clock
 * and timers, and the ring 0 host interface shim (Spec 12.3, 12.4, 12.6).
 * What changed: the connection manager under test is xhci98's TbtU4 core
 * behind its TBT_HOST accessor instead of usb4cm's U4CM_OPS; the host
 * interface capabilities report the emulator's default version 00h; the
 * cases are record 22's. Changed for xhci98 by Yeo Kheng Meng and Claude,
 * 2026-10-15. usb4-emu is read in place and never modified; everything
 * this builds goes under xhci98's out/.
 *
 * Build and run: scripts/usb4-crosscheck/run-crosscheck.sh (usb4-emu's
 * MSYS2 UCRT64 gcc and glib). A reading recorded per run, not a gate.
 */
#include "qemu/osdep.h"
#include "hw/usb4/usb4-fabric-int.h"
#include "u4cm-glue.h"

int usb4_test_trace;
static int failures, checks;
static int verbose;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
    } \
} while (0)

/* ------------------------------------------------------------------ */
/* Fakes for the neighbours of the fabric (as fabric-test.c's)          */
/* ------------------------------------------------------------------ */

struct USB4Device {
    const char *serial;
    void *router;
    unsigned ndfp;
    int tp;
    USB4Dialect dialect;
#ifdef XC_HAVE_ASM2464
    USB4DeviceModel model;
    uint64_t uid;
#endif
};

#ifdef XC_HAVE_ASM2464
USB4DeviceModel usb4_device_model(USB4Device *d)
{
    return d->model;
}

uint64_t usb4_device_uid(USB4Device *d)
{
    return d->uid;
}
#endif

static int fake_host;
static int fake_tp[3];
static USB4TunnelState tp_state[3];
static int tp_calls[3];
static int u3_calls;
static USB4TunnelState u3_state;

void usb4_icm_hotplug(USB4ICM *icm, USB4Router *r, unsigned adapter,
                      bool unplug)
{
    CHECK(0, "ICM hot plug without an ICM");
}

void usb4_icm_reset(USB4ICM *icm)
{
    CHECK(0, "ICM reset without an ICM");
}

USB4Fabric *usb4_host_get_fabric(USB4Host *h)
{
    return NULL;
}

USB4TunnelPort *usb4_host_get_port_tunnel(USB4Host *h, unsigned port)
{
    return port == 1 || port == 2 ? (USB4TunnelPort *)&fake_tp[port] : NULL;
}

USB4TunnelPort *usb4_device_get_pcie_down_target(USB4Device *d,
                                                 unsigned dport)
{
    return (USB4TunnelPort *)&d->tp;
}

uint16_t usb4_device_pcie_down_rid(USB4Device *d, unsigned dport)
{
    return 0x0300 | dport;
}

unsigned usb4_device_downstream_ports(USB4Device *d)
{
    return d->ndfp;
}

const char *usb4_device_vendor_name(USB4Device *d)
{
    return d->dialect == USB4_DIALECT_TB3 ? "QEMU Thunderbolt" : "QEMU";
}

const char *usb4_device_model_name(USB4Device *d)
{
    return "USB4 NVMe Enclosure";
}

void usb4_tunnel_port_set_state(USB4TunnelPort *p, USB4Device *dev,
                                USB4TunnelState st, USB4TunnelReason why)
{
    for (int i = 1; i <= 2; i++) {
        if (p == (USB4TunnelPort *)&fake_tp[i]) {
            tp_state[i] = st;
            tp_calls[i]++;
            printf("  [model] port %d PCIe tunnel %s (reason %d)\n", i,
                   st == USB4_TUNNEL_UP ? "UP" : "DOWN", why);
        }
    }
}

void usb4_device_usb3_tunnel(USB4Device *d, USB4TunnelState st,
                             USB4TunnelReason why)
{
    u3_calls++;
    u3_state = st;
    printf("  [model] USB3 tunnel %s\n", st == USB4_TUNNEL_UP ? "UP" : "DOWN");
}

USB4Dialect usb4_device_dialect(USB4Device *d)
{
    return d->dialect;
}

/* usb4-emu QEMU fork 2a893ea: the TB3 dock's link controller UUID (the
 * derived one, usb4-device's default) and the backpressure resume the
 * fabric calls when a configuration hold ends (this shim never holds). */
bool usb4_device_lc_uuid_captured(USB4Device *d)
{
    return false;
}

void usb4_host_tx_resume(USB4Host *h)
{
}

const char *usb4_device_nvme_drive(USB4Device *d)
{
    return "disk0";
}

const char *usb4_device_nvme_serial(USB4Device *d)
{
    return d->serial;
}

void *usb4_device_get_router(USB4Device *d)
{
    return d->router;
}

void usb4_device_set_router(USB4Device *d, void *router)
{
    d->router = router;
}

/* Virtual clock and timers, advanced by the core's stalls and sleeps. */
struct USB4Timer {
    USB4TimerCB *cb;
    void *opaque;
    bool armed;
    int64_t deadline;
};

static int64_t now_ms;
static uint32_t now_us;
#define MAX_TIMERS 32
static USB4Timer *timers[MAX_TIMERS];

USB4Timer *usb4_timer_new(USB4TimerCB *cb, void *opaque)
{
    USB4Timer *t = g_new0(USB4Timer, 1);

    t->cb = cb;
    t->opaque = opaque;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!timers[i]) {
            timers[i] = t;
            return t;
        }
    }
    abort();
}

void usb4_timer_mod_ms(USB4Timer *t, int64_t ms)
{
    t->armed = true;
    t->deadline = now_ms + ms;
}

void usb4_timer_del(USB4Timer *t)
{
    t->armed = false;
}

void usb4_timer_free(USB4Timer *t)
{
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timers[i] == t) {
            timers[i] = NULL;
        }
    }
    g_free(t);
}

int64_t usb4_clock_ns(void)
{
    return now_ms * 1000000;
}

static uint32_t us_acc;

static void advance_us(uint32_t us)
{
    now_us += us;
    us_acc += us;
    while (us_acc >= 1000) {
        us_acc -= 1000;
        now_ms++;
        for (int i = 0; i < MAX_TIMERS; i++) {
            USB4Timer *t = timers[i];
            if (t && t->armed && t->deadline <= now_ms) {
                t->armed = false;
                t->cb(t->opaque);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Host interface shim: ring 0 only (as cm-host-test.c's)              */
/* ------------------------------------------------------------------ */

#define ARENA_PHYS  0x00100000u
#define ARENA_SIZE  0x10000u
static uint8_t arena[ARENA_SIZE];
static USB4Fabric *fab;

static struct {
    uint32_t base, size, pi, ci, ctrl, bufsz;
} txr, rxr;
static GQueue rx_fifo;
static uint32_t n_mmio;
static uint32_t n_hr_reset;

typedef struct { uint8_t pdf; uint16_t len; uint8_t d[256]; } ShimFrame;

static uint8_t *phys(uint32_t pa, uint32_t len)
{
    CHECK(pa >= ARENA_PHYS && pa + len <= ARENA_PHYS + ARENA_SIZE,
          "DMA outside the arena: %08x+%u", pa, len);
    return arena + (pa - ARENA_PHYS);
}

static void shim_rx_flush(void)
{
    ShimFrame *f;

    while (rxr.ctrl >> 31 && rxr.size && rxr.pi != rxr.ci &&
           (f = g_queue_pop_head(&rx_fifo))) {
        uint8_t *d = phys(rxr.base + 16 * rxr.pi, 16);
        uint32_t ba = usb4_ld_le32(d);
        uint32_t bs = rxr.bufsz ? rxr.bufsz : 4096;

        CHECK(f->len <= bs, "RX buffer too small");
        memcpy(phys(ba, f->len), f->d, f->len);
        CHECK(usb4_ld_le32(d + 8) & (1u << 22), "RX descriptor without RS");
        /* Spec 12.4.1 Table 12-8: the write-back's bit 22 is Buffer
         * Overflow, set only on an overflow - as usb4-emu's usb4-host.c
         * writes it; usb4cm's host shim echoed RS there. */
        usb4_st_le32(d + 8, f->len | (f->pdf << 12) | (1u << 21));
        rxr.pi = (rxr.pi + 1) % rxr.size;
        g_free(f);
    }
}

void usb4_host_rx_frame(USB4Host *h, uint8_t pdf, const uint8_t *frame,
                        size_t len)
{
    ShimFrame *f;

    CHECK(usb4_ctl_frame_crc_ok(frame, len), "rx frame CRC");
    if (!(rxr.ctrl >> 31)) {
        return;
    }
    f = g_new0(ShimFrame, 1);
    f->pdf = pdf;
    f->len = len;
    memcpy(f->d, frame, len);
    g_queue_push_tail(&rx_fifo, f);
    shim_rx_flush();
}

static void shim_tx_kick(void)
{
    while (txr.ctrl >> 31 && txr.size && txr.ci != txr.pi) {
        uint8_t *d = phys(txr.base + 16 * txr.ci, 16);
        uint32_t dw2 = usb4_ld_le32(d + 8);
        uint32_t len = dw2 & 0xfff;
        uint8_t buf[256];

        CHECK(len && len <= 256, "TX len %u", len);
        CHECK(((dw2 >> 12) & 0xf) == ((dw2 >> 16) & 0xf), "SOF != EOF PDF");
        memcpy(buf, phys(usb4_ld_le32(d), len), len);
        if (dw2 & (1u << 22)) {
            usb4_st_le32(d + 8, dw2 | (1u << 21));
        }
        txr.ci = (txr.ci + 1) % txr.size;
        usb4_fabric_tx_frame(fab, (dw2 >> 12) & 0xf, buf, len);
    }
}

static unsigned long shim_rd(unsigned long off)
{
    n_mmio++;
    switch (off) {
    case 0x39640: return USB4_HOST_IF_TOTAL_PATHS;  /* version 00h */
    case 0x00008: return (txr.pi << 16) | txr.ci;
    case 0x08008: return (rxr.pi << 16) | rxr.ci;
    case 0x0000c: return txr.size;
    case 0x0800c: return rxr.size | (rxr.bufsz << 16);
    case 0x19800: return txr.ctrl;
    case 0x29800: return rxr.ctrl;
    }
    return 0;
}

static void shim_wr(unsigned long off, unsigned long v)
{
    ShimFrame *f;

    n_mmio++;
    switch (off) {
    case 0x39858:
        CHECK(0, "a Host Interface Reset: record 22 section 4.4 forbids it");
        break;
    case 0x39898:
        if (v & 1) {
            n_hr_reset++;
        }
        break;
    case 0x00000: txr.base = v & ~0xfu; txr.pi = txr.ci = 0; break;
    case 0x08000: rxr.base = v & ~0xfu; rxr.pi = rxr.ci = 0; break;
    case 0x00004: case 0x08004: CHECK(v == 0, "64-bit ring base"); break;
    case 0x0000c: txr.size = v & 0xffff; txr.pi = txr.ci = 0; break;
    case 0x0800c:
        rxr.size = v & 0xffff;
        rxr.bufsz = (v >> 16) & 0xfff;
        rxr.pi = rxr.ci = 0;
        break;
    case 0x00008:
        txr.pi = v >> 16;
        CHECK(txr.size == 0 || txr.pi < txr.size, "TX producer %u", txr.pi);
        shim_tx_kick();
        break;
    case 0x08008:
        rxr.ci = v & 0xffff;
        CHECK(rxr.size == 0 || rxr.ci < rxr.size, "RX consumer %u", rxr.ci);
        shim_rx_flush();
        break;
    case 0x19800:
        txr.ctrl = v;
        CHECK(!(v >> 31) || (v & (1u << 30)), "TX ring 0 not Raw Mode");
        shim_tx_kick();
        break;
    case 0x29800:
        rxr.ctrl = v;
        if (!(v >> 31)) {
            rxr.pi = rxr.ci = 0;
            while ((f = g_queue_pop_head(&rx_fifo))) {
                g_free(f);
            }
        }
        shim_rx_flush();
        break;
    }
}

static unsigned long shim_clock_us(void)
{
    return now_us;
}

static void shim_advance(unsigned long us)
{
    advance_us((uint32_t)us);
}

static void shim_log(const char *label, unsigned long value)
{
    if (verbose || strstr(label, "u4.port") || strstr(label, "u4.pcie") ||
        strstr(label, "u4.usb3") || strstr(label, "u4.preserved") ||
        strstr(label, "u4.held") || strstr(label, "u4.inert")) {
        printf("  [cm] %s %08lx\n", label, value);
    }
}

static const XC_OPS shim_ops = {
    shim_rd, shim_wr, shim_advance, shim_clock_us, shim_log
};

/* ------------------------------------------------------------------ */
/* The cases                                                            */
/* ------------------------------------------------------------------ */

static void new_fabric(USB4HostModel model, bool preos)
{
    USB4FabricConfig cfg = {
        .profile = USB4_PROFILE_USB4_GENERIC, .dialect = USB4_DIALECT_USB4,
        .router_vendor_id = 0x46f4, .router_device_id = 0x4040,
        .num_usb4_ports = 2, .host_model = model, .preos_cm = preos,
        /* asm4242's defaults, as usb4-emu's usb4-host properties and its
         * fabric-test.c ASM_CFG_DEFAULT set them: the quirks on */
        .asm4242 = { .adp_dp = 5, .adp_hi = 7, .adp_pcie = 8, .adp_usb3 = 10,
                     .quirk_pending = true, .quirk_credits = true,
                     .nvm_version = 0x200011, .nvm_date = 0x250708 },
    };
    ShimFrame *f;

    if (model == USB4_HOST_MODEL_ASM4242) {
        cfg.router_vendor_id = 0x174c;
        cfg.router_device_id = 0x2428;
    }

    if (fab) {
        usb4_fabric_free(fab);
    }
    fab = usb4_fabric_new((USB4Host *)&fake_host, &cfg);
    memset(&txr, 0, sizeof(txr));
    memset(&rxr, 0, sizeof(rxr));
    while ((f = g_queue_pop_head(&rx_fifo))) {
        g_free(f);
    }
    memset(tp_state, 0, sizeof(tp_state));
    memset(tp_calls, 0, sizeof(tp_calls));
    u3_calls = 0;
    n_hr_reset = 0;
}

static long cm_start(bool usb3)
{
    memset(arena, 0, sizeof(arena));
    return xc_start(&shim_ops, arena, ARENA_PHYS, ARENA_SIZE, usb3);
}

#define ST(port)    xc_get(XC_STATE, (port))
#define WHY(port)   xc_get(XC_WHY, (port))

static void cm_poll(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        xc_poll();
        advance_us(200000);
    }
}

static USB4Device dev1 = { .serial = "USB4EMU01" };
static USB4Device dev2 = { .serial = "USB4EMU02" };
static USB4Device dock = { .serial = "USB4EMU03", .dialect = USB4_DIALECT_TB3 };

static const char *model_name(USB4HostModel m)
{
    return m == USB4_HOST_MODEL_INTEL_ADL ? "intel-adl" :
           m == USB4_HOST_MODEL_ASM4242 ? "asm4242" : "minimal";
}

/* U.1 and U.2 of record 22 section 14.2, on the model: a cold device, a
 * hot plug on the other port, USB3, unplug and re-plug. */
static void test_cold_hot_unplug(USB4HostModel model)
{
    long rc;

    printf("== %s: cold device on port 1, hot plug on port 2, USB3\n", model_name(model));
    new_fabric(model, false);
    CHECK(usb4_fabric_attach(fab, 1, &dev1, NULL), "attach 1");
    rc = cm_start(true);
    CHECK(rc == 0, "start %ld (inert reason %lu)", rc, xc_get(XC_INERT, 0));
    CHECK(ST(1) == (unsigned long)xc_state_up(), "port 1 UP (state %lu why %lu)", ST(1), WHY(1));
    CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "the model's PCIe tunnel up");
    CHECK(tp_calls[1] == 1, "one UP transition (%d)", tp_calls[1]);
    CHECK(xc_get(XC_BONDED, 1), "bonded");
    CHECK(xc_get(XC_USB3_UP, 1), "USB3 up (TMU ok %lu, why %lu)", xc_get(XC_TMU_OK, 1), WHY(1));
    CHECK(u3_calls >= 1 && u3_state == USB4_TUNNEL_UP, "the model's USB3 tunnel up");
    if (model == USB4_HOST_MODEL_INTEL_ADL) {
        CHECK(xc_get(XC_PCIE_DOWN, 1) == 8, "intel-adl: PCIe Down 8 on port 1 (%lu)",
              xc_get(XC_PCIE_DOWN, 1));
        CHECK(!xc_get(XC_HOST_ADP_PRESENT, 10) && !xc_get(XC_HOST_ADP_PRESENT, 11),
              "intel-adl: adapters 10 and 11 not present");
        CHECK(xc_get(XC_HOST_QUIRKS, 0) & xc_quirk_usb3(), "the 8087:463E quirk");
    }
    if (model == USB4_HOST_MODEL_ASM4242) {
        CHECK(xc_get(XC_PCIE_DOWN, 1) == 8, "asm4242: PCIe Down 8 on port 1 (%lu)",
              xc_get(XC_PCIE_DOWN, 1));
        CHECK(xc_get(XC_HOST_ADP_PRESENT, 10) && xc_get(XC_HOST_ADP_PRESENT, 11),
              "asm4242: USB3 Down 10 and 11 present");
    }

    CHECK(usb4_fabric_attach(fab, 2, &dev2, NULL), "attach 2");
    cm_poll(3);
    CHECK(ST(2) == (unsigned long)xc_state_up(), "hot plug: port 2 UP (state %lu why %lu)",
          ST(2), WHY(2));
    CHECK(usb4_fabric_tunnel_state(fab, 2) == USB4_TUNNEL_UP, "the model's port 2 up");

    usb4_fabric_detach(fab, 2);
    cm_poll(2);
    CHECK(ST(2) == (unsigned long)xc_state_empty(), "unplug: port 2 EMPTY (state %lu)", ST(2));
    CHECK(xc_get(XC_LD_CALLS, 0) == 1 && xc_get(XC_LD_CLOSED, 0),
          "teardown read back: path closed (%lu, %lu)", xc_get(XC_LD_CALLS, 0),
          xc_get(XC_LD_CLOSED, 0));
    CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "port 1 untouched");
    CHECK(usb4_fabric_attach(fab, 2, &dev2, NULL), "re-attach 2");
    cm_poll(4);
    CHECK(ST(2) == (unsigned long)xc_state_up(), "re-plug: port 2 UP (state %lu why %lu)",
          ST(2), WHY(2));
    CHECK(usb4_fabric_tunnel_state(fab, 2) == USB4_TUNNEL_UP, "the model's port 2 up again");
    printf("   %lu control requests, %lu retries, %lu timeouts, %lu hot plug events,"
           " %lu acknowledgements, %u MMIO accesses\n",
           xc_get(XC_REQUESTS, 0), xc_get(XC_RETRIES, 0), xc_get(XC_TIMEOUTS, 0),
           xc_get(XC_HOTPLUGS, 0), xc_get(XC_ACKS, 0), n_mmio);
    xc_stop();
    usb4_fabric_detach(fab, 1);
    usb4_fabric_detach(fab, 2);
}

/* U.6: the model's pre-OS connection manager; the tunnel preserved. */
static void test_preos(USB4HostModel model)
{
    int calls;

    printf("== %s: pre-OS connection manager, preserve\n", model_name(model));
    new_fabric(model, true);
    CHECK(usb4_fabric_attach(fab, 1, &dev1, NULL), "pre-OS attach");
    CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "pre-OS tunnel up");
    usb4_fabric_preos_end(fab);
    calls = tp_calls[1];
    CHECK(cm_start(true) == 0, "start");
    CHECK(ST(1) == (unsigned long)xc_state_up() && xc_get(XC_PCIE_PRESERVED, 1),
          "preserved (state %lu why %lu)", ST(1), WHY(1));
    CHECK(tp_calls[1] == calls, "no transition, no flap (%d -> %d)", calls, tp_calls[1]);
    CHECK(n_hr_reset == 0, "no reset of any kind");
    cm_poll(3);
    CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "still up across idle ticks");
    usb4_fabric_detach(fab, 1);
    cm_poll(2);
    CHECK(ST(1) == (unsigned long)xc_state_empty(), "its unplug: EMPTY");
    xc_stop();
}

/* U.9: stop and a new instance over the configured fabric. */
static void test_restart(void)
{
    int calls;

    printf("== minimal: the driver's stop and a second start\n");
    new_fabric(USB4_HOST_MODEL_MINIMAL, false);
    CHECK(usb4_fabric_attach(fab, 1, &dev1, NULL), "attach");
    CHECK(cm_start(false) == 0, "first start");
    xc_stop();
    calls = tp_calls[1];
    CHECK(cm_start(false) == 0, "second start");
    CHECK(ST(1) == (unsigned long)xc_state_up() && xc_get(XC_PCIE_PRESERVED, 1),
          "the second instance preserves the first's tunnel (state %lu why %lu)",
          ST(1), WHY(1));
    CHECK(tp_calls[1] == calls, "no flap across the restart");
    xc_stop();
    usb4_fabric_detach(fab, 1);
}

#ifdef XC_HAVE_ASM2464
/*
 * The ORICO AAGM2-U4 (ASMedia ASM2464PD) device router model on the
 * intel-adl host (usb4-emu docs/captures/ORICO-ASM2464.md): an old
 * Thunderbolt 3 style DROM that marks adapter 4 (a USB3 Up adapter)
 * unused, no CLx, the TMU capability at 0xc0, NVM Sector Size failing
 * with status 2, Router Ready 5 ms after Enumerated, Buffer Allocation
 * with PCIe alone, about 590 ms to come back after a Downstream Port
 * Reset. Record 22 section 6.11.
 */
static USB4Device asm1 = { .serial = "ORICO01", .model = USB4_DEVICE_MODEL_ASM2464 };
static USB4Device asm2 = { .serial = "ORICO02", .model = USB4_DEVICE_MODEL_ASM2464,
                           .uid = 0x174c608e70912701ULL };

static void asm2464_quiet(const char *what)
{
    CHECK(xc_get(XC_ROP_FAILED, 0) == 0, "%s: no router operation failed (%lu)", what,
          xc_get(XC_ROP_FAILED, 0));
    CHECK(xc_get(XC_ROP_TIMEOUTS, 0) == 0, "%s: no router operation timed out", what);
    CHECK(xc_get(XC_CR_TIMEOUTS, 0) == 0, "%s: no Configuration Ready timeout", what);
    CHECK(xc_get(XC_RETRIES, 0) == 0 && xc_get(XC_TIMEOUTS, 0) == 0,
          "%s: no control retries or timeouts (%lu, %lu)", what, xc_get(XC_RETRIES, 0),
          xc_get(XC_TIMEOUTS, 0));
    CHECK(xc_get(XC_USB3_BUILT, 0) == 0 && xc_get(XC_USB3_FAILED, 0) == 0 && u3_calls == 0,
          "%s: no USB3 tunnel tried on the unused adapter (%lu built, %lu failed, %d)",
          what, xc_get(XC_USB3_BUILT, 0), xc_get(XC_USB3_FAILED, 0), u3_calls);
    CHECK(xc_get(XC_TMU_ENABLED, 0) == 0, "%s: no TMU without a USB3 tunnel", what);
    CHECK(xc_get(XC_DROM_CRC, 0) == 0, "%s: DROM CRC good", what);
}

static void test_asm2464_cold_hot(void)
{
    long rc;

    printf("== intel-adl + asm2464: cold on port 1, hot plug on port 2, unplug, re-plug\n");
    new_fabric(USB4_HOST_MODEL_INTEL_ADL, false);
    CHECK(usb4_fabric_attach(fab, 1, &asm1, NULL), "attach 1");
    rc = cm_start(true);
    CHECK(rc == 0, "start %ld (inert reason %lu)", rc, xc_get(XC_INERT, 0));
    CHECK(ST(1) == (unsigned long)xc_state_up(), "port 1 UP (state %lu why %lu)", ST(1), WHY(1));
    CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "the model's PCIe tunnel up");
    CHECK(tp_calls[1] == 1, "one UP transition (%d)", tp_calls[1]);
    CHECK(xc_get(XC_BONDED, 1), "bonded");
    CHECK(!xc_get(XC_USB3_UP, 1), "no USB3 tunnel");
    CHECK(xc_get(XC_DEV_ADP_PRESENT, (1UL << 8) | 3), "PCIe Up adapter 3 present");
    CHECK(!xc_get(XC_DEV_ADP_PRESENT, (1UL << 8) | 4), "adapter 4 unused by the DROM");
    CHECK(strcmp(xc_vendor_name(1), "ASMedia") == 0 && strcmp(xc_model_name(1), "246x") == 0,
          "DROM names '%s' '%s'", xc_vendor_name(1), xc_model_name(1));
    asm2464_quiet("cold");

    CHECK(usb4_fabric_attach(fab, 2, &asm2, NULL), "attach 2");
    cm_poll(4);
    CHECK(ST(2) == (unsigned long)xc_state_up(), "hot plug: port 2 UP (state %lu why %lu)",
          ST(2), WHY(2));
    CHECK(usb4_fabric_tunnel_state(fab, 2) == USB4_TUNNEL_UP, "the model's port 2 up");
    usb4_fabric_detach(fab, 2);
    cm_poll(2);
    CHECK(ST(2) == (unsigned long)xc_state_empty(), "unplug: port 2 EMPTY (state %lu)", ST(2));
    CHECK(xc_get(XC_LD_CALLS, 0) == 1 && xc_get(XC_LD_CLOSED, 0),
          "teardown read back: path closed (%lu, %lu)", xc_get(XC_LD_CALLS, 0),
          xc_get(XC_LD_CLOSED, 0));
    CHECK(usb4_fabric_attach(fab, 2, &asm2, NULL), "re-attach 2");
    cm_poll(4);
    CHECK(ST(2) == (unsigned long)xc_state_up(), "re-plug: port 2 UP (state %lu why %lu)",
          ST(2), WHY(2));
    CHECK(usb4_fabric_tunnel_state(fab, 2) == USB4_TUNNEL_UP, "the model's port 2 up again");
    CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "port 1 untouched");
    asm2464_quiet("hot");
    printf("   %lu control requests, %lu router operations, %lu routers enumerated,"
           " %lu hot plug events\n",
           xc_get(XC_REQUESTS, 0), xc_get(XC_ROP_ISSUED, 0), xc_get(XC_ENUMERATED, 0),
           xc_get(XC_HOTPLUGS, 0));
    xc_stop();
    usb4_fabric_detach(fab, 1);
    usb4_fabric_detach(fab, 2);
}

/* The pre-OS connection manager's tunnel to the asm2464: preserved; then
 * XhciCmPreOsCleanup 2: a Downstream Port Reset, the router back about
 * 590 ms later, and the tunnel built afresh with no retry storm. */
static void test_asm2464_preos(unsigned long clean)
{
    int calls;
    unsigned long req;

    printf("== intel-adl + asm2464: pre-OS tunnel, XhciCmPreOsCleanup %lu\n", clean);
    new_fabric(USB4_HOST_MODEL_INTEL_ADL, true);
    CHECK(usb4_fabric_attach(fab, 1, &asm1, NULL), "pre-OS attach");
    CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "pre-OS tunnel up");
    usb4_fabric_preos_end(fab);
    calls = tp_calls[1];
    memset(arena, 0, sizeof(arena));
    CHECK(xc_start_clean(&shim_ops, arena, ARENA_PHYS, ARENA_SIZE, 1, clean) == 0, "start");
    if (clean == 0) {
        CHECK(ST(1) == (unsigned long)xc_state_up() && xc_get(XC_PCIE_PRESERVED, 1),
              "preserved (state %lu why %lu)", ST(1), WHY(1));
        CHECK(tp_calls[1] == calls, "no transition (%d -> %d)", calls, tp_calls[1]);
        CHECK(xc_get(XC_DPR, 0) == 0, "no port reset");
    } else {
        CHECK(xc_get(XC_DPR, 0) == 1, "one Downstream Port Reset (%lu)", xc_get(XC_DPR, 0));
        cm_poll(6);
        CHECK(ST(1) == (unsigned long)xc_state_up() && !xc_get(XC_PCIE_PRESERVED, 1),
              "built afresh after the reset (state %lu why %lu)", ST(1), WHY(1));
        CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "the model's tunnel up");
        CHECK(xc_get(XC_CONNECTS, 0) == 1, "one connect (%lu)", xc_get(XC_CONNECTS, 0));
    }
    req = xc_get(XC_REQUESTS, 0);
    cm_poll(5);
    CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "still up across idle ticks");
    CHECK(xc_get(XC_REQUESTS, 0) - req <= 5 * 8, "idle ticks stay quiet (%lu requests)",
          xc_get(XC_REQUESTS, 0) - req);
    asm2464_quiet("pre-OS");
    usb4_fabric_detach(fab, 1);
    cm_poll(2);
    CHECK(ST(1) == (unsigned long)xc_state_empty(), "its unplug: EMPTY");
    xc_stop();
}
#endif

/* Task 40-A.8, record 22 section 8: the TB3 dock model (the CalDigit
 * TS3 Plus of the Framework 13 capture, usb4-device dialect=tb3) on a
 * USB4 host - USB4 Version 02h, Upstream Adapter 0 at power-on. */
static void test_tb3_dock(USB4HostModel model)
{
    printf("== %s: Thunderbolt 3 dock on port 1, unplug, re-plug\n", model_name(model));
    new_fabric(model, false);
    CHECK(usb4_fabric_attach(fab, 1, &dock, NULL), "attach the dock");
    CHECK(cm_start(true) == 0, "start");
    CHECK(ST(1) == (unsigned long)xc_state_up(), "dock: port 1 UP (state %lu why %lu)",
          ST(1), WHY(1));
    CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "the model's PCIe tunnel up");
    CHECK(!xc_get(XC_USB3_UP, 1), "no USB3 tunnel over a Thunderbolt 3 link");
    usb4_fabric_detach(fab, 1);
    cm_poll(2);
    CHECK(ST(1) == (unsigned long)xc_state_empty(), "unplug: EMPTY (state %lu)", ST(1));
    CHECK(usb4_fabric_attach(fab, 1, &dock, NULL), "re-attach the dock");
    cm_poll(4);
    CHECK(ST(1) == (unsigned long)xc_state_up(), "re-plug: UP (state %lu why %lu)",
          ST(1), WHY(1));
    CHECK(usb4_fabric_tunnel_state(fab, 1) == USB4_TUNNEL_UP, "the model's tunnel up again");
    xc_stop();
    usb4_fabric_detach(fab, 1);
}

static void test_empty(void)
{
    printf("== nothing attached\n");
    new_fabric(USB4_HOST_MODEL_MINIMAL, false);
    CHECK(cm_start(true) == 0, "start");
    CHECK(ST(1) == (unsigned long)xc_state_empty() &&
          ST(2) == (unsigned long)xc_state_empty(), "both ports EMPTY");
    xc_stop();
}

int main(int argc, char **argv)
{
    usb4_test_trace = getenv("USB4_TRACE") != NULL;
    verbose = getenv("U4CM_VERBOSE") != NULL;
    g_queue_init(&rx_fifo);
    test_cold_hot_unplug(USB4_HOST_MODEL_MINIMAL);
    test_cold_hot_unplug(USB4_HOST_MODEL_INTEL_ADL);
    test_preos(USB4_HOST_MODEL_MINIMAL);
    test_preos(USB4_HOST_MODEL_INTEL_ADL);
    test_cold_hot_unplug(USB4_HOST_MODEL_ASM4242);
    test_preos(USB4_HOST_MODEL_ASM4242);
    test_restart();
#ifdef XC_HAVE_ASM2464
    test_asm2464_cold_hot();
    test_asm2464_preos(0);
    test_asm2464_preos(2);
#endif
    test_tb3_dock(USB4_HOST_MODEL_MINIMAL);
    test_tb3_dock(USB4_HOST_MODEL_INTEL_ADL);
    test_empty();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
