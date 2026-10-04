/*
 * xhci_sshub.c - the SuperSpeed hub class inside the bus, its pure half
 * (xhci_sshub.h; roadmap-hcd.md tasks 30-A.1 and 30-A.2;
 * xhci-data-structures.md section 11).
 *
 * DDK-free: part of the pure core. IRQL: any.
 */

#include "xhci.h"
#include "xhci_enum.h"
#include "xhci_hub.h"
#include "xhci_pipe.h"
#include "xhci_sshub.h"

/* IRQL: any. */
ULONG XhciSsHubParseDescriptor(const UCHAR *data, ULONG length,
                               PXHCI_SSHUB_DESC out)
{
    ULONG declared;

    if (data == NULL || out == NULL) {
        return XHCI_SSHUB_BAD_PARAM;
    }
    out->Ports = 0;
    out->Characteristics = 0;
    out->PowerGoodMs = 0;
    out->ControllerCurrent = 0;
    out->HeaderDecodeLatency = 0;
    out->HubDelayNs = 0;
    out->Removable = 0;
    out->Managed = 0;
    if (length < XHCI_SSHUB_DESC_BYTES) {
        return XHCI_SSHUB_MALFORMED;
    }
    declared = (ULONG)data[0];
    if (declared < XHCI_SSHUB_DESC_BYTES || declared > length ||
        (ULONG)data[1] != XHCI_SSHUB_DESC_TYPE || data[2] == 0 ||
        (ULONG)data[2] > XHCI_SSHUB_MAX_PORTS) {
        return XHCI_SSHUB_MALFORMED;
    }
    out->Ports = (ULONG)data[2];
    out->Characteristics = (ULONG)data[3] | ((ULONG)data[4] << 8);
    out->PowerGoodMs = (ULONG)data[5] * 2UL;
    out->ControllerCurrent = (ULONG)data[6];
    out->HeaderDecodeLatency = (ULONG)data[7];
    out->HubDelayNs = (ULONG)data[8] | ((ULONG)data[9] << 8);
    out->Removable = (ULONG)data[10] | ((ULONG)data[11] << 8);
    out->Managed = XhciHubManagedPorts(out->Ports);
    return XHCI_SSHUB_OK;
}

/* IRQL: any. */
ULONG XhciSsHubLinkState(ULONG status)
{
    return (status & XHCI_SSHUB_PORT_LINK_MASK) >> XHCI_SSHUB_PORT_LINK_SHIFT;
}

/* Whether the machine holds something a disconnect takes away (as
 * xhci_hub.c's). */
static ULONG xhciSsHubHolds(ULONG state)
{
    return state != XHCI_ENUM_EMPTY && state != XHCI_ENUM_FAILED &&
           state != XHCI_ENUM_GONE;
}

/* IRQL: any. */
VOID XhciSsHubPortDecide(ULONG state, ULONG status, ULONG change,
                         PXHCI_SSHUB_PORT_DECISION out)
{
    ULONG connected;
    ULONG link;

    if (out == NULL) {
        return;
    }
    out->Clear = change & XHCI_SSHUB_C_PORT_MASK;
    out->Disconnect = 0;
    out->Connect = 0;
    out->OverCurrent = 0;
    out->Repower = 0;
    out->WarmReset = 0;
    out->ConfigError = (change & XHCI_SSHUB_C_PORT_CONFIG_ERROR) != 0;
    out->LinkChange = (change & XHCI_SSHUB_C_PORT_LINK_STATE) != 0;
    out->Resume = 0;
    out->Resumed = 0;
    connected = (status & XHCI_SSHUB_PORT_CONNECTION) != 0;
    link = XhciSsHubLinkState(status);

    if ((change & XHCI_SSHUB_C_PORT_OVER_CURRENT) != 0) {
        out->OverCurrent = 1;
        out->Repower = (status & XHCI_SSHUB_PORT_POWER) == 0;
    }
    if (link == XHCI_SSHUB_LINK_INACTIVE ||
        link == XHCI_SSHUB_LINK_COMPLIANCE) {
        /* Only a warm reset leaves these states (USB 3.2 7.5.2 and 7.5.3,
         * to verify): whatever the port held goes first, and what the
         * reset leaves is decided afresh by the executor. */
        out->WarmReset = 1;
        out->Disconnect = xhciSsHubHolds(state);
        return;
    }
    if ((change & XHCI_SSHUB_C_PORT_CONNECTION) != 0) {
        out->Disconnect = xhciSsHubHolds(state);
        out->Connect = connected && !out->ConfigError;
        return;
    }
    if (out->ConfigError) {
        /* The link could not be configured and is not in a state a warm
         * reset recovers (that branch, above, comes first): enumerating
         * it again would meet the same error, so it stays down until its
         * next connect change. */
        out->Disconnect = xhciSsHubHolds(state);
        return;
    }
    if (!connected) {
        out->Disconnect = state != XHCI_ENUM_EMPTY &&
                          state != XHCI_ENUM_GONE;
        return;
    }
    if ((status & XHCI_SSHUB_PORT_ENABLE) == 0 && xhciSsHubHolds(state)) {
        /* No C_PORT_ENABLE at SuperSpeed: a held device on a port that no
         * longer reads enabled is enumerated afresh. */
        out->Disconnect = 1;
        out->Connect = 1;
        return;
    }
    if (xhciSsHubHolds(state) && (status & XHCI_SSHUB_PORT_ENABLE) != 0) {
        if (link == XHCI_SSHUB_LINK_U3) {
            out->Resume = 1;
        } else if (out->LinkChange && link == XHCI_SSHUB_LINK_U0) {
            out->Resumed = 1;
        }
    }
    if (state == XHCI_ENUM_EMPTY) {
        out->Connect = 1;
    }
}

