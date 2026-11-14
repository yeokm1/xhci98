/*
 * uas_iu.c - the pure core of xhciuas.sys (see uas_iu.h).
 *
 * IRQL: any; no function here takes a lock or touches the DDK.
 */

#include "uas_iu.h"
/* For its structure and numbers alone; its IOCTL codes need the DDK and are
 * not expanded here. */
#include "../xhci98_xfer.h"

/* UasTeleBucket and UasTeleDepthBucket index arrays the FDO sizes with the
 * bus's counts (uas.h): the two pairs must stay equal. */
XHCI_C_ASSERT(uas_tele_buckets, UAS_TELE_BUCKETS == XHCI98_TELE_BUCKETS);
XHCI_C_ASSERT(uas_tele_depths, UAS_TELE_DEPTHS == XHCI98_TELE_DEPTHS);

/* ------------------------------------------------------------------ */
/* Tags                                                               */
/* ------------------------------------------------------------------ */

VOID UasTagInit(PUAS_TAGS tags, ULONG count)
{
    if (count > UAS_MAX_TAGS) {
        count = UAS_MAX_TAGS;
    }
    tags->Count = count;
    tags->Next = 0;
    tags->Map = 0;
}

/*
 * The search starts after the tag handed out last, so a tag just freed is
 * the last to be reused: a device still finishing an aborted task under that
 * tag answers OVERLAPPED TAG ATTEMPTED to an immediate reuse.
 */
ULONG UasTagAlloc(PUAS_TAGS tags)
{
    ULONG i;
    ULONG index;

    for (i = 0; i < tags->Count; i++) {
        index = (tags->Next + i) % tags->Count;
        if ((tags->Map & (1UL << index)) == 0) {
            tags->Map |= 1UL << index;
            tags->Next = (index + 1) % tags->Count;
            return index + 1;
        }
    }
    return 0;
}

BOOLEAN UasTagFree(PUAS_TAGS tags, ULONG tag)
{
    if (tag == 0 || tag > tags->Count ||
        (tags->Map & (1UL << (tag - 1))) == 0) {
        return FALSE;
    }
    tags->Map &= ~(1UL << (tag - 1));
    return TRUE;
}

BOOLEAN UasTagBusy(const UAS_TAGS *tags, ULONG tag)
{
    if (tag == 0 || tag > tags->Count) {
        return FALSE;
    }
    return (BOOLEAN)((tags->Map & (1UL << (tag - 1))) != 0);
}

