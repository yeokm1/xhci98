/*
 * cm_vals.h - the pure half of xhcicm.sys's start: which of the driver's
 * two PnP roles a device is (design record 21 section 3.2) and the
 * registry values it reads once at start, with their defaults and the rule
 * each is held to (record 21 section 8). DDK-free and host-tested
 * (test\test_cm.c); cm_reg.c and cm_entry.c are the DDK halves.
 *
 * IRQL: any. Takes no lock; every function works on the caller's memory.
 */

#ifndef CM_VALS_H
#define CM_VALS_H

#include "../xhci_compat.h"

/*
 * The two roles. The NHI function driver is installed by xhcicm.inf on
 * every target; the tunnel bridge's upper filter by xhcicmf.inf on the NT
 * targets alone. They are told apart by the PDO's hardware ids: a PCI
 * class code of 06/04 (a PCI-to-PCI bridge, "&CC_0604" in one of the ids
 * pci.sys gives every bridge) is the filter's device, anything else the
 * NHI's. The class rather than the device id is what decides, so a new
 * bridge id in the filter INF (task 39-0.4 settles which) needs no code.
 */
#define CM_ROLE_NONE    0   /* no id list to decide by: take no role */
#define CM_ROLE_NHI     1
#define CM_ROLE_FILTER  2
/* Since task 42-A.5 (record 21 section 7.4, revision 3) AddDevice decides
 * by CmRoleDecide, not by the class alone: the LowerFilters child is a
 * third role, and CmClassifyIds' Role is only what the class says. */
#define CM_ROLE_CHILD   3

/*
 * What a PDO's hardware ids say. Vendor and Device are the 16-bit PCI ids
 * from the first id's "VEN_xxxx" and "DEV_xxxx" (0 when absent); ClassCode
 * the 24-bit class, subclass and interface from the first "CC_xxxxxx" of
 * six digits in any id (0 when none carries six).
 */
typedef struct _CM_IDS {
    ULONG Role;       /* CM_ROLE_*                                         */
    ULONG Vendor;
    ULONG Device;
    ULONG ClassCode;
} CM_IDS, *PCM_IDS;

/*
 * Classify a REG_MULTI_SZ id list of `chars` WCHARs (IoGetDeviceProperty's
 * DevicePropertyHardwareID) into *out, and return its Role.
 */
ULONG CmClassifyIds(const WCHAR *ids, ULONG chars, PCM_IDS out);

/*
 * AddDevice's role (record 21 section 7.4, revision 3, decision M15), from
 * the classified ids and whether their vendor and device are one of
 * xhcicmf.inf's bridge models (`filterListed`, cm_ports.c's
 * CmPortsFilterId): CM_ROLE_NHI for an id of the NHI table or the USB4 host
 * class xhcicm.inf also binds (CmProfileFor not UNKNOWN: xhcicm.inf's
 * matching contract and nothing wider, so a device carrying a stale mark is
 * never driven as an NHI; an unlisted Intel NHI bound by hand for
 * XhciCmMode 2 is a passive child since 42-A.5); CM_ROLE_FILTER
 * for a bridge (class 0604) whose id the filter INF names; CM_ROLE_CHILD
 * for every other PDO, an unreadable id list included - which is a device
 * this driver was named a lower filter of, and whose AddDevice must never
 * fail for want of a role (decision M7).
 */
ULONG CmRoleDecide(const CM_IDS *ids, ULONG filterListed);

/*
 * The role by position (design record 33 section 4.4, task 45.12), after
 * CmRoleDecide, whose NHI answer stands whatever the stack holds: a
 * CM_ROLE_FILTER answer (a bridge the filter INF lists) becomes the child
 * when `pciAbove` is 0 - AddDevice found no device of the PCI driver above
 * the PDO, so this call is the device's LowerFilters registration, made
 * before the PCI driver's FDO - and stays the bridge filter when the PCI
 * driver's FDO is above it (the filter INF's UpperFilters). Every other
 * role is returned as it came, and every role when `byPlace` is 0: the
 * placement is Windows 2000 to XP x64's alone (record 33 sections 4.4 and
 * 7.8: NT 6.x untouched, the lower and the upper call one bridge filter,
 * decision M15).
 */
ULONG CmRolePlace(ULONG role, ULONG pciAbove, ULONG byPlace);