/* IRQL: any. */
ULONG XhciSsHubResumeProgress(ULONG status)
{
    ULONG link;

    if ((status & XHCI_SSHUB_PORT_CONNECTION) == 0) {
        return XHCI_HUB_RESUME_GONE;
    }
    link = XhciSsHubLinkState(status);
    switch (link) {
    case XHCI_SSHUB_LINK_U3:
    case XHCI_SSHUB_LINK_RESUME:
    case XHCI_SSHUB_LINK_RECOVERY:
        return XHCI_HUB_RESUME_PENDING;
    case XHCI_SSHUB_LINK_U0:
    case XHCI_SSHUB_LINK_U1:
    case XHCI_SSHUB_LINK_U2:
        if ((status & XHCI_SSHUB_PORT_ENABLE) != 0) {
            return XHCI_HUB_RESUME_DONE;
        }
        return XHCI_HUB_RESUME_DISABLED;
    default:
        return XHCI_HUB_RESUME_DISABLED;
    }
}

/* IRQL: any. */
ULONG XhciSsHubClearSelector(ULONG changeBit)
{
    switch (changeBit) {
    case XHCI_SSHUB_C_PORT_CONNECTION:
        return XHCI_HUB_FEAT_C_PORT_CONNECTION;
    case XHCI_SSHUB_C_PORT_OVER_CURRENT:
        return XHCI_HUB_FEAT_C_PORT_OVER_CURRENT;
    case XHCI_SSHUB_C_PORT_RESET:
        return XHCI_HUB_FEAT_C_PORT_RESET;
    case XHCI_SSHUB_C_BH_PORT_RESET:
        return XHCI_SSHUB_FEAT_C_BH_PORT_RESET;
    case XHCI_SSHUB_C_PORT_LINK_STATE:
        return XHCI_SSHUB_FEAT_C_PORT_LINK_STATE;
    case XHCI_SSHUB_C_PORT_CONFIG_ERROR:
        return XHCI_SSHUB_FEAT_C_PORT_CONFIG_ERROR;
    default:
        return 0;
    }
}

/* IRQL: any. */
ULONG XhciSsHubResetKind(ULONG status, PULONG converted)
{
    ULONG link;
    ULONG kind;

    link = XhciSsHubLinkState(status);
    if (link == XHCI_SSHUB_LINK_INACTIVE ||
        link == XHCI_SSHUB_LINK_COMPLIANCE) {
        kind = XHCI_SSHUB_RESET_WARM;
    } else if ((status & XHCI_SSHUB_PORT_CONNECTION) == 0) {
        kind = XHCI_SSHUB_RESET_NONE;
    } else {
        switch (link) {
        case XHCI_SSHUB_LINK_U0:
        case XHCI_SSHUB_LINK_U1:
        case XHCI_SSHUB_LINK_U2:
        case XHCI_SSHUB_LINK_RECOVERY:
            kind = XHCI_SSHUB_RESET_HOT;
            break;
        default:
            /* U3, a stuck Polling or Hot Reset, Loopback, or a connection
             * the link state does not account for: a hot reset cannot
             * start there (USB 3.2 7.4.2, to verify). */
            kind = XHCI_SSHUB_RESET_WARM;
            break;
        }
    }
    if (converted != NULL) {
        *converted = kind == XHCI_SSHUB_RESET_WARM;
    }
    return kind;
}

