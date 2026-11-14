/*
 * tbt_topo.h - finding a Thunderbolt domain's tunnel ports and reading
 * them, the pure half (roadmap-usb4.md tasks 39-A.5 and 40-A.4; design
 * record 21 sections 6.3 and 6.5, design record 22 section 7.6).
 *
 * Three things live here, all over PCI configuration space reached through
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
 *   - under the USB4 profile (record 22 section 7.6), the router-named
 *     source and the port-to-member correspondence: the host router's Get
 *     PCIe Downstream Entry Mapping answer names a PCIe Down adapter's
 *     bridge (XhciTbtTopoAddNamed), and where it does not, the member
 *     whose secondary bus gained a device when a port's tunnel, and no
 *     other, came up is learned as that port's (XhciTbtLearnArm,
 *     XhciTbtLearn); the bindings live in the caller's XHCI_TBT_BINDS for
 *     the instance. A tunnel change then acts on the port's own member
 *     (XhciTbtPortMember), and on the whole set while it is not known;
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
#define XHCI_TBT_TOPO_UNLISTED      8UL /* no member; hot-plug bridges seen,
                                           none a source names: the
                                           override is the way              */
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
#define XHCI_TBT_SRC_ROUTER         4UL /* router-named: the USB4 host
                                           router's Get PCIe Downstream
                                           Entry Mapping (record 22 7.6)   */
#define XHCI_TBT_SRC_SHPC           5UL /* an SHPC bridge (the emulator)    */
#define XHCI_TBT_SRC_ACPI           6UL /* ACPI-named: the root port's _DSD
                                           usb4-host-interface names this
                                           NHI (record 22 7.6.1, amd64)    */


#define XHCI_TBT_MAX_SIBLINGS       16UL    /* kept for the log             */
#define XHCI_TBT_MAX_CANDIDATES     16UL    /* kept for the log             */
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
    const ULONG *FamilyNhiIds;      /* the family's NHI ids, complete and
                                       holding this NHI's own, or source 3
                                       takes nothing                        */
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
    /* the USB4 profile's router-named members (record 22 section 7.6) */
    ULONG FindOutcome;  /* XhciTbtTopoFind's own outcome, kept when a
                           router-named member turns Outcome into SET       */
    ULONG Named;        /* members XhciTbtTopoAddNamed added                */
    ULONG AcpiNamed;    /* members XhciTbtTopoAddAcpi added                 */
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
 *      SRC_OVERRIDE. An entry with no function there, not a type 1
 *      function, or repeating one, is dropped (Dropped); with every entry
 *      dropped the outcome is OVERRIDE_BAD and the set empty. An entry
 *      that cannot be read makes it INCOMPLETE, empty. The discovery
 *      below still runs, so the log can name what it saw.
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
 *      when the bus carries exactly one NHI of the family and it is this
 *      one (FamilyNhis counts every function whose id is in
 *      in->FamilyNhiIds, started, disabled or failed alike; a list that
 *      does not hold this NHI's id proves nothing and takes nothing).
 *      Otherwise every listed root port is AMBIGUOUS and left out: which
 *      is whose is ACPI's to say (Linux acpi.c tb_acpi_add_link), unread
 *      here.
 *   4. The emulator: a type 1 sibling of the parent, or of the NHI on a
 *      root bus, with an SHPC capability, SRC_SHPC.
 *
 * Any other bridge, however hot-plug capable, is never a member; with no
 * member the outcome is UNLISTED (hot-plug bridges were seen and logged;
 * only the override can name one), AMBIGUOUS, or NO_CANDIDATE, and
 * re-enumeration stays disabled (record 21 section 6.4). The log's MEMBER
 * flags are set from the final set. A function on the search bus that
 * could not be read completely makes it INCOMPLETE with an empty set,
 * never a set that might lack a port. More than XHCI_TBT_MAX_TUNNELS
 * members is MANY, empty. Exclusive ownership across domains is decided
 * after this, from every domain's claims (XhciTbtExclusive).
 *
 * Under the USB4 profile the router-named members join after this
 * (XhciTbtTopoAddNamed), before the claims are published.
 *
 * Returns topo->Outcome, also kept in topo->FindOutcome; topo->Set holds
 * topo->Tunnels ports for SET and OVERRIDE and none otherwise. The
 * port-to-bridge correspondence lives in the caller's XHCI_TBT_BINDS and
 * outlives a rediscovery.
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

/* ---- the USB4 profile's correspondence (record 22 section 7.6) --------- */

/*
 * XHCI_TBT_BINDS.How[]: how a bridge is known as a USB4 port's. A bridge
 * is at most one port's and a port has at most one bridge; two claims
 * that disagree leave both unknown for the instance (CONFLICT), never one
 * picked.
 */
#define XHCI_TBT_CORR_NONE          0UL
#define XHCI_TBT_CORR_NAMED         1UL /* the router named it              */
#define XHCI_TBT_CORR_LEARNED       2UL /* its secondary bus gained a device
                                           at the port's tunnel up          */
#define XHCI_TBT_CORR_CONFLICT      3UL /* claims disagreed: never known    */

/* USB4 ports a correspondence can name: 1 to 32 (TBT_U4_PORT.Index). */
#define XHCI_TBT_MAX_PORTS          32UL
#define XHCI_TBT_MAX_BINDS          8UL
#define XHCI_TBT_NO_MEMBER          0xFFFFFFFFUL

/*
 * One domain's port-to-bridge correspondence, kept by bridge location so
 * that it outlives a rediscovery: the caller owns it for the life of the
 * driver instance (XhciTbtBindsInit at start), beside its XHCI_TBT_TOPO,
 * and passes both. A binding answers only while its bridge is a member of
 * the set as discovered now (XhciTbtPortMember).
 */
typedef struct _XHCI_TBT_BINDS {
    ULONG Count;
    ULONG Port[XHCI_TBT_MAX_BINDS];     /* 1-based, 0 for a conflicted one  */
    ULONG Bdf[XHCI_TBT_MAX_BINDS];
    ULONG How[XHCI_TBT_MAX_BINDS];      /* XHCI_TBT_CORR_*                  */
    ULONG PortConflict; /* bit n-1: port n had claims that disagreed        */
    ULONG Overflow;     /* a claim found no room: no port is known          */
    ULONG Pending;      /* bit n-1: port n's PCIe tunnel came up, its bridge
                           not yet learned (XhciTbtLearnUp)                 */
    /* the learning baseline: each member's secondary bus and the devices
       whose function 0 answered there (bit d for device d) */
    ULONG Armed;
    ULONG BaseCount;
    ULONG BaseBdf[XHCI_TBT_MAX_TUNNELS];
    ULONG BaseBus[XHCI_TBT_MAX_TUNNELS];
    ULONG BaseSeen[XHCI_TBT_MAX_TUNNELS];
    ULONG BaseDown;     /* bit i: BaseBdf[i]'s port read DOWN at a reading
                           where nothing answered behind it, and nothing
                           has been read answering there since (the link
                           evidence of record 22 section 7.6.4)             */
    ULONG BaseHeld;     /* bit i: BaseBdf[i] read occupied at an attempt
                           that ended before the baseline moved: its
                           BaseDown bit stands only while it stays occupied */
    /* the narrowed rule's at-start ports and start reading (record 22
       section 7.6.3) */
    ULONG AtStart;      /* bit n-1: port n pending with a tunnel found up at
                           start, no TUNNEL_UP since (XhciTbtLearnAtStart)  */
    ULONG StartTaken;   /* the start reading below was taken                */
    ULONG StartOk;      /* ... and is trusted                               */
    ULONG StartCount;
    ULONG StartBdf[XHCI_TBT_MAX_TUNNELS];   /* the members then             */
    ULONG StartOcc;     /* bit i: StartBdf[i] had a function behind it      */
} XHCI_TBT_BINDS, *PXHCI_TBT_BINDS;

/* XhciTbtTopoAddNamed's, XhciTbtLearnArm's and XhciTbtLearn's return. */
#define XHCI_TBT_MAP_ADDED          1UL /* named: joined the set, ROUTER    */
#define XHCI_TBT_MAP_KNOWN          2UL /* named: a member already, now (or
                                           still) the port's; learn: the
                                           port's bridge was known already  */
