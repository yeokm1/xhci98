/*
 * u4_fakefab.h - a C89 fake USB4 fabric for test_u4cm (roadmap-usb4.md
 * task 40-A.3; design record 22 section 14.1, decision U11).
 *
 * One FAB is one domain: a modelled host interface (ring 0 in Raw Mode
 * over a fake physical arena, as tbt_fake.h models it, but one instance
 * per domain so two domains can run at once), a host router and up to
 * FAB_MAX_ROUTERS - 1 device routers with router, adapter and path
 * configuration spaces, control packets with CRC-32C, error notifications,
 * hot plug events, router operations each switchable between supported,
 * not supported, failing and never completing, Pending Packets, LTSSM,
 * the USB3 bandwidth handshake, the TMU's post time, lane bonding, Lock,
 * Downstream Port Reset. Its register semantics are written from the USB4
 * Specification 2.0 (sections 6.4, 8.2, 8.3, 12) and the usb4-emu model's
 * documented behaviour (record 22 section 2.1), not from the code under
 * test. Presets: usb4-emu's `minimal` and `intel-adl` host models, a USB4
 * device router, a Thunderbolt 3 device router (Titan Ridge or Alpine
 * Ridge, with the Plug Events and link controller capabilities), the
 * ASMedia ASM2464PD device router of usb4-emu's `asm2464` model (its
 * captured quirks), and the pre-OS connection manager's leftovers.
 *
 * A virtual clock in microseconds advances only when the code under test
 * stalls or sleeps. Test-only code; C89 with the C library.
 */

#ifndef U4_FAKEFAB_H
#define U4_FAKEFAB_H

#include "../src/cm/tbt_ring.h"

#define FAB_MAX_ROUTERS     8
#define FAB_MAX_ADP         16      /* adapters 0..15 */
#define FAB_HOPS            32      /* path entries 0..31 per adapter */
#define FAB_ADP_DW          64
#define FAB_RTR_DW          1024
#define FAB_MEM_BYTES       16384
#define FAB_PHYS            0x00400000UL
#define FAB_RXQ             256
#define FAB_TXLOG           2048
#define FAB_WRLOG           4096

/* Capability offsets this model uses (any are legal, Spec 8.2). */
#define FAB_CAP_LANE        0x08
#define FAB_CAP_PORT        0x10
#define FAB_CAP_TMU_ADP     0x30
#define FAB_CAP_ADP         0x08    /* PCIe, USB3, DP */
#define FAB_RCAP_TMU        0x30    /* router TMU capability */
#define FAB_RCAP_PLUG       0x30    /* Thunderbolt 3: Plug Events VSEC */
#define FAB_RCAP_LC         0x40    /* Thunderbolt 3: link controller VSEC (long form) */
#define FAB_LC_START        0x10
#define FAB_LC_PORT_SIZE    0x100

/* Router operation behaviour, per opcode */
#define FAB_ROP_OK          0
#define FAB_ROP_NS          1       /* Operation Not Supported */
#define FAB_ROP_FAIL        2       /* Status 1 */
#define FAB_ROP_HANG        3       /* OV never clears */

/* Router operation slots */
#define FAB_OP_DROM         0
#define FAB_OP_MAP          1
#define FAB_OP_CAPS         2
#define FAB_OP_BA           3
#define FAB_OP_NVM          4       /* NVM Sector Size, 25h: the ASM2464 preset only */
#define FAB_OPS             5

/* Device kinds */
#define FAB_DEV_USB4        1
#define FAB_DEV_TBT3_TR     2       /* Titan Ridge DD 8086:15EF */
#define FAB_DEV_TBT3_AR     3       /* Alpine Ridge LP bridge 8086:15C0 */
#define FAB_DEV_ASM2464     4       /* ASMedia ASM2464PD 174C:2463, the ORICO capture */

typedef struct FAB_ADP {
    int Present;
    unsigned long Cs[FAB_ADP_DW];
    unsigned long Path[2 * FAB_HOPS];
    int PpStuck[FAB_HOPS];          /* Pending Packets never drain */
    int PeStuck;                    /* Path Enable will not clear */
    int Peer;                       /* lane: the router on the other end, -1 none */
    int PeerAdp;
    int DetectStuck;                /* PCIe: LTSSM never Detect while PE is 0 */
    int HcaStuck;                   /* USB3 Down: HCA never follows CMR */
    int LtssmForce;                 /* PCIe: nonzero reads LTSSM LtssmForce - 1 */
    int Lane1;                      /* a lane 1 adapter (every fixture: even numbers) */
    unsigned long Consumed;         /* USB3: consumed bandwidth, CS_1 11:0 | 23:12 */
} FAB_ADP;