/*
 * The NHI profiles (record 21 section 1.4, and the coverage plan of the
 * owner's 2026-10-15 decisions; orchestrator's table, owner to confirm):
 * which connection manager a matched NHI gets and, for the firmware one
 * (the ICM), which generation's message set. Chosen at AddDevice from the
 * ids; task 39-A.4 confirms it after start from the firmware status, and a
 * profile whose client is not implemented yet starts, logs and does
 * nothing - it never fails the devnode.
 *
 * Intel's ids are Linux drivers/thunderbolt/nhi.h's
 * PCI_DEVICE_ID_INTEL_*_NHI* (mirror pinned at c6859eed), AMD's FreeBSD
 * sys/dev/thunderbolt/nhi_reg.h's DEVICE_PINK_SARDINE_* (pinned at
 * 9d50245a); legal-provenance.md has the rows. Only 15BF has been read by
 * this project [obs]; every other row is untested. Not claimed: the
 * USB-only Alpine Ridge NHIs (15DC 15DD 15DE), Cactus Ridge 2C, Win Ridge
 * and Redwood Ridge. Falcon, Alpine and Titan Ridge on an Apple machine,
 * and Light Ridge and Cactus Ridge 4C, run the software CM's
 * Thunderbolt-host profile (design record 22 section 19), which a run-time
 * check of the subsystem and the firmware status selects; the INFs bind
 * the last two by their class form on NT 6.x alone (section 19.9).
 *
 * A profile is a hardware candidate only. Whether the platform is ready,
 * who owns the connection manager and whether the router protocol is
 * supported are separate tests after start (task 39-A.4); any of them
 * failing, or a profile with no client yet, gives the INERT start of
 * cm_nhi.c (CM_INERT_*): started, logged, no hardware write.
 */
#define CM_PROFILE_UNKNOWN  0  /* ids unread (Windows 98) or not listed     */
#define CM_PROFILE_ICM_FR   1  /* Falcon Ridge (Thunderbolt 2) ICM          */
#define CM_PROFILE_ICM_AR   2  /* Alpine Ridge ICM: the captured one        */
#define CM_PROFILE_ICM_TR   3  /* Titan Ridge ICM                           */
#define CM_PROFILE_ICM_ICL  4  /* Ice Lake's integrated ICM, ICM only       */
#define CM_PROFILE_USB4     5  /* the USB4 software CM alone (Phase 40):
                                  Lunar, Panther and Wildcat Lake, Barlow
                                  Ridge, AMD, and any other NHI of class
                                  0C0340 not listed                         */
#define CM_PROFILE_ICM_MR   6  /* Maple Ridge: the ICM with Titan Ridge's
                                  messages, or the USB4 software CM         */
#define CM_PROFILE_ICM_TGL  7  /* Tiger Lake to Meteor Lake: the ICM with
                                  Ice Lake's messages, or the USB4 software
                                  CM (design record 22 section 3.1)         */

#define CM_CLASS_USB4_NHI   0x0C0340UL

/* The profile of a classified NHI. */
ULONG CmProfileFor(const CM_IDS *ids);

/*
 * The INERT start (coverage plan of 2026-10-15, addendum 2): the devnode is
 * started and the reason logged, and the driver writes nothing to the
 * hardware - no reset, no ring, no approve, no teardown, no mailbox. Never
 * a failed devnode. CmStartDecision gives the reason the ids and the mode
 * already decide, before any register is read; the readiness, ownership
 * and protocol reasons found after start follow it (cm_nhi.c).
 */
#define CM_INERT_NONE            0  /* a connection manager runs           */
#define CM_INERT_MODE_OFF        1  /* XhciCmMode 0                        */
#define CM_INERT_PROFILE_UNKNOWN 2  /* no profile for the ids (or unread)  */
#define CM_INERT_NOT_BUILT       3  /* the profile's client is not built   */
#define CM_INERT_FORCE_REFUSED   4  /* XhciCmMode 3 on a host whose router
                                       is not a USB4 router               */
/* The reasons found after start (cm_nhi.c, task 39-A.4). */
#define CM_INERT_CONFIG          5  /* configuration space unreadable       */
#define CM_INERT_SELECTION       6  /* tbt_hw.c TbtHwSelect said INERT: its
                                       reason is the log's cm.select.reason */
