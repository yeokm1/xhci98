/*
 * tbt_topo.h - finding a Thunderbolt domain's tunnel ports and reading
 * them, the pure half (roadmap-usb4.md task 39-A.5; design record 21
 * sections 6.3 and 6.5).
 *
 * Four things live here, all over PCI configuration space reached through
 * accessors the caller supplies (the driver passes HalGetBusData-backed
 * ones on every target; the host suite passes a modelled machine):
 *
 *   - the discovery of record 21 section 6.3: a domain's tunnel-port SET,
 *     each member with the source that put it there - the override list
 *     XhciCmTunnelBridge, a hot-plug port of the controller's own switch by
 *     the family's port ids, an integrated host's root port by the
 *     family's root port ids when it is the bus's only NHI of the family,
 *     or the emulator's SHPC bridge - and its own state. Any other bridge,
 *     however hot-plug capable, is never a member: the PCI association is
 *     ACPI's ("usb4-host-interface", Linux drivers/thunderbolt/acpi.c
 *     tb_acpi_add_links), which this driver does not read, and the fabric
 *     mapping (USB4 port to PCIe down adapter, Linux usb4.c
 *     usb4_switch_map_pcie_down and tb.c tb_find_pcie_down) names no PCI
 *     location. Every tunnel change acts on the whole set; exclusive
 *     ownership across domains is the caller's, with XhciTbtExclusive;
 *   - learning, beyond the record's sources, for a domain with hot-plug
 *     candidates and no member: the one candidate whose reading went from
 *     down to up across a tunnel up;
 *   - the port reading of section 6.5: Link Status Data Link Layer Link
 *     Active where Link Capabilities says it is reported, Slot Status
 *     Presence Detect State where the slot is implemented, and an SHPC
 *     slot's presence, folded into one verdict per port, and a set's
 *     verdicts folded into one;
 *   - the bounded poll of section 6.5 as a step machine over a caller's
 *     millisecond clock: before a tunnel up, until the port reads up; before
 *     a tunnel down, until it reads down; 2 s either way.
 *
 * The register layouts are the public specifications' (no copy of either
 * is kept in docs/references/; cited by section):
 *   - PCI Local Bus Specification 3.0, section 6.1 (the type 0/1 common
 *     header: Vendor ID 00h, Device ID 02h, Status 06h with Capabilities
 *     List at bit 4, Header Type 0Eh with the layout in bits 6:0 and
 *     multi-function in bit 7) and section 6.7 (the capabilities list:
 *     Capabilities Pointer 34h, each entry's ID and Next Pointer bytes, the
 *     bottom two bits of a pointer reserved);
 *   - PCI-to-PCI Bridge Architecture Specification 1.2, section 3.2 (the
 *     type 1 header: Primary, Secondary and Subordinate Bus Number at 18h,
 *     19h and 1Ah);
 *   - PCI Express Base Specification 4.0, section 7.5.3 (the PCI Express
 *     Capability, ID 10h): 7.5.3.2 PCI Express Capabilities at +02h
 *     (Device/Port Type bits 7:4, 4 a Root Port and 6 a switch Downstream
 *     Port; Slot Implemented bit 8), 7.5.3.6 Link Capabilities at +0Ch
 *     (Data Link Layer Link Active Reporting Capable bit 20), 7.5.3.8 Link
 *     Status at +12h (Data Link Layer Link Active bit 13), 7.5.3.9 Slot
 *     Capabilities at +14h (Hot-Plug Surprise bit 5, Hot-Plug Capable bit
 *     6), 7.5.3.11 Slot Status at +1Ah (Presence Detect Changed bit 3,
 *     Presence Detect State bit 6, Data Link Layer State Changed bit 8);
 *   - PCI Standard Hot-Plug Controller and Subsystem Specification 1.0,
 *     section 4.2 (the SHPC capability, ID 0Ch: DWORD Select at +02h, DWORD
 *     Data at +04h; DWORD 3 Slot Configuration with Number of Slots
 *     Implemented in bits 4:0 and First Device Number in bits 12:8; DWORD
 *     9 + n the Logical Slot Register of slot n, PRSNT1#/PRSNT2# in bits
 *     11:10, 11b an empty slot).
 *
 * C89, DDK-free: part of the pure core, with no kernel service, lock or
 * allocation. IRQL: any for the decoders and the poll machine, which take
 * no XHCI_TBT_CFG; the functions that take one run at their accessors'
 * IRQL, which with the driver's HalGetBusData accessors is PASSIVE_LEVEL
 * on the connection-manager thread, under no spin lock.
 */