#define XHCI_TBT_MAP_LEARNED        3UL /* learn: the member is the port's  */
#define XHCI_TBT_MAP_NO_BRIDGE      4UL /* named: no function there, or not
                                           a PCI-to-PCI bridge              */
#define XHCI_TBT_MAP_UNREAD         5UL /* named: the bridge could not be
                                           read, the discovery is voided
                                           INCOMPLETE; arm/learn: a member
                                           could not be read, nothing taken */
#define XHCI_TBT_MAP_REFUSED        6UL /* named: the discovery admits no
                                           router-named member, or it is
                                           the NHI or its parent            */
#define XHCI_TBT_MAP_FULL           7UL /* named: no room; the discovery is
                                           voided MANY                      */
#define XHCI_TBT_MAP_CONFLICT       8UL /* the claim disagrees with one
                                           already made: neither is kept    */
#define XHCI_TBT_MAP_NOT_GAINED     9UL /* learn: no member gained a device */
#define XHCI_TBT_MAP_SEVERAL        10UL /* learn: more than one gained, or
                                            another tunnel is pending       */
#define XHCI_TBT_MAP_INACTIVE       11UL /* learn: the member that gained is
                                            not active (another domain
                                            claims it)                      */
#define XHCI_TBT_MAP_NOT_ARMED      12UL /* learn: no baseline over this set;
                                            one is taken now                */
#define XHCI_TBT_MAP_BAD_PARAM      13UL
#define XHCI_TBT_MAP_NOT_UP         14UL /* learn: no XhciTbtLearnUp for the
                                            port pending                    */
#define XHCI_TBT_MAP_SHAPE          15UL /* learn: the port pending alone,
                                            but what its member gained is
                                            no tunnel's far end and carries
                                            no link evidence (record 22
                                            section 7.6.4)                  */

/* Zeroes `binds`: nothing known, nothing armed. */
VOID XhciTbtBindsInit(PXHCI_TBT_BINDS binds);