#define CM_INERT_SAFE_MODE       7  /* the firmware is in safe mode         */
#define CM_INERT_SOFTWARE_CM     8  /* no longer given: the USB4 software
                                       connection manager runs where
                                       TbtHwSelect admits it (task 40-A.6);
                                       the number stays reserved            */
#define CM_INERT_RESOURCES       9  /* no common buffer below 4 GB, or no
                                       thread                               */
#define CM_INERT_RING_REFUSED   10  /* ring 0 refused before any register of
                                       it was written                       */
#define CM_INERT_POWER          11  /* the function was not in D0 when the
                                       start would have activated it        */
#define CM_INERT_BUS_MASTER     12  /* Bus Master could not be set (or its
                                       command register not read)           */
/* 14: the start's activation deferred for the ASM4242's handover on NT
 * (record 32 section 12.8.15.16): transient, the thread ends it. */
#define CM_INERT_HO_DEFER       14
#define CM_INERT_FORCE_ICM_REFUSED 13 /* XhciCmMode 2 on a host with no ICM
                                       row, or whose selection found no ICM
                                       running: never the software CM     */

/*
 * The profiles whose client this build carries, one bit per CM_PROFILE_*:
 * every ICM profile since task 39-A.4 (tbt_icm.c's Falcon, Alpine and
 * Titan Ridge and Ice Lake message tables; Maple Ridge and Tiger Lake to
 * Meteor Lake on the Titan Ridge and Ice Lake tables, where tbt_hw.c's
 * selection finds their ICM running), and since task 40-A.6 the USB4
 * software connection manager (tbt_u4cm.c, design record 22), which also
 * takes Maple Ridge and Tiger Lake to Meteor Lake where tbt_hw.c's
 * selection admits it.
 */
#define CM_PROFILES_BUILT ((1UL << CM_PROFILE_ICM_FR) |                    \
                           (1UL << CM_PROFILE_ICM_AR) |                    \
                           (1UL << CM_PROFILE_ICM_TR) |                    \
                           (1UL << CM_PROFILE_ICM_ICL) |                   \
                           (1UL << CM_PROFILE_USB4) |                      \
                           (1UL << CM_PROFILE_ICM_MR) |                    \
                           (1UL << CM_PROFILE_ICM_TGL))

/*
 * The inert reason for a start in `mode` (XhciCmMode as taken) on an NHI
 * of `profile`, against `built` (CM_PROFILES_BUILT, a parameter so the host
 * suite can drive every branch). XhciCmMode 2 takes an Intel NHI the table
 * does not list as Alpine Ridge (record 21 section 8): `intel` says
 * whether the vendor is Intel's. XhciCmMode 3 is the one forced route to
 * the USB4 software CM (orchestrator's decision of 2026-10-15, owner to
 * confirm; design record 22 section 3.6): refused on the Falcon, Alpine
 * and Titan Ridge hosts and on Ice Lake, whose host routers are not USB4
 * routers, and on ids unread; and it bypasses none of
 * the after-start tests of task 39-A.4 (failed platform init, unreadable
 * registers, a running firmware CM where the register says so, an
 * unsupported protocol, tunnel preservation). The Intel firmware status is
 * never read on a non-Intel host.
 */
ULONG CmStartDecision(ULONG mode, ULONG profile, ULONG intel, ULONG built);

/*
 * The values, by index (record 21 section 8; the last three record 22
 * section 12's). Each is a REG_DWORD in the
 * NHI's driver (software) key, written by every install path of
 * xhcicm.inf and xhcicm-amd64.inf at the default below under
 * FLG_ADDREG_NOCLOBBER, so a written default reads exactly as an absent
 * value does.
 */
#define CM_VAL_MODE               0  /* XhciCmMode, D11                     */
#define CM_VAL_APPROVE            1  /* XhciCmApprove, D4                   */
#define CM_VAL_REENUM             2  /* XhciCmReenum                        */
#define CM_VAL_POLL_IDLE_MS       3  /* XhciCmPollIdleMs, D3                */
#define CM_VAL_CMD_TIMEOUT_MS     4  /* XhciCmCmdTimeoutMs                  */
#define CM_VAL_APPROVE_TIMEOUT_MS 5  /* XhciCmApproveTimeoutMs              */
#define CM_VAL_SAFE_EJECT         6  /* XhciCmSafeEject, D10 (task 39-A.8)  */
#define CM_VAL_LOG_VERBOSITY      7  /* XhciCmLogVerbosity, the shared
                                        ladder                         */