#ifndef TBT_TOPO_H
#define TBT_TOPO_H

#include "../xhci_compat.h"

/*
 * A function's location, packed as a PCI routing id: bus in 15:8, device
 * in 7:3, function in 2:0. XHCI_TBT_NO_BDF is "none".
 */
#define XHCI_TBT_BDF(b, d, f)   ((((ULONG)(b) & 0xFFUL) << 8) | \
                                 (((ULONG)(d) & 0x1FUL) << 3) | \
                                 ((ULONG)(f) & 0x7UL))
#define XHCI_TBT_BDF_BUS(x)     (((ULONG)(x) >> 8) & 0xFFUL)
#define XHCI_TBT_BDF_DEV(x)     (((ULONG)(x) >> 3) & 0x1FUL)
#define XHCI_TBT_BDF_FN(x)      ((ULONG)(x) & 0x7UL)
#define XHCI_TBT_NO_BDF         0xFFFFFFFFUL

/* Configuration space, the common header and the type 1 header. */
#define XHCI_TBT_CFG_ID             0x00UL  /* vendor 15:0, device 31:16    */
#define XHCI_TBT_CFG_STATUS         0x04UL  /* Status in 31:16              */
#define XHCI_TBT_CFG_STATUS_CAPLIST 0x00100000UL /* the dword's bit 20     */
#define XHCI_TBT_CFG_HEADER         0x0CUL  /* Header Type in 23:16         */
#define XHCI_TBT_CFG_BUSES          0x18UL  /* primary 7:0, secondary 15:8,
                                               subordinate 23:16            */
#define XHCI_TBT_CFG_CAPPTR         0x34UL
#define XHCI_TBT_HDR_LAYOUT(h)      ((h) & 0x7FUL)
#define XHCI_TBT_HDR_MF             0x80UL
#define XHCI_TBT_HDR_BRIDGE         0x01UL

/* Capability ids. */
#define XHCI_TBT_CAP_SHPC           0x0CUL
#define XHCI_TBT_CAP_PCIE           0x10UL

/* The PCI Express Capability, offsets from its start. */
#define XHCI_TBT_PCIE_CAPS          0x00UL  /* PCIe Capabilities in 31:16   */
#define XHCI_TBT_PCIE_LINKCAP       0x0CUL
#define XHCI_TBT_PCIE_LINKSTA       0x10UL  /* Link Status in 31:16         */
#define XHCI_TBT_PCIE_SLOTCAP       0x14UL
#define XHCI_TBT_PCIE_SLOTSTA       0x18UL  /* Slot Status in 31:16         */

/* PCI Express Capabilities register (16 bits). */
#define XHCI_TBT_PCIE_TYPE(c)       (((c) >> 4) & 0xFUL)
#define XHCI_TBT_PCIE_TYPE_ROOT     0x4UL
#define XHCI_TBT_PCIE_TYPE_UP       0x5UL
#define XHCI_TBT_PCIE_TYPE_DOWN     0x6UL
#define XHCI_TBT_PCIE_SLOT_IMPL     0x0100UL
/* Link Capabilities. */
#define XHCI_TBT_LINKCAP_DLLLARC    0x00100000UL
/* Link Status (16 bits). */
#define XHCI_TBT_LINKSTA_DLLLA      0x2000UL
/* Slot Capabilities. */
#define XHCI_TBT_SLOTCAP_HPS        0x00000020UL
#define XHCI_TBT_SLOTCAP_HPC        0x00000040UL
/* Slot Status (16 bits). */
#define XHCI_TBT_SLOTSTA_PDC        0x0008UL
#define XHCI_TBT_SLOTSTA_PDS        0x0040UL
#define XHCI_TBT_SLOTSTA_DLLSC      0x0100UL

