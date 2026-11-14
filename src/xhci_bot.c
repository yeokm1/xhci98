/*
 * xhci_bot.c - the Bulk-Only transport's state as the bus sees it, the pure
 * half (xhci_bot.h; roadmap-usb4 task 49.34, design record 20 section 4.3.4).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_bot.h"

static ULONG xhciBotLe32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static ULONG xhciBotBe32(const UCHAR *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) |
           (ULONG)p[3];
}

static VOID xhciBotPutLe32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFFUL);
    p[1] = (UCHAR)((v >> 8) & 0xFFUL);
    p[2] = (UCHAR)((v >> 16) & 0xFFUL);
    p[3] = (UCHAR)((v >> 24) & 0xFFUL);
}

/* Fail closed: UNKNOWN, left only through a SYNC event. */
static VOID xhciBotUnknown(PXHCI_BOT bot, ULONG why)
{
    bot->State = XHCI_BOT_UNKNOWN;
    bot->Flags = 0;
    bot->LastUnknown = why;
}

/* The device is defined to be waiting for a CBW, nothing carried since. */
static VOID xhciBotSync(PXHCI_BOT bot)
{
    bot->State = XHCI_BOT_SYNC;
    bot->Flags = 0;
}

VOID XhciBotInit(PXHCI_BOT bot)
{
    ULONG i;

    bot->State = XHCI_BOT_UNKNOWN;
    bot->Flags = 0;
    bot->Tag = 0;
    bot->Length = 0;
    bot->Moved = 0;
    bot->DirIn = 0;
    bot->Opcode = 0;
    bot->Service = 0;
    bot->MaxLun = XHCI_BOT_LUN_UNKNOWN;
    bot->MaxLunSeen = 0;
    bot->StashLength = 0;
    for (i = 0; i < XHCI_BOT_STASH_BYTES; i++) {
        bot->Stash[i] = 0;
    }
    bot->Cap.Kind = XHCI_BOT_CAP_NONE;
    bot->Cap.LastLbaLo = 0;
    bot->Cap.LastLbaHi = 0;
    bot->Cap.BlockLength = 0;
    bot->Events = 0;
    bot->LastUnknown = XHCI_BOT_WHY_INIT;
}

VOID XhciBotSelected(PXHCI_BOT bot, ULONG first, ULONG isInterface)
{
    bot->Events++;
    if (first && !isInterface) {
        /* Reset and addressed by the bus's enumeration, nothing carried:
         * what the drive held before is another configuration's. */
        xhciBotSync(bot);
        bot->MaxLun = XHCI_BOT_LUN_UNKNOWN;
        bot->MaxLunSeen = 0;
        bot->Cap.Kind = XHCI_BOT_CAP_NONE;
        return;
    }
    /* A class driver selecting the setting again at its start crosses no
     * transport; any other select proves nothing of it (this bus may answer
     * SELECT_INTERFACE with success after a STALLed SET_INTERFACE). */
    if (isInterface && bot->State == XHCI_BOT_SYNC &&
        (bot->Flags & XHCI_BOT_F_CARRIED) == 0) {
        return;
    }
    xhciBotUnknown(bot, XHCI_BOT_WHY_SELECT);
}

VOID XhciBotPortReset(PXHCI_BOT bot)
{
    bot->Events++;
    xhciBotSync(bot);
}

ULONG XhciBotParseCbw(const UCHAR *bytes, ULONG length, ULONG maxLun,
                      PULONG tag, PULONG dataLength, PULONG dirIn,
                      PULONG opcode, PULONG service)
{
    ULONG flags;
    ULONG lun;
    ULONG cb;

    if (bytes == NULL || length != XHCI_BOT_CBW_BYTES ||
        xhciBotLe32(bytes) != XHCI_BOT_CBW_SIG) {
        return 0;
    }
    flags = (ULONG)bytes[12];
    lun = (ULONG)bytes[13];
    cb = (ULONG)bytes[14];
    /* Reserved bits zero (section 5.1): flags 6:0, LUN 7:4, length 7:5. */
    if ((flags & 0x7FUL) != 0 || (lun & 0xF0UL) != 0 ||
        (cb & 0xE0UL) != 0 || cb < 1 || cb > 16) {
        return 0;
    }
    if (maxLun != XHCI_BOT_LUN_UNKNOWN && lun > maxLun) {
        return 0;
    }
    *tag = xhciBotLe32(bytes + 4);
    *dataLength = xhciBotLe32(bytes + 8);
    *dirIn = (flags & 0x80UL) != 0 ? 1UL : 0UL;
    *opcode = (ULONG)bytes[15];
    *service = cb >= 2 ? ((ULONG)bytes[16] & 0x1FUL) : 0UL;
    return 1;
}