/*
 * The router-named source (record 22 sections 6.3 and 7.6): an entry of
 * the host router's Get PCIe Downstream Entry Mapping answer (operation
 * 30h, USB4 Specification Tables 8-26, 8-47 and 8-48) whose non-FPB
 * Routing ID `bdf` (bus 15:8, device 7:3, function 2:0, the packing of
 * XHCI_TBT_BDF and of TBT_U4_PORT.MapRid) names the bridge of the PCIe
 * Down adapter of USB4 port `port` (1-based, TBT_U4_PORT.Index). Called
 * after every XhciTbtTopoFind the caller acts on, once per entry the
 * router answered, before the set's claims are published for
 * XhciTbtExclusive.
 *
 * What it allows, and nothing else:
 *   - with the discovery's outcome SET, NO_CANDIDATE, UNLISTED or
 *     AMBIGUOUS, a location where a function answers and is a PCI-to-PCI
 *     bridge (header layout 1), neither the NHI nor its parent, joins the
 *     set with source XHCI_TBT_SRC_ROUTER (ADDED; Outcome becomes SET,
 *     FindOutcome keeps the discovery's). An ambiguous listed root port
 *     the router names joins too, the exclusive-ownership check deciding
 *     as for any claim. No function there or not a bridge: NO_BRIDGE.
 *   - A bridge already a member, by any source, keeps that source and is
 *     bound to the port (KNOWN).
 *   - With the override (OVERRIDE) the override is the whole set: a named
 *     member is bound (KNOWN), any other location REFUSED. With
 *     OVERRIDE_BAD, INCOMPLETE, MANY or no discovery: REFUSED.
 *   - A named location that cannot be read (a failed access, or a
 *     present function whose header reads all-ones) voids the discovery:
 *     the set is emptied, Outcome INCOMPLETE (UNREAD), and the caller
 *     discovers again; one more member than the set has room for voids
 *     it as MANY (FULL). Never a set that might lack a named port.
 *   - The port bound to another bridge, or the bridge to another port, by
 *     naming or learning: CONFLICT, both withdrawn for the instance; a
 *     valid bridge not yet a member still joins the set (membership is
 *     not correspondence: the whole set acts for it). The same naming
 *     again is KNOWN.
 */
ULONG XhciTbtTopoAddNamed(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo,
                          PXHCI_TBT_BINDS binds, ULONG port, ULONG bdf);

/*
 * The ACPI-named source (record 22 section 7.6.1): bridge `bdf`, whose
 * root port's _DSD names this NHI, offered to the set under
 * XhciTbtTopoAddNamed's rules with no port and no binding: the member joins
 * unbound with source XHCI_TBT_SRC_ACPI (ADDED); a bridge already a member
 * keeps its source (KNOWN); the outcomes, refusals, UNREAD and FULL as
 * there. The firmware's usb4-port-number is never a binding.
 */
ULONG XhciTbtTopoAddAcpi(PXHCI_TBT_CFG io, PXHCI_TBT_TOPO topo, ULONG bdf);

/*
 * The learned correspondence (record 22 section 7.6), for a port whose
 * bridge the router did not name: the member whose secondary bus gained a
 * device (any device number's function 0 newly answering) while that
 * port's PCIe tunnel, and no other not yet attributed, had come up. A
 * gain on a bridge bound to another port (named or learned) is that
 * port's own and explains nothing here. What the member gained must be a
 * tunnel's far end, or carry the link's evidence (XhciTbtGainAdmits,
 * record 22 section 7.6.4).
 *
 * XhciTbtLearnArm takes the baseline: for every member, its secondary bus
 * and which devices answer there, and for each empty member whether its
 * port reads DOWN (or an earlier baseline read it so with the member empty
 * since, kept by location: BaseDown). The caller arms after each discovery it
 * acts on and after each tunnel-down it handles; XhciTbtLearn moves the
 * baseline forward itself. Returns 0, UNREAD (a member could not be read:
 * not armed) or BAD_PARAM (an empty set among them).
 */
ULONG XhciTbtLearnArm(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                      PXHCI_TBT_BINDS binds);

/*
 * Record 22 section 7.6.5 (task 49.18): an SHPC member's evidence that an
 * arm could not take, read again without an arm.
 *
 * XhciTbtShpcSet: 1 when the set has members and every one is of source
 * SHPC, the first condition for giving the learning an accessor that
 * writes DWORD Select (the driver half checks each bridge itself).
 *
 * XhciTbtLearnOwed: bit i for each member i of an armed baseline over this
 * set that is empty there and has no BaseDown bit; 0 for a baseline not
 * armed or taken over another set.
 *
 * XhciTbtLearnEvidence: for each owed member alone, its occupancy read
 * again; where that read is whole, the secondary bus is the baseline's,
 * nothing answers and the port then reads DOWN, its BaseDown bit is set.
 * Anything else leaves the member owed. Sets bits only: the baseline, the
 * armed mark, BaseHeld and every other bit stand, whatever fails. Returns
 * the members still owed. The caller makes it only while no port is
 * pending.
 *
 * XhciTbtEvidenceDue: 1 when a refresh is due at an idle pass: something
 * owed, no port pending, and either less than XHCI_TBT_POLL_BUDGET_MS
 * since `armMs` (the instance's millisecond clock, unsigned elapsed time)
 * or a member's record (`count` entries: 0 for no published filter owned
 * by the domain, else that publication's serial) differing from the one
 * taken before the last reading.
 */
ULONG XhciTbtShpcSet(const XHCI_TBT_TOPO *topo);
ULONG XhciTbtLearnOwed(const XHCI_TBT_TOPO *topo, const XHCI_TBT_BINDS *binds);
ULONG XhciTbtLearnEvidence(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                           PXHCI_TBT_BINDS binds);
ULONG XhciTbtEvidenceDue(ULONG owed, ULONG pending, ULONG armMs, ULONG nowMs,
                         const ULONG *record, const ULONG *recordAt,
                         ULONG count);

