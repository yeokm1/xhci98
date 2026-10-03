/*
 * uas_iu.c - the pure core of xhciuas.sys (see uas_iu.h).
 *
 * IRQL: any; no function here takes a lock or touches the DDK.
 */

#include "uas_iu.h"

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
 * stops at the next interface descriptor.
 */
ULONG UasParseConfig(const UCHAR *cfg, ULONG length, PUAS_CONFIG_INFO info)
{
    ULONG at;
    ULONG len;
    ULONG type;
    ULONG found;
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
        } else if (found && type == UAS_DESC_ENDPOINT && len >= 7) {
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
    case 0x43:  /* READ TOC */
    case 0x46:  /* GET CONFIGURATION */
    case 0x4A:  /* GET EVENT STATUS NOTIFICATION */
    case 0x5A:  /* MODE SENSE(10) */
    case 0x88:  /* READ(16) */
    case 0x9E:  /* SERVICE ACTION IN(16): READ CAPACITY(16) */
    case 0xA0:  /* REPORT LUNS */
    case 0xA8:  /* READ(12) */
    case 0xBE:  /* READ CD */
        return UAS_DIR_IN;
    case 0x0A:  /* WRITE(6) */
    case 0x15:  /* MODE SELECT(6) */
    case 0x2A:  /* WRITE(10) */
    case 0x2E:  /* WRITE AND VERIFY(10) */
    case 0x55:  /* MODE SELECT(10) */
    case 0x8A:  /* WRITE(16) */
    case 0xAA:  /* WRITE(12) */
        return UAS_DIR_OUT;
    default:
        return UAS_DIR_NONE;
    }
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