ULONG XhciBotParseCsw(const UCHAR *bytes, ULONG length, ULONG tag,
                      ULONG dataLength, PULONG status, PULONG residue)
{
    ULONG st;
    ULONG res;

    if (bytes == NULL || length != XHCI_BOT_CSW_BYTES ||
        xhciBotLe32(bytes) != XHCI_BOT_CSW_SIG ||
        xhciBotLe32(bytes + 4) != tag) {
        return 0;
    }
    res = xhciBotLe32(bytes + 8);
    st = (ULONG)bytes[12];
    if (st > 2 || res > dataLength) {
        return 0;
    }
    *status = st;
    *residue = res;
    return 1;
}

ULONG XhciBotUsable(ULONG moved, ULONG dataLength, ULONG residue)
{
    ULONG relevant;

    relevant = residue <= dataLength ? dataLength - residue : 0UL;
    return moved < relevant ? moved : relevant;
}

ULONG XhciBotParseSense(const UCHAR *bytes, ULONG usable, PULONG key,
                        PULONG asc, PULONG ascq)
{
    ULONG code;

    if (bytes == NULL || usable < 1) {
        return 0;
    }
    code = (ULONG)bytes[0] & 0x7FUL;
    if (code == 0x70UL || code == 0x71UL) {
        /* Fixed format: key at 2, ASC/ASCQ at 12/13, reached only when the
         * additional sense length (byte 7) covers them. */
        if (usable < 14 || (ULONG)bytes[7] < 6) {
            return 0;
        }
        *key = (ULONG)bytes[2] & 0x0FUL;
        *asc = (ULONG)bytes[12];
        *ascq = (ULONG)bytes[13];
        return 1;
    }
    if (code == 0x72UL || code == 0x73UL) {
        if (usable < 4) {
            return 0;
        }
        *key = (ULONG)bytes[1] & 0x0FUL;
        *asc = (ULONG)bytes[2];
        *ascq = (ULONG)bytes[3];
        return 1;
    }
    return 0;
}

ULONG XhciBotParseCapacity(const UCHAR *bytes, ULONG usable, ULONG sixteen,
                           XHCI_BOT_CAP *cap)
{
    if (bytes == NULL) {
        return 0;
    }
    if (sixteen) {
        if (usable < 12) {
            return 0;
        }
        cap->LastLbaHi = xhciBotBe32(bytes);
        cap->LastLbaLo = xhciBotBe32(bytes + 4);
        cap->BlockLength = xhciBotBe32(bytes + 8);
    } else {
        if (usable < 8) {
            return 0;
        }
        cap->LastLbaHi = 0;
        cap->LastLbaLo = xhciBotBe32(bytes);
        cap->BlockLength = xhciBotBe32(bytes + 4);
    }
    cap->Kind = XHCI_BOT_CAP_SIZE;
    return 1;
}

/* The open command keeps its data phase's head when its answer is one the
 * machine reads (a capacity, a sense). */
static ULONG xhciBotStashes(const XHCI_BOT *bot)
{
    if (!bot->DirIn) {
        return 0;
    }
    return (bot->Opcode == XHCI_BOT_OP_READ_CAPACITY10 ||
            bot->Opcode == XHCI_BOT_OP_REQUEST_SENSE ||
            (bot->Opcode == XHCI_BOT_OP_SERVICE_IN16 &&
             bot->Service == XHCI_BOT_SA_READ_CAPACITY16)) ? 1UL : 0UL;
}