/*
 * The tunnels not yet attributed. The driver half calls XhciTbtLearnUp for
 * EVERY TBT_U4_ACT_TUNNEL_UP of a drained batch before it calls
 * XhciTbtLearn for any of them (a port already bound is not recorded; a
 * conflicted one is, its device being unattributable), and
 * XhciTbtLearnDown only on affirmative evidence that the port's PCIe
 * tunnel is closed for that generation - a teardown whose read-back
 * proved the path closed (TBT_U4_LINKDOWN.PathClosed) or the router's
 * removal evidence (record 22 section 7.1 items 1 to 3) - never on a
 * TBT_U4_ACT_TUNNEL_DOWN alone (an eject whose teardown failed queues one
 * too); then arms again. A port stays pending until it is learned or
 * proven down, and while any other port is pending no gain is attributed:
 * an earlier tunnel whose device has not answered yet could explain it.
 */
VOID XhciTbtLearnUp(PXHCI_TBT_BINDS binds, ULONG port);
VOID XhciTbtLearnDown(PXHCI_TBT_BINDS binds, ULONG port);

/*
 * The narrowed rule's at-start ports (record 22 section 7.6.3).
 * XhciTbtLearnAtStart marks a pending, unbound port whose PCIe tunnel was
 * found up at start (the driver half calls it at the start alone, after
 * XhciTbtLearnUp); a port newly marked makes the next full reading of the
 * set (XhciTbtLearnArm's, or XhciTbtLearn's) the start reading: the
 * members, which had a function behind them, and whether it is trusted -
 * every member of source SWITCH or ROUTER, every pending port an at-start
 * port, every occupied member no binding explains holding device 0 alone,
 * an Upstream Switch Port (a tunnel's far end, never a function of the
 * controller's own), and as many of those as at-start ports.
 * XhciTbtLearnNotAtStart ends the mark, at every TUNNEL_UP of the
 * port; XhciTbtLearnDown, a LEARNED or KNOWN outcome and XhciTbtLearnBound
 * end it too. With no mark left the start reading is forgotten.
 */
VOID XhciTbtLearnAtStart(PXHCI_TBT_BINDS binds, ULONG port);
VOID XhciTbtLearnNotAtStart(PXHCI_TBT_BINDS binds, ULONG port);

/*
 * For the PCIe tunnel of `port` that came up (each TBT_U4_ACT_TUNNEL_UP
 * whose port and generation are still live; again later while it returns
 * NOT_GAINED). The outcomes, in this order:
 *   - the port already bound (named or learned): KNOWN, nothing read;
 *   - the port conflicted, or the table overflowed: CONFLICT;
 *   - the port not pending (no XhciTbtLearnUp): NOT_UP, nothing read;
 *   - no baseline, or one taken over another set: NOT_ARMED, and one is
 *     taken now;
 *   - a member that cannot be read, or whose secondary bus moved, or the
 *     one gain's shape or port reading (XhciTbtGainAdmits) that cannot be
 *     read: UNREAD, the baseline kept;
 *   - otherwise every member is read and the baseline moves to this
 *     reading; then, counting gains on bridges not bound to another port:
 *     none: NOT_GAINED (the device may still answer Configuration Request
 *     Retry; the port stays pending); more than one, or another port
 *     pending: SEVERAL - unless (record 22 section 7.6.3) the start
 *     reading is taken and trusted, every other pending port is an
 *     at-start port and this one is not, every member is active, the
 *     bridge that gained was a member at the start reading and empty
 *     then, and XhciTbtGainAdmits admits what it gained; one gain with
 *     the port pending alone that XhciTbtGainAdmits does not admit (no
 *     tunnel's far end, no link evidence): SHAPE; the bridge that gained
 *     inactive (`active`, per
 *     member as XhciTbtExclusive gives it; NULL for all active):
 *     INACTIVE; conflicted or bound elsewhere: CONFLICT; else LEARNED,
 *     the port's bridge for the instance. Only LEARNED (and KNOWN) ends
 *     the port's pending mark; every other outcome leaves it pending
 *     until XhciTbtLearnDown.
 */
ULONG XhciTbtLearn(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                   PXHCI_TBT_BINDS binds, ULONG port, const ULONG *active);

/*
 * The index in topo->Set of USB4 port `port`'s bridge, named or learned,
 * or XHCI_TBT_NO_MEMBER when it is not known or not a member of the set
 * now (the tunnel change then acts on the whole set, and no stor98
 * statement names one member of a set of more than one: record 22
 * sections 7.6 and 10.2).
 */
ULONG XhciTbtPortMember(const XHCI_TBT_TOPO *topo,
                        const XHCI_TBT_BINDS *binds, ULONG port);