/* The USB4 profile's (design record 22 section 12, task 40-A.6). */
#define CM_VAL_USB3_TUNNEL        8  /* XhciCmUsb3Tunnel, decision U3       */
#define CM_VAL_LANE_BONDING       9  /* XhciCmLaneBonding, decision U4      */
#define CM_VAL_PREOS_CLEANUP     10  /* XhciCmPreOsCleanup, decision U2     */
/* The log's emission (record 21 section 8, the owner's decision of
 * 2026-10-16): 1 hands the ring to DbgPrint in every flavour. */
#define CM_VAL_LOG_DEBUGVIEW     11  /* XhciCmLogDebugView                  */
/* Thunderbolt networking (design record 35 section 8.1, task 45.59):
 * XhciCmNet gates every inter-domain answer, path and the network PDO;
 * the others size and pace the service's rings (record 35 sections 5.3,
 * 5.4). Each refused out of range, the default standing. */
#define CM_VAL_NET               12  /* XhciCmNet                           */
#define CM_VAL_NET_POLL_MS       13  /* XhciCmNetPollMs                     */
#define CM_VAL_NET_RX_FRAMES     14  /* XhciCmNetRxFrames                   */
#define CM_VAL_NET_TX_FRAMES     15  /* XhciCmNetTxFrames                   */
#define CM_VAL_NET_E2E           16  /* XhciCmNetE2E                        */
#define CM_VAL_COUNT             17


/* XhciCmMode (record 21 section 8, decision D11). */
#define CM_MODE_OFF       0  /* bind, log, touch nothing                    */
#define CM_MODE_AUTO      1  /* the default                                 */
#define CM_MODE_FORCE_ICM 2  /* the ICM client on an unlisted Intel id      */
#define CM_MODE_FORCE_SW  3  /* force the USB4 software CM (record 22 3.6)  */
#define CM_MODE_MAX       3

/*
 * XhciCmTunnelBridge, REG_SZ, a comma-separated list of "bb:dd.f" bridges
 * that replaces record 21 section 6.3's discovery as the domain's whole
 * tunnel-port set. Absent, empty and "auto" (any case, alone), the form
 * the INFs write since task 45.50 (record 21 section 8.1), all mean no
 * override, the driver's own finding. Held here
 * unparsed; task 39-A.5 parses it. A string of this many characters or
 * more (seven bridges and their separators fit) is refused, not
 * truncated.
 */
#define CM_TUNNEL_BRIDGE_CHARS 64

/*
 * XhciCmBridgeNumbering, REG_DWORD, Windows 98 SE and ME only (record 21
 * section 6.1.1): 1 numbers the bridges a tunnel brings up below a tunnel
 * port before the CONFIGMG re-enumeration (tbt_brnum.c), 0 leaves them as
 * they came up (the behaviour before it: Code 29 on a hot-added bridge
 * until a restart). Ignored on the NT targets. The x86 xhcicm.inf's 98 SE
 * and ME install writes it at this default, only where it is missing (task
 * 44.46, owner 2026-10-26, replacing the owner's 2026-10-16 "no INF writes
 * it"; the INF gate reads the default from this line); absent reads as
 * the default, and the installer never asks for it (/set and the window's
 * Advanced row set it). A value other than 0 or 1 is refused and the
 * default stands.
 */
#define CM_BRIDGE_NUMBERING_DEFAULT 1UL

/*
 * XhciCmNoIrqStart, REG_DWORD, Windows 98 SE and ME only (design record 28
 * section 7, task 42-A.15): 1 starts an NVMe function below a tunnel port
 * whose interrupt cannot be routed with no interrupt, for a bound Stor98
 * that polls (record 28 section 4); 0 leaves such a function at its
 * problem code. Read where XhciCmBridgeNumbering is read and written as
 * it is (task 44.46: the x86 xhcicm.inf's 98 SE and ME install, at this
 * default, which the INF gate reads from this line); absent reads as the
 * default, any other value is refused and the default stands. Ignored on
 * the NT targets.
 */