/* IRQL: any. */
ULONG XhciSsHubResetProgress(ULONG status, ULONG change, PULONG warmSeen)
{
    ULONG done;
    ULONG up;

    done = (change & (XHCI_SSHUB_C_PORT_RESET | XHCI_SSHUB_C_BH_PORT_RESET))
           != 0;
    if (warmSeen != NULL) {
        *warmSeen = (change & XHCI_SSHUB_C_BH_PORT_RESET) != 0;
    }
    if (!done && (status & XHCI_SSHUB_PORT_RESET) != 0) {
        return XHCI_HUB_RESET_PENDING;
    }
    if ((status & XHCI_SSHUB_PORT_CONNECTION) == 0) {
        return XHCI_HUB_RESET_FAILED;
    }
    up = (status & XHCI_SSHUB_PORT_ENABLE) != 0 &&
         XhciSsHubLinkState(status) == XHCI_SSHUB_LINK_U0;
    if (!done && !up) {
        /* Neither the change nor a trained link yet: the hub may not have
         * started the reset. */
        return XHCI_HUB_RESET_PENDING;
    }
    return up ? XHCI_HUB_RESET_ENABLED : XHCI_HUB_RESET_FAILED;
}

/* IRQL: any. */
ULONG XhciSsHubResetClears(ULONG change, ULONG warm)
{
    ULONG clear;

    clear = change & (XHCI_SSHUB_C_PORT_RESET | XHCI_SSHUB_C_BH_PORT_RESET);
    if (warm || (change & XHCI_SSHUB_C_BH_PORT_RESET) != 0) {
        clear |= change & (XHCI_SSHUB_C_PORT_LINK_STATE |
                           XHCI_SSHUB_C_PORT_CONNECTION);
    }
    return clear;
}

/* ----------------------------------------------------------------------- */
/* SuperSpeedPlus downstream rates                                          */
/* ----------------------------------------------------------------------- */

/* IRQL: any. */
ULONG XhciSsHubHasExtStatus(ULONG bcdUsb, const XHCI_PIPE_BOS *bos)
{
    return bcdUsb >= XHCI_SSHUB_BCD_EXT_STATUS && bos != NULL &&
           bos->HasSuperSpeedPlus;
}

/* IRQL: any. */
ULONG XhciSsHubSublinkRate(const XHCI_PIPE_BOS *bos, ULONG ssid,
                           PULONG laneKbps, PULONG plus)
{
    ULONG attr;
    ULONG lsm;
    ULONG kbps;
    ULONG count;
    ULONG i;

    if (laneKbps == NULL || plus == NULL) {
        return XHCI_SSHUB_BAD_PARAM;
    }
    *laneKbps = 0;
    *plus = 0;
    if (bos == NULL || !bos->HasSuperSpeedPlus) {
        return XHCI_SSHUB_NOT_FOUND;
    }
    count = bos->SspSublinks;
    if (count > XHCI_PIPE_BOS_SUBLINKS) {
        count = XHCI_PIPE_BOS_SUBLINKS;
    }
    for (i = 0; i < count; i++) {
        attr = bos->SspSublink[i];
        if (XHCI_SSHUB_SSA_SSID(attr) != (ssid & 0xFUL) ||
            (XHCI_SSHUB_SSA_ST(attr) & XHCI_SSHUB_SSA_ST_TX) != 0) {
            continue;
        }
        lsm = XHCI_SSHUB_SSA_LSM(attr);
        switch (XHCI_SSHUB_SSA_LSE(attr)) {
        case 0:
            kbps = lsm / 1000UL;
            break;
        case 1:
            kbps = lsm;
            break;
        case 2:
            kbps = lsm * 1000UL;
            break;
        default:
            if (lsm > 4294UL) {
                return XHCI_SSHUB_NOT_FOUND;
            }
            kbps = lsm * 1000000UL;
            break;
        }
        if (kbps == 0) {
            return XHCI_SSHUB_NOT_FOUND;
        }
        *laneKbps = kbps;
        *plus = XHCI_SSHUB_SSA_LP(attr) == XHCI_SSHUB_SSA_LP_SSP;
        return XHCI_SSHUB_OK;
    }
    return XHCI_SSHUB_NOT_FOUND;
}

/* IRQL: any. */
ULONG XhciSsHubDownstream(const XHCI_PIPE_BOS *bos, ULONG extStatus,
                          PXHCI_SSHUB_LINK out)
{
    ULONG lane;
    ULONG plus;
    ULONG lanes;
    ULONG answer;

    if (out == NULL) {
        return XHCI_SSHUB_BAD_PARAM;
    }
    out->LaneKbps = 0;
    out->Lanes = 0;
    out->Kbps = 0;
    out->Plus = 0;
    answer = XhciSsHubSublinkRate(bos, XHCI_SSHUB_EXT_RX_SSID(extStatus),
                                  &lane, &plus);
    if (answer != XHCI_SSHUB_OK) {
        return answer;
    }
    lanes = XHCI_SSHUB_EXT_RX_LANES(extStatus);
    if (lane > 0xFFFFFFFFUL / lanes) {
        return XHCI_SSHUB_NOT_FOUND;
    }
    out->LaneKbps = lane;
    out->Lanes = lanes;
    out->Kbps = lane * lanes;
    out->Plus = (plus || lanes > 1 || out->Kbps > XHCI_RATE_GEN1_KBPS)
                    ? 1UL : 0UL;
    return XHCI_SSHUB_OK;
}