/*
 * The learning's window (record 22 section 7.6, task 44.57): when each
 * pending port is tried again, and when its tries stop. It bounds the
 * reads only: a port whose window closed stays pending (XHCI_TBT_BINDS),
 * exactly as before; it is simply not tried. The clock is the caller's,
 * one per instance beside its XHCI_TBT_BINDS; it reads nothing and calls
 * nothing. Times are milliseconds of a free-running counter, compared as
 * unsigned elapsed time (a wrap is harmless).
 *   - XhciTbtLearnOpen opens port `port`'s window at `nowMs` for
 *     connection generation `tag` (kept in Tag[], for the caller to
 *     compare), whatever it held (open, closed or neither): the next try
 *     due at once.
 *   - XhciTbtLearnDue: CLOSED for a closed window; WAIT for a port tried
 *     in the current pass; TRY for a port with no window (the caller
 *     opens one first); for an open one OVER once XHCI_TBT_LEARN_WINDOW_MS
 *     have passed since it opened, else TRY when the next try is due,
 *     else WAIT.
 *   - XhciTbtLearnPass starts a pass: no port counts as tried in it.
 *   - XhciTbtLearnMissed after a try that left the port pending marks
 *     it tried in the current pass (any window or none), then: while
 *     less than XHCI_TBT_LEARN_EAGER_MS have passed since the window
 *     opened, the next try is due at once (the next pass); after that the
 *     first gap is `periodMs` (at least 1), each later one twice the one
 *     before, every gap held to XHCI_TBT_LEARN_GAP_MAX_MS. Nothing for a
 *     window not open.
 *   - XhciTbtLearnClose closes the window (OVER seen): no more tries
 *     until XhciTbtLearnOpen.
 *   - XhciTbtLearnEnd forgets the window, open or closed (the port is not
 *     pending any more, or the window was another generation's).
 *   - XhciTbtLearnBound consumes port `port`'s pending mark when the port
 *     has a binding now, as XhciTbtLearn's KNOWN outcome does, reading
 *     nothing; 1 then, else 0 (a closed window stops the reads, never
 *     this bookkeeping).
 * A port of 0 or above XHCI_TBT_MAX_PORTS, or a NULL clock, is ignored
 * (Due: CLOSED).
 */
#define XHCI_TBT_LEARN_EAGER_MS     1000UL  /* PCIe Base 6.6.1's 1.0 s      */
#define XHCI_TBT_LEARN_GAP_MAX_MS   5000UL
#define XHCI_TBT_LEARN_WINDOW_MS    60000UL /* [linux] 60 s after a reset   */

#define XHCI_TBT_LEARN_WAIT         0UL
#define XHCI_TBT_LEARN_TRY          1UL
#define XHCI_TBT_LEARN_OVER         2UL
#define XHCI_TBT_LEARN_CLOSED       3UL

typedef struct _XHCI_TBT_LEARN_CLOCK {
    ULONG Open;         /* bit n-1: port n's window is open                */
    ULONG Closed;       /* bit n-1: port n's window closed, no more tries  */
    ULONG Tried;        /* bit n-1: port n tried in the current pass       */
    ULONG Since[XHCI_TBT_MAX_PORTS];    /* when the window opened          */
    ULONG Next[XHCI_TBT_MAX_PORTS];     /* when the next try is due        */
    ULONG Gap[XHCI_TBT_MAX_PORTS];      /* the last gap, 0 while eager     */
    ULONG Tag[XHCI_TBT_MAX_PORTS];      /* the generation it was opened for */
} XHCI_TBT_LEARN_CLOCK, *PXHCI_TBT_LEARN_CLOCK;

VOID XhciTbtLearnOpen(PXHCI_TBT_LEARN_CLOCK clk, ULONG port, ULONG nowMs,
                      ULONG tag);
ULONG XhciTbtLearnDue(const XHCI_TBT_LEARN_CLOCK *clk, ULONG port,
                      ULONG nowMs);
VOID XhciTbtLearnMissed(PXHCI_TBT_LEARN_CLOCK clk, ULONG port, ULONG nowMs,
                        ULONG periodMs);
VOID XhciTbtLearnClose(PXHCI_TBT_LEARN_CLOCK clk, ULONG port);
VOID XhciTbtLearnPass(PXHCI_TBT_LEARN_CLOCK clk);
VOID XhciTbtLearnEnd(PXHCI_TBT_LEARN_CLOCK clk, ULONG port);
ULONG XhciTbtLearnBound(PXHCI_TBT_BINDS binds, ULONG port);

/* ---- a tunnel found up at start: its port inferred (record 22 7.6.2) -- */

/*
 * XHCI_TBT_INFER_IN.RouterLink[]: the host router's PCIe Down adapter of
 * each host USB4 port as read once (ADP_PCIE_CS_0, the caller's read):
 * UP with Path Enable set and the LTSSM in L0, L1, Recovery or
 * Recovery.Idle; DOWN with Path Enable clear or the LTSSM in Detect; NONE
 * for a port with no PCIe Down adapter; UNKNOWN for a failed read or any
 * other LTSSM value (a transitional one included).
 */
#define XHCI_TBT_RLINK_NONE         0UL
#define XHCI_TBT_RLINK_UP           1UL
#define XHCI_TBT_RLINK_DOWN         2UL
#define XHCI_TBT_RLINK_UNKNOWN      3UL

typedef struct _XHCI_TBT_INFER_IN {
    ULONG Usb4Host;     /* the host router is USB4, not Thunderbolt 3      */
    ULONG Ports;        /* host USB4 ports, 1 to XHCI_TBT_MAX_PORTS        */
    ULONG UpMask;       /* bit n-1: port n's PCIe tunnel is up             */
    ULONG Conflict;     /* bit n-1: port n conflicted for the inference    */
    ULONG RouterLink[XHCI_TBT_MAX_PORTS];   /* XHCI_TBT_RLINK_*, by port-1 */
    const ULONG *Active;    /* per member as XhciTbtExclusive gives it,
                               NULL for all active                         */
} XHCI_TBT_INFER_IN, *PXHCI_TBT_INFER_IN;