/* The SHPC capability, offsets from its start, and its register set. */
#define XHCI_TBT_SHPC_SELECT        0x02UL  /* one byte                     */
#define XHCI_TBT_SHPC_DATA          0x04UL
#define XHCI_TBT_SHPC_SLOTCFG       3UL     /* DWORD index                  */
#define XHCI_TBT_SHPC_SLOT0         9UL     /* DWORD index of slot 0        */
#define XHCI_TBT_SHPC_NSI(c)        ((c) & 0x1FUL)
#define XHCI_TBT_SHPC_FDN(c)        (((c) >> 8) & 0x1FUL)
#define XHCI_TBT_SHPC_PRSNT(s)      (((s) >> 10) & 0x3UL)
#define XHCI_TBT_SHPC_PRSNT_EMPTY   0x3UL

/*
 * The accessors. Read takes a dword-aligned offset and returns 1 when the
 * access was made, the dword in *value (all-ones where no function
 * answers). Write writes `width` bytes (1, 2 or 4) at `offset`, naturally
 * aligned, and returns 1 when the access was made; it is used for the SHPC
 * DWORD Select byte of a bridge alone, and may be NULL, in which case no
 * SHPC register is read and an SHPC port gives no presence reading.
 *
 * The writer is the driver half's to qualify, per target: it must reach a
 * type 1 (bridge) function's configuration space byte-wise at an offset
 * (Microsoft documents restrictions on HalSetBusDataByOffset writes to a
 * bridge's header, and Windows 98's HAL exports are a smaller set), and
 * the select-then-read sequence of one SHPC read must not interleave with
 * another of this driver's own on the same bridge: the caller serialises
 * its port reads, which the connection-manager thread does by running them
 * all. Where no writer is qualified, pass NULL.
 */
typedef ULONG (*XHCI_TBT_READ)(PVOID context, ULONG bdf, ULONG offset,
                               PULONG value);
typedef ULONG (*XHCI_TBT_WRITE)(PVOID context, ULONG bdf, ULONG offset,
                                ULONG width, ULONG value);

typedef struct _XHCI_TBT_CFG {
    XHCI_TBT_READ Read;
    XHCI_TBT_WRITE Write;
    PVOID Context;
    ULONG Reads;        /* accesses made through Read, for the log          */
    ULONG Writes;       /* accesses made through Write                      */
} XHCI_TBT_CFG, *PXHCI_TBT_CFG;

/* ---- decoders, over raw register values ------------------------------- */

/*
 * The PCI Express Capabilities register: 1 when the port is downstream-
 * facing (a Root Port or a switch Downstream Port), the only ports whose
 * Slot Implemented bit and Link Active report mean anything here.
 */
ULONG XhciTbtPcieDownstream(ULONG pcieCaps);

/* 1 when the slot is implemented on a downstream-facing port. */
ULONG XhciTbtPcieSlotImplemented(ULONG pcieCaps);

/*
 * Link Active: *valid is 1 when the port is downstream-facing and Link
 * Capabilities has Data Link Layer Link Active Reporting Capable; *active
 * is Link Status bit 13 then, and 0 otherwise.
 */
VOID XhciTbtDecodeLink(ULONG pcieCaps, ULONG linkCap, ULONG linkStatus,
                       PULONG valid, PULONG active);

/*
 * Presence: *valid is 1 when the slot is implemented; then *present is
 * Presence Detect State, *changed Presence Detect Changed and *dllChanged
 * Data Link Layer State Changed. All 0 when not valid.
 */
VOID XhciTbtDecodeSlot(ULONG pcieCaps, ULONG slotStatus, PULONG valid,
                       PULONG present, PULONG changed, PULONG dllChanged);

/* 1 when an implemented slot reports Hot-Plug Capable. */
ULONG XhciTbtPcieHotPlug(ULONG pcieCaps, ULONG slotCap);

/* 1 when an SHPC Logical Slot Register reports a card present. */
ULONG XhciTbtShpcSlotPresent(ULONG slotRegister);

/* ---- capability walk --------------------------------------------------- */

/* XhciTbtFindCapStatus's return. */
#define XHCI_TBT_CAP_ABSENT         0UL /* no list, or not in it            */
#define XHCI_TBT_CAP_FOUND          1UL
#define XHCI_TBT_CAP_FAILED         2UL /* an access failed, or the list is
                                           malformed (a loop past 48)       */