VOID XhciBotSubmit(PXHCI_BOT bot, ULONG in, ULONG length,
                   const UCHAR *bytes)
{
    ULONG tag;
    ULONG dataLength;
    ULONG dirIn;
    ULONG opcode;
    ULONG service;
    ULONG keep;

    bot->Events++;
    switch (bot->State) {
    case XHCI_BOT_SYNC:
    case XHCI_BOT_IDLE:
    case XHCI_BOT_SENSE:
        if (in || length != XHCI_BOT_CBW_BYTES) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_NOT_CBW);
            return;
        }
        if (!XhciBotParseCbw(bytes, length, bot->MaxLun, &tag, &dataLength,
                             &dirIn, &opcode, &service)) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_BAD_CBW);
            return;
        }
        keep = 0;
        if (bot->State == XHCI_BOT_SENSE) {
            if (opcode != XHCI_BOT_OP_REQUEST_SENSE) {
                xhciBotUnknown(bot, XHCI_BOT_WHY_SENSE);
                return;
            }
            keep = XHCI_BOT_F_SENSE_CMD |
                   (bot->Flags & XHCI_BOT_F_CAP_FAILED);
        }
        bot->Flags = XHCI_BOT_F_CARRIED | keep;
        bot->Tag = tag;
        bot->Length = dataLength;
        bot->Moved = 0;
        bot->DirIn = dirIn;
        bot->Opcode = opcode;
        bot->Service = service;
        bot->StashLength = 0;
        bot->State = XHCI_BOT_CBW_SENT;
        return;
    case XHCI_BOT_DATA:
        if ((in ? 1UL : 0UL) != bot->DirIn) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_DIRECTION);
        }
        return;
    case XHCI_BOT_STATUS:
        if (!in) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_DIRECTION);
        }
        return;
    case XHCI_BOT_UNKNOWN:
        return;
    default:
        /* CBW_SENT, a stall's clear owed, a recovery's clears owed. */
        xhciBotUnknown(bot, XHCI_BOT_WHY_EARLY);
        return;
    }
}

static VOID xhciBotStash(PXHCI_BOT bot, ULONG actual, const UCHAR *bytes)
{
    ULONG room;
    ULONG n;
    ULONG i;

    if (!xhciBotStashes(bot) || actual == 0) {
        return;
    }
    if (bytes == NULL) {
        bot->Flags |= XHCI_BOT_F_STASH_BAD;
        return;
    }
    room = XHCI_BOT_STASH_BYTES - bot->StashLength;
    n = actual < room ? actual : room;
    for (i = 0; i < n; i++) {
        bot->Stash[bot->StashLength + i] = bytes[i];
    }
    bot->StashLength += n;
}

/* A validated CSW with status 0 or 1: what the command's answer records. */
static VOID xhciBotEnded(PXHCI_BOT bot, ULONG status, ULONG residue)
{
    XHCI_BOT_CAP cap;
    ULONG usable;
    ULONG sixteen;
    ULONG key;
    ULONG asc;
    ULONG ascq;
    ULONG readable;

    usable = XhciBotUsable(bot->Moved, bot->Length, residue);
    if (usable > bot->StashLength) {
        usable = bot->StashLength;
    }
    readable = (bot->Flags & XHCI_BOT_F_STASH_BAD) == 0 ? 1UL : 0UL;
    if (status == 1) {
        if ((bot->Flags & XHCI_BOT_F_SENSE_CMD) != 0) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_SENSE);
            return;
        }
        sixteen = (bot->Opcode == XHCI_BOT_OP_SERVICE_IN16) ? 1UL : 0UL;
        bot->Flags = XHCI_BOT_F_CARRIED;
        if (bot->Opcode == XHCI_BOT_OP_READ_CAPACITY10 ||
            (sixteen && bot->Service == XHCI_BOT_SA_READ_CAPACITY16)) {
            bot->Flags |= XHCI_BOT_F_CAP_FAILED;
        }
        bot->State = XHCI_BOT_SENSE;
        return;
    }
    if ((bot->Flags & XHCI_BOT_F_SENSE_CMD) != 0 &&
        (bot->Flags & XHCI_BOT_F_CAP_FAILED) != 0) {
        /* A READ CAPACITY that failed, and the sense it owed: "medium not
         * present" is the no-medium answer; anything else records none. */
        bot->Cap.Kind = XHCI_BOT_CAP_NONE;
        if (readable &&
            XhciBotParseSense(bot->Stash, usable, &key, &asc, &ascq) &&
            key == 0x02UL && asc == 0x3AUL) {
            bot->Cap.Kind = XHCI_BOT_CAP_NO_MEDIUM;
        }
    } else if (bot->Opcode == XHCI_BOT_OP_READ_CAPACITY10 ||
               (bot->Opcode == XHCI_BOT_OP_SERVICE_IN16 &&
                bot->Service == XHCI_BOT_SA_READ_CAPACITY16)) {
        sixteen = (bot->Opcode == XHCI_BOT_OP_SERVICE_IN16) ? 1UL : 0UL;
        bot->Cap.Kind = XHCI_BOT_CAP_NONE;
        if (readable &&
            XhciBotParseCapacity(bot->Stash, usable, sixteen, &cap)) {
            bot->Cap = cap;
        }
    }
    bot->Flags = XHCI_BOT_F_CARRIED;
    bot->State = XHCI_BOT_IDLE;
}