typedef struct _XHCI_TBT_INFER_OUT {
    ULONG Port;         /* 1-based, the port inferred                      */
    ULONG Member;       /* its index in topo->Set                          */
    ULONG Bdf;          /* the member's location                           */
    ULONG Buses;        /* the member's bus number register as read        */
} XHCI_TBT_INFER_OUT, *PXHCI_TBT_INFER_OUT;

/* XhciTbtInfer's return: MADE, or the first condition that failed. */
#define XHCI_TBT_INFER_MADE         0UL
#define XHCI_TBT_INFER_BAD_PARAM    1UL
#define XHCI_TBT_INFER_HOST         2UL /* I2: a Thunderbolt 3 host        */
#define XHCI_TBT_INFER_SET          3UL /* I3: not a discovered set of the
                                           switch's own ports alone        */
#define XHCI_TBT_INFER_INACTIVE     4UL /* I3: a member another domain
                                           claims                          */
#define XHCI_TBT_INFER_BOUND        5UL /* I4: a binding, of any kind, or
                                           an overflow in the table        */
#define XHCI_TBT_INFER_CONFLICTED   6UL /* I4: the port conflicted for the
                                           inference                       */
#define XHCI_TBT_INFER_NOT_ONE      7UL /* I5: not exactly one PCIe tunnel
                                           up                              */
#define XHCI_TBT_INFER_NOT_AT_START 8UL /* I5: that port no at-start port  */
#define XHCI_TBT_INFER_PENDING      9UL /* I5: that port not pending, or
                                           another port pending too        */
#define XHCI_TBT_INFER_ROUTER       10UL /* I6: the router's links do not
                                            read that port alone linked    */
#define XHCI_TBT_INFER_NOT_ARMED    11UL /* I7: no learning baseline over
                                            this set                       */
#define XHCI_TBT_INFER_UNREAD       12UL /* I7: a register unreadable      */
#define XHCI_TBT_INFER_NO_REPORT    13UL /* I7: a member that does not
                                            report Link Active             */
#define XHCI_TBT_INFER_MOVED        14UL /* I7: bus numbers changed, or not
                                            the baseline's                 */
#define XHCI_TBT_INFER_OCCUPANCY    15UL /* I7: not exactly one member with
                                            a function behind it           */
#define XHCI_TBT_INFER_LINKS        16UL /* I7: Link Active disagrees with
                                            the occupancy                  */
#define XHCI_TBT_INFER_SHAPE        17UL /* I7: what answers behind the
                                            occupied member is not device 0
                                            alone, an Upstream Switch Port */

/*
 * Record 22 section 7.6.2's conditions I2 to I7 over what the caller read
 * (`in`: the router's side) and what this reads (the set, in one pass:
 * every member's bus numbers, PCI Express capability, Link Capabilities
 * and Link Status, the function-0 probe of every device number on its
 * secondary bus and, where one answers, device 0's header type and PCI
 * Express port type, then every member's bus numbers again). MADE with *out
 * filled, or the first condition that failed (*out zeroed). It reads and
 * changes nothing: the binding it describes is the caller's to keep,
 * apart from `binds`, of which it reads the table, the pending and
 * at-start marks and the baseline. The caller runs it again at the eject
 * and compares.
 */
ULONG XhciTbtInfer(PXHCI_TBT_CFG io, const XHCI_TBT_TOPO *topo,
                   const XHCI_TBT_BINDS *binds, const XHCI_TBT_INFER_IN *in,
                   PXHCI_TBT_INFER_OUT out);

/*
 * The read-back after an inferred teardown (record 22 section 7.6.2):
 * CLOSED when the member at `bdf` reads its bus number register equal to
 * `buses`, Link Active reported and clear, and no function 0 answering on
 * its secondary bus; OPEN when it is readable and Link Active is set or a
 * function answers; UNREAD otherwise (a failed access, an all-ones
 * register, moved bus numbers, Link Active not reported).
 */
#define XHCI_TBT_RB_CLOSED          1UL
#define XHCI_TBT_RB_OPEN            2UL
#define XHCI_TBT_RB_UNREAD          3UL
ULONG XhciTbtInferReadBack(PXHCI_TBT_CFG io, ULONG bdf, ULONG buses);

/*
 * Whether the function behind the member at `bdf` answers (record 22
 * section 11.1.14, task 49.11): the bridge's bus number register read,
 * its secondary bus valid only when nonzero, not 0xFF, above the primary
 * and not above the subordinate; then the id at function 0 of each device
 * number of that bus (a PCI Express link has device 0 alone; a PCI
 * bridge's slot puts its card at the slot's device number). PRESENT at
 * the first vendor id other than 0000h, FFFFh and 0001h; else NOT_READY
 * where one read 0001h (a Configuration Request Retry Status as software
 * sees it); else ABSENT (every one 0000h or FFFFh); UNREAD for bus numbers
 * not valid or a failed read. Reads only.
 */
#define XHCI_TBT_FN_PRESENT         1UL
#define XHCI_TBT_FN_ABSENT          2UL
#define XHCI_TBT_FN_NOT_READY       3UL
#define XHCI_TBT_FN_UNREAD          4UL
ULONG XhciTbtFunctionRead(PXHCI_TBT_CFG io, ULONG bdf);

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
    ULONG ShpcSeen;     /* every slot was read, whether or not the DWORD
                           Select was then put back (ShpcValid needs both) */
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
 * 1 when XhciTbtPortRead of the bridge at `bdf` is DOWN: it reports at
 * least one of Link Active, Presence Detect State and the SHPC slots (the
 * last only through an accessor that can write DWORD Select), and each
 * one it reports says absent. Else 0, a failed read included.
 */