#define XHCI_TBT_CAP_GONE           3UL /* a read came back all-ones        */

/*
 * The capability list of the function at `bdf`: FOUND with the
 * capability's offset in *offset, or why not. A pointer below 40h ends
 * the list (ABSENT).
 */
ULONG XhciTbtFindCapStatus(PXHCI_TBT_CFG io, ULONG bdf, ULONG capId,
                           PULONG offset);

/* 1 when XhciTbtFindCapStatus says FOUND, 0 for anything else. */
ULONG XhciTbtFindCap(PXHCI_TBT_CFG io, ULONG bdf, ULONG capId,
                     PULONG offset);

/* ---- discovery (record 21 section 6.3) --------------------------------- */

/* XHCI_TBT_TOPO.Outcome. */
#define XHCI_TBT_TOPO_NONE          0UL /* not run                          */
#define XHCI_TBT_TOPO_SET           1UL /* a set, by discovery              */
#define XHCI_TBT_TOPO_OVERRIDE      2UL /* XhciCmTunnelBridge's set         */
#define XHCI_TBT_TOPO_NO_CANDIDATE  3UL /* no member, no hot-plug bridge    */
#define XHCI_TBT_TOPO_MANY          4UL /* members past the set's room      */
#define XHCI_TBT_TOPO_OVERRIDE_BAD  5UL /* the override named no bridge     */
#define XHCI_TBT_TOPO_NHI_ABSENT    6UL /* nothing answers at the NHI       */
#define XHCI_TBT_TOPO_BAD_PARAM     7UL
#define XHCI_TBT_TOPO_LEARN         8UL /* no member; hot-plug bridges seen */
#define XHCI_TBT_TOPO_INCOMPLETE    9UL /* a function on the search bus
                                           could not be read: try again     */
#define XHCI_TBT_TOPO_AMBIGUOUS     10UL /* listed root ports, but more than
                                            one NHI of the family: none     */

/* XHCI_TBT_BRIDGE.Flags. */
#define XHCI_TBT_BR_PCIE            0x001UL /* a PCI Express capability    */
#define XHCI_TBT_BR_SLOT            0x002UL /* an implemented slot         */
#define XHCI_TBT_BR_HOTPLUG         0x004UL /* Hot-Plug Capable            */
#define XHCI_TBT_BR_SURPRISE        0x008UL /* Hot-Plug Surprise           */
#define XHCI_TBT_BR_SHPC            0x010UL /* an SHPC capability          */
#define XHCI_TBT_BR_CANDIDATE       0x020UL /* HOTPLUG, SHPC or LISTED     */
#define XHCI_TBT_BR_LINKREPORT      0x040UL /* Link Active reported        */
#define XHCI_TBT_BR_LISTED          0x080UL /* its id is in the family's
                                               switch or root port list    */
#define XHCI_TBT_BR_UNREAD          0x100UL /* a capability walk failed    */
#define XHCI_TBT_BR_AMBIGUOUS       0x200UL /* a listed root port left out:
                                               two NHIs of the family      */
#define XHCI_TBT_BR_MEMBER          0x400UL /* in the set                  */

/* XHCI_TBT_TOPO.Source[]: what put a port in the set (record 21 6.3). */
#define XHCI_TBT_SRC_SWITCH         1UL /* a listed hot-plug port beside the
                                           NHI's parent                     */
#define XHCI_TBT_SRC_ROOTPORT       2UL /* a listed root port beside an
                                           integrated NHI, its only NHI     */
#define XHCI_TBT_SRC_OVERRIDE       3UL /* XhciCmTunnelBridge               */
#define XHCI_TBT_SRC_LEARNED        4UL /* came up across a tunnel up       */
#define XHCI_TBT_SRC_SHPC           5UL /* an SHPC bridge (the emulator)    */

#define XHCI_TBT_MAX_SIBLINGS       16UL    /* kept for the log             */
#define XHCI_TBT_MAX_CANDIDATES     16UL    /* kept for learning            */
/* Tunnel ports one domain can have: a two-port part's two downstream
 * ports, an integrated host's root ports. */