ULONG UasTagsInUse(const UAS_TAGS *tags)
{
    ULONG n;
    ULONG map;

    n = 0;
    for (map = tags->Map; map != 0; map &= map - 1) {
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* LUNs                                                               */
/* ------------------------------------------------------------------ */

BOOLEAN UasLunEncode(ULONG lun, PUCHAR out)
{
    ULONG i;

    for (i = 0; i < 8; i++) {
        out[i] = 0;
    }
    if (lun < 256) {
        out[1] = (UCHAR)lun;
        return TRUE;
    }
    if (lun < 0x4000) {
        out[0] = (UCHAR)(0x40 | (lun >> 8));
        out[1] = (UCHAR)(lun & 0xFF);
        return TRUE;
    }
    return FALSE;
}

/* Single-level LUNs only: bytes 2..7 must be zero, and peripheral
 * addressing must name bus 0. */
BOOLEAN UasLunDecode(const UCHAR *in, PULONG lun)
{
    ULONG i;

    for (i = 2; i < 8; i++) {
        if (in[i] != 0) {
            return FALSE;
        }
    }
    switch (in[0] >> 6) {
    case 0:
        if ((in[0] & 0x3F) != 0) {
            return FALSE;
        }
        *lun = in[1];
        return TRUE;
    case 1:
        *lun = ((ULONG)(in[0] & 0x3F) << 8) | in[1];
        return TRUE;
    default:
        return FALSE;
    }
}

/* ------------------------------------------------------------------ */
/* Information units                                                  */
/* ------------------------------------------------------------------ */

static VOID uasZero(PUCHAR p, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        p[i] = 0;
    }
}

static VOID uasPut16(PUCHAR p, ULONG v)
{
    p[0] = (UCHAR)((v >> 8) & 0xFF);
    p[1] = (UCHAR)(v & 0xFF);
}

static ULONG uasGet16(const UCHAR *p)
{
    return ((ULONG)p[0] << 8) | p[1];
}

/*
 * COMMAND IU: id, reserved, tag; byte 4 the task attribute in bits 2:0 and
 * the priority (0 here) in bits 6:3; byte 6 the additional CDB length in
 * dwords in bits 7:2 (0 here: a CDB of up to 16 bytes fits the fixed field);
 * bytes 8..15 the LUN; bytes 16..31 the CDB, zero-padded.
 */
ULONG UasIuBuildCommand(PUCHAR iu, ULONG size, ULONG tag, ULONG lun,
                        ULONG attribute, const UCHAR *cdb, ULONG cdbLength)
{
    ULONG i;

    if (size < UAS_IU_COMMAND_LENGTH || tag == 0 || tag > 0xFFFF ||
        cdbLength == 0 || cdbLength > 16 || attribute > 7) {
        return 0;
    }
    uasZero(iu, UAS_IU_COMMAND_LENGTH);
    iu[0] = UAS_IU_COMMAND;
    uasPut16(iu + 2, tag);
    iu[4] = (UCHAR)attribute;
    if (!UasLunEncode(lun, iu + 8)) {
        return 0;
    }
    for (i = 0; i < cdbLength; i++) {
        iu[16 + i] = cdb[i];
    }
    return UAS_IU_COMMAND_LENGTH;
}

/* TASK MANAGEMENT IU: id, reserved, tag, function, reserved, the tag of
 * the managed task, LUN. */
ULONG UasIuBuildTaskMgmt(PUCHAR iu, ULONG size, ULONG tag, ULONG function,
                         ULONG taskTag, ULONG lun)
{
    if (size < UAS_IU_TASK_MGMT_LENGTH || tag == 0 || tag > 0xFFFF ||
        function > 0xFF || taskTag > 0xFFFF) {
        return 0;
    }
    uasZero(iu, UAS_IU_TASK_MGMT_LENGTH);
    iu[0] = UAS_IU_TASK_MGMT;
    uasPut16(iu + 2, tag);
    iu[4] = (UCHAR)function;
    uasPut16(iu + 6, taskTag);
    if (!UasLunEncode(lun, iu + 8)) {
        return 0;
    }
    return UAS_IU_TASK_MGMT_LENGTH;
}

/*
 * The three IUs a device sends on the status pipe (SENSE, RESPONSE, and the
 * two READY forms). SENSE: status qualifier at 4..5, status at 6, sense
 * length at 14..15, sense data from 16 - a length beyond what arrived is cut
 * to what arrived, and beyond 252 to 252.
 */
ULONG UasIuParse(const UCHAR *buf, ULONG length, PUAS_IU_INFO info)
{
    ULONG sense;

    uasZero((PUCHAR)info, sizeof(*info));
    if (length < UAS_IU_READY_LENGTH) {
        return UAS_PARSE_SHORT;
    }
    info->Id = buf[0];
    info->Tag = uasGet16(buf + 2);
    switch (buf[0]) {
    case UAS_IU_SENSE:
        if (length < UAS_IU_SENSE_HEADER) {
            return UAS_PARSE_SHORT;
        }
        info->Qualifier = uasGet16(buf + 4);
        info->Status = buf[6];
        sense = uasGet16(buf + 14);
        if (sense > length - UAS_IU_SENSE_HEADER) {
            sense = length - UAS_IU_SENSE_HEADER;
        }
        if (sense > UAS_SENSE_MAX) {
            sense = UAS_SENSE_MAX;
        }
        info->SenseLength = sense;
        return UAS_PARSE_OK;
    case UAS_IU_RESPONSE:
        if (length < UAS_IU_RESPONSE_LENGTH) {
            return UAS_PARSE_SHORT;
        }
        info->ResponseInfo = ((ULONG)buf[4] << 16) | ((ULONG)buf[5] << 8) |
                             buf[6];
        info->ResponseCode = buf[7];
        return UAS_PARSE_OK;
    case UAS_IU_READ_READY:
    case UAS_IU_WRITE_READY:
        return UAS_PARSE_OK;
    default:
        return UAS_PARSE_UNKNOWN;
    }
}

/* ------------------------------------------------------------------ */
/* The configuration descriptor                                       */
/* ------------------------------------------------------------------ */

#define UAS_DESC_INTERFACE      0x04
#define UAS_DESC_ENDPOINT       0x05
#define UAS_DESC_SS_COMPANION   0x30

/*
 * The first alternate setting of class 08/06/62, and in it each endpoint's
 * Pipe Usage descriptor, which follows the endpoint (after its SuperSpeed
 * companion, when there is one). A companion's bmAttributes bits 4:0 are
 * MaxStreams, the exponent of the streams the endpoint supports. The walk
 * stops at the next interface descriptor. Only the first bNumEndpoints
 * endpoint descriptors are the setting's, as the bus opens them (task
 * 36.46, XhciPipeFindInterface): one past them, and the companion and Pipe
 * Usage descriptors that follow it, describe no pipe and are ignored.
 */
ULONG UasParseConfig(const UCHAR *cfg, ULONG length, PUAS_CONFIG_INFO info)
{
    ULONG at;
    ULONG len;
    ULONG type;
    ULONG found;
    ULONG endpoints;
    ULONG lastEp;
    ULONG lastPacket;
    ULONG lastStreams;
    ULONG haveEp;
    ULONG id;
    ULONG seen;
    ULONG exp;

    uasZero((PUCHAR)info, sizeof(*info));
    if (length < 9 || cfg[1] != 0x02) {
        return UAS_CFG_MALFORMED;
    }
    at = cfg[0];
    found = 0;
    endpoints = 0;
    haveEp = 0;
    lastEp = 0;
    lastPacket = 0;
    lastStreams = 0;
    seen = 0;
    while (at + 2 <= length) {
        len = cfg[at];
        type = cfg[at + 1];
        if (len < 2 || at + len > length) {
            return found ? UAS_CFG_MALFORMED : UAS_CFG_NO_INTERFACE;
        }
        if (type == UAS_DESC_INTERFACE && len >= 9) {
            if (found) {
                break;
            }
            if (cfg[at + 5] == UAS_CLASS && cfg[at + 6] == UAS_SUBCLASS &&
                cfg[at + 7] == UAS_PROTOCOL) {
                found = 1;
                info->InterfaceNumber = cfg[at + 2];
                info->AlternateSetting = cfg[at + 3];
                info->EndpointCount = cfg[at + 4];
            }
        } else if (found && type == UAS_DESC_ENDPOINT && len >= 7 &&
                   endpoints >= info->EndpointCount) {
            haveEp = 0;
        } else if (found && type == UAS_DESC_ENDPOINT && len >= 7) {
            endpoints++;
            haveEp = 1;
            lastEp = cfg[at + 2];
            lastPacket = ((ULONG)cfg[at + 5] << 8 | cfg[at + 4]) & 0x7FF;
            lastStreams = 0;
        } else if (found && haveEp && type == UAS_DESC_SS_COMPANION &&
                   len >= 6) {
            info->SuperSpeed = TRUE;
            exp = cfg[at + 3] & 0x1F;
            lastStreams = (exp == 0) ? 0 : (exp > 16 ? 0x10000UL : 1UL << exp);
        } else if (found && haveEp && type == UAS_DESC_PIPE_USAGE &&
                   len >= 4) {
            id = cfg[at + 2];
            if (id >= 1 && id <= UAS_PIPES && (seen & (1UL << id)) == 0) {
                seen |= 1UL << id;
                info->EndpointAddress[id - 1] = lastEp;
                info->MaxPacket[id - 1] = lastPacket;
                info->MaxStreams[id - 1] = lastStreams;
            }
            haveEp = 0;
        }
        at += len;
    }
    if (!found) {
        return UAS_CFG_NO_INTERFACE;
    }
    if (seen != 0x1E) {
        return UAS_CFG_BAD_PIPES;
    }
    /* Direction: command and data-out are OUT, status and data-in IN. */
    if ((info->EndpointAddress[UAS_PIPE_COMMAND - 1] & 0x80) != 0 ||
        (info->EndpointAddress[UAS_PIPE_STATUS - 1] & 0x80) == 0 ||
        (info->EndpointAddress[UAS_PIPE_DATA_IN - 1] & 0x80) == 0 ||
        (info->EndpointAddress[UAS_PIPE_DATA_OUT - 1] & 0x80) != 0) {
        return UAS_CFG_BAD_PIPES;
    }
    return UAS_CFG_OK;
}

/* ------------------------------------------------------------------ */
/* REPORT LUNS                                                        */
/* ------------------------------------------------------------------ */

ULONG UasParseReportLuns(const UCHAR *buf, ULONG length, PUCHAR luns,
                         ULONG max)
{
    ULONG listLength;
    ULONG at;
    ULONG n;
    ULONG lun;
    ULONG i;
    BOOLEAN dup;

    if (length < 8) {
        return 0;
    }
    listLength = ((ULONG)buf[0] << 24) | ((ULONG)buf[1] << 16) |
                 ((ULONG)buf[2] << 8) | buf[3];
    if (listLength > length - 8) {
        listLength = length - 8;
    }
    n = 0;
    for (at = 8; at + 8 <= 8 + listLength && n < max; at += 8) {
        if (!UasLunDecode(buf + at, &lun) || lun > 255) {
            continue;
        }
        dup = FALSE;
        for (i = 0; i < n; i++) {
            if (luns[i] == lun) {
                dup = TRUE;
            }
        }
        if (!dup) {
            luns[n++] = (UCHAR)lun;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Status and direction                                               */
/* ------------------------------------------------------------------ */

/*
 * GOOD with every byte moved is SUCCESS; GOOD short is DATA_OVERRUN, the
 * port-driver convention for an underrun, with the caller reporting the
 * bytes that moved; BUSY and TASK SET FULL are BUSY, which the class driver
 * retries; anything else (CHECK CONDITION above all) is ERROR, with the
 * caller adding the autosense flag when it copied sense.
 */
ULONG UasSrbStatus(ULONG scsiStatus, ULONG requested, ULONG transferred)
{
    switch (scsiStatus) {
    case UAS_SCSI_GOOD:
        return (transferred < requested) ? UAS_SRB_DATA_OVERRUN
                                         : UAS_SRB_SUCCESS;
    case UAS_SCSI_BUSY:
    case UAS_SCSI_TASK_SET_FULL:
        return UAS_SRB_BUSY;
    default:
        return UAS_SRB_ERROR;
    }
}

ULONG UasCdbDirection(UCHAR opcode)
{
    switch (opcode) {
    case 0x03:  /* REQUEST SENSE */
    case 0x08:  /* READ(6) */
    case 0x12:  /* INQUIRY */
    case 0x1A:  /* MODE SENSE(6) */
    case 0x23:  /* READ FORMAT CAPACITIES */
    case 0x25:  /* READ CAPACITY(10) */
    case 0x28:  /* READ(10) */
    case 0x3C:  /* READ BUFFER(10) */
    case 0x43:  /* READ TOC */
    case 0x46:  /* GET CONFIGURATION */
    case 0x4A:  /* GET EVENT STATUS NOTIFICATION */
    case 0x4D:  /* LOG SENSE */
    case 0x5A:  /* MODE SENSE(10) */
    case 0x5E:  /* PERSISTENT RESERVE IN */
    case 0x88:  /* READ(16) */
    case 0x9E:  /* SERVICE ACTION IN(16): READ CAPACITY(16) */
    case 0xA0:  /* REPORT LUNS */
    case 0xA2:  /* SECURITY PROTOCOL IN */
    case 0xA8:  /* READ(12) */
    case 0xBE:  /* READ CD */
        return UAS_DIR_IN;
    case 0x0A:  /* WRITE(6) */
    case 0x15:  /* MODE SELECT(6) */
    case 0x2A:  /* WRITE(10) */
    case 0x2E:  /* WRITE AND VERIFY(10) */
    case 0x3B:  /* WRITE BUFFER(10) */
    case 0x41:  /* WRITE SAME(10) */
    case 0x55:  /* MODE SELECT(10) */
    case 0x5F:  /* PERSISTENT RESERVE OUT */
    case 0x8A:  /* WRITE(16) */
    case 0x93:  /* WRITE SAME(16) */
    case 0xAA:  /* WRITE(12) */
    case 0xB5:  /* SECURITY PROTOCOL OUT */
        return UAS_DIR_OUT;
    /* 0x42 is not here: UNMAP (OUT) on a disk, READ SUB-CHANNEL (IN) on
     * an MMC drive, and the opcode alone cannot tell which. */
    default:
        return UAS_DIR_NONE;
    }
}

ULONG UasCdbDataDirection(const UCHAR *cdb, ULONG cdbLength)
{
    /* SAT: byte 2 bit 3, T_DIR, 1 from the device and 0 to it, in both
     * ATA PASS-THROUGH CDBs. */
    if ((cdb[0] == 0x85 && cdbLength >= 16) ||
        (cdb[0] == 0xA1 && cdbLength >= 12)) {
        return ((cdb[2] & 0x08) != 0) ? UAS_DIR_IN : UAS_DIR_OUT;
    }
    return UasCdbDirection(cdb[0]);
}

/* ------------------------------------------------------------------ */
/* Storage ids                                                        */
/* ------------------------------------------------------------------ */

typedef struct _UAS_OUT {
    char *Buf;
    ULONG Size;
    ULONG Used;
    BOOLEAN Overflow;
} UAS_OUT;

static VOID uasCh(UAS_OUT *o, char c)
{
    if (o->Used < o->Size) {
        o->Buf[o->Used] = c;
        o->Used++;
    } else {
        o->Overflow = TRUE;
    }
}

static VOID uasStr(UAS_OUT *o, const char *s)
{
    while (*s != 0) {
        uasCh(o, *s);
        s++;
    }
}

/* An id character: printable ASCII other than space, comma and backslash,
 * which become underscores (a comma ends an INF id, a backslash splits an
 * instance path). */
static char uasIdChar(UCHAR c)
{
    if (c <= 0x20 || c >= 0x7F || c == ',' || c == '\\') {
        return '_';
    }
    return (char)c;
}

/* A text character keeps spaces. */
static char uasTextChar(UCHAR c)
{
    if (c < 0x20 || c >= 0x7F) {
        return '_';
    }
    return (char)c;
}

/* The field without its trailing spaces (and NULs). */
static ULONG uasTrimmed(const UCHAR *p, ULONG n)
{
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == 0)) {
        n--;
    }
    return n;
}

static VOID uasField(UAS_OUT *o, const UCHAR *p, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        uasCh(o, uasIdChar(p[i]));
    }
}

static VOID uasDecimal(UAS_OUT *o, ULONG v)
{
    char digits[10];
    ULONG n;

    n = 0;
    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0 && n < 10);
    while (n > 0) {
        n--;
        uasCh(o, digits[n]);
    }
}