VOID XhciBotComplete(PXHCI_BOT bot, ULONG in, ULONG how, ULONG actual,
                     ULONG requested, const UCHAR *bytes)
{
    ULONG status;
    ULONG residue;

    bot->Events++;
    in = in ? 1UL : 0UL;
    switch (bot->State) {
    case XHCI_BOT_CBW_SENT:
        if (in) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_EARLY);
            return;
        }
        if (how != XHCI_BOT_XFER_OK || actual != XHCI_BOT_CBW_BYTES) {
            /* A device STALLs a CBW it found invalid: reset recovery. */
            xhciBotUnknown(bot, XHCI_BOT_WHY_CBW_END);
            return;
        }
        bot->State = bot->Length != 0 ? XHCI_BOT_DATA : XHCI_BOT_STATUS;
        return;
    case XHCI_BOT_DATA:
        if (in != bot->DirIn) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_DIRECTION);
            return;
        }
        if (how != XHCI_BOT_XFER_OK && how != XHCI_BOT_XFER_STALL) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_DATA_END);
            return;
        }
        if (actual > bot->Length - bot->Moved || actual > requested) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_OVERRUN);
            return;
        }
        if (in) {
            xhciBotStash(bot, actual, bytes);
        }
        bot->Moved += actual;
        if (how == XHCI_BOT_XFER_STALL) {
            bot->State = XHCI_BOT_DATA_STALL;
        } else if (bot->Moved == bot->Length || actual < requested) {
            bot->State = XHCI_BOT_STATUS;
        }
        return;
    case XHCI_BOT_STATUS:
        if (!in) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_DIRECTION);
            return;
        }
        if (how == XHCI_BOT_XFER_STALL) {
            if ((bot->Flags & XHCI_BOT_F_CSW_RETRIED) != 0) {
                xhciBotUnknown(bot, XHCI_BOT_WHY_CSW_END);
            } else {
                bot->State = XHCI_BOT_STATUS_STALL;
            }
            return;
        }
        if (how != XHCI_BOT_XFER_OK) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_CSW_END);
            return;
        }
        if (!XhciBotParseCsw(bytes, actual, bot->Tag, bot->Length, &status,
                             &residue)) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_BAD_CSW);
            return;
        }
        if (status == 2) {
            xhciBotUnknown(bot, XHCI_BOT_WHY_PHASE);
            return;
        }
        xhciBotEnded(bot, status, residue);
        return;
    case XHCI_BOT_UNKNOWN:
        return;
    case XHCI_BOT_SYNC:
    case XHCI_BOT_IDLE:
    case XHCI_BOT_SENSE:
        xhciBotUnknown(bot, XHCI_BOT_WHY_OTHER);
        return;
    default:
        xhciBotUnknown(bot, XHCI_BOT_WHY_EARLY);
        return;
    }
}

VOID XhciBotClearHalt(PXHCI_BOT bot, ULONG in, ULONG ok)
{
    bot->Events++;
    in = in ? 1UL : 0UL;
    if (bot->State == XHCI_BOT_UNKNOWN) {
        return;
    }
    if (!ok) {
        xhciBotUnknown(bot, XHCI_BOT_WHY_CLEAR);
        return;
    }
    switch (bot->State) {
    case XHCI_BOT_DATA_STALL:
        if (in == bot->DirIn) {
            bot->State = XHCI_BOT_STATUS;
            return;
        }
        break;
    case XHCI_BOT_STATUS_STALL:
        if (in) {
            bot->Flags |= XHCI_BOT_F_CSW_RETRIED;
            bot->State = XHCI_BOT_STATUS;
            return;
        }
        break;
    case XHCI_BOT_RECOVERY:
        /* Section 5.3.4's order: Bulk-In, then Bulk-Out. */
        if (in && (bot->Flags & XHCI_BOT_F_RECOV_IN) == 0) {
            bot->Flags |= XHCI_BOT_F_RECOV_IN;
            return;
        }
        if (!in && (bot->Flags & XHCI_BOT_F_RECOV_IN) != 0) {
            xhciBotSync(bot);
            return;
        }
        xhciBotUnknown(bot, XHCI_BOT_WHY_RECOVERY);
        return;
    default:
        break;
    }
    xhciBotUnknown(bot, XHCI_BOT_WHY_CLEAR);
}