#define XHCI_TBT_MAX_TUNNELS        4UL

/* One type 1 function on the search bus, for the set and the log. */
typedef struct _XHCI_TBT_BRIDGE {
    ULONG Bdf;
    ULONG Id;           /* vendor 15:0, device 31:16                        */
    ULONG Buses;        /* the 18h dword: primary, secondary, subordinate   */
    ULONG PcieCap;      /* the capability's offset, 0 for none              */
    ULONG PcieCaps;     /* PCI Express Capabilities, 0 for none             */
    ULONG SlotCap;      /* Slot Capabilities, 0 where no slot               */
    ULONG ShpcCap;      /* the capability's offset, 0 for none              */
    ULONG Flags;        /* XHCI_TBT_BR_*                                    */
} XHCI_TBT_BRIDGE, *PXHCI_TBT_BRIDGE;

/*
 * What a discovery is asked. The id lists are the NHI's family's, from
 * record 21 section 3.3's tables (each id vendor 15:0, device 31:16); the
 * caller picks the family by the NHI's own id. A NULL list is empty.
 */
typedef struct _XHCI_TBT_TOPO_IN {
    ULONG Nhi;          /* the NHI's location                               */
    ULONG Overrides;    /* how many XhciCmTunnelBridge named, 0 for none    */
    ULONG Override[XHCI_TBT_MAX_TUNNELS];
    const ULONG *FamilyNhiIds;      /* the family's NHI ids                 */
    ULONG FamilyNhiIdCount;
    const ULONG *SwitchPortIds;     /* its discrete switch port ids         */
    ULONG SwitchPortIdCount;
    const ULONG *RootPortIds;       /* its integrated tunnel root port ids  */
    ULONG RootPortIdCount;
} XHCI_TBT_TOPO_IN, *PXHCI_TBT_TOPO_IN;

/*
 * One domain's discovery, and its tunnel ports' state. The caller owns it
 * (it is too large for a kernel stack frame on Windows 98: keep it in the
 * device extension); XhciTbtTopoFind writes all of it.
 */
typedef struct _XHCI_TBT_TOPO {
    ULONG Outcome;      /* XHCI_TBT_TOPO_*                                  */
    ULONG Nhi;          /* the NHI's location as given                      */
    ULONG NhiId;        /* its vendor/device                                */
    ULONG Parent;       /* the NHI's parent bridge, or XHCI_TBT_NO_BDF when
                           the NHI is on a root bus                         */
    ULONG ParentType;   /* its PCI Express port type, 0 for none            */
    ULONG SearchBus;    /* the bus the siblings were read on                */
    ULONG FamilyNhis;   /* on a root bus: NHIs of the family there, this
                           one included, started or not                     */
    ULONG Siblings;     /* type 1 siblings seen, the parent excluded; those
                           past XHCI_TBT_MAX_SIBLINGS are counted, not kept */
    XHCI_TBT_BRIDGE Sibling[XHCI_TBT_MAX_SIBLINGS];
    ULONG Candidates;   /* siblings hot-plug capable, SHPC or listed        */
    ULONG Cand[XHCI_TBT_MAX_CANDIDATES];    /* their locations, in bus order */
    ULONG Ambiguous;    /* listed root ports left out (FamilyNhis > 1)      */
    ULONG Unread;       /* functions not read completely                    */
    ULONG Dropped;      /* override entries dropped (no bridge, repeated)   */
    /* the set */
    ULONG Tunnels;      /* how many                                         */
    ULONG Tunnel;       /* the first, or XHCI_TBT_NO_BDF                    */
    ULONG Set[XHCI_TBT_MAX_TUNNELS];        /* their locations              */
    ULONG Source[XHCI_TBT_MAX_TUNNELS];     /* XHCI_TBT_SRC_*               */
    ULONG Last[XHCI_TBT_MAX_TUNNELS];       /* the last XHCI_TBT_PORT_* read */
    ULONG Base[XHCI_TBT_MAX_TUNNELS];       /* each one's at XhciTbtSetArm  */
    ULONG BaseArmed;
    XHCI_TBT_BRIDGE Pick;   /* the first one's reading                      */
    /* learning: each candidate's reading at XhciTbtLearnArm */
    ULONG LearnArmed;
    ULONG Before[XHCI_TBT_MAX_CANDIDATES];
    /* the walk's own state */
    ULONG Visited[8];   /* one bit per bus number                           */
    UCHAR Stack[256];   /* buses still to walk                              */
} XHCI_TBT_TOPO, *PXHCI_TBT_TOPO;

