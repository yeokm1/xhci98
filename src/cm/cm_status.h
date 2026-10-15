/*
 * cm_status.h - the status record xhcicm.sys publishes per NHI for the
 * install tool's device viewer (design record 24 section 3): its layout,
 * shared byte for byte by the driver (cm_status.c, cm_statos.c) and by
 * XHCIINST.EXE (xhciinst\inst_core.c), so it uses no DDK or Windows type.
 *
 * Every field is a little-endian 32-bit unsigned value at a fixed offset,
 * or a byte string; the readers take bytes, never a structure overlay.
 * A layout change is a new CM_ST_VERSION: a reader refuses a version it
 * does not know.
 */

#ifndef CM_STATUS_H
#define CM_STATUS_H

#define CM_ST_MAGIC             0x534D4358UL    /* 'XCMS' */
#define CM_ST_VERSION           1UL

#define CM_ST_MAX_PORTS         4UL     /* XHCI_TBT_MAX_TUNNELS */
#define CM_ST_MAX_ROUTERS       24UL
#define CM_ST_NAME_BYTES        32UL    /* 31 printable characters, NUL */

/* The header. */
#define CM_ST_HDR_BYTES         128UL
#define CM_ST_O_MAGIC           0UL
#define CM_ST_O_VERSION         4UL
#define CM_ST_O_BYTES           8UL     /* the whole record */
#define CM_ST_O_GENERATION      12UL    /* bumped on every change, never 0 */
#define CM_ST_O_UPTIME_S        16UL    /* the system's uptime at the write, in s */
#define CM_ST_O_NHI_ID          20UL    /* vendor 15:0, device 31:16 */
#define CM_ST_O_CM              24UL    /* CM_ST_CM_* */
#define CM_ST_O_PROFILE         28UL    /* the driver's CM_PROFILE_* */
#define CM_ST_O_STAGE           32UL    /* CM_ST_STAGE_* */
#define CM_ST_O_INERT           36UL    /* the driver's CM_INERT_* */
#define CM_ST_O_SECURITY        40UL    /* ICM security level, or CM_ST_UNKNOWN */
#define CM_ST_O_FW_VERSION      44UL    /* ICM ROM or NVM version, or CM_ST_UNKNOWN */
#define CM_ST_O_PORT_COUNT      48UL    /* tunnel ports in the set, 0..4 */
#define CM_ST_O_PORT_ACTIVE     52UL    /* bit n: port n is an active member */
#define CM_ST_O_PORTS           56UL    /* CM_ST_MAX_PORTS bus/dev/fn words */
#define CM_ST_O_ROUTER_COUNT    72UL
#define CM_ST_O_ENTRY_BYTES     76UL    /* CM_ST_ENTRY_BYTES */
#define CM_ST_O_FLAGS           80UL    /* CM_ST_F_* */
#define CM_ST_O_PATHS           84UL    /* per port, CM_ST_PATH_BYTES: its
                                           Windows 98 instance path (count,
                                           then up to 7 elements, leaf
                                           first), count 0 where unknown */
#define CM_ST_PATH_BYTES        8UL
#define CM_ST_O_SYSTIME_LO      116UL   /* KeQuerySystemTime at the write, */
#define CM_ST_O_SYSTIME_HI      120UL   /* as read: 100 ns units since 1601 */
#define CM_ST_O_ECHO            124UL   /* the last XhciCmStatusAsk read, 0 none */
#define CM_ST_PATH_MAX          7UL