/* The SuperSpeedPlus ID at exactly `kbps` among the root port protocol's
 * named IDs, or 0. */
static ULONG xhciSsHubPsivAt(const XHCI_PORT_MAP *map, ULONG rootPort,
                             ULONG kbps)
{
    ULONG rate;
    ULONG plus;
    ULONG v;

    for (v = 1; v <= 15; v++) {
        if (XhciPortRate(map, rootPort, v, &rate, &plus) == XHCI_CAPS_OK &&
            plus && rate == kbps) {
            return v;
        }
    }
    return 0;
}

/* IRQL: any. */
ULONG XhciSsHubPsiv(const XHCI_PORT_MAP *map, ULONG rootPort,
                    const XHCI_SSHUB_LINK *link, PULONG psiv,
                    PULONG matched)
{
    const XHCI_PROTOCOL *proto;
    ULONG v;

    if (map == NULL || psiv == NULL || matched == NULL) {
        return XHCI_SSHUB_BAD_PARAM;
    }
    *psiv = 0;
    *matched = 0;
    if (rootPort == 0 || rootPort > map->PortCount ||
        rootPort > XHCI_MAX_ROOT_PORTS ||
        map->Protocol[rootPort - 1] == XHCI_PORT_NO_PROTOCOL) {
        return XHCI_SSHUB_NOT_FOUND;
    }
    v = 0;
    if (link != NULL && link->Plus) {
        proto = &map->Protocols[map->Protocol[rootPort - 1]];
        if (proto->Major == 3 && proto->PsiCount == 0) {
            /* The default IDs name two 10 Gbit/s modes; the lanes tell
             * them apart. */
            if (link->Kbps == 10000000UL) {
                v = (link->Lanes >= 2) ? XHCI_PSIV_SSP_GEN1X2
                                       : XHCI_PSIV_SSP_GEN2X1;
            } else if (link->Kbps == 20000000UL) {
                v = XHCI_PSIV_SSP_GEN2X2;
            }
        } else if (proto->Major == 3) {
            /* A PSI DWORD names the aggregate rate (Codex review of
             * 034a119, finding 2), so only that is a match; the lane
             * rate's ID is a guess, given unmatched, and the controller's
             * output Slot Context settles it after Address Device
             * (XhciSsHubAdoptSpeed). */
            v = xhciSsHubPsivAt(map, rootPort, link->Kbps);
            if (v == 0) {
                v = xhciSsHubPsivAt(map, rootPort, link->LaneKbps);
                if (v != 0) {
                    *psiv = v;
                    return XHCI_SSHUB_OK;
                }
            }
        }
        if (v != 0) {
            *psiv = v;
            *matched = 1;
            return XHCI_SSHUB_OK;
        }
    }
    if (XhciPortPsivForSpeed(map, rootPort, XHCI_SPEED_SUPER, &v) !=
        XHCI_CAPS_OK) {
        return XHCI_SSHUB_NOT_FOUND;
    }
    *psiv = v;
    *matched = (link == NULL || !link->Plus) ? 1UL : 0UL;
    return XHCI_SSHUB_OK;
}

/* IRQL: any. */
ULONG XhciSsHubAdoptSpeed(const XHCI_PORT_MAP *map, ULONG rootPort,
                          ULONG given, ULONG output)
{
    ULONG cls;

    if (map == NULL || output == 0 || output == given) {
        return given;
    }
    cls = XHCI_SPEED_UNKNOWN;
    if (XhciPortSpeedClass(map, rootPort, output, &cls) != XHCI_CAPS_OK ||
        cls != XHCI_SPEED_SUPER) {
        return given;
    }
    return output;
}

/* IRQL: any. */
ULONG XhciSsHubLooksPaired(ULONG ssVendor, ULONG ssRoot, ULONG ssTier,
                           ULONG ssRoute, ULONG hsVendor, ULONG hsRoot,
                           ULONG hsTier, ULONG hsRoute, ULONG companionOfSs)
{
    return ssVendor == hsVendor && ssTier == hsTier && ssRoute == hsRoute &&
           ssRoot != 0 && companionOfSs != 0 && companionOfSs == hsRoot;
}