/*
 * Discovers the tunnel-port set of the domain whose NHI is at in->Nhi,
 * record 21 section 6.3's sources in its order. No layout is assumed;
 * each test reads the hardware:
 *
 *   1. in->Override (XhciCmTunnelBridge): when given it is the whole set,
 *      SRC_OVERRIDE. An entry that is not a type 1 function, or repeats
 *      one, is dropped (Dropped); with every entry dropped the outcome is
 *      OVERRIDE_BAD and the set empty. The discovery below still runs, so
 *      the log can name what it saw.
 *   2. The discrete switch: the NHI's parent is the type 1 function whose
 *      secondary bus is the NHI's bus; a member is a type 1 sibling of the
 *      parent (not the parent) whose id is in in->SwitchPortIds and which
 *      is Hot-Plug Capable in an implemented slot, SRC_SWITCH. The id test
 *      makes the set the controller's own switch and nothing else on the
 *      bus (Linux drivers/thunderbolt/icm.c get_upstream_port knows the
 *      switch by its ids too).
 *   3. The integrated root ports: the NHI on a root bus (no parent); a
 *      member is a type 1 function on that bus whose id is in
 *      in->RootPortIds, SRC_ROOTPORT, hot-plug capable or not - but only
 *      when the bus carries one NHI of the family (FamilyNhis, counting
 *      every function whose id is in in->FamilyNhiIds, started, disabled
 *      or failed alike, and this NHI). With two, every listed root port
 *      is AMBIGUOUS and left out: which is whose is ACPI's to say (Linux
 *      acpi.c tb_acpi_add_link), unread here.
 *   4. The emulator: a type 1 sibling of the parent, or of the NHI on a
 *      root bus, with an SHPC capability, SRC_SHPC.
 *
 * Any other bridge, however hot-plug capable, is never a member; with no
 * member the outcome is LEARN (hot-plug bridges were seen; XhciTbtLearn
 * may add one), AMBIGUOUS, or NO_CANDIDATE, and re-enumeration stays
 * disabled (record 21 section 6.4). A function on the search bus that
 * could not be read completely makes it INCOMPLETE with an empty set,
 * never a set that might lack a port. More than XHCI_TBT_MAX_TUNNELS
 * members is MANY, empty. Exclusive ownership across domains is decided
 * after this, from every domain's claims (XhciTbtExclusive).
 *
 * Returns topo->Outcome; topo->Set holds topo->Tunnels ports for SET and
 * OVERRIDE and none otherwise.
 */
ULONG XhciTbtTopoFind(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO_IN *in,
                      PXHCI_TBT_TOPO topo);

/*
 * Exclusive ownership (record 21 section 6.3): for each of this domain's
 * `count` members in set[], active[i] is 1 when no other domain claims it
 * (others[0..otherCount-1], every other domain's claims by any source
 * together) and 0 when one does: a port two domains claim is inactive for
 * both until only one does. Returns the number left inactive.
 */
ULONG XhciTbtExclusive(const ULONG *set, ULONG count, const ULONG *others,
                       ULONG otherCount, PULONG active);

/*
 * Parses XhciCmTunnelBridge, a REG_SZ of one to XHCI_TBT_MAX_TUNNELS
 * locations "bb:dd.f" in hexadecimal separated by commas (bus one or two
 * digits, device one or two digits up to 1F, function one digit 0-7, no
 * spaces), `chars` WCHARs long or ending at the first NUL. Returns the
 * number parsed into list[] (at most `capacity`), or 0 for anything else.
 */
ULONG XhciTbtParseBdfList(PCWSTR text, ULONG chars, PULONG list,
                          ULONG capacity);