typedef struct FAB_ROUTER {
    int Used;
    int Kind;                       /* 0 host, FAB_DEV_* */
    int Tbt3;
    int Connected;                  /* attached to its parent's lanes */
    int Parent;                     /* router index, -1 for the host */
    int ParentAdp;                  /* the parent's lane 0 adapter */
    unsigned long Rcs[FAB_RTR_DW];
    FAB_ADP Adp[FAB_MAX_ADP];
    unsigned MaxAdp;
    unsigned UpAdp;
    int RopMode[FAB_OPS];
    unsigned long CapsData;         /* Get Capabilities index 0, DW 0 */
    unsigned long BaData[5];
    unsigned BaLen;
    unsigned long BaPerLane[FAB_MAX_ADP];   /* nonzero: baMaxPCIe for that lane 0 */
    unsigned long BaLastMeta;
    unsigned char Drom[256];
    unsigned DromLen;
    unsigned long MapData[4][2];
    unsigned MapEntries;
    unsigned MapNext;
    int NoRouterReady;
    int NoConfigReady;
    int PostStuck;
    int BondPossible;               /* PORT_CS_18 BE / LC PORT_ATTR BE */
    int Lane1Present;               /* lane 1 trains with lane 0 */
    int BondNoWidth;                /* Lane Bonding never reaches x2 */
    int Lane1NoDisable;             /* Lane Disable never takes */
    int DpPlugEvents;               /* intel-adl: an event per DP IN at enumeration */
    unsigned TmuAt;                 /* the router TMU capability's offset, 0 none */
    unsigned long FailStatus;       /* FAB_ROP_FAIL's Status (0: 1) */
    unsigned long RrDelayUs;        /* Router Ready this long after Enumerated */
    int RrPending;
    unsigned long RrAtUs;
    unsigned long LinkDelayUs;      /* a Downstream Port Reset: the link back this
                                     * long after the reset (0: at once) */
    int ReconnectPending;           /* the link down since a reset */
    int ReconnectArmed;             /* the reset released: ReconnectAtUs runs */
    unsigned long ReconnectAtUs;
    unsigned long DprReleasedAtUs;  /* the last such release */
    unsigned long ReconnectedAtUs;  /* and the link back after it */
    unsigned long NvmOps;           /* NVM Sector Size requests */
    unsigned long AdpTouches[FAB_MAX_ADP];  /* requests addressed to each adapter */
} FAB_ROUTER;

typedef struct FAB_FRAME {
    unsigned long Pdf;
    unsigned long Words;
    unsigned long AtUs;
    unsigned long Dw[64];
} FAB_FRAME;

/* A hook's answer: 0 go on, 1 swallow the request (nothing applied, no
 * answer), 2 apply it and lose the answer. */
#define FAB_HOOK_GO         0
#define FAB_HOOK_SWALLOW    1
#define FAB_HOOK_DROP_ANSWER 2

struct FAB;
typedef int (*FAB_HOOK)(struct FAB *f, unsigned long pdf, int router,
                        unsigned long cs, unsigned long adp, unsigned long addr,
                        unsigned long n, const unsigned long *data);

typedef struct FAB {
    TBT_HOST Host;                  /* Context = this */
    unsigned long NowUs;
    unsigned long TickUs;           /* the clock's quantum, 0 exact */
    int ClockFrozen;                /* the clock reads FrozenUs: it never moves */
    unsigned long FrozenUs;

    /* the host interface */
    unsigned long Caps;
    unsigned long TxBaseLo, TxSize, TxCtrl, TxProd, TxCons;
    unsigned long RxBaseLo, RxSize, RxSizeReg, RxCtrl, RxProd, RxCons;
    unsigned long Imr[4];
    unsigned long HrReset;
    unsigned long HrResetAtUs;
    unsigned long HrResets;
    unsigned long MemDw[FAB_MEM_BYTES / 4];
    FAB_FRAME RxQ[FAB_RXQ];
    unsigned long RxQCount;
    unsigned long RxDropped;
    int Stopped;                    /* answer nothing */

    /* the fabric */
    FAB_ROUTER R[FAB_MAX_ROUTERS];
    FAB_HOOK Hook;
    void *HookCtx;

    /* logs */
    unsigned long Requests;
    unsigned long Reads;
    unsigned long Writes;
    unsigned long TxCount;
    unsigned long TxBadCrc;
    unsigned long AcksPlain;
    unsigned long AcksEnh;
    unsigned long HpAcks;
    unsigned long Notifications;    /* error notifications sent */
    unsigned long WrCount;
    unsigned long Lane1LockWrites;  /* ADP_CS_4 of a lane 1 adapter written:
                                     * undefined behaviour (Spec 8.2.2.1) */
    int WrRouter[FAB_WRLOG];
    unsigned long WrCs[FAB_WRLOG];
    unsigned long WrAdp[FAB_WRLOG];
    unsigned long WrAddr[FAB_WRLOG];
    unsigned long WrVal[FAB_WRLOG];
    unsigned long WrAtUs[FAB_WRLOG];
    unsigned long LogCount;
    const char *LastLabel;
    unsigned long LastValue;
    int Verbose;
} FAB;