/*
 * Peripheral device type (INQUIRY byte 0 bits 4:0) to the type word in the
 * ids and the generic id the class INFs bind: Windows 2000 and later's
 * disk.inf and cdrom.inf by the bare generic names, NUSB's usbntmap.inf on
 * Windows 98 SE by the USBSTOR\ forms of them (USBSTOR\GenDisk,
 * USBSTOR\GenCdRom; roadmap-hcd.md decisions table, "Mass storage on Windows
 * 98 SE"). The reduced block command set (0x0E) is presented as a disk.
 */
static const char *uasTypeName(UCHAR type, const char **generic)
{
    switch (type & 0x1F) {
    case 0x00:
    case 0x0E:
        *generic = "GenDisk";
        return "Disk";
    case 0x01:
        *generic = "GenSequential";
        return "Sequential";
    case 0x04:
        *generic = "GenWorm";
        return "Worm";
    case 0x05:
        *generic = "GenCdRom";
        return "CdRom";
    case 0x07:
        *generic = "GenOptical";
        return "Optical";
    case 0x08:
        *generic = "GenChanger";
        return "Changer";
    default:
        *generic = NULL;
        return "Other";
    }
}

#define UAS_ENUMERATOR  "XHCIUAS\\"

ULONG UasBuildId(ULONG kind, const UCHAR *inquiry, ULONG inquiryLength,
                 const char *serial, ULONG lun, char *out, ULONG size)
{
    UAS_OUT o;
    const char *type;
    const char *generic;
    const UCHAR *ven;
    const UCHAR *prod;
    const UCHAR *rev;
    ULONG n;
    ULONG i;

    if (inquiryLength < UAS_INQUIRY_LENGTH || size == 0) {
        return 0;
    }
    o.Buf = out;
    o.Size = size;
    o.Used = 0;
    o.Overflow = FALSE;
    type = uasTypeName(inquiry[0], &generic);
    ven = inquiry + 8;
    prod = inquiry + 16;
    rev = inquiry + 32;

    switch (kind) {
    case UAS_ID_DEVICE:
        uasStr(&o, UAS_ENUMERATOR);
        uasStr(&o, type);
        uasStr(&o, "&Ven_");
        uasField(&o, ven, uasTrimmed(ven, 8));
        uasStr(&o, "&Prod_");
        uasField(&o, prod, uasTrimmed(prod, 16));
        uasStr(&o, "&Rev_");
        uasField(&o, rev, uasTrimmed(rev, 4));
        uasCh(&o, 0);
        break;

    case UAS_ID_HARDWARE:
        /* Most specific first; the padded fields keep their width. */
        uasStr(&o, UAS_ENUMERATOR);
        uasStr(&o, type);
        uasField(&o, ven, 8);
        uasField(&o, prod, 16);
        uasField(&o, rev, 4);
        uasCh(&o, 0);
        uasStr(&o, UAS_ENUMERATOR);
        uasStr(&o, type);
        uasField(&o, ven, 8);
        uasField(&o, prod, 16);
        uasCh(&o, 0);
        uasStr(&o, UAS_ENUMERATOR);
        uasStr(&o, type);
        uasField(&o, ven, 8);
        uasCh(&o, 0);
        if (generic != NULL) {
            uasStr(&o, "USBSTOR\\");
            uasStr(&o, generic);
            uasCh(&o, 0);
            uasStr(&o, generic);
            uasCh(&o, 0);
        }
        uasCh(&o, 0);
        break;

    case UAS_ID_COMPATIBLE:
        uasStr(&o, UAS_ENUMERATOR);
        uasStr(&o, type);
        uasCh(&o, 0);
        uasCh(&o, 0);
        break;

    case UAS_ID_INSTANCE:
        /* The serial when the device has one, else the LUN alone - the
         * PDO then reports UniqueID FALSE and PnP makes it unique. */
        if (serial != NULL && serial[0] != 0) {
            for (i = 0; serial[i] != 0 && i < 126; i++) {
                uasCh(&o, uasIdChar((UCHAR)serial[i]));
            }
            uasCh(&o, '&');
        }
        uasDecimal(&o, lun);
        uasCh(&o, 0);
        break;

    case UAS_ID_TEXT:
        n = uasTrimmed(ven, 8);
        for (i = 0; i < n; i++) {
            uasCh(&o, uasTextChar(ven[i]));
        }
        if (n != 0) {
            uasCh(&o, ' ');
        }
        n = uasTrimmed(prod, 16);
        for (i = 0; i < n; i++) {
            uasCh(&o, uasTextChar(prod[i]));
        }
        uasStr(&o, " UAS Device");
        uasCh(&o, 0);
        break;

    default:
        return 0;
    }
    return o.Overflow ? 0 : o.Used;
}

/* ------------------------------------------------------------------ */
/* Telemetry (task 36.21)                                             */
/* ------------------------------------------------------------------ */

ULONG UasTeleBucket(ULONG bytes)
{
    ULONG limit;
    ULONG i;

    limit = 0x1000UL;
    for (i = 0; i < UAS_TELE_BUCKETS - 1; i++) {
        if (bytes <= limit) {
            return i;
        }
        limit = (i < 2) ? limit * 4UL : limit * 2UL;
    }
    return UAS_TELE_BUCKETS - 1;
}

ULONG UasTeleDepthBucket(ULONG active)
{
    ULONG limit;
    ULONG i;

    limit = 1;
    for (i = 0; i < UAS_TELE_DEPTHS - 1; i++) {
        if (active <= limit) {
            return i;
        }
        limit *= 2UL;
    }
    return UAS_TELE_DEPTHS - 1;
}

VOID UasTeleAdd64(PULONG lo, PULONG hi, ULONG v)
{
    ULONG before;

    before = *lo;
    *lo = before + v;
    if (*lo < before) {
        *hi += 1UL;
    }
}