/* One router. */
#define CM_ST_ENTRY_BYTES       128UL
#define CM_ST_E_KIND            0UL     /* CM_ST_KIND_* */
#define CM_ST_E_PLACE           4UL     /* port or link 7:0, depth 15:8 */
#define CM_ST_E_ROUTE_LO        8UL
#define CM_ST_E_ROUTE_HI        12UL
#define CM_ST_E_APPROVAL        16UL    /* CM_ST_APPR_* */
#define CM_ST_E_PORT_STATE      20UL    /* USB4: state 7:0, why 15:8; else 0 */
#define CM_ST_E_IDS             24UL    /* see CM_ST_KIND_* */
#define CM_ST_E_GEN             28UL    /* CM_ST_GEN_* 7:0, USB4 version 15:8 */
#define CM_ST_E_LANES           32UL    /* CM_ST_LANES_* */
#define CM_ST_E_TUNNELS         36UL    /* PCIe 3:0, USB3 7:4, DP 11:8: CM_ST_TUN_* */
#define CM_ST_E_BRIDGE          40UL    /* its tunnel port's bus/dev/fn, or CM_ST_UNKNOWN */
#define CM_ST_E_SHORT_ID        44UL    /* the UUID's last 16 bits, or CM_ST_UNKNOWN */
#define CM_ST_E_VENDOR          64UL    /* CM_ST_NAME_BYTES */
#define CM_ST_E_MODEL           96UL    /* CM_ST_NAME_BYTES */

#define CM_ST_MAX_BYTES (CM_ST_HDR_BYTES + CM_ST_MAX_ROUTERS * CM_ST_ENTRY_BYTES)

#define CM_ST_UNKNOWN           0xFFFFFFFFUL

/* A bus/dev/fn word: bus 15:8, device 7:3, function 2:0 (XHCI_TBT_BDF). */
#define CM_ST_BDF_BUS(b)        (((b) >> 8) & 0xFFUL)
#define CM_ST_BDF_DEV(b)        (((b) >> 3) & 0x1FUL)
#define CM_ST_BDF_FN(b)         ((b) & 0x7UL)

#define CM_ST_CM_NONE           0UL     /* inert, stopped, or not yet bound */
#define CM_ST_CM_ICM            1UL     /* the firmware connection manager's client */
#define CM_ST_CM_USB4           2UL     /* the USB4 software connection manager */

#define CM_ST_STAGE_STOPPED     0UL
#define CM_ST_STAGE_INERT       1UL
#define CM_ST_STAGE_ACTIVE      2UL
#define CM_ST_STAGE_FAILED      3UL

#define CM_ST_F_TRUNCATED       0x00000001UL    /* more routers than entries */

/* Kinds, and what CM_ST_E_IDS holds for each. */
#define CM_ST_KIND_ICM          1UL     /* an ICM-announced device: the
                                           Thunderbolt vendor 15:0 and
                                           model 31:16 ids, or CM_ST_UNKNOWN */
#define CM_ST_KIND_USB4_PORT    2UL     /* a router on a host USB4 port: its
                                           vendor 15:0, device 31:16 */
#define CM_ST_KIND_USB4_LINK    3UL     /* a deeper router (record 22
                                           section 7.1): CM_ST_UNKNOWN */

#define CM_ST_GEN_UNKNOWN       0UL
#define CM_ST_GEN_TBT3          1UL
#define CM_ST_GEN_USB4          2UL

#define CM_ST_LANES_UNKNOWN     0UL
#define CM_ST_LANES_SINGLE      1UL
#define CM_ST_LANES_BONDED      2UL

#define CM_ST_APPR_UNKNOWN      0UL
#define CM_ST_APPR_APPROVED     1UL     /* the tunnel exists */
#define CM_ST_APPR_BOOT         2UL     /* approved before the system started */
#define CM_ST_APPR_REJECTED     3UL     /* the firmware rejected it */
#define CM_ST_APPR_FAILED       4UL     /* the approval was refused */
#define CM_ST_APPR_UNANSWERED   5UL     /* the approval was never answered */
#define CM_ST_APPR_PENDING      6UL     /* not approved (policy), or not yet */
#define CM_ST_APPR_HELD         7UL     /* USB4: held, failed or unsupported */

#define CM_ST_TUN_NONE          0UL
#define CM_ST_TUN_UP            1UL
#define CM_ST_TUN_PRESERVED     2UL     /* found at start, kept */
#define CM_ST_TUN_FAILED        3UL
#define CM_ST_TUN_EJECTED       4UL     /* torn down after an eject */
#define CM_ST_TUN_UNKNOWN       5UL

#endif /* CM_STATUS_H */