ULONG XhciTbtPortDown(PXHCI_TBT_CFG io, ULONG bdf);

/*
 * Whether a function newly answering behind the bridge at `bdf` (its
 * secondary bus `bus`, `seen` the devices whose function 0 answers there,
 * bit d for device d) may be taken for a tunnel's (record 22 section
 * 7.6.4): YES when it is a tunnel's far end - device 0 alone, a
 * PCI-to-PCI bridge whose PCI Express capability says Upstream Switch
 * Port - or, for any other shape, when `wasDown` (the bridge's port read
 * DOWN at an earlier reading where nothing answered behind it, and
 * nothing has been read answering there since) and its port reads UP now;
 * UNREAD when the shape, or that port reading, could not be read (a
 * failed access, all-ones, the bridge gone) and no signal the reading
 * did read whole before failing says absent; else NO. A host controller's
 * own integrated function behind a port of its switch (its xHCI, its NHI)
 * is an endpoint whose port does not read DOWN while it is there, so it
 * is not YES. Reads only, but the SHPC reading's DWORD Select write where
 * the accessor has one.
 */
#define XHCI_TBT_GAIN_NO            0UL
#define XHCI_TBT_GAIN_YES           1UL
#define XHCI_TBT_GAIN_UNREAD        2UL
ULONG XhciTbtGainAdmits(PXHCI_TBT_CFG io, ULONG bdf, ULONG bus, ULONG seen,
                        ULONG wasDown);

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

/* ---- chain ports (design record 34 sections 3.1 and 3.9, task 45.13) --- */

/*
 * A chain port is a PCI-to-PCI bridge below a tunnel-port member that a
 * chained device's tunnel lands on: a switch Downstream Port (Device/Port
 * Type 6) with Slot Implemented and Slot Capabilities Hot-Plug Capable,
 * inside the member's secondary to subordinate range. The walk records at
 * most XHCI_TBT_CHAIN_MAX of them per member and never descends below one
 * (one level of chain ports, record 34 section 11). A bridge level is one
 * bridge between the member and a function: the first device's upstream
 * port, on the member's secondary bus, is at depth 1, its downstream ports
 * at depth 2; the walk goes no deeper than XHCI_TBT_CHAIN_LEVELS.
 */
#define XHCI_TBT_CHAIN_MAX          4UL
#define XHCI_TBT_CHAIN_LEVELS       6UL     /* XHCI_TBT_BRNUM_MAX_DEPTH     */
#define XHCI_TBT_CHAIN_STACK        64UL    /* buses waiting to be walked    */

/* Slot Capabilities Power Controller Present (bit 1); Slot Control, the
 * low half of the dword at XHCI_TBT_PCIE_SLOTSTA, Power Controller Control
 * (bit 10, 1 = off). PCI Express Base 4.0, 7.5.3.9 and 7.5.3.10. */
#define XHCI_TBT_SLOTCAP_PCP        0x00000002UL
#define XHCI_TBT_SLOTCTL_PCC_OFF    0x0400UL

/* XhciTbtChainPorts' outcome. */
#define XHCI_TBT_CHAIN_COMPLETE     0UL
#define XHCI_TBT_CHAIN_INCOMPLETE   1UL
#define XHCI_TBT_CHAIN_BAD_PARAM    2UL

/* Why a walk is incomplete (XHCI_TBT_CHAIN_SET.Why). */
#define XHCI_TBT_CHAIN_WHY_NONE     0UL
#define XHCI_TBT_CHAIN_WHY_MEMBER   1UL     /* the member's header unread,
                                               all ones or unnumbered       */
#define XHCI_TBT_CHAIN_WHY_READ     2UL     /* an access failed             */
#define XHCI_TBT_CHAIN_WHY_RETRY    3UL     /* a vendor read 0001h          */
#define XHCI_TBT_CHAIN_WHY_CAP      4UL     /* a capability walk failed or
                                               found the function gone      */
#define XHCI_TBT_CHAIN_WHY_STACK    5UL     /* more buses than the walk holds */

typedef struct _XHCI_TBT_CHAIN_PORT {
    ULONG Bdf;          /* its location                                     */
    ULONG Id;           /* its identity dword (vendor 15:0, device 31:16)   */
    ULONG Secondary;    /* its secondary bus at the walk                    */
    ULONG Subordinate;  /* its subordinate bus at the walk                  */
    ULONG Member;       /* the member's index the caller passed             */
    ULONG Depth;        /* its bridge level below the member                */
    ULONG Unpowered;    /* 1: a Power Controller reading power off          */
    ULONG Rot;          /* record 34 section 3.3.7 item 2: the device numbers
                           of the bridges from the member's secondary bus
                           down to and including it, summed mod 4 (the
                           rotation of an interrupt pin read behind it)     */
} XHCI_TBT_CHAIN_PORT, *PXHCI_TBT_CHAIN_PORT;