/* ------------------------------------------------------------------ */
/* The transfer policy (task 36.22)                                   */
/* ------------------------------------------------------------------ */

ULONG UasPolicyValue(ULONG sent, const XHCI98_TRANSFER_POLICY *reply,
                     PULONG source)
{
    ULONG i;

    if (sent == UAS_POLICY_SENT_UNSUPPORTED) {
        *source = XHCI98_TELE_SRC_UNSUPPORTED;
        return UAS_POLICY_FALLBACK_BYTES;
    }
    if (sent == UAS_POLICY_SENT_REFUSED) {
        *source = XHCI98_TELE_SRC_MALFORMED;
        return UAS_POLICY_FALLBACK_BYTES;
    }
    if (sent != UAS_POLICY_SENT_OK || reply == NULL) {
        *source = XHCI98_TELE_SRC_TRANSPORT;
        return UAS_POLICY_FALLBACK_BYTES;
    }
    *source = XHCI98_TELE_SRC_MALFORMED;
    if (reply->Signature != XHCI98_POLICY_SIGNATURE ||
        reply->Version != XHCI98_POLICY_VERSION ||
        reply->Size != sizeof(XHCI98_TRANSFER_POLICY) ||
        reply->Status != XHCI98_POLICY_SUCCESS ||
        reply->MinBytes != XHCI98_POLICY_MIN_BYTES ||
        reply->MaxBytes != XHCI98_POLICY_MAX_BYTES ||
        reply->StepBytes != XHCI98_POLICY_STEP_BYTES ||
        reply->CommandBytes < XHCI98_POLICY_MIN_BYTES ||
        reply->CommandBytes > XHCI98_POLICY_MAX_BYTES ||
        (reply->CommandBytes % XHCI98_POLICY_STEP_BYTES) != 0) {
        return UAS_POLICY_FALLBACK_BYTES;
    }
    for (i = 0; i < 4; i++) {
        if (reply->Reserved[i] != 0) {
            return UAS_POLICY_FALLBACK_BYTES;
        }
    }
    *source = XHCI98_TELE_SRC_BUS;
    return reply->CommandBytes;
}

ULONG UasDeviceLimit(ULONG busBytes, ULONG busSource, ULONG found,
                     ULONG type, ULONG length, ULONG value, PULONG source,
                     PULONG why)
{
    ULONG bytes;

    *source = busSource;
    if (!found) {
        *why = UAS_DEVLIM_ABSENT;
        return busBytes;
    }
    if (type != UAS_DEVLIM_REG_DWORD || length != 4UL) {
        *why = UAS_DEVLIM_TYPE;
        return busBytes;
    }
    if (value < UAS_DEVLIM_MIN_BYTES || value > UAS_DEVLIM_MAX_BYTES) {
        *why = UAS_DEVLIM_RANGE;
        return busBytes;
    }
    if (busSource != XHCI98_TELE_SRC_BUS) {
        *why = UAS_DEVLIM_NO_BUS;
        return busBytes;
    }
    /* Nearest 4 KiB, ties up; inside the range no overflow is possible
     * and the result is 64 KiB to 2 MiB already (uas_iu.h). */
    bytes = (value + XHCI98_POLICY_STEP_BYTES / 2UL) &
            ~(XHCI98_POLICY_STEP_BYTES - 1UL);
    if (bytes < XHCI98_POLICY_MIN_BYTES) {
        bytes = XHCI98_POLICY_MIN_BYTES;
    }
    if (bytes > XHCI98_POLICY_MAX_BYTES) {
        bytes = XHCI98_POLICY_MAX_BYTES;
    }
    *why = UAS_DEVLIM_USED;
    *source = XHCI98_TELE_SRC_DEVICE;
    return bytes;
}

/* ------------------------------------------------------------------ */
/* Task 45.23: the wake check and the medium check                    */
/* ------------------------------------------------------------------ */

ULONG UasSenseClass(const UCHAR *sense, ULONG length)
{
    ULONG code;
    ULONG key;
    ULONG asc;

    if (sense == NULL || length < 3) {
        return UAS_SENSE_OTHER;
    }
    code = sense[0] & 0x7FUL;
    if (code == 0x70UL || code == 0x71UL) {
        if (length < 13) {
            return UAS_SENSE_OTHER;
        }
        key = sense[2] & 0x0FUL;
        asc = sense[12];
    } else if (code == 0x72UL || code == 0x73UL) {
        key = sense[1] & 0x0FUL;
        asc = sense[2];
    } else {
        return UAS_SENSE_OTHER;
    }
    switch (key) {
    case 0x06UL:    /* UNIT ATTENTION */
        return (asc == 0x28UL) ? UAS_SENSE_UA_MEDIUM : UAS_SENSE_UA;
    case 0x02UL:    /* NOT READY */
        if (asc == 0x3AUL) {
            return UAS_SENSE_NO_MEDIUM;
        }
        return (asc == 0x04UL) ? UAS_SENSE_BECOMING : UAS_SENSE_OTHER;
    case 0x05UL:    /* ILLEGAL REQUEST */
        return UAS_SENSE_ILLEGAL;
    default:
        return UAS_SENSE_OTHER;
    }
}

ULONG UasVpdListComplete(const UCHAR *page0, ULONG length)
{
    if (page0 == NULL || length < 4 || page0[1] != 0x00) {
        return 0;
    }
    return (uasGet16(page0 + 2) <= length - 4) ? 1UL : 0UL;
}

ULONG UasVpdListHas(const UCHAR *page0, ULONG length, UCHAR page)
{
    ULONG n;
    ULONG i;

    if (!UasVpdListComplete(page0, length)) {
        return 0;
    }
    n = uasGet16(page0 + 2);
    for (i = 0; i < n; i++) {
        if (page0[4 + i] == page) {
            return 1;
        }
    }
    return 0;
}