/* Build */
void fabInit(FAB *f);
int  fabHostMinimal(FAB *f);        /* usb4-emu `minimal`: returns the host index 0 */
int  fabHostAdl(FAB *f);            /* usb4-emu `intel-adl` */
int  fabHostAsm4242(FAB *f);        /* usb4-emu `asm4242`: ASMedia, public sources */
int  fabDevice(FAB *f, int kind);   /* a device router, unplugged; its index */
int  fabChild(FAB *f, int dev);     /* a USB4 router behind a device's downstream port */

/* Attach and detach a device to host USB4 port `port` (1-based); events
 * are sent where hot plug is enabled. */
void fabPlug(FAB *f, int port, int dev);
void fabUnplug(FAB *f, int port);
/* Attach a child router to a device's downstream port as a pre-OS CM
 * would have it: enumerated at depth 2, unlocked; and pull it. */
void fabPlugChild(FAB *f, int dev, int child, int enumerate);
/* Put `dev` behind port `port` in place of what is there, with no event
 * and Lock as it was: a router a predecessor enumerated, swapped between
 * two checks (only its UUID tells). */
void fabSwapQuiet(FAB *f, int port, int dev);
void fabUnplugChild(FAB *f, int dev);

/* The pre-OS connection manager's leftovers on `port` (record 22 section
 * 9.1): host and device enumerated, bonded, PTO and CV, PCIe paths at lane
 * HopIDs 8 and 9, Path Enable both ends; `usb3` adds a USB3 tunnel at 10
 * and 11. */
void fabPreos(FAB *f, int port, int usb3);
/* A DisplayPort OUT adapter added to a router (its adapter count grown). */
void fabAddDpOut(FAB *f, int router, unsigned adp);
/* An adapter entry with its disabled bit added to a router's DROM (the
 * USB4 layout fabDevice builds), its length and CRC-32 fixed. */
void fabDromMarkUnused(FAB *f, int router, unsigned adp);

/* The whole domain loses power across a sleep (Spec 3.3): every router,
 * the host's included, back to its power-on state; links stay connected,
 * the host's lane adapters locked again. */
void fabPowerCycle(FAB *f);

/* Queries */
int  fabPortLane0(const FAB *f, int port);
int  fabPortPcie(const FAB *f, int port);
int  fabPortUsb3(const FAB *f, int port);
int  fabDevAt(const FAB *f, int port);
unsigned long fabPath0(const FAB *f, int router, int adp, int hop);
unsigned long fabPath1(const FAB *f, int router, int adp, int hop);
unsigned long fabAdp(const FAB *f, int router, int adp, int dw);
unsigned long fabRtr(const FAB *f, int router, int dw);
int  fabPcieUp(const FAB *f, int port);     /* both PCIe adapters enabled, L0 */
int  fabUsb3Up(const FAB *f, int port);
/* The index of the first logged write matching, from `from`; -1. */
long fabFindWrite(const FAB *f, long from, int router, unsigned long cs,
                  unsigned long adp, unsigned long addr);

/* A frame from the fabric, now (a notification, a hot plug event). */
void fabInject(FAB *f, unsigned long pdf, const unsigned long *dw, unsigned long words);

/* The DROM's CRC-32C (as the routers' DROMs carry it). */
unsigned long fabCrcOf(const unsigned char *p, unsigned long n);

/* The block for TbtU4Start */
void *fabMem(FAB *f);

#endif /* U4_FAKEFAB_H */
