/*
 * tbt_hw.h - which host interface this is, what its platform needs before
 * anything is read, and which connection manager owns it (roadmap-usb4.md
 * task 39-A.3; design record 21 section 1.4; the orchestrator's coverage
 * plan of 2026-10-15, owner to confirm).
 *
 * Selection is four tests, kept apart (that plan's point 1):
 *   (a) a hardware candidate, by PCI vendor, device and class, at AddDevice:
 *       TbtHwMatch, over a hardware-profile table that is separate from the
 *       ICM message table of tbt_icm.h, because hardware traits (the
 *       integrated controllers' power sequence, Intel's interrupt contract)
 *       do not follow the message families;
 *   (b) platform readiness, after start: TbtHwPlatformInit runs the
 *       generation's own PCI-configuration sequence (force power, the
 *       firmware-ready wait, LTR) through the driver's configuration
 *       accessor, before anything decides which connection manager exists;
 *   (c) connection-manager ownership, by the row's own predicate, and
 *   (d) a supported host interface version - both in TbtHwSelect.
 *
 * Outcomes: TBT_SEL_ICM (the client of tbt_icm.h); TBT_SEL_SAFE_MODE (the
 * firmware wants an NVM update; nothing is sent); TBT_SEL_SOFTWARE, the
 * positive admission of the USB4 software connection manager (Phase 40;
 * until it exists the driver treats it as inert); and TBT_SEL_INERT with a
 * reason: the devnode starts, logs the reason, and the driver touches
 * nothing - no reset, no ring, no protocol session, no mailbox. A timeout,
 * a register that does not decode, a firmware mode this client does not
 * run, an unlisted version, ICM enabled with a failed handshake - all
 * INERT; none of them is a licence to take the fabric over, and a failed
 * ICM never falls back to the software CM. Intel's firmware-status
 * register is read only on an Intel row that has an ICM.
 *
 * ICM admission (orchestrator decision A, 2026-10-15, owner to confirm):
 * the row's candidate predicate (Falcon Ridge and Ice Lake: not Apple;
 * Tiger Lake to Meteor Lake and Maple Ridge: NVM authentication done), AND
 * the firmware status's ICM enable (Linux icm.c icm_firmware_start's check;
 * a clear bit never enters its restart path here), AND where the message
 * row has the mode query (Alpine Ridge, Titan Ridge, Maple Ridge:
 * icm_ar_get_mode) connection-manager mode. Software admission (decision
 * B', 2026-10-15, owner to confirm): (1) a host with no firmware CM by
 * identity (no ICM row: Lunar Lake, Panther Lake, Wildcat Lake, Barlow
 * Ridge, AMD, any other class 0C0340 host), on the platform and register
 * checks alone, the Intel firmware status never read; (2) a Tiger Lake
 * class or Maple Ridge host whose firmware status reads validly with the
 * ICM enable clear - best effort, not gated on NVM authentication, which
 * gates only the ICM; (3) XhciCmMode 3, the one forced route, on a Tiger
 * Lake class or Maple Ridge host whose ICM enable reads clear - refused on
 * Falcon, Alpine and Titan Ridge and Ice Lake, which are not USB4 hosts,
 * and while the ICM runs. An unreadable register or a failed platform
 * init is INERT, never a disabled ICM. Pre-OS tunnels are the software
 * CM's to discover and keep, never to reset.
 *
 * Sources: Linux drivers/thunderbolt (mirror pinned at c6859eed), read
 * under design record 20's rule since the owner's decision of 2026-10-15 -
 * pci.c (nhi_ids, nhi_pci_check_quirks, icl_nhi_*), nhi.c (nhi_select_cm,
 * nhi_reset), icm.c (icm_probe and its is_supported and get_mode hooks),
 * nhi.h and nhi_regs.h - and FreeBSD sys/dev/thunderbolt/nhi_reg.h (pinned
 * at 9d50245a) for AMD's Pink Sardine ids. Cited by file and symbol; no
 * function body copied.
 *
 * IRQL: TbtHwMatch and TbtHwForcePowerValue are pure and callable at any
 * IRQL. The others run their accessors' callbacks and wait through
 * TBT_HOST.Sleep: PASSIVE_LEVEL on the connection-manager thread, no lock
 * held (record 21 section 4.4).
 *
 * DDK-free: part of the pure core. C89, no 64-bit arithmetic.
 */