static ULONG uasSame(const UCHAR *a, const UCHAR *b, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

ULONG UasIdMatch(const UAS_LUN_ID *was, const UAS_LUN_ID *now)
{
    if (was == NULL || now == NULL || !was->Read || !now->Read ||
        now->MediumChanged || was->Removable != now->Removable) {
        return 0;
    }
    if (was->Removable) {
        return (was->CapState == UAS_ID_CAP_NO_MEDIUM &&
                now->CapState == UAS_ID_CAP_NO_MEDIUM) ? 1UL : 0UL;
    }
    if (was->SerialLength == 0 && was->DevIdLength == 0) {
        return 0;
    }
    if (was->SerialLength > UAS_ID_SERIAL_MAX ||
        was->DevIdLength > UAS_ID_DEVID_MAX ||
        was->SerialLength != now->SerialLength ||
        was->DevIdLength != now->DevIdLength ||
        !uasSame(was->Serial, now->Serial, was->SerialLength) ||
        !uasSame(was->DevId, now->DevId, was->DevIdLength)) {
        return 0;
    }
    if (was->CapState != UAS_ID_CAP_VALID ||
        now->CapState != UAS_ID_CAP_VALID) {
        return 0;
    }
    return uasSame(was->Cap, now->Cap, sizeof(was->Cap));
}

ULONG UasWakeDecide(ULONG busKnown, ULONG withdrawOk, ULONG declareTried,
                    ULONG queryOk, ULONG state, ULONG epoch, ULONG verified,
                    ULONG pastDeadline)
{
    if (!busKnown) {
        return UAS_WAKE_RELEASE;
    }
    if (withdrawOk && !declareTried) {
        return UAS_WAKE_RELEASE;
    }
    if (!queryOk || state == XHCI98_RESUME_FAILED) {
        return UAS_WAKE_FAIL;
    }
    if (state == XHCI98_RESUME_PENDING) {
        return pastDeadline ? UAS_WAKE_FAIL : UAS_WAKE_WAIT;
    }
    if (state != XHCI98_RESUME_RUNNING) {
        return UAS_WAKE_FAIL;
    }
    return (epoch == verified) ? UAS_WAKE_SAME : UAS_WAKE_CHECK;
}

/* ------------------------------------------------------------------ */
/* Task 45.52: pass-through and SMART (design record 13 section 16)   */
/* ------------------------------------------------------------------ */

static ULONG uasRd16(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

static ULONG uasRd32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static VOID uasWr32(PUCHAR p, ULONG v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
    p[2] = (UCHAR)(v >> 16);
    p[3] = (UCHAR)(v >> 24);
}

static VOID uasBytes(PUCHAR dst, const UCHAR *src, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

static VOID uasClear(PUCHAR dst, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        dst[i] = 0;
    }
}

/* offset + length lies within limit, with no sum formed first. */
static ULONG uasFits(ULONG offset, ULONG length, ULONG limit)
{
    return offset <= limit && length <= limit - offset;
}

ULONG UasPtHeaderSize(ULONG kind, ULONG layout)
{
    if (layout != UAS_PT_LAYOUT_32 && layout != UAS_PT_LAYOUT_64) {
        return 0;
    }
    switch (kind) {
    case UAS_PT_SCSI:
    case UAS_PT_SCSI_DIRECT:
        return (layout == UAS_PT_LAYOUT_64) ? 56 : 44;
    case UAS_PT_ATA:
    case UAS_PT_ATA_DIRECT:
        return (layout == UAS_PT_LAYOUT_64) ? 48 : 40;
    default:
        return 0;
    }
}

ULONG UasPtTimeout(ULONG seconds)
{
    if (seconds == 0) {
        return UAS_PT_TIMEOUT_DEFAULT;
    }
    return (seconds > UAS_PT_TIMEOUT_MAX) ? UAS_PT_TIMEOUT_MAX : seconds;
}

ULONG UasPtMultiTarget(UCHAR opcode)
{
    /* COPY, COMPARE, COPY AND VERIFY, and every service action of 83h
     * (EXTENDED COPY, POPULATE TOKEN, WRITE USING TOKEN): SPC-4. */
    return opcode == 0x18 || opcode == 0x39 || opcode == 0x3A ||
           opcode == 0x83;
}

/* A 28-bit command whose count ACS marks not applicable and which moves
 * one 512-byte block: IDENTIFY DEVICE, IDENTIFY PACKET DEVICE, SMART READ
 * DATA and READ ATTRIBUTE THRESHOLDS. */
static ULONG uasAtaCountNa(const UCHAR *cur)
{
    return cur[6] == 0xEC || cur[6] == 0xA1 ||
           (cur[6] == 0xB0 && (cur[0] == 0xD0 || cur[0] == 0xD1));
}

/*
 * SAT-3, ATA PASS-THROUGH (16): byte 1 PROTOCOL (4:1) and EXTEND (0);
 * byte 2 CK_COND (5), T_DIR (3), BYTE_BLOCK (2), T_LENGTH (1:0); then
 * FEATURES, COUNT, LBA in high/low byte pairs, DEVICE, COMMAND, CONTROL.
 * With data, T_LENGTH 2 and BYTE_BLOCK 1: the length is the count field
 * in 512-byte blocks, so the count must agree with the buffer.
 */
ULONG UasAtaCdb(ULONG flags, const UCHAR *prev, const UCHAR *cur,
                ULONG length, ULONG ckCond, PUCHAR cdb)
{
    ULONG ext;
    ULONG data;
    ULONG count;
    ULONG protocol;

    ext = (flags & UAS_ATA_48BIT) != 0;
    data = length != 0;
    count = (ULONG)cur[1] | (ext ? ((ULONG)prev[1] << 8) : 0);
    if (data) {
        if ((length & 511) != 0) {
            return UAS_PT_INVALID;
        }
        if (!ext && uasAtaCountNa(cur)) {
            /* One block, whatever the count register held. */
            if (length != 512) {
                return UAS_PT_INVALID;
            }
            count = 1;
        } else if (count * 512UL != length) {
            return UAS_PT_INVALID;
        }
        if ((flags & UAS_ATA_USE_DMA) != 0) {
            protocol = 6;
        } else {
            protocol = ((flags & UAS_ATA_DATA_IN) != 0) ? 4 : 5;
        }
    } else {
        protocol = 3;
    }
    uasClear(cdb, 16);
    cdb[0] = 0x85;
    cdb[1] = (UCHAR)((protocol << 1) | (ext ? 1 : 0));
    cdb[2] = (UCHAR)(((!data && ckCond) ? 0x20 : 0) |
                     (((flags & UAS_ATA_DATA_IN) != 0 && data) ? 0x08 : 0) |
                     (data ? 0x06 : 0));
    cdb[3] = ext ? prev[0] : 0;
    cdb[4] = cur[0];
    cdb[5] = (UCHAR)(ext ? (count >> 8) : 0);
    cdb[6] = (UCHAR)count;
    cdb[7] = ext ? prev[2] : 0;
    cdb[8] = cur[2];
    cdb[9] = ext ? prev[3] : 0;
    cdb[10] = cur[3];
    cdb[11] = ext ? prev[4] : 0;
    cdb[12] = cur[4];
    cdb[13] = cur[5];
    cdb[14] = cur[6];
    return UAS_PT_OK;
}

ULONG UasPtPlan(ULONG kind, ULONG layout, const UCHAR *buf, ULONG inLength,
                ULONG outLength, ULONG maxTransfer, PUAS_PT_PLAN plan)
{
    ULONG hs;
    ULONG at;
    ULONG length;
    ULONG limit;
    ULONG dataIn;
    ULONG flags;
    ULONG direct;
    ULONG timeout;

    uasClear((PUCHAR)plan, sizeof(*plan));
    hs = UasPtHeaderSize(kind, layout);
    if (hs == 0 || inLength < hs || uasRd16(buf) != hs) {
        return UAS_PT_INVALID;
    }
    if (outLength < hs) {
        return UAS_PT_TOO_SMALL;
    }
    plan->Kind = kind;
    plan->Layout = layout;
    plan->HeaderSize = hs;
    at = (layout == UAS_PT_LAYOUT_64) ? 24 : 20;
    plan->DataPtrLo = uasRd32(buf + at);
    plan->DataPtrHi = (layout == UAS_PT_LAYOUT_64) ? uasRd32(buf + at + 4)
                                                   : 0;
    direct = kind == UAS_PT_SCSI_DIRECT || kind == UAS_PT_ATA_DIRECT;

    if (kind == UAS_PT_SCSI || kind == UAS_PT_SCSI_DIRECT) {
        plan->CdbLength = buf[6];
        plan->SenseLength = buf[7];
        dataIn = buf[8];
        length = uasRd32(buf + 12);
        timeout = uasRd32(buf + 16);
        plan->SenseOffset = uasRd32(buf + ((layout == UAS_PT_LAYOUT_64) ? 32
                                                                       : 24));
        if (plan->CdbLength < 1 || plan->CdbLength > 16 || dataIn > 2 ||
            plan->SenseLength > UAS_PT_SENSE_MAX || length > maxTransfer) {
            return UAS_PT_INVALID;
        }
        uasBytes(plan->Cdb, buf + ((layout == UAS_PT_LAYOUT_64) ? 36 : 28),
                 plan->CdbLength);
        if (UasPtMultiTarget(plan->Cdb[0])) {
            return UAS_PT_INVALID;
        }
        if (length == 0) {
            plan->Dir = UAS_DIR_NONE;
        } else if (dataIn == 1) {
            plan->Dir = UAS_DIR_IN;
        } else if (dataIn == 0) {
            plan->Dir = UAS_DIR_OUT;
        } else {
            plan->Dir = UasCdbDataDirection(plan->Cdb, plan->CdbLength);
            if (plan->Dir == UAS_DIR_NONE) {
                return UAS_PT_INVALID;
            }
        }
        /* A SAT CDB is whole or refused: the IU pads a short one with
         * zeros into a command the device would run. Asking for the
         * registers with data is refused (16.8), at any length. */
        if ((plan->Cdb[0] == 0x85 && plan->CdbLength != 16) ||
            (plan->Cdb[0] == 0xA1 && plan->CdbLength < 12)) {
            return UAS_PT_INVALID;
        }
        if (length != 0 && (plan->Cdb[2] & 0x20) != 0 &&
            (plan->Cdb[0] == 0x85 || plan->Cdb[0] == 0xA1)) {
            return UAS_PT_INVALID;
        }
    } else {
        flags = uasRd16(buf + 2);
        length = uasRd32(buf + 8);
        timeout = uasRd32(buf + 12);
        uasBytes(plan->Prev, buf + ((layout == UAS_PT_LAYOUT_64) ? 32 : 24),
                 8);
        uasBytes(plan->Cur, buf + ((layout == UAS_PT_LAYOUT_64) ? 40 : 32), 8);
        if ((flags & ~0x1FUL) != 0 ||
            ((flags & UAS_ATA_DATA_IN) != 0 &&
             (flags & UAS_ATA_DATA_OUT) != 0) ||
            length > maxTransfer ||
            (length != 0) !=
                ((flags & (UAS_ATA_DATA_IN | UAS_ATA_DATA_OUT)) != 0)) {
            return UAS_PT_INVALID;
        }
        if (UasAtaCdb(flags, plan->Prev, plan->Cur, length, 1, plan->Cdb) !=
            UAS_PT_OK) {
            return UAS_PT_INVALID;
        }
        plan->CdbLength = 16;
        plan->Extend = (flags & UAS_ATA_48BIT) != 0;
        if (length == 0) {
            plan->Dir = UAS_DIR_NONE;
        } else {
            plan->Dir = ((flags & UAS_ATA_DATA_IN) != 0) ? UAS_DIR_IN
                                                          : UAS_DIR_OUT;
        }
    }
    plan->DataLength = length;
    plan->Timeout = UasPtTimeout(timeout);

    if (length != 0) {
        if (direct) {
            if (plan->DataPtrLo == 0 && plan->DataPtrHi == 0) {
                return UAS_PT_INVALID;
            }
        } else {
            if (plan->DataPtrHi != 0) {
                return UAS_PT_INVALID;
            }
            plan->DataOffset = plan->DataPtrLo;
            limit = (plan->Dir == UAS_DIR_IN) ? outLength : inLength;
            if (plan->DataOffset < hs ||
                !uasFits(plan->DataOffset, length, limit)) {
                return UAS_PT_INVALID;
            }
        }
    }
    if (plan->SenseLength != 0) {
        if (plan->SenseOffset < hs ||
            !uasFits(plan->SenseOffset, plan->SenseLength, outLength)) {
            return UAS_PT_INVALID;
        }
        if (length != 0 && !direct &&
            plan->SenseOffset < plan->DataOffset + length &&
            plan->DataOffset < plan->SenseOffset + plan->SenseLength) {
            return UAS_PT_INVALID;
        }
    }
    return UAS_PT_OK;
}

static ULONG uasSenseKey(const UCHAR *sense, ULONG length)
{
    if (length >= 3 && (sense[0] & 0x7E) == 0x70) {
        return sense[2] & 0x0F;
    }
    if (length >= 2 && (sense[0] & 0x7E) == 0x72) {
        return sense[1] & 0x0F;
    }
    return 0;
}

ULONG UasAtaRegsFromSense(const UCHAR *sense, ULONG length, PUCHAR prev,
                          PUCHAR cur)
{
    ULONG end;
    ULONG p;
    ULONG dlen;
    const UCHAR *d;

    uasClear(prev, 8);
    uasClear(cur, 8);
    if (sense == NULL || length < 8) {
        return UAS_ATAREG_NONE;
    }
    if ((sense[0] & 0x7F) == 0x72) {
        /* The ATA Status Return descriptor (SAT-3): EXTEND, ERROR, then
         * COUNT and LBA as high/low pairs, DEVICE, STATUS. */
        end = 8 + (ULONG)sense[7];
        if (end > length) {
            end = length;
        }
        for (p = 8; p + 2 <= end; p += 2 + dlen) {
            dlen = sense[p + 1];
            if (sense[p] == 0x09 && dlen >= 12 && p + 14 <= end) {
                d = sense + p;
                cur[0] = d[3];
                cur[1] = d[5];
                cur[2] = d[7];
                cur[3] = d[9];
                cur[4] = d[11];
                cur[5] = d[12];
                cur[6] = d[13];
                if ((d[2] & 1) != 0) {
                    prev[1] = d[4];
                    prev[2] = d[6];
                    prev[3] = d[8];
                    prev[4] = d[10];
                }
                return UAS_ATAREG_OK;
            }
        }
        return UAS_ATAREG_NONE;
    }
    if ((sense[0] & 0x7F) == 0x70) {
        /* Fixed format (SAT-3): ERROR, STATUS, DEVICE, COUNT 7:0 in
         * INFORMATION; EXTEND, the upper-non-zero flags and LBA 23:0 in
         * COMMAND-SPECIFIC INFORMATION; ASC/ASCQ 00h/1Dh. */
        if (length < 14 || sense[12] != 0x00 || sense[13] != 0x1D) {
            return UAS_ATAREG_NONE;
        }
        cur[0] = sense[3];
        cur[6] = sense[4];
        cur[5] = sense[5];
        cur[1] = sense[6];
        cur[2] = sense[9];
        cur[3] = sense[10];
        cur[4] = sense[11];
        return ((sense[8] & 0x60) != 0) ? UAS_ATAREG_PARTIAL : UAS_ATAREG_OK;
    }
    return UAS_ATAREG_NONE;
}

/* A request the drive answered: done, short, or ended with a SCSI status. */
static ULONG uasAnswered(ULONG srbStatus, ULONG scsiStatus)
{
    if (srbStatus == UAS_SRB_SUCCESS || srbStatus == UAS_SRB_DATA_OVERRUN) {
        return 1;
    }
    return (srbStatus == UAS_SRB_ERROR || srbStatus == UAS_SRB_BUSY) &&
           scsiStatus != UAS_SCSI_GOOD;
}

static ULONG uasTransportResult(ULONG srbStatus)
{
    switch (srbStatus) {
    case UAS_SRB_TIMEOUT:
        return UAS_PTR_TIMEOUT;
    case UAS_SRB_NO_DEVICE:
        return UAS_PTR_NO_DEVICE;
    case UAS_SRB_BUSY:
        return UAS_PTR_BUSY;
    case UAS_SRB_INVALID_REQUEST:
        return UAS_PTR_INVALID;
    default:
        return UAS_PTR_IO_ERROR;
    }
}

static ULONG uasBusyStatus(ULONG scsiStatus)
{
    return scsiStatus == UAS_SCSI_BUSY || scsiStatus == UAS_SCSI_TASK_SET_FULL;
}

ULONG UasPtResult(const UAS_PT_PLAN *plan, PUCHAR buf, ULONG outLength,
                  ULONG srbStatus, ULONG scsiStatus, ULONG moved,
                  const UCHAR *sense, ULONG senseLength,
                  PULONG information)
{
    ULONG info;
    ULONG sl;
    ULONG r;
    ULONG at;
    UCHAR prev[8];
    UCHAR cur[8];

    *information = 0;
    if (!uasAnswered(srbStatus, scsiStatus)) {
        return uasTransportResult(srbStatus);
    }
    if (moved > plan->DataLength) {
        moved = plan->DataLength;
    }
    if (sense == NULL) {
        senseLength = 0;
    }
    info = plan->HeaderSize;
    if (plan->Kind == UAS_PT_SCSI || plan->Kind == UAS_PT_SCSI_DIRECT) {
        sl = (senseLength < plan->SenseLength) ? senseLength
                                               : plan->SenseLength;
        buf[2] = (UCHAR)scsiStatus;
        if (sl != 0) {
            uasBytes(buf + plan->SenseOffset, sense, sl);
            if (plan->SenseOffset + sl > info) {
                info = plan->SenseOffset + sl;
            }
        }
        buf[7] = (UCHAR)sl;
        uasWr32(buf + 12, moved);
        if (plan->Kind == UAS_PT_SCSI && plan->Dir == UAS_DIR_IN &&
            moved != 0 && plan->DataOffset + moved > info) {
            info = plan->DataOffset + moved;
        }
    } else {
        /* ATA_PASS_THROUGH_EX has no SCSI status to carry these. */
        if (uasBusyStatus(scsiStatus)) {
            return UAS_PTR_BUSY;
        }
        if (scsiStatus == UAS_SCSI_GOOD) {
            /* A non-data command asked for its registers (CK_COND); GOOD
             * brought none, and an echo of the sent ones would read as the
             * drive's answer (SMART RETURN STATUS's 4Fh/C2h). */
            if (plan->DataLength == 0) {
                return UAS_PTR_IO_ERROR;
            }
            /* Written by this driver, not read from the drive. */
            uasBytes(prev, plan->Prev, 8);
            uasBytes(cur, plan->Cur, 8);
            cur[0] = 0x00;
            cur[6] = 0x50;
        } else {
            r = UasAtaRegsFromSense(sense, senseLength, prev, cur);
            if (r == UAS_ATAREG_PARTIAL && plan->Extend) {
                return UAS_PTR_IO_ERROR;
            }
            if (r == UAS_ATAREG_NONE) {
                uasBytes(prev, plan->Prev, 8);
                uasBytes(cur, plan->Cur, 8);
                cur[0] = 0x04;
                cur[6] = 0x51;
            }
        }
        cur[7] = 0;
        prev[7] = 0;
        at = (plan->Layout == UAS_PT_LAYOUT_64) ? 32 : 24;
        uasBytes(buf + at, prev, 8);
        uasBytes(buf + at + 8, cur, 8);
        uasWr32(buf + 8, moved);
        if (plan->Kind == UAS_PT_ATA && plan->Dir == UAS_DIR_IN &&
            moved != 0 && plan->DataOffset + moved > info) {
            info = plan->DataOffset + moved;
        }
    }
    if (info > outLength) {
        info = outLength;
    }
    *information = info;
    return UAS_PTR_SUCCESS;
}

ULONG UasSiocCheck(const UCHAR *buf, ULONG inLength, ULONG outLength,
                   PULONG control, PULONG timeout, PULONG capacity)
{
    static const UCHAR signature[8] = { 'S', 'C', 'S', 'I', 'D', 'I', 'S',
                                        'K' };
    ULONG room;
    ULONG i;

    *control = 0;
    *timeout = 0;
    *capacity = 0;
    if (inLength < UAS_SIOC_LENGTH) {
        return UAS_PT_UNKNOWN;
    }
    for (i = 0; i < 8; i++) {
        if (buf[4 + i] != signature[i]) {
            return UAS_PT_UNKNOWN;
        }
    }
    *control = uasRd32(buf + 16);
    if (*control < UAS_SIOC_SMART_VERSION || *control > UAS_SIOC_WRITE_LOG) {
        return UAS_PT_UNKNOWN;
    }
    if (outLength < UAS_SIOC_LENGTH) {
        return UAS_PT_TOO_SMALL;
    }
    room = ((inLength > outLength) ? inLength : outLength) - UAS_SIOC_LENGTH;
    if (uasRd32(buf) != UAS_SIOC_LENGTH || uasRd32(buf + 24) > room) {
        return UAS_PT_INVALID;
    }
    *timeout = uasRd32(buf + 12);
    *capacity = uasRd32(buf + 24);
    return UAS_PT_OK;
}

VOID UasSmartVersion(PUCHAR out)
{
    uasClear(out, UAS_SMART_VERSION_LENGTH);
    out[0] = 1;     /* bVersion */
    out[1] = 1;     /* bRevision */
    out[3] = 1;     /* bIDEDeviceMap */
    out[4] = 1 | 4; /* CAP_ATA_ID_CMD | CAP_SMART_CMD */
}

ULONG UasSmartPlan(ULONG control, const UCHAR *payload, ULONG payloadIn,
                   ULONG replyRoom, PUAS_SMART_PLAN plan)
{
    static const UCHAR none[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    const UCHAR *regs;
    UCHAR feature;
    ULONG smart;
    ULONG count;

    uasClear((PUCHAR)plan, sizeof(*plan));
    plan->Control = control;
    if (payloadIn < UAS_SMART_IN_LENGTH) {
        return UAS_SMART_INVALID_BUFFER;
    }
    /* SENDCMDINPARAMS: cBufferSize, then IDEREGS - Features, SectorCount,
     * SectorNumber, CylLow, CylHigh, DriveHead, Command, Reserved. */
    regs = payload + 4;
    count = 0;
    smart = 1;
    switch (control) {
    case UAS_SIOC_IDENTIFY:
        if (regs[6] != 0xEC) {
            return UAS_SMART_INVALID_COMMAND;
        }
        smart = 0;
        count = 1;
        break;
    case UAS_SIOC_READ_ATTRIBS:
        feature = 0xD0;
        count = 1;
        break;
    case UAS_SIOC_READ_THRESHOLDS:
        feature = 0xD1;
        count = 1;
        break;
    case UAS_SIOC_READ_LOG:
        feature = 0xD5;
        count = regs[1];
        if (count == 0) {
            return UAS_SMART_INVALID_COMMAND;
        }
        break;
    case UAS_SIOC_ENABLE_SMART:
        feature = 0xD8;
        break;
    case UAS_SIOC_DISABLE_SMART:
        feature = 0xD9;
        break;
    case UAS_SIOC_RETURN_STATUS:
        feature = 0xDA;
        plan->ReturnRegs = 1;
        break;
    case UAS_SIOC_AUTOSAVE:
        feature = 0xD2;
        break;
    case UAS_SIOC_SAVE_ATTRIBS:
        feature = 0xD3;
        break;
    case UAS_SIOC_OFFLINE_DIAGS:
        feature = 0xD4;
        break;
    case UAS_SIOC_AUTO_OFFLINE:
        feature = 0xDB;
        break;
    default:
        /* SMART_VERSION is answered by the caller; WRITE LOG is refused. */
        return UAS_SMART_INVALID_COMMAND;
    }
    if (smart && (regs[6] != 0xB0 || regs[0] != feature)) {
        return UAS_SMART_INVALID_COMMAND;
    }
    plan->Cur[0] = regs[0];
    plan->Cur[1] = (count != 0) ? (UCHAR)count : regs[1];
    plan->Cur[2] = regs[2];
    plan->Cur[3] = smart ? 0x4F : regs[3];
    plan->Cur[4] = smart ? 0xC2 : regs[4];
    plan->Cur[5] = regs[5];
    plan->Cur[6] = regs[6];
    plan->DataLength = count * 512UL;
    plan->OutLength = UAS_SMART_OUT_HEADER + plan->DataLength +
                      (plan->ReturnRegs ? 8 : 0);
    if (replyRoom < plan->OutLength) {
        return UAS_SMART_INVALID_BUFFER;
    }
    if (UasAtaCdb((count != 0) ? UAS_ATA_DATA_IN : 0, none, plan->Cur,
                  plan->DataLength, count == 0, plan->Cdb) != UAS_PT_OK) {
        return UAS_SMART_INVALID_COMMAND;
    }
    return UAS_SMART_NO_ERROR;
}

ULONG UasSmartResult(const UAS_SMART_PLAN *plan, PUCHAR out, ULONG room,
                     ULONG srbStatus, ULONG scsiStatus, ULONG moved,
                     const UCHAR *data, const UCHAR *sense,
                     ULONG senseLength, PULONG outBytes,
                     PULONG driverError)
{
    UCHAR prev[8];
    UCHAR cur[8];
    ULONG result;
    ULONG r;
    ULONG size;
    UCHAR ideError;

    *outBytes = 0;
    *driverError = UAS_SMART_NO_ERROR;
    ideError = 0;
    size = 0;
    if (sense == NULL) {
        senseLength = 0;
    }
    if (!uasAnswered(srbStatus, scsiStatus)) {
        result = uasTransportResult(srbStatus);
    } else if (uasBusyStatus(scsiStatus)) {
        result = UAS_PTR_BUSY;
    } else if (scsiStatus == UAS_SCSI_GOOD) {
        /* A data command's success; a register command that came back
         * GOOD returned no registers, and RETURN STATUS has no answer
         * without them. */
        if (plan->ReturnRegs || moved < plan->DataLength) {
            *driverError = UAS_SMART_IDE_ERROR;
            ideError = 0x04;
            result = UAS_PTR_IO_ERROR;
        } else {
            size = plan->DataLength;
            result = UAS_PTR_SUCCESS;
        }
    } else {
        r = UasAtaRegsFromSense(sense, senseLength, prev, cur);
        if (r == UAS_ATAREG_NONE) {
            if (uasSenseKey(sense, senseLength) == 0x05) {
                *driverError = UAS_SMART_NOT_SUPPORTED;
                result = UAS_PTR_NOT_SUPPORTED;
            } else {
                *driverError = UAS_SMART_IDE_ERROR;
                ideError = 0x04;
                result = UAS_PTR_IO_ERROR;
            }
        } else if ((cur[6] & 0x21) != 0 || plan->DataLength != 0) {
            /* ERR or DF; or a data command that did not end GOOD. */
            *driverError = UAS_SMART_IDE_ERROR;
            ideError = ((cur[6] & 0x21) != 0) ? cur[0] : 0x04;
            result = UAS_PTR_IO_ERROR;
        } else {
            if (plan->ReturnRegs) {
                size = 8;
            }
            result = UAS_PTR_SUCCESS;
        }
    }
    if (room < UAS_SMART_OUT_HEADER) {
        return (result == UAS_PTR_SUCCESS) ? UAS_PTR_INVALID : result;
    }
    if (result == UAS_PTR_SUCCESS &&
        (plan->ReturnRegs ? 8 : size) > room - UAS_SMART_OUT_HEADER) {
        /* The plan checked the room it was given; this is the output's. */
        *driverError = UAS_SMART_INVALID_BUFFER;
        ideError = 0;
        size = 0;
        result = UAS_PTR_INVALID;
    }
    uasClear(out, UAS_SMART_OUT_HEADER);
    uasWr32(out, size);
    out[4] = (UCHAR)*driverError;
    out[5] = ideError;
    if (result == UAS_PTR_SUCCESS) {
        if (plan->ReturnRegs) {
            /* IDEREGS: Error, Count, LBA low, mid, high, Device, Status. */
            uasBytes(out + UAS_SMART_OUT_HEADER, cur, 7);
            out[UAS_SMART_OUT_HEADER + 7] = 0;
        } else if (size != 0) {
            uasBytes(out + UAS_SMART_OUT_HEADER, data, size);
        }
        *outBytes = UAS_SMART_OUT_HEADER + size;
    } else {
        *outBytes = UAS_SMART_OUT_HEADER;
    }
    return result;
}

ULONG UasNoAtaCdb(const UCHAR *cdb, ULONG cdbLength)
{
    if (cdb == NULL || cdbLength == 0) {
        return 0;
    }
    return (cdb[0] == 0x85 || cdb[0] == 0xA1) ? 1UL : 0UL;
}

ULONG UasAtaBit(const UCHAR *cdb, ULONG cdbLength)
{
    if (cdb == NULL || cdbLength == 0) {
        return 0;
    }
    if (cdb[0] == 0xA1) {
        return UAS_ATA_REFUSED_12;
    }
    return (cdb[0] == 0x85) ? UAS_ATA_REFUSED_16 : 0UL;
}

ULONG UasAtaRefusal(const UCHAR *cdb, ULONG cdbLength, ULONG scsiStatus,
                    const UCHAR *sense, ULONG senseLength)
{
    ULONG key;
    ULONG asc;
    ULONG ascq;

    if (scsiStatus != UAS_SCSI_CHECK_CONDITION || sense == NULL) {
        return 0;
    }
    if (senseLength >= 14 && (sense[0] & 0x7E) == 0x70) {
        key = sense[2] & 0x0FUL;
        asc = sense[12];
        ascq = sense[13];
    } else if (senseLength >= 4 && (sense[0] & 0x7E) == 0x72) {
        key = sense[1] & 0x0FUL;
        asc = sense[2];
        ascq = sense[3];
    } else {
        return 0;
    }
    if (key != 0x05 || asc != 0x20 || ascq != 0x00) {
        return 0;
    }
    return UasAtaBit(cdb, cdbLength);
}

ULONG UasInvalidOpcodeSense(PUCHAR sense, ULONG room)
{
    UCHAR fixed[UAS_INVALID_CDB_SENSE];
    ULONG n;

    uasClear(fixed, sizeof(fixed));
    fixed[0] = 0x70;
    fixed[2] = 0x05;
    fixed[7] = (UCHAR)(UAS_INVALID_CDB_SENSE - 8);
    fixed[12] = 0x20;
    n = (room < UAS_INVALID_CDB_SENSE) ? room : UAS_INVALID_CDB_SENSE;
    if (sense != NULL && n != 0) {
        uasBytes(sense, fixed, n);
    } else {
        n = 0;
    }
    return n;
}

ULONG UasInvalidCdbSense(PUCHAR sense, ULONG room)
{
    UCHAR fixed[UAS_INVALID_CDB_SENSE];
    ULONG n;

    uasClear(fixed, sizeof(fixed));
    fixed[0] = 0x70;
    fixed[2] = 0x05;
    fixed[7] = (UCHAR)(UAS_INVALID_CDB_SENSE - 8);
    fixed[12] = 0x24;
    n = (room < UAS_INVALID_CDB_SENSE) ? room : UAS_INVALID_CDB_SENSE;
    if (sense != NULL && n != 0) {
        uasBytes(sense, fixed, n);
    } else {
        n = 0;
    }
    return n;
}

VOID UasFailPack(PULONG w, ULONG event, ULONG origin, ULONG tag,
                 ULONG opcode, ULONG srbStatus, ULONG scsiStatus,
                 const UCHAR *sense, ULONG senseLength, ULONG slotState,
                 ULONG xfers, ULONG timeout, ULONG length)
{
    ULONG key;
    ULONG asc;
    ULONG ascq;

    key = 0xFF;
    asc = 0;
    ascq = 0;
    if (sense != NULL && senseLength >= 14 && (sense[0] & 0x7E) == 0x70) {
        key = sense[2] & 0x0FUL;
        asc = sense[12];
        ascq = sense[13];
    } else if (sense != NULL && senseLength >= 4 &&
               (sense[0] & 0x7E) == 0x72) {
        key = sense[1] & 0x0FUL;
        asc = sense[2];
        ascq = sense[3];
    }
    w[0] = ((event & 0xFFUL) << 24) | ((origin & 0xFFUL) << 16) |
           ((tag & 0xFFUL) << 8) | (opcode & 0xFFUL);
    w[1] = ((srbStatus & 0xFFUL) << 24) | ((scsiStatus & 0xFFUL) << 16) |
           (asc << 8) | ascq;
    w[2] = (key << 24) | ((slotState & 0xFFUL) << 16) |
           ((xfers & 0xFFUL) << 8) | ((timeout > 255) ? 255UL : timeout);
    w[3] = length;
}

ULONG UasFastResetDue(ULONG fast, ULONG armed, ULONG progress, ULONG now,
                      ULONG abortAt, ULONG startedAt)
{
    if (!fast || !armed || progress) {
        return 0;
    }
    if (now - abortAt > UAS_FAST_WINDOW) {
        return 0;
    }
    /* Started before the answered abort: that abort's own aftermath. */
    if (startedAt - abortAt > 0x7FFFFFFFUL) {
        return 0;
    }
    return 1;
}

ULONG UasFinalTimeoutDue(ULONG final, ULONG dataOutstanding,
                         ULONG finalAge)
{
    if (!final || !dataOutstanding) {
        return UAS_FINAL_TIMEOUT_NONE;
    }
    return (finalAge >= UAS_FINAL_TIMEOUT_AGE) ? UAS_FINAL_TIMEOUT_RESET
                                               : UAS_FINAL_TIMEOUT_WAIT;
}

ULONG UasFinalGiveUpDue(ULONG resets)
{
    return (resets >= UAS_FINAL_RESET_LIMIT) ? 1UL : 0UL;
}

ULONG UasDataDelivered(ULONG ok, ULONG bytes)
{
    return (ok && bytes != 0) ? 1UL : 0UL;
}

VOID UasFinalCount(PULONG counts, ULONG dirs)
{
    if ((dirs & UAS_FINAL_DIR_IN) != 0 && counts[0] != 0xFFFFFFFFUL) {
        counts[0]++;
    }
    if ((dirs & UAS_FINAL_DIR_OUT) != 0 && counts[1] != 0xFFFFFFFFUL) {
        counts[1]++;
    }
}

VOID UasFinalClear(PULONG counts, ULONG dir)
{
    if ((dir & UAS_FINAL_DIR_IN) != 0) {
        counts[0] = 0;
    }
    if ((dir & UAS_FINAL_DIR_OUT) != 0) {
        counts[1] = 0;
    }
}

ULONG UasFinalDue(const ULONG *counts, ULONG dirs)
{
    if ((dirs & UAS_FINAL_DIR_IN) != 0 && UasFinalGiveUpDue(counts[0])) {
        return 1UL;
    }
    if ((dirs & UAS_FINAL_DIR_OUT) != 0 && UasFinalGiveUpDue(counts[1])) {
        return 1UL;
    }
    return 0UL;
}

ULONG UasFinalCancelDue(ULONG fast, ULONG final, ULONG dataOutstanding,
                        ULONG waited)
{
    return (fast && final && dataOutstanding &&
            waited >= UAS_FAST_CANCEL_WAIT) ? 1UL : 0UL;
}