VOID XhciBotClassRequest(PXHCI_BOT bot, ULONG request, ULONG ok,
                         ULONG stalled, ULONG answer, ULONG answered)
{
    ULONG lun;
    ULONG first;

    bot->Events++;
    if (request == XHCI_BOT_REQ_RESET) {
        if (ok) {
            bot->State = XHCI_BOT_RECOVERY;
            bot->Flags = 0;
        } else {
            xhciBotUnknown(bot, XHCI_BOT_WHY_RECOVERY);
        }
        return;
    }
    if (request == XHCI_BOT_REQ_MAX_LUN) {
        /* The STALL's exception is the first request's alone, whatever that
         * request's outcome. */
        first = bot->MaxLunSeen ? 0UL : 1UL;
        bot->MaxLunSeen = 1;
        /* Only between commands, never inside a reset recovery (whose
         * clears must follow its reset with nothing between). */
        if (bot->State != XHCI_BOT_SYNC && bot->State != XHCI_BOT_IDLE &&
            bot->State != XHCI_BOT_UNKNOWN) {
            bot->MaxLun = XHCI_BOT_LUN_UNKNOWN;
            xhciBotUnknown(bot, XHCI_BOT_WHY_MAX_LUN);
            return;
        }
        if (ok && answered >= 1 && answer <= 15) {
            lun = answer;
        } else if (stalled && first) {
            lun = 0; /* section 3.2: a device with one LUN may STALL it */
        } else {
            bot->MaxLun = XHCI_BOT_LUN_UNKNOWN;
            xhciBotUnknown(bot, XHCI_BOT_WHY_MAX_LUN);
            return;
        }
        if (bot->MaxLun != XHCI_BOT_LUN_UNKNOWN && bot->MaxLun != lun) {
            bot->MaxLun = XHCI_BOT_LUN_UNKNOWN;
            xhciBotUnknown(bot, XHCI_BOT_WHY_MAX_LUN);
            return;
        }
        bot->MaxLun = lun;
        return;
    }
    xhciBotUnknown(bot, XHCI_BOT_WHY_OTHER);
}

VOID XhciBotOther(PXHCI_BOT bot)
{
    bot->Events++;
    xhciBotUnknown(bot, XHCI_BOT_WHY_OTHER);
}

ULONG XhciBotKeepable(const XHCI_BOT *bot)
{
    if (bot->State != XHCI_BOT_SYNC && bot->State != XHCI_BOT_IDLE) {
        return 0;
    }
    return bot->MaxLun == 0 ? 1UL : 0UL;
}

VOID XhciBotBuildCbw(UCHAR *cbw, ULONG tag, ULONG dataLength, ULONG dirIn,
                     const UCHAR *cdb, ULONG cdbLength)
{
    ULONG i;

    for (i = 0; i < XHCI_BOT_CBW_BYTES; i++) {
        cbw[i] = 0;
    }
    xhciBotPutLe32(cbw, XHCI_BOT_CBW_SIG);
    xhciBotPutLe32(cbw + 4, tag);
    xhciBotPutLe32(cbw + 8, dataLength);
    cbw[12] = dirIn ? 0x80 : 0x00;
    cbw[13] = 0;
    if (cdbLength < 1) {
        cdbLength = 1;
    }
    if (cdbLength > 16) {
        cdbLength = 16;
    }
    cbw[14] = (UCHAR)cdbLength;
    for (i = 0; i < cdbLength; i++) {
        cbw[15 + i] = cdb[i];
    }
}

ULONG XhciBotSameMedium(const XHCI_BOT_CAP *recorded,
                        const XHCI_BOT_CAP *now)
{
    if (recorded->Kind == XHCI_BOT_CAP_NONE ||
        recorded->Kind != now->Kind) {
        return 0;
    }
    if (recorded->Kind == XHCI_BOT_CAP_NO_MEDIUM) {
        return 1;
    }
    return (recorded->LastLbaLo == now->LastLbaLo &&
            recorded->LastLbaHi == now->LastLbaHi &&
            recorded->BlockLength == now->BlockLength) ? 1UL : 0UL;
}

ULONG XhciBotTurSense(ULONG key, ULONG asc, ULONG ascq)
{
    if (key == 0x06UL && asc == 0x29UL) {
        return XHCI_BOT_TUR_RETRY;
    }
    if (key == 0x02UL && asc == 0x04UL && ascq == 0x01UL) {
        return XHCI_BOT_TUR_BECOMING;
    }
    if (key == 0x02UL && asc == 0x3AUL) {
        return XHCI_BOT_TUR_NO_MEDIUM;
    }
    return XHCI_BOT_TUR_FAIL;
}

/* xhci_bot.h (record 20 section 4.3.4, M18): the owner's default of
 * 2026-11-13, Windows 2000's hibernation alone. */
ULONG XhciBotKeepDefault(ULONG isW2k)
{
    return isW2k ? XHCI_BOT_KEEP_HIBERNATE : 0UL;
}