/*
 * Learning, a step beyond record 21 section 6.3's sources, for a LEARN
 * domain (or one whose every member was learned): XhciTbtLearnArm reads
 * every candidate before a tunnel up; XhciTbtLearn reads them again after
 * it (once the caller's poll has ended) and adds to the set, SRC_LEARNED,
 * the one candidate not yet in it that read DOWN when armed and UP now.
 * Nothing is learned when two or more did, when another came up from an
 * uncertain reading (MIXED, GONE or NONE), when any candidate's reading
 * failed at either end, when discovery was not complete or left listed
 * ports ambiguous, or when there were more candidates than are kept: the
 * uniqueness could not be judged. Returns the number learned, 0 or 1, and
 * disarms either way.
 */
VOID XhciTbtLearnArm(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo);
ULONG XhciTbtLearn(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo);

/* ---- the port reading (record 21 section 6.5) -------------------------- */

/* XHCI_TBT_PORT.Verdict and XhciTbtPortRead's return. */
#define XHCI_TBT_PORT_ERROR         0UL /* an access or walk failed         */
#define XHCI_TBT_PORT_UP            1UL /* every reading says up            */
#define XHCI_TBT_PORT_DOWN          2UL /* every reading says down          */
#define XHCI_TBT_PORT_MIXED         3UL /* the readings disagree            */
#define XHCI_TBT_PORT_NONE          4UL /* the port has nothing to read     */
#define XHCI_TBT_PORT_GONE          5UL /* the port bridge itself reads
                                           all-ones (not its endpoint)      */

typedef struct _XHCI_TBT_PORT {
    ULONG Verdict;      /* XHCI_TBT_PORT_*                                  */
    ULONG Id;           /* vendor 15:0, device 31:16                        */
    ULONG PcieCap;      /* offset, 0 for none                               */
    ULONG PcieCaps;
    ULONG LinkCap;
    ULONG LinkStatus;
    ULONG SlotStatus;
    ULONG LinkValid;    /* Link Active is reported                          */
    ULONG LinkActive;
    ULONG SlotValid;    /* the slot is implemented                          */
    ULONG Presence;     /* Presence Detect State                            */
    ULONG PresenceChanged;
    ULONG DllChanged;   /* Data Link Layer State Changed                    */
    ULONG ShpcCap;      /* offset, 0 for none                               */
    ULONG ShpcValid;    /* the SHPC slots were read                         */
    ULONG ShpcSlots;    /* Number of Slots Implemented                      */
    ULONG ShpcPresent;  /* slots reporting a card present                   */
    ULONG ShpcFirst;    /* 1 + the first present slot's index, 0 for none   */
} XHCI_TBT_PORT, *PXHCI_TBT_PORT;

/*
 * Reads the tunnel port at `bdf` and folds every valid reading into
 * port->Verdict, which it returns: UP when Link Active (where reported),
 * Presence Detect State (where the slot is implemented) and the SHPC
 * slots (any present) all say present, DOWN when they all say absent,
 * MIXED when they disagree, NONE when the port has none of the three.
 * A failed access or capability walk is ERROR, never NONE. GONE is the
 * port bridge reading all-ones; an endpoint behind a downed tunnel reading
 * all-ones is not read here and is a presence signal only. An SHPC read
 * writes the DWORD Select byte and puts back the value it found there; a
 * failure to put it back is ERROR.
 */
ULONG XhciTbtPortRead(PXHCI_TBT_CFG io, ULONG bdf, PXHCI_TBT_PORT port);

/*
 * Folds a set's readings (verdicts[0..count-1]) into one for the poll of
 * `want`, where the ICM does not say which port a device is on. `bases`
 * is each member's reading when the poll began (XhciTbtSetArm), or NULL.
 *
 * Up wanted: UP when a member whose base was DOWN, MIXED or GONE now reads
 * UP (that port made progress), or when every member reads UP; never on a
 * member that was already up, so an existing tunnel on another port does
 * not end the settling window early. Down wanted: DOWN when every member
 * reads DOWN, GONE when every member reads DOWN or GONE and one GONE: an
 * affirmative reading from every member, since the departed device's port
 * is not known. Otherwise ERROR if any member's read failed, NONE if every
 * member reads NONE, else MIXED: with another device still tunnelled the
 * down poll runs to its timeout, which the caller treats as "not read
 * clear" and still rescans every port of the set.
 */