#define CM_NOIRQ_START_DEFAULT 1UL

/*
 * XhciCmAsm4242Handover, REG_DWORD (design record 32 section 12.8, task
 * 46.12): 1 sends an ASM4242 host the platform's USB4 handover from the
 * start, before ring 0 - on 98 SE and ME when the firmware's tables and
 * code are a build read and name the host found (cm_oscos.c), on the NT
 * targets when the board is a known board's profile (cm_oscnt.c, section
 * 12.8.15.11) or, with XhciCmAsm4242Reads 1, by the read path; 0 never
 * sends it, by any route. Read where XhciCmBridgeNumbering is read and
 * written as it is (each xhcicm.inf install, at this default, which the
 * INF gate reads from this line); absent reads as the default, any other
 * value is refused and the default stands.
 */
#define CM_PLAT_HANDOVER_DEFAULT 1UL

/*
 * XhciCmAsm4242Structural, REG_DWORD, every target (design record 32
 * sections 12.8.6, 12.8.10 and 12.8.15.17, task 46.12): 1 also sends the
 * handover to firmware that is not a known build but whose five methods
 * have a known build's canonical form (the same handover code); 0 (the
 * default) recognises and reports such firmware (the status record's
 * tier 3) and sends it nothing. A known build is sent the handover
 * whatever this says; XhciCmAsm4242Handover 0 overrides both. Read and
 * written as XhciCmAsm4242Handover is, at this default (the INF gate
 * reads it from this line); absent reads as the default, any other value
 * is refused and the default stands. On the NT targets it applies to the
 * read path alone (section 12.8.15.10), so it matters there only with
 * XhciCmAsm4242Reads 1.
 */
#define CM_HO_STRUCTURAL_DEFAULT 0UL

/*
 * XhciCmAsm4242Reads, REG_DWORD, the NT targets only (design record 32
 * sections 12.8.15.10 and 12.8.15.12, task 46.12): 1 lets the read path
 * (B, C or D) read the firmware's tables and data to find an ASM4242
 * board that is no known board's profile; 0 (the default) reads nothing
 * and sends nothing to such a board, and says so in the status record
 * (bit 22). A known board is sent its profile's command whatever this
 * says. Read and written as XhciCmAsm4242Handover is, by the NT installs
 * alone; absent reads as the default, any other value is refused and the
 * default stands. 98 SE and ME neither read nor write it.
 */
#define CM_HO_READS_DEFAULT 0UL

/*
 * XhciCmAsmediaPcie7Credits, REG_DWORD (design record 22 section 18.8,
 * task 44.41): a bench experiment. 1 writes 7 credits, IFC 1 and ISE 0
 * into the two protocol-adapter HopID 8 entries of a PCIe tunnel this
 * driver builds between an ASM4242 host router and an ASM2464 device
 * router (tbt_u4cm.h, TBT_U4_CONFIG AsmPcie7Credits); 0 keeps them as
 * read. The INFs write it at 0 since task 45.50 and the installer does
 * not ask for it; absent reads as 0, and any other value is refused and 0
 * stands. Read once per
 * start, machine-wide key first, as every value.
 */
#define CM_ASM_PCIE7_DEFAULT 0UL

/*
 * XhciCmUsb4V2, REG_DWORD (design record 22 sections 6.2 and 12, decision
 * U30, task 45.20): a bench switch. 1 runs a USB4 version 2 router this
 * driver enumerates as version 2 (CMUV 2.0) and keys section 6.2.2's
 * version 2 obligations on each router's own version and capabilities;
 * 0 is the version 1 run of 3.3.0.0 (tbt_u4cm.h, TBT_U4_CONFIG Usb4V2).
 * The INFs write it at 1 since task 45.50 and the installer does not
 * ask for it; absent reads as 1, and any other value is refused and 1
 * stands. Read once per
 * start, machine-wide key first, as every value.
 */
#define CM_USB4_V2_DEFAULT 1UL

