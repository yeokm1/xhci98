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
 * USB-only Alpine Ridge NHIs (15DC 15DD 15DE), Light Ridge, Cactus Ridge,
 * Win Ridge and Redwood Ridge; Apple machines are excluded at run time by
 * a platform check, not here.
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
#define CM_PROFILE_ICM_TR   3  /* Titan Ridge ICM; Maple Ridge, TR messages */
#define CM_PROFILE_ICM_ICL  4  /* integrated ICM (Ice Lake to Meteor Lake),
                                  the USB4 software CM when no firmware CM
                                  runs                                      */
#define CM_PROFILE_USB4     5  /* the USB4 software CM (Phase 40): an NHI of
                                  class 0C0340 not listed above             */

#define CM_CLASS_USB4_NHI   0x0C0340UL

/* The profile of a classified NHI. */
ULONG CmProfileFor(const CM_IDS *ids);

/*
 * The INERT start (coverage plan of 2026-10-15, addendum 2): the devnode is
 * started and the reason logged, and the driver writes nothing to the
 * hardware - no reset, no ring, no approve, no teardown, no mailbox. Never
 * a failed devnode. CmStartDecision gives the reason the ids and the mode
 * already decide, before any register is read; task 39-A.4 adds the
 * readiness, ownership and protocol reasons found after start.
 */
#define CM_INERT_NONE            0  /* a connection manager runs           */
#define CM_INERT_MODE_OFF        1  /* XhciCmMode 0                        */
#define CM_INERT_PROFILE_UNKNOWN 2  /* no profile for the ids (or unread)  */
#define CM_INERT_NOT_BUILT       3  /* the profile's client is not built   */
#define CM_INERT_FORCE_REFUSED   4  /* XhciCmMode 3 on a Thunderbolt 3 host */

/*
 * The profiles whose client this build carries, one bit per CM_PROFILE_*.
 * None in the scaffold of task 39-A.1: every start is inert. Task 39-A.4
 * sets the Alpine Ridge bit; Phase 40 the USB4 one.
 */
#define CM_PROFILES_BUILT 0UL

/*
 * The inert reason for a start in `mode` (XhciCmMode as taken) on an NHI
 * of `profile`, against `built` (CM_PROFILES_BUILT, a parameter so the host
 * suite can drive every branch). XhciCmMode 2 takes an Intel NHI the table
 * does not list as Alpine Ridge (record 21 section 8): `intel` says
 * whether the vendor is Intel's. XhciCmMode 3 is the one forced route to
 * the USB4 software CM (orchestrator's decision of 2026-10-15, owner to
 * confirm): refused on the Falcon, Alpine and Titan Ridge hosts, whose
 * routers are not USB4 routers, and on ids unread; and it bypasses none of
 * the after-start tests of task 39-A.4 (failed platform init, unreadable
 * registers, a running firmware CM where the register says so, an
 * unsupported protocol, tunnel preservation). The Intel firmware status is
 * never read on a non-Intel host.
 */
ULONG CmStartDecision(ULONG mode, ULONG profile, ULONG intel, ULONG built);

/*
 * The values, by index (record 21 section 8). Each is a REG_DWORD in the
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
#define CM_VAL_LOG_VERBOSITY      7  /* XhciLogVerbosity, the shared ladder */
#define CM_VAL_COUNT              8

/* XhciCmMode (record 21 section 8, decision D11). */
#define CM_MODE_OFF       0  /* bind, log, touch nothing                    */
#define CM_MODE_AUTO      1  /* the default                                 */
#define CM_MODE_FORCE_ICM 2  /* the ICM client on an unlisted Intel id      */
#define CM_MODE_FORCE_SW  3  /* force the USB4 software CM (Phase 40)       */
#define CM_MODE_MAX       3

/*
 * XhciCmTunnelBridge, REG_SZ, a comma-separated list of "bb:dd.f" bridges
 * that replaces record 21 section 6.3's discovery as the domain's whole
 * tunnel-port set. No INF writes it: its absence is meaningful (record 21
 * section 8), so absent and empty both mean no override. Held here
 * unparsed; task 39-A.5 parses it. A string of this many characters or
 * more (seven bridges and their separators fit) is refused, not
 * truncated.
 */
#define CM_TUNNEL_BRIDGE_CHARS 64

typedef struct _CM_VALUES {
    ULONG Value[CM_VAL_COUNT];  /* what the driver runs with                */
    ULONG Raw[CM_VAL_COUNT];    /* what the registry gave, when Found       */
    ULONG Found;                /* bit n: value n was read as a REG_DWORD   */
    ULONG Refused;              /* bit n: out of range, the default stood   */
    ULONG Clamped;              /* bit n: held to its bounds                */
    WCHAR TunnelBridge[CM_TUNNEL_BRIDGE_CHARS];  /* NUL-terminated          */
    ULONG TunnelBridgeChars;    /* 0: no override                           */
    ULONG TunnelBridgeRefused;  /* present and too long                     */
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

/* The default and bounds of value `index`, for the INF gate's cross-check
 * and the host suite. Returns 0 for an index out of range. */
ULONG CmValueDefault(ULONG index);

#endif /* CM_VALS_H */