ULONG XhciTbtSetVerdict(const ULONG *verdicts, const ULONG *bases,
                        ULONG count, ULONG want);

/* Reads every port of the set into topo->Base[] and arms the baseline. */
VOID XhciTbtSetArm(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo);

/*
 * Reads every port of the set into topo->Last[] and returns their fold
 * for `want`, with the baseline when armed; NONE for an empty set.
 */
ULONG XhciTbtSetRead(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo, ULONG want);

/* ---- the bounded poll (record 21 section 6.5) -------------------------- */

#define XHCI_TBT_WANT_UP            1UL /* before a tunnel up               */
#define XHCI_TBT_WANT_DOWN          2UL /* before a tunnel down             */

#define XHCI_TBT_POLL_BUDGET_MS     2000UL
#define XHCI_TBT_POLL_STEP_MS       50UL    /* between reads                */

/* XHCI_TBT_POLL.State and the step functions' return. */
#define XHCI_TBT_POLL_RUNNING       0UL
#define XHCI_TBT_POLL_REACHED       1UL /* the port read what was wanted    */
#define XHCI_TBT_POLL_GONE          2UL /* down wanted; the port vanished   */
#define XHCI_TBT_POLL_TIMEOUT       3UL /* the budget passed first          */
#define XHCI_TBT_POLL_NO_READING    4UL /* the port has nothing to read     */
#define XHCI_TBT_POLL_ABANDONED     5UL /* the caller gave up on it         */
#define XHCI_TBT_POLL_BAD_PARAM     6UL

typedef struct _XHCI_TBT_POLL {
    ULONG Want;         /* XHCI_TBT_WANT_*                                  */
    ULONG Start;        /* the caller's clock at XhciTbtPollBegin, in ms    */
    ULONG Budget;       /* ms                                               */
    ULONG State;        /* XHCI_TBT_POLL_*                                  */
    ULONG Steps;        /* readings judged                                  */
    ULONG Last;         /* the last verdict, XHCI_TBT_PORT_*                */
    ULONG Elapsed;      /* ms from Start to the step that ended it          */
} XHCI_TBT_POLL, *PXHCI_TBT_POLL;

/*
 * Starts a poll for `want` at `nowMs` (any free-running millisecond clock;
 * differences are taken modulo 2^32). `budgetMs` 0 means
 * XHCI_TBT_POLL_BUDGET_MS. Returns RUNNING, or BAD_PARAM for a bad want.
 */
ULONG XhciTbtPollBegin(PXHCI_TBT_POLL poll, ULONG want, ULONG nowMs,
                       ULONG budgetMs);

/*
 * Judges one reading taken at `nowMs`. Up wanted: UP ends it REACHED.
 * Down wanted: DOWN ends it REACHED and GONE ends it GONE. Either way NONE
 * ends it NO_READING at once (there is nothing to wait for); otherwise,
 * once `nowMs` is the budget or more past Start, TIMEOUT; else RUNNING.
 * A poll already ended keeps its State. Returns poll->State.
 */
ULONG XhciTbtPollStep(PXHCI_TBT_POLL poll, ULONG verdict, ULONG nowMs);

/*
 * Reads the port at `bdf` (into *port, which may not be NULL) and judges
 * the reading. Returns poll->State.
 */
ULONG XhciTbtPollPort(PXHCI_TBT_CFG io, ULONG bdf, PXHCI_TBT_POLL poll,
                      PXHCI_TBT_PORT port, ULONG nowMs);

/*
 * Ends a running poll ABANDONED (a device disconnected for the device a
 * tunnel-up poll waits on: the tunnel-down sequence runs in its place).
 * Returns poll->State.
 */
ULONG XhciTbtPollAbandon(PXHCI_TBT_POLL poll, ULONG nowMs);

/*
 * How long to wait before the next reading of a running poll: the step,
 * or what is left of the budget when that is less; 0 when the poll has
 * ended or the budget is spent.
 */
ULONG XhciTbtPollWait(const XHCI_TBT_POLL *poll, ULONG nowMs);

#endif /* TBT_TOPO_H */