#ifndef TBT_HW_H
#define TBT_HW_H

#include "../xhci_compat.h"
#include "tbt_regs.h"
#include "tbt_ring.h"
#include "tbt_icm.h"

/* ---------------------------------------------------------------------- */
/* Hardware profiles                                                      */
/* ---------------------------------------------------------------------- */

/* Hardware traits. */
#define TBT_HW_ICL_POWER        0x0001UL    /* pci.c icl_nhi_ops: force power, LTR, LC mailbox */
#define TBT_HW_INTEL_INTERRUPTS 0x0002UL    /* pci.c QUIRK_AUTO_CLEAR_INT: mask written directly */
#define TBT_HW_FR_E2E           0x0004UL    /* pci.c QUIRK_E2E: E2E rings only; ring 0 never is */
#define TBT_HW_CAPTURED         0x0008UL    /* a capture of this id exists (8086:15BF) */
#define TBT_HW_FORCED           0x0010UL    /* an unlisted Intel id taken by XhciCmMode 2 */
#define TBT_HW_CLASS_MATCH      0x0020UL    /* matched by class 0C0340 alone */

/* Ownership predicates, each one of Linux icm.c's is_supported hooks for
 * the ids icm_probe gives it. Apple machines are excluded for this
 * release by the platform policy the driver supplies, on every row. */
#define TBT_HW_OWN_NONE         0UL     /* no ICM row: the USB4 software CM's (Phase 40) */
#define TBT_HW_OWN_PRESENT      1UL     /* icm_fr_is_supported: present and not Apple */
#define TBT_HW_OWN_ICM_RUNNING  2UL     /* icm_ar_is_supported: ICM enabled (no firmware start) */
#define TBT_HW_OWN_AUTH_DONE    3UL     /* icm_tgl_is_supported: NVM auth done within 10 ms */

typedef struct _TBT_HW_PROFILE {
    ULONG VendorId;
    ULONG DeviceId;             /* 0 for the class entry */
    const char *Name;
    ULONG Flags;                /* TBT_HW_* */
    ULONG Ownership;            /* TBT_HW_OWN_* */
    const TBT_ICM_PROFILE *Icm; /* the message row, NULL for none */
} TBT_HW_PROFILE, *PTBT_HW_PROFILE;

#define TBT_HW_VENDOR_INTEL     0x8086UL
#define TBT_HW_VENDOR_AMD       0x1022UL
#define TBT_HW_CLASS_USB4       0x0C0340UL  /* Spec 12.6; nhi.h PCI_CLASS_SERIAL_USB_USB4 */

/*
 * (a) The candidate for an NHI's ids and its 24-bit class code, or NULL.
 * Listed ids first (Linux nhi.h, pci_ids.h, pci.c nhi_ids; FreeBSD
 * nhi_reg.h DEVICE_PINK_SARDINE_*); then, with `forceIntel` (XhciCmMode 2,
 * record 21 section 8), an unlisted Intel id as an Alpine Ridge ICM; then
 * any vendor's class 0C0340 as a software-CM candidate. The Thunderbolt 3
 * USB-only NHIs (15DC, 15DD, 15DE), which have no ICM row, are not
 * candidates in this release. Any IRQL.
 */
const TBT_HW_PROFILE *TbtHwMatch(ULONG vendorId,
                                 ULONG deviceId,
                                 ULONG classCode,
                                 BOOLEAN forceIntel);