typedef struct _XHCI_TBT_CHAIN_SET {
    XHCI_TBT_CHAIN_PORT Port[XHCI_TBT_CHAIN_MAX];
    ULONG Count;        /* candidates recorded                              */
    ULONG Over;         /* candidates found past XHCI_TBT_CHAIN_MAX         */
    ULONG Incomplete;   /* 1 when the walk is incomplete                    */
    ULONG Why;          /* XHCI_TBT_CHAIN_WHY_*                             */
    ULONG FailBdf;      /* where it became incomplete, or XHCI_TBT_NO_BDF   */
} XHCI_TBT_CHAIN_SET, *PXHCI_TBT_CHAIN_SET;

/*
 * Walks the member at `memberBdf` (its index `member` is copied into each
 * candidate) and fills *out (emptied first). Returns
 * XHCI_TBT_CHAIN_COMPLETE, XHCI_TBT_CHAIN_INCOMPLETE (out->Why; the
 * candidates recorded before are kept but must not be acted on) or
 * XHCI_TBT_CHAIN_BAD_PARAM. Reads only.
 */
ULONG XhciTbtChainPorts(PXHCI_TBT_CFG io, ULONG memberBdf, ULONG member,
                        PXHCI_TBT_CHAIN_SET out);

/*
 * An owed chain action (record 34 sections 3.1 and 3.9): keyed by the
 * connection manager's own identity of the chained device (the firmware
 * connection manager's table entry; the software one's port), stamped with
 * the tunnel's serial and the time it was first owed, with the candidates
 * already invalidated for it. The caller keeps the action itself beside it.
 */
#define XHCI_TBT_CHAIN_OWED_MS      10000UL

/* What the last try lacked (XHCI_TBT_CHAIN_OWED.Missing). */
#define XHCI_TBT_CHAIN_MISS_NONE    0UL     /* no candidate under any member */
#define XHCI_TBT_CHAIN_MISS_WALK    1UL     /* a walk was incomplete         */
#define XHCI_TBT_CHAIN_MISS_FILTER  2UL     /* a candidate with no child
                                               filter yet                    */

typedef struct _XHCI_TBT_CHAIN_OWED {
    ULONG Used;
    ULONG Key;          /* the chained device's identity                    */
    ULONG Serial;       /* its tunnel's                                     */
    ULONG Since;        /* the caller's milliseconds when first owed        */
    ULONG Missing;      /* XHCI_TBT_CHAIN_MISS_*                            */
    ULONG DoneCount;    /* candidates invalidated for it so far ...         */
    ULONG Done[XHCI_TBT_CHAIN_MAX * XHCI_TBT_MAX_TUNNELS];  /* ... where    */
} XHCI_TBT_CHAIN_OWED, *PXHCI_TBT_CHAIN_OWED;

/*
 * Keeps an action owed in owed[0 .. count - 1]: the entry of the same key
 * is kept when its serial is the same (Missing updated; Since and the done
 * candidates kept) and restarted when it differs (a new tunnel: Since now,
 * nothing done); else the first free entry is taken. Returns the entry's
 * index, or count when every entry holds another key (nothing kept).
 */
ULONG XhciTbtChainOwe(PXHCI_TBT_CHAIN_OWED owed, ULONG count, ULONG key,
                      ULONG serial, ULONG missing, ULONG nowMs);

/* 1 when the candidate at `bdf` was already invalidated for the entry. */
ULONG XhciTbtChainDone(const XHCI_TBT_CHAIN_OWED *e, ULONG bdf);

/* Records the candidate at `bdf` invalidated for the entry (once). */
VOID XhciTbtChainMark(PXHCI_TBT_CHAIN_OWED e, ULONG bdf);

/* 1 when the entry has been owed for XHCI_TBT_CHAIN_OWED_MS or more. */
ULONG XhciTbtChainOwedExpired(const XHCI_TBT_CHAIN_OWED *e, ULONG nowMs);

/*
 * The chained unit's parent (record 34 section 3.10): what the function at
 * `bdf` is below the member at `memberBdf` now, by a stricter test than
 * the walk's candidate (which records an unnumbered candidate for its
 * invalidation). A bridge is NUMBERED when it is a present type 1 function
 * on a bus inside the member's secondary to subordinate range, its own
 * secondary bus above its bus and above the member's secondary bus, its
 * subordinate at or above its secondary and at or below the member's
 * subordinate. Outcomes: YES, a numbered bridge that is a chain-port
 * candidate (a PCI Express capability of Device/Port Type downstream port,
 * Slot Implemented and Hot-Plug Capable), with *secondary its secondary
 * bus; NOT, a numbered bridge that is no candidate; UNNUMBERED, a type 1
 * function inside the range whose buses fail the rule, candidate or not;
 * OUTSIDE, a function on a bus outside the range, or no type 1 function;
 * MEMBER, the member's own header unread, all ones or unnumbered; FAILED,
 * an access failed, or the function, or a dword of its capability, reads
 * all ones, or Retry Status, or its capability walk failed. *secondary is
 * 0 but for YES. Its own parent is admitted only on YES; a bridge between
 * it and the member only on NOT. Reads only.
 */
#define XHCI_TBT_CPT_YES            0UL
#define XHCI_TBT_CPT_NOT            1UL
#define XHCI_TBT_CPT_UNNUMBERED     2UL
#define XHCI_TBT_CPT_MEMBER         3UL
#define XHCI_TBT_CPT_FAILED         4UL
#define XHCI_TBT_CPT_OUTSIDE        5UL
ULONG XhciTbtChainPortTest(PXHCI_TBT_CFG io, ULONG bdf, ULONG memberBdf,
                           PULONG secondary);

#endif /* TBT_TOPO_H */