/*
 * XhciCmGen4LinkRecovery, REG_DWORD (design record 22 sections 6.2.3 item
 * 4 and 12, decision U33, task 45.56): a bench switch. 1 sets Enable Gen
 * 4 Link Recovery on both ends of a depth-1 Gen 4 link between two USB4
 * version 2 routers, with the recovery check after each LINK_RECOVERY;
 * 0 never sets it, an uncorrectable error on such a link being a
 * disconnect as in task 45.20 (tbt_u4cm.h, TBT_U4_CONFIG
 * Gen4LinkRecovery). XhciCmUsb4V2 0 never sets it either. The INFs
 * write it at 1 and the installer does not ask for it; absent reads as
 * 1, and any other value is refused and 1 stands. Read once per start,
 * machine-wide key first, as every value.
 */
#define CM_GEN4_ELR_DEFAULT 1UL

/*
 * XhciCmBridgeReq, REG_DWORD, Windows 2000, XP and XP x64 only (design
 * record 33 section 9.1, task 45.12): 1 rewrites the requirement list of
 * a hot-added bridge below an active tunnel port (record 33 section 6)
 * and lets the mark service write its bridge marks (section 4.3); 0 does
 * neither, lists passing untouched and each attempt still recorded with
 * outcome "off"; 2 computes and records every attempt, replaces no list
 * and writes no bridge mark (a reading mode for the bench). The NT 5.x
 * install paths write it at 1 since task 45.50; absent reads as 1, any other value is refused and 1 stands. Read
 * once per start, machine-wide key first, as every value, and never on
 * Vista and later or on 98 SE and ME.
 */
/*
 * XhciCmRingInterrupt, REG_DWORD (design record 21 section 4.5.4, task
 * 45.22): 1 lets the NHI's line interrupt end the connection-manager
 * thread's idle wait where the start grants one and it is proven
 * (cm_rirqos.c), on 2000 to 10; 0 polls as before. 98 SE and ME poll
 * whatever it says until their route is read. The NT install paths write
 * it at 0 since task 45.50 and the installer does not ask for it (owner,
 * 2026-10-29: off by default);
 * absent reads as 0, and any other value (2 and above are reserved for the
 * message step) is refused and 0 stands. Read once per start, machine-wide
 * key first, as every value.
 */
#define CM_RING_IRQ_DEFAULT 0UL
#define CM_RING_IRQ_MAX     1UL

#define CM_BRIDGE_REQ_DEFAULT 1UL
#define CM_BRIDGE_REQ_OFF     0UL
#define CM_BRIDGE_REQ_ON      1UL
#define CM_BRIDGE_REQ_RECORD  2UL

typedef struct _CM_VALUES {
    ULONG Value[CM_VAL_COUNT];  /* what the driver runs with                */
    ULONG Raw[CM_VAL_COUNT];    /* what the registry gave, when Found       */
    ULONG Found;                /* bit n: value n was read as a REG_DWORD   */
    ULONG Refused;              /* bit n: out of range, the default stood   */
    ULONG Clamped;              /* bit n: held to its bounds                */
    WCHAR TunnelBridge[CM_TUNNEL_BRIDGE_CHARS];  /* NUL-terminated          */
    ULONG TunnelBridgeChars;    /* 0: no override                           */
    ULONG TunnelBridgeRefused;  /* present and too long                     */
    ULONG BridgeNumbering;      /* XhciCmBridgeNumbering as run with        */
    ULONG BridgeNumberingFound; /* read as a REG_DWORD                      */
    ULONG BridgeNumberingRaw;   /* ... what it read                         */
    ULONG BridgeNumberingRefused; /* ... out of range: the default stood   */
    ULONG PlatformHandover;     /* XhciCmAsm4242Handover as run with       */
    ULONG PlatformHandoverFound;
    ULONG PlatformHandoverRaw;
    ULONG PlatformHandoverRefused;
    ULONG HandoverStructural;   /* XhciCmAsm4242Structural as run with     */
    ULONG HandoverStructuralFound;
    ULONG HandoverStructuralRaw;
    ULONG HandoverStructuralRefused;
    ULONG HandoverReads;        /* XhciCmAsm4242Reads as run with (NT)     */
    ULONG HandoverReadsFound;
    ULONG HandoverReadsRaw;
    ULONG HandoverReadsRefused;
    ULONG NoIrqStart;           /* XhciCmNoIrqStart as run with             */
    ULONG NoIrqStartFound;
    ULONG NoIrqStartRaw;
    ULONG NoIrqStartRefused;
    ULONG AsmPcie7;             /* XhciCmAsmediaPcie7Credits as run with    */
    ULONG AsmPcie7Found;
    ULONG AsmPcie7Raw;
    ULONG AsmPcie7Refused;
    ULONG Usb4V2;               /* XhciCmUsb4V2 as run with                 */
    ULONG Usb4V2Found;
    ULONG Usb4V2Raw;
    ULONG Usb4V2Refused;
    ULONG Gen4Elr;              /* XhciCmGen4LinkRecovery as run with       */
    ULONG Gen4ElrFound;
    ULONG Gen4ElrRaw;
    ULONG Gen4ElrRefused;
    ULONG BridgeReq;            /* XhciCmBridgeReq as run with              */
    ULONG BridgeReqFound;
    ULONG BridgeReqRaw;
    ULONG BridgeReqRefused;
    ULONG RingIrq;              /* XhciCmRingInterrupt as run with          */
    ULONG RingIrqFound;
    ULONG RingIrqRaw;
    ULONG RingIrqRefused;
    /* XhciCmQemuIrqDead, read by the qemu flavour alone (never published):
     * after this many claims the ring interrupt's service routine goes on
     * acknowledging and stops counting and queueing - a line that died,
     * for the lost-interrupt leg of task 45.22. 0: never. */
    ULONG QemuIrqDead;
} CM_VALUES, *PCM_VALUES;