/* The ring start flags this host's interrupt contract asks for. */
ULONG TbtHwRingFlags(const TBT_HW_PROFILE *hw);

/* ---------------------------------------------------------------------- */
/* (b) Platform readiness                                                 */
/* ---------------------------------------------------------------------- */

/* The NHI's own PCI configuration space, one aligned DW at a byte offset
 * (IRP_MN_READ_CONFIG / WRITE_CONFIG on NT, the PCI BIOS route on 9x). */
typedef struct _TBT_CFG {
    PVOID Context;
    ULONG (*Read32)(PVOID context, ULONG offset);
    VOID  (*Write32)(PVOID context, ULONG offset, ULONG value);
} TBT_CFG, *PTBT_CFG;

/* The VS_CAP_22 value that sets or clears force power (Linux pci.c
 * icl_nhi_force_power). Any IRQL. */
ULONG TbtHwForcePowerValue(ULONG vsCap22, BOOLEAN on);

/* What TbtHwPlatformInit changed, so that only that is undone
 * (orchestrator decision C, 2026-10-15). */
typedef struct _TBT_HW_POWER {
    BOOLEAN ForcePowerSetHere;  /* force power was clear and this driver set it */
    BOOLEAN FwReadyFailed;      /* the last firmware-ready poll never saw it */
} TBT_HW_POWER, *PTBT_HW_POWER;

/*
 * Power-up on a profile with TBT_HW_ICL_POWER (Linux pci.c icl_nhi_resume
 * as its .init): force power with the DMA delay, wait for VS_CAP_9
 * FW_READY (350 reads 3 ms apart), then LTR from VS_CAP_16 into VS_CAP_15.
 * TBT_OK (and at once on any other profile), TBT_E_TIMEOUT, or TBT_E_HW for
 * a configuration space reading all ones. Run at start and at every D0
 * (decision D: icl_nhi_ops .init is icl_nhi_resume). `power` records
 * whether force power was set here, and keeps that across calls.
 */
LONG TbtHwPlatformInit(const TBT_HW_PROFILE *hw,
                       const TBT_CFG *cfg,
                       const TBT_HOST *host,
                       PTBT_HW_POWER power);

/*
 * NOT CALLED IN THE FIRST CUT: record 21 section 1.4.3 leaves system
 * suspend with an integrated controller unsupported and sends no LC
 * mailbox handshake. Kept for Phase 40.
 *
 * System sleep on a profile with TBT_HW_ICL_POWER while its ICM owns it
 * (Linux pci.c icl_nhi_suspend_noirq, the firmware-suspend branch): the LC
 * mailbox's GO2SX, or GO2SX_NO_WAKE, as Linux pairs them with `wake`.
 * TBT_OK at once when nothing applies.
 */
LONG TbtHwPlatformSuspend(const TBT_HW_PROFILE *hw,
                          const TBT_CFG *cfg,
                          const TBT_HOST *host,
                          BOOLEAN icmOwned,
                          BOOLEAN wake);

/* Stop and shutdown on a TBT_HW_ICL_POWER profile: force power off as
 * Linux pci.c icl_nhi_shutdown does - but only when `power` says this
 * driver set it and the instance was active; an inert instance (`active`
 * FALSE) never clears it, not even after its own firmware-ready timeout
 * (record 21 sections 1.4.2 and 5.6); a force power the firmware or the
 * BIOS left on stays on. TBT_E_HW when the register reads
 * all ones: nothing restored, and the record kept for another try. */
LONG TbtHwPlatformShutdown(const TBT_HW_PROFILE *hw, const TBT_CFG *cfg,
                           PTBT_HW_POWER power, BOOLEAN active);

/* The Apple platform policy's mark (record 21 section 1.4.2 test 1): the
 * NHI's subsystem vendor 106B. Any IRQL. */
#define TBT_HW_VENDOR_APPLE     0x106BUL
BOOLEAN TbtHwIsApple(ULONG subsystemVendorId);