/* Every value at its default, nothing found. */
VOID CmValuesDefault(PCM_VALUES v);

/*
 * Take one DWORD the registry gave (found nonzero) or did not (found 0),
 * under the value's rule: a switch out of its range is refused and the
 * default stands; a period or timeout is held to its bounds. XhciLogVerbosity
 * is taken as read: xhci_log.c's XhciLogApplySwitches refuses its own.
 */
VOID CmValuesTake(PCM_VALUES v, ULONG index, ULONG found, ULONG raw);


/*
 * Take XhciCmTunnelBridge: `chars` WCHARs, not counting any terminator. An
 * empty string is no override; one of CM_TUNNEL_BRIDGE_CHARS or more is
 * refused and leaves no override.
 */
VOID CmValuesTakeBridge(PCM_VALUES v, const WCHAR *text, ULONG chars);

/* Take XhciCmBridgeNumbering: found nonzero with the DWORD read, or not
 * found; 0 and 1 are taken, any other value is refused (the default
 * stands, the refusal recorded). */
VOID CmValuesTakeBridgeNumbering(PCM_VALUES v, ULONG found, ULONG raw);

/* Take XhciCmNoIrqStart, under XhciCmBridgeNumbering's rule. */
VOID CmValuesTakeNoIrqStart(PCM_VALUES v, ULONG found, ULONG raw);

/* Take XhciCmAsmediaPcie7Credits: 0 or 1, anything else refused. */
VOID CmValuesTakeAsmPcie7(PCM_VALUES v, ULONG found, ULONG raw);

/* Take XhciCmUsb4V2: 0 or 1, anything else refused (1 stands). */
VOID CmValuesTakeUsb4V2(PCM_VALUES v, ULONG found, ULONG raw);

/* Take XhciCmGen4LinkRecovery: 0 or 1, anything else refused (1 stands). */
VOID CmValuesTakeGen4Elr(PCM_VALUES v, ULONG found, ULONG raw);

/* Take XhciCmBridgeReq: 0, 1 or 2, anything else refused (1 stands). */
VOID CmValuesTakeBridgeReq(PCM_VALUES v, ULONG found, ULONG raw);

/* Take XhciCmRingInterrupt: 0 or 1, anything else refused (0 stands). */
VOID CmValuesTakeRingIrq(PCM_VALUES v, ULONG found, ULONG raw);
VOID CmValuesTakePlatformHandover(PCM_VALUES v, ULONG found, ULONG raw);
VOID CmValuesTakeHandoverStructural(PCM_VALUES v, ULONG found, ULONG raw);
VOID CmValuesTakeHandoverReads(PCM_VALUES v, ULONG found, ULONG raw);

/* The default and bounds of value `index`, for the INF gate's cross-check
 * and the host suite. Returns 0 for an index out of range. */
ULONG CmValueDefault(ULONG index);

#endif /* CM_VALS_H */