/* The LC mailbox (Linux pci.c icl_nhi_lc_mailbox_cmd and _complete):
 * command and VALID into VS_CAP_19, VS_CAP_18 DONE polled 1 ms apart up to
 * `timeoutMs`, then VS_CAP_19 cleared. TBT_OK or TBT_E_TIMEOUT. */
LONG TbtHwLcMailbox(const TBT_CFG *cfg, const TBT_HOST *host, ULONG cmd,
                    ULONG timeoutMs);

/* The host router's fused UUID on a TBT_HW_ICL_POWER profile, for the log:
 * VS_CAP_10, VS_CAP_11, then two DWs of ones (Linux icm.c
 * icm_icl_set_uuid, which reads; nothing is written). FALSE elsewhere. */
BOOLEAN TbtHwReadHostUuid(const TBT_HW_PROFILE *hw,
                          const TBT_CFG *cfg,
                          PULONG uuid);

/* ---------------------------------------------------------------------- */
/* (c) and (d): ownership and version                                     */
/* ---------------------------------------------------------------------- */

#define TBT_SEL_ICM             1UL
#define TBT_SEL_SAFE_MODE       2UL
#define TBT_SEL_INERT           3UL
#define TBT_SEL_SOFTWARE        4UL

#define TBT_INERT_NONE              0UL
#define TBT_INERT_PLATFORM          1UL     /* (b) failed or was not run */
#define TBT_INERT_NOT_DECODING      2UL     /* BAR0 reads all ones, or no paths */
#define TBT_INERT_FORCED_ICM_EN     3UL     /* XhciCmMode 3 with the ICM running */
#define TBT_INERT_APPLE             4UL     /* platform policy */
#define TBT_INERT_NHI_VERSION       5UL     /* host interface version not 0 */
#define TBT_INERT_ICM_NOT_RUNNING   6UL     /* firmware status: ICM enable clear */
#define TBT_INERT_NOT_AUTHENTICATED 7UL     /* NVM authentication never done */
#define TBT_INERT_MODE              8UL     /* authentication, endpoint or unknown mode */
#define TBT_INERT_FORCED_UNSUPPORTED 9UL    /* XhciCmMode 3 on a host that is not USB4 */
#define TBT_INERT_CONFLICT          10UL    /* ICM enable read set, then clear */

typedef struct _TBT_HW_SELECTION {
    ULONG Outcome;      /* TBT_SEL_* */
    ULONG Reason;       /* TBT_INERT_*, when INERT */
    ULONG Caps;         /* Host Interface Capabilities as read */
    ULONG FwStatus;     /* firmware status, 0 when not read */
    ULONG Mode;         /* operating mode, 0xFFFFFFFF when not read */
} TBT_HW_SELECTION, *PTBT_HW_SELECTION;

/*
 * Decide, after (a) and (b): `platformReady` is (b)'s result, `apple` the
 * driver's platform policy, `forceSoftware` XhciCmMode 3. Reads Host
 * Interface Capabilities, then on an Intel ICM row the firmware status by
 * the row's predicate, the interface version for the ICM (record 21
 * section 1.4.2 test 4: 0 on Alpine Ridge, below 0x40 on the other rows;
 * Linux nhi.c nhi_reset resets only from REG_CAPS_VERSION_2, and this
 * driver resets nothing), and where the message
 * row reads the mode (TBT_ICM_CAP_GET_MODE: Linux icm.c icm_ar_get_mode)
 * the NVM authentication poll (60 reads 50 ms apart) and the operating
 * mode: connection-manager mode is ICM, safe mode is SAFE_MODE, any other
 * INERT (icm_firmware_init rejects them too).
 */
VOID TbtHwSelect(const TBT_HW_PROFILE *hw,
                 const TBT_HOST *host,
                 BOOLEAN platformReady,
                 BOOLEAN apple,
                 BOOLEAN forceSoftware,
                 PTBT_HW_SELECTION out);

#endif /* TBT_HW_H */
