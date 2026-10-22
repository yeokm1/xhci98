/*
 * tr_core.h - the pure half of XHCITR.EXE and XHCITR64.EXE (design record
 * 25 section 10; roadmap-usb4.md task 41-A.2).
 *
 * Everything here is a function of its arguments: no Windows header, no
 * registry, no device, no file. The tool links tr_core.c (and
 * xhciinst\inst_core.c, record 24's rules) and so does the host suite
 * test\test_tr.c, under /Za, so what the suite checks is what the tool
 * runs. C89.
 *
 * The answers of the hub and host controller IOCTLs and of xhcicm.sys's
 * query are read as bytes at the documented offsets, never through a
 * structure overlay: the MSVC 6.0 headers lack the _EX, _EX_V2 and
 * descriptor-request layouts, and every one of them is packed.
 */

#ifndef TR_CORE_H
#define TR_CORE_H

#include <stddef.h>
#include "../xhcimark/mark_core.h"
#include "../src/cm/cm_status.h"
#include "../src/cm/cm_query.h"

typedef unsigned long TR_U32;

#define TR_ID_CHARS             208
#define TR_PATH_CHARS           260

/* ---- text ------------------------------------------------------------------ */

/* A bounded text buffer. Every '\n' written becomes CR LF, the form the
 * details pane's edit control and the report file both take. */
typedef struct _TR_TEXT {
    char *Buf;
    size_t Cap;
    size_t Len;
    int Overflow;
} TR_TEXT;

void tr_text_init(TR_TEXT *t, char *buf, size_t cap);
void tr_put(TR_TEXT *t, const char *s);

/* A small printf: %s %c %d %u %x %X, each with an optional '-' or '0' flag,
 * a width and an 'l' size; %% a percent sign. Nothing else. */
void tr_printf(TR_TEXT *t, const char *fmt, ...);

/* The same into a plain buffer, always terminated. */
void tr_sprintf(char *out, size_t cap, const char *fmt, ...);

/* Hex bytes, 16 to a line, each line indented by `indent` spaces. */
void tr_hex(TR_TEXT *t, const unsigned char *b, size_t n, int indent);

void tr_copy(char *dst, const char *src, size_t cap);
int tr_eq(const char *a, const char *b);         /* without case */

/* ---- the USB IOCTLs (usbioctl.h's numbers; FILE_ANY_ACCESS, buffered) ---- */

#define TR_IOCTL_GET_HCD_DRIVERKEY_NAME             0x00220424UL
#define TR_IOCTL_USB_GET_ROOT_HUB_NAME              0x00220408UL
#define TR_IOCTL_USB_GET_NODE_INFORMATION           0x00220408UL
#define TR_IOCTL_USB_GET_NODE_CONNECTION_INFORMATION 0x0022040CUL
#define TR_IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION 0x00220410UL
#define TR_IOCTL_USB_GET_NODE_CONNECTION_NAME       0x00220414UL
#define TR_IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME 0x00220420UL
#define TR_IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX 0x00220448UL
#define TR_IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2 0x0022045CUL

/* A compile-time size check: a negative array size stops the build. */
#define TR_SIZE_CHECK(name, cond) typedef char name[(cond) ? 1 : -1]

/* The requests the tool builds, from their documented layouts, packed as
 * usbioctl.h packs them (pshpack1.h). The answers are read as bytes at
 * the offsets below. */
#pragma pack(push, 1)
typedef struct _TR_USB_CONN_EX {        /* USB_NODE_CONNECTION_INFORMATION_EX */
    TR_U32 ConnectionIndex;
    unsigned char DeviceDescriptor[18];
    unsigned char CurrentConfigurationValue;
    unsigned char Speed;
    unsigned char DeviceIsHub;
    unsigned short DeviceAddress;
    TR_U32 NumberOfOpenPipes;
    TR_U32 ConnectionStatus;
} TR_USB_CONN_EX;
typedef struct _TR_USB_CONN_V2 {        /* ..._EX_V2                     */
    TR_U32 ConnectionIndex;
    TR_U32 Length;
    TR_U32 SupportedUsbProtocols;
    TR_U32 Flags;
} TR_USB_CONN_V2;
typedef struct _TR_USB_DESC_REQ {       /* USB_DESCRIPTOR_REQUEST         */
    TR_U32 ConnectionIndex;
    unsigned char bmRequest;
    unsigned char bRequest;
    unsigned short wValue;
    unsigned short wIndex;
    unsigned short wLength;
} TR_USB_DESC_REQ;
#pragma pack(pop)
TR_SIZE_CHECK(trCheckConnEx, sizeof(TR_USB_CONN_EX) == 35);
TR_SIZE_CHECK(trCheckConnV2, sizeof(TR_USB_CONN_V2) == 16);
TR_SIZE_CHECK(trCheckDescReq, sizeof(TR_USB_DESC_REQ) == 12);

/* NOTIFYICONDATAA with the version 2 fields (the shell's balloons), which
 * MSVC 6.0's shellapi.h lacks; the x64 build takes the WDK's own. Version 1
 * is its first 88 bytes (record 25 section 7.2). */
#ifndef _WIN64
typedef struct _TR_NIDA {
    TR_U32 cbSize;
    void *hWnd;
    unsigned int uID;
    unsigned int uFlags;
    unsigned int uCallbackMessage;
    void *hIcon;
    char szTip[128];
    TR_U32 dwState;
    TR_U32 dwStateMask;
    char szInfo[256];
    unsigned int uTimeoutOrVersion;
    char szInfoTitle[64];
    TR_U32 dwInfoFlags;
} TR_NIDA;
#define TR_NIDA_V1_BYTES        88
#define TR_NIDA_V2_BYTES        488
TR_SIZE_CHECK(trCheckNidV2, sizeof(TR_NIDA) == TR_NIDA_V2_BYTES);
TR_SIZE_CHECK(trCheckNidV1, offsetof(TR_NIDA, szTip) + 64 == TR_NIDA_V1_BYTES);
#endif

/* USB_NODE_CONNECTION_INFORMATION(_EX), packed: ConnectionIndex 0, the
 * device descriptor 4, CurrentConfigurationValue 22, Speed (the non-_EX
 * form: LowSpeed) 23, DeviceIsHub 24, DeviceAddress 25, NumberOfOpenPipes
 * 27, ConnectionStatus 31, then one USB_PIPE_INFO (an endpoint descriptor
 * and a ULONG schedule offset, 11 bytes) per open pipe from 35. */
#define TR_CI_BYTES             35
#define TR_CI_PIPE_BYTES        11
#define TR_MAX_PIPES            32

/* USB_NODE_CONNECTION_INFORMATION_EX_V2: ConnectionIndex 0, Length 4,
 * SupportedUsbProtocols 8 (bit 0 1.1, bit 1 2.0, bit 2 3.0), Flags 12. */
#define TR_V2_BYTES             16
#define TR_V2_PROTO_USB300      0x04UL
#define TR_V2_F_SS_NOW          0x01UL  /* operating at SuperSpeed or higher */
#define TR_V2_F_SS_CAPABLE      0x02UL
#define TR_V2_F_SSP_NOW         0x04UL  /* operating at SuperSpeedPlus       */
#define TR_V2_F_SSP_CAPABLE     0x08UL

/* USB_DESCRIPTOR_REQUEST, packed: ConnectionIndex 0, the setup packet
 * 4..11 (bmRequest, bRequest, wValue, wIndex, wLength), data from 12. */
#define TR_DR_BYTES             12

/* USB_NODE_INFORMATION, packed: NodeType 0 (0 a hub), the hub descriptor
 * 4 (71 bytes), HubIsBusPowered 75. */
#define TR_NI_BYTES             76

/* The name answers (USB_ROOT_HUB_NAME, USB_HCD_DRIVERKEY_NAME:
 * ActualLength 0, WCHAR name 4; USB_NODE_CONNECTION_NAME and
 * _DRIVERKEY_NAME: ConnectionIndex 0, ActualLength 4, name 8). The UTF-16
 * name copied as ASCII ('?' for anything else); 1 when it held one. */
int tr_wide_name(const unsigned char *b, size_t n, size_t offset, char *out,
                 size_t cap);

/* USB_CONNECTION_STATUS. */
#define TR_CS_NONE              0UL
#define TR_CS_CONNECTED         1UL

typedef struct _TR_CONN {
    TR_U32 Port;
    unsigned char Dev[18];
    unsigned int Config;
    unsigned int Speed;         /* 0 Low, 1 Full, 2 High, 3 Super          */
    int IsHub;
    unsigned int Address;
    TR_U32 OpenPipes;
    TR_U32 Status;              /* TR_CS_*                                 */
    int PipeCount;              /* the pipes held below, at most TR_MAX_PIPES */
    unsigned char Pipe[TR_MAX_PIPES][7];
} TR_CONN;

/* An answer of ..._INFORMATION (ex 0) or ..._INFORMATION_EX (ex 1) of n
 * bytes. 1 when it holds the fixed part. */
int tr_conn_parse(const unsigned char *b, size_t n, int ex, TR_CONN *c);

const char *tr_conn_status_text(TR_U32 status);

/* The speed as shown: the _EX code, and the _EX_V2 flags where read. */
#define TR_SPEED_LOW            0
#define TR_SPEED_FULL           1
#define TR_SPEED_HIGH           2
#define TR_SPEED_SUPER          3
#define TR_SPEED_SUPERPLUS      4
#define TR_SPEED_UNKNOWN        0xFFU   /* the original IOCTL: not Low Speed */
int tr_speed(unsigned int exSpeed, int haveV2, TR_U32 v2Flags);
const char *tr_speed_name(int speed);

/* The hub node's answer: 1 for a hub with its port count and power. */
int tr_node_parse(const unsigned char *b, size_t n, unsigned int *ports,
                  int *busPowered);

/* ---- descriptors (USB 2.0 chapter 9, USB 3.2 chapter 9) ------------------- */

const char *tr_class_name(unsigned int cls);

/* Each decodes field by field, then lists the bytes; anything short or
 * malformed is said so and the bytes still listed. */
void tr_dec_device(TR_TEXT *t, const unsigned char *d, size_t n);
void tr_dec_config(TR_TEXT *t, const unsigned char *d, size_t n);
void tr_dec_bos(TR_TEXT *t, const unsigned char *d, size_t n);
void tr_dec_hub(TR_TEXT *t, const unsigned char *d, size_t n);

/* A string descriptor as ASCII ('?' for anything else). 1 when valid. */
int tr_dec_string(const unsigned char *d, size_t n, char *out, size_t cap);

/* The SuperSpeedPlus capability's sublink speeds as one line ("5 Gbit/s,
 * 10 Gbit/s per lane"): what the device supports, never what its link
 * negotiated (decision T3). 1 when the BOS holds one. */
int tr_bos_ssp_text(const unsigned char *d, size_t n, char *out, size_t cap);

/* The protocols a port supports (_EX_V2's SupportedUsbProtocols) as a
 * list: "USB 1.1, USB 2.0, USB 3.x", or "none reported". */
void tr_protocols_text(TR_U32 proto, char *out, size_t cap);

/* The configuration descriptor's bMaxPower in mA for the speed (2 mA units
 * below SuperSpeed, 8 mA from it); 1 when read. */
int tr_config_power(const unsigned char *d, size_t n, int speed,
                    unsigned int *ma, int *selfPowered);

/* ---- xhcicm.sys's query (src\cm\cm_query.h, record 25 section 4) ---------- */

typedef struct _TR_QUERY {
    const unsigned char *Base;
    TR_U32 Bytes;               /* the answer's size field                  */
    TR_U32 Pass;
    TR_U32 AgeMs;
    TR_U32 Live;                /* CM_LIVE_*                                */
    TR_U32 Flags;               /* CM_Q_F_*                                 */
    TR_U32 EpochLo;             /* this start of this NHI                   */
    TR_U32 EpochHi;
    TR_U32 NhiBdf;
    TR_U32 ContentGen;          /* CM_Q_O_CONTENT_GEN, never 0             */
    char Id[TR_ID_CHARS];       /* the NHI's instance id                    */
    const unsigned char *Rec;   /* the status record                         */
    TR_U32 RecBytes;
    TR_U32 ExtOffset;
    TR_U32 ExtCount;
    TR_U32 ExtBytes;
    /* The removal block (version 2; record 25 section 7.6). */
    TR_U32 Version;             /* CM_Q_VERSION_1 or CM_Q_VERSION           */
    TR_U32 RmFlags;             /* CM_Q_RF_*; 0 in version 1               */
    TR_U32 RmCount;
    TR_U32 RmMember;            /* CM_Q_UNKNOWN in version 1                */
    TR_U32 RmLost;
} TR_QUERY;

/* An answer of n bytes (no more than the call returned), version 1 or
 * 2. 1 a whole, consistent answer; -1 the header alone, or a header whose
 * size field asks for more than n (q->Bytes says how much, at most
 * CM_Q_MAX_BYTES);
 * 0 anything else: wrong magic or version, a record that does not check,
 * counts, offsets or sizes that disagree, an epoch of 0. */
int tr_query_parse(const unsigned char *b, size_t n, TR_QUERY *q);

/* Whether an answer belongs to the NHI devnode devId (record 25 section
 * 4.2): its instance ID equals devId; or, only when the answer gives no
 * instance ID (Windows 98 SE and ME), the record's NHI vendor and device
 * equal devId's VEN_ and DEV_ and nameUnique says no other NHI devnode of
 * the same reading carries the same query-device name. 1 when it does,
 * *byName 1 for the second rule. */
int tr_query_matches(const TR_QUERY *q, const char *devId, int nameUnique,
                     int *byName);

/* Display only: the last pass is younger than CM_Q_CURRENT_MS. */
int tr_query_current(const TR_QUERY *q);

/* The first gate of an eject (record 25 section 6): the record's stage
 * active, neither inert nor stopping, the liveness ACTIVE. 1 when it
 * passes; else 0 with the reason in *why. */
int tr_query_eject_ready(const TR_QUERY *q, const char **why);

/* 1 when b was published at least CM_Q_NEW_PASSES passes after a, in the
 * same start: one move could be a snapshot built before a and published
 * after it, two prove one built after (record 25 section 6.2). The counter
 * wraps past 0 to 1. */
int tr_query_new_pass(const TR_QUERY *a, const TR_QUERY *b);

/* 1 when both answers are of the same start and content generation. */
int tr_query_same_content(const TR_QUERY *a, const TR_QUERY *b);

typedef struct _TR_EXT {
    TR_U32 Rate;                /* Gbit/s per lane, 0 unknown              */
    TR_U32 Width;               /* lanes, 0 unknown                        */
    TR_U32 LinkState;
    TR_U32 GenRaw;
    TR_U32 Ids;
    TR_U32 Flags;
} TR_EXT;

/* The index-th router extension; 1 when it exists. */
int tr_query_ext(const TR_QUERY *q, TR_U32 index, TR_EXT *x);

/* ---- the status record's texts (record 24 section 3; cm_status.h) --------- */

const char *tr_tun_text(TR_U32 v);
const char *tr_appr_text(TR_U32 v);
const char *tr_cm_text(TR_U32 cm);
const char *tr_stage_text(TR_U32 stage);
const char *tr_live_text(TR_U32 live);

/* "Thunderbolt 3", "USB4 v1", "generation not known". */
void tr_gen_text(TR_U32 gen, char *out, size_t cap);

/* The link as shown: "USB4 Gen 3 x2 (40 Gbit/s)", "one lane; speed not
 * known", "not reported by the firmware" (an ICM entry), from the entry's
 * kind and lanes and the extension (NULL when there is none). */
void tr_link_text(TR_U32 kind, TR_U32 lanes, const TR_EXT *x, char *out,
                  size_t cap);

/* 1 for a Thunderbolt 3 router: the record's generation says so, or a
 * USB4 Version of major 0 (a Thunderbolt 3 router behind a USB4 host). */
int tr_gen_tbt3(TR_U32 gen);

/* tr_link_text, but a Thunderbolt 3 router's lane-adapter rate is worded
 * "Thunderbolt 3, 20 Gbit/s per lane, x2 (40 Gbit/s)", never as a USB4
 * generation. */
void tr_link_text_gen(TR_U32 gen, TR_U32 kind, TR_U32 lanes, const TR_EXT *x,
                      char *out, size_t cap);

/* A router entry's name: "<vendor> <model>", or "(no name given)". */
void tr_router_name(const unsigned char *entry, char *out, size_t cap);

/* ---- the tray's events (record 25 section 7.2) ---------------------------- */

#define TR_SNAP_NHI             4
#define TR_SNAP_ROUTERS         96      /* TR_SNAP_NHI x CM_ST_MAX_ROUTERS */
#define TR_NAME_CHARS           72

typedef struct _TR_SROUTER {
    TR_U32 Nhi;                 /* the NHI's key (the caller's)             */
    TR_U32 EpochLo;
    TR_U32 EpochHi;
    TR_U32 Kind;                /* CM_ST_KIND_*                             */
    TR_U32 RouteLo;
    TR_U32 RouteHi;
    TR_U32 Place;
    TR_U32 Ids;
    TR_U32 ShortId;
    TR_U32 Approval;
    TR_U32 Tunnels;
    TR_U32 Bridge;
    TR_U32 Gen;
    char Name[TR_NAME_CHARS];
} TR_SROUTER;

typedef struct _TR_SNAP {
    int Valid;
    int NhiCount;
    TR_U32 NhiKey[TR_SNAP_NHI];
    TR_U32 Live[TR_SNAP_NHI];
    TR_U32 EpochLo[TR_SNAP_NHI];
    TR_U32 EpochHi[TR_SNAP_NHI];
    TR_U32 RmFlags[TR_SNAP_NHI];        /* the removal block's, 0 in v1    */
    TR_U32 RmCount[TR_SNAP_NHI];
    int Count;
    int Truncated;
    TR_SROUTER R[TR_SNAP_ROUTERS];
} TR_SNAP;

void tr_snap_init(TR_SNAP *s);

/* One NHI's answer added: its liveness, epoch and router entries. 0 when
 * the snapshot is full. */
int tr_snap_add(TR_SNAP *s, TR_U32 nhiKey, const TR_QUERY *q);

/* 1 when two entries are the same router (record 25 section 7.2): the same
 * NHI and epoch, and for USB4 entries (kinds 2, 3) the route, for ICM
 * entries the link and depth, ids and UUID suffix. */
int tr_same_router(const TR_SROUTER *a, const TR_SROUTER *b);

/* The events are descriptive only (decision T8): an eject's outcome is the
 * eject transaction's message, never a diff's. */
#define TR_EV_CONNECTED         1
#define TR_EV_WAITING           2
#define TR_EV_FAILED            3
#define TR_EV_STOPPED           5
#define TR_EV_LEFT              6   /* disconnected                          */
#define TR_EV_REMOVED           7   /* removed unexpectedly (record 25 7.6);
                                       it names no unit                     */

typedef struct _TR_EVENT {
    int Kind;
    TR_U32 Gen;
    char Name[TR_NAME_CHARS];   /* "a Thunderbolt device" without identity */
} TR_EVENT;

/* The events between two snapshots, at most cap; none unless both are
 * valid. Router events only for an NHI both hold at the same epoch (a new
 * epoch is a new set of devices). Entries sharing a key have no identity:
 * a change in their number is one generic event. Approval events only for
 * ICM entries (a USB4 entry's approval word folds held, failed and
 * unsupported together); a router that leaves is "disconnected", never
 * "unplugged while in use" (decision T9); the stop event when the
 * liveness leaves ACTIVE. Revision 16 (section 7.6): one "removed
 * unexpectedly" event, naming no unit, for an NHI whose removal count
 * differs, both snapshots of it watched and ACTIVE at the same epoch. */
int tr_diff(const TR_SNAP *old, const TR_SNAP *now, TR_EVENT *ev, int cap);

/* The NHI details' line for an answer's removal block (record 25 section
 * 7.6, item 5): "3 since this start; the last on tunnel port 00:1C.4"
 * (and "; 1 in a loss of the controller" where any), "none since this
 * start", "not watched on this system", "not reported by this driver". */
void tr_removal_text(const TR_QUERY *q, char *out, size_t cap);

/* The balloon's text and title for an event. */
void tr_event_text(const TR_EVENT *e, char *out, size_t cap);
const char *tr_event_title(int kind);

/* Revision 16 (section 7.6, item 4, as read by leg T.11 on 2026-10-22): a
 * poll's warning and its other events as one balloon's text, since a
 * second balloon from the same icon replaces the first at once on XP and
 * later. Each event's text on a line of its own, in the order given; a
 * line that does not fit whole is left out. The number of lines written. */
int tr_events_text(const TR_EVENT *ev, int count, char *out, size_t cap);

/* ---- the tree's keys --------------------------------------------------------- */

/* "<prefix>:<a>:<b>", bounded: the selection and expanded nodes are found
 * again by these after a refresh (record 25 section 3.4). */
void tr_key(char *out, size_t cap, const char *prefix, const char *a,
            const char *b);

/* A USB port path: parent's path, then '.', then the port ("3", "3.1"). */
void tr_port_path(char *out, size_t cap, const char *parent, TR_U32 port);

/* ---- View check boxes, persisted ----------------------------------------- */

#define TR_V_TB                 0x0001UL
#define TR_V_USB                0x0002UL
#define TR_V_WINUSB             0x0004UL
#define TR_V_EMPTY              0x0008UL
#define TR_V_DESC               0x0010UL
#define TR_V_BEHIND             0x0020UL
#define TR_V_AUTO               0x0040UL
#define TR_V_DEFAULT            (TR_V_TB | TR_V_USB | TR_V_WINUSB | \
                                 TR_V_EMPTY | TR_V_BEHIND | TR_V_AUTO)
#define TR_V_ALL                0x007FUL

/* ---- the command line (record 25 section 7.3) ----------------------------- */

typedef struct _TR_OPTS {
    int Tray;
    int Eject;
    char EjectId[TR_ID_CHARS];
    int Report;
    char ReportPath[TR_PATH_CHARS];
    int SelfTest;
    int Result;                 /* /result: the elevated stop's answer   */
    char ResultName[TR_PATH_CHARS];
    int Confirmed;              /* /confirmed: the confirmed list's digest */
    char ConfirmedDigest[TR_PATH_CHARS];
    int SetLevel;               /* /setlevel (record 25 section 7.5)       */
    int Level[2];               /* per TR_LV_XHCI98 / TR_LV_XHCICM: 0-4,
                                 * or -1 for not asked                      */
} TR_OPTS;

/* XHCITR [/tray] [/eject id=<instance id> [/result <section>]
 * [/confirmed <digest>]] [/setlevel xhci98=<n> xhcicm=<n> [/result
 * <section>]] [/report <file>] [/selftest]. Switches start with '/' or
 * '-', without case; /eject takes id=<id> as the next argument, /report
 * its file, /result a section name (tr_res_name_ok) and /confirmed a
 * digest (tr_res_digest_ok). /setlevel takes one or two of xhci98=<n> and
 * xhcicm=<n> as the next arguments, each driver once, <n> a single digit
 * 0-4 and nothing else (tr_level_arg). Returns 0 with opts filled, 1
 * refused with why in err. /eject, /setlevel, /report and /selftest stand
 * alone; /result goes with /eject or /setlevel, /confirmed with /eject
 * only (the elevated runs of sections 6.6 and 7.5). */
int tr_parse_args(int argc, char **argv, TR_OPTS *o, char *err, size_t cap);

/* ---- small text helpers ---------------------------------------------- */

/* "" for one, "s" for any other count. */
const char *tr_plural(long n);

/* Text wrapped for a console made one paragraph per line for a balloon or
 * a message box: a single line break (LF or CR LF) becomes a space, an
 * empty line between paragraphs is kept; in place. */
void tr_join_lines(char *s);

/* ---- what a unit is called (owner, 2026-10-16) ---------------------------- */

/* A drive-letter set: the letters A to Z, upper case, each once, in order,
 * NUL-terminated; 27 bytes hold any set. tr_letters_add merges the letters
 * of src (any case; anything else is skipped). */
#define TR_LETTERS_BYTES        27
void tr_letters_add(char *set, const char *src);

/* "E:, F:", or "" for an empty set; the whole alphabet takes
 * TR_LETTERS_TEXT_BYTES ("X:" 26 times, ", " 25 times, NUL). A smaller out
 * holds as many whole letters as fit, never a trailing separator. */
#define TR_LETTERS_TEXT_BYTES   103
void tr_letters_text(const char *set, char *out, size_t cap);

/* What the user sees for a unit: its disks' names with their letters
 * ("Samsung SSD 970 EVO (E:)", "QEMU NVMe Ctrl (E:, F:)"), or, with no
 * disk (diskNames empty), the unit's own name. The letters are kept whole:
 * names too long for the rest of out end in "...". */
void tr_unit_label(const char *unitName, const char *diskNames,
                   const char *letters, char *out, size_t cap);

/* One disk in the confirmation: "Disk drive <name>, volume E:" (volumes
 * for more than one, nothing after the name for none). */
void tr_disk_line(const char *name, const char *letters, char *out,
                  size_t cap);

/* The confirmation's lead, in one pass: with disks, "This stops <unit>
 * and everything on it:", one indented "Disk drive ..." line each (no full
 * stop after a letter), an empty line; without, "Stop <label>?" and an
 * empty line; then "These devices will stop:". diskLines are n plain
 * lines (tr_disk_line's). */
void tr_confirm_lead(TR_TEXT *t, const char *unitName, const char *label,
                     const char *const *diskLines, int n);

/* A name as shown: leading and trailing white space removed, every run of
 * white space inside made one space (Windows 98 pads a disk's model with
 * spaces); in place. */
void tr_tidy_name(char *s);

/* ---- plain USB devices (record 25 section 6.5, revision 7) ---------------- */

/* 1 when one of the MULTI_SZ ids (no more than bytes long) begins with
 * prefix, without case. */
int tr_msz_has_prefix(const char *msz, size_t bytes, const char *prefix);

/* 1 when a devnode is an input device: setup class HIDClass, Keyboard or
 * Mouse, bound to hidusb, or its instance id or a hardware or compatible
 * id beginning USB\Class_03 or HID\. Any argument may be NULL. */
int tr_node_is_hid(const char *id, const char *hw, size_t hwBytes,
                   const char *compat, size_t compatBytes,
                   const char *service, const char *cls);

/* 1 when the ids say a hub (USB\Class_09). */
int tr_node_is_hub(const char *hw, size_t hwBytes, const char *compat,
                   size_t compatBytes);

/* What is known of the device at a USB port and of its set: everything
 * Windows stops with it (section 6.5). */
typedef struct _TR_USBF {
    int Nt;
    int W2k;                    /* 2000, stopped here: Code 24 is held       */
    int HaveDevnode;            /* the port's devnode was found in the walk  */
    int IsHub;                  /* the port says so, or ids of its set do    */
    int IsController;
    int Hid;                    /* a devnode of its set is input             */
    int Radio;                  /* device class E0 (a Bluetooth radio)       */
    int Disks;                  /* setup class DiskDrive, in its set         */
    int Volumes;                /* NT: setup class Volume, in its set        */
    int SetKnown;               /* every relation of the set was read       */
    int SetCount;               /* the devnodes of the set, itself included */
    int Started;                /* DN_STARTED                                */
    TR_U32 Problem;             /* 0, or the problem code                    */
    int ReadWhole;              /* every devnode of its set read whole       */
    int CallAvailable;          /* the routines of the eject were found      */
} TR_USBF;

#define TR_CM_PROB_HELD_FOR_EJECT 47UL
#define TR_CM_PROB_DEVICE_NOT_THERE 24UL
#define TR_USB_SET_MAX          64      /* what one confirmation lists       */

#define TR_UE_DETAILS   0       /* details only: never listed              */
#define TR_UE_LISTED    1       /* listed with the reason, not offered     */
#define TR_UE_OFFER     2       /* Safely Remove offered                   */

/* 1 for a devnode held for eject: Code 47, or, when w2k says Windows 2000
 * and a stop of this devnode was accepted, Code 24: 2000 has no Code 47
 * and leaves a removed devnode that is still attached at Code 24 (run 41,
 * the bridge after its stop; run 42, a USB stick after its stop), and
 * Code 24 is also its failed start. */
int tr_held_for_eject(TR_U32 problem, int w2k);

/* That w2k argument: 1 on Windows 2000 (w2k) for a devnode this process
 * stopped (stoppedHere) and, for the device at a USB port (atPort), only
 * while the port reports a configuration (config): the stopped device keeps
 * its configuration until it is unplugged, and one plugged in again starts
 * unconfigured, so a later failed start at Code 24 is not read as the
 * stop. */
int tr_w2k_code24(int w2k, int stoppedHere, int atPort, unsigned int config);

/* The words for a devnode that is not running (record 25 section 14): the
 * tag ("stopped - unplug it" when held for eject, tr_held_for_eject;
 * "not started - problem code N" for any other problem; "not started") and
 * the sentence ("Stopped: you can unplug it.", "Windows could not start
 * this device (problem code N).", ...). 1 only when held for eject, the one
 * state in which the device may be unplugged. */
int tr_stopped_text(TR_U32 problem, int w2k, char *tag, size_t tagCap,
                    char *sentence, size_t sentenceCap);

/* What a walk right after an accepted stop reads (section 6.5 step 5):
 * TR_UA_UNPLUG for the walk whole and the device gone, or held for eject
 * (tr_held_for_eject). TR_UA_STARTED for the device started again;
 * TR_UA_UNKNOWN for anything else, no walk included. started is DN_STARTED
 * with no problem code. */
#define TR_UA_UNPLUG    0
#define TR_UA_STARTED   1
#define TR_UA_UNKNOWN   2
int tr_usb_after_kind(int walked, int found, int walkBad, int started,
                      TR_U32 problem, int w2k);

/* The rule of section 6.5. *why is the reason for anything but
 * TR_UE_OFFER ("a hub", "an input device", "stopped - unplug it", ...),
 * NULL for it. */
int tr_usb_rule(const TR_USBF *f, const char **why);

/* The check right before the call (section 6.5 step 3): 1 when the device
 * found again is under the devnode it was confirmed under (parent ids,
 * without case), has exactly the devnodes below it that were confirmed
 * (any order, without case), and is still offered by f; else 0 and *why. */
int tr_usb_recheck(const char *parentNow, const char *parentThen,
                   const char *const *nowIds, int n,
                   const char *const *thenIds, int m, const TR_USBF *f,
                   const char **why);

/* The answer to the call in plain words, into out; returns the outcome
 * (INST_EJ_*). `what` is what the user knows the device by in a refusal:
 * its drive letters ("F:", "F:, G:"), or its name when it has none.
 * NT: CM_Request_Device_EjectA's answer, its veto type and veto name;
 * vetoInSet 1 when the veto name is a devnode of the device's own set (a
 * volume refusing for an open file or folder, as XP reports it). */
int tr_usb_answer_nt(TR_U32 cr, TR_U32 vetoType, const char *vetoName,
                     int vetoInSet, const char *what, char *out, size_t cap);

/* CM_Request_Device_EjectA's answer as the outcome tables read it: Windows
 * XP answers a vetoed request CR_SUCCESS with the veto type set (read on
 * the T.7 leg), so a veto type other than PNP_VetoTypeUnknown (0) turns
 * success into CR_REMOVE_VETOED. The caller sets the type to 0 first. */
TR_U32 tr_eject_cr(TR_U32 cr, TR_U32 vetoType);

/* 98 SE and ME: CM_Query_Remove_SubTree's answer. CR_REMOVE_VETOED names
 * no cause, so the text claims none. */
int tr_usb_answer9x(TR_U32 cr, const char *what, char *out, size_t cap);

/* Text for a console: every line longer than width broken at the last
 * space before it (a word longer than the line is cut); LF or CR LF in,
 * CR LF out. out always terminated. */
void tr_wrap(const char *in, char *out, size_t cap, int width);

/* ---- start at login (record 25 section 7.4, revision 8) ------------------ */

/* The per-user value under HKCU\Software\xHCI98\XHCITR. */
#define TR_LOGIN_VALUE          "TrayAtLogin"

/* Whether a start goes on. Only a /tray start (the one the Run value makes
 * at login) reads the value, and only a DWORD 0 stops it; absent, another
 * type or any other number starts as before. haveValue: the value was
 * read as a DWORD. */
int tr_tray_login_start(int trayMode, int haveValue, TR_U32 value);

/* The unexpected-removal warning's option (record 25 section 7.6 item 8):
 * the DWORD under the same settings key, read as TR_LOGIN_VALUE is - only
 * a DWORD 0 turns the warning off; absent, another type or any other
 * number leaves it on. haveValue: the value was read as a DWORD. */
#define TR_WARN_VALUE           "WarnRemoved"
int tr_warn_removed(int haveValue, TR_U32 value);

/* ---- the tree's labels (record 25 revision 10 addendum) ------------------ */

/* VEN_ and DEV_ of a PCI id or instance id ("PCI\VEN_8086&DEV_02ED&..."),
 * without case; 1 when both were read. */
int tr_pci_ids(const char *id, unsigned int *ven, unsigned int *dev);

/* Windows 98 and ME: the bus and device of a function on a root bus, from
 * the instance id's last element "BUS_hh&DEV_hh&FUNC_hh"; 1 when read. */
int tr_pci_loc9x(const char *id, unsigned int *bus, unsigned int *dev);

/* A PCI vendor's short name ("Intel"), or NULL for one not in the table. */
const char *tr_pci_vendor(unsigned int ven);

#define TR_TB_NOT               0   /* not a Thunderbolt chip's xHCI        */
#define TR_TB_TB3               1   /* its NHI runs the firmware CM (ICM)   */
#define TR_TB_USB4              2   /* its NHI runs the USB4 software CM    */
#define TR_TB_UNKNOWN           3   /* an NHI's, generation not published   */

/* What follows a controller's description in the tree: " - Intel
 * 8086:02ED", " - vendor 1AB2 1AB2:0001", or for a Thunderbolt chip's
 * own xHCI " - Thunderbolt 3 controller's USB (8086:15C1)"; "" (or the
 * Thunderbolt words alone) when the ids were not read. */
void tr_ctrl_suffix(char *out, size_t cap, int haveIds, unsigned int ven,
                    unsigned int dev, int tbKind);

/* "Root hub - 18 ports (14 USB 2.0, 4 USB 3.x)"; without the split when
 * known is 0 (a port did not report its protocols); "Root hub" for 0
 * ports. */
void tr_roothub_label(char *out, size_t cap, unsigned int ports, int known,
                      unsigned int usb2, unsigned int usb3);

/* A port's protocol for its label (", USB 3", ", USB 2", ", USB 1.1"), or
 * "" when _EX_V2 did not report it. */
const char *tr_port_tag(int haveV2, TR_U32 proto);

/* ---- resources (record 25 revision 10, addendum) ------------------------- */

/* ResType_* and the logical configuration kinds of cfgmgr32.h; the
 * descriptor layouts are cfgmgr32.h's, packed to one byte, the same in the
 * Windows 98 DDK's Win32 cfgmgr32.h and in WDK 7.1's. */
#define TR_RES_MEM              1UL
#define TR_RES_IO               2UL
#define TR_RES_DMA              3UL
#define TR_RES_IRQ              4UL
#define TR_RES_BUSNUMBER        6UL
#define TR_RES_MEMLARGE         7UL
#define TR_LC_BASIC             0UL
#define TR_LC_FILTERED          1UL
#define TR_LC_ALLOC             2UL
#define TR_LC_BOOT              3UL

/* One descriptor's data (CM_Get_Res_Des_Data, n bytes) as one or more
 * indented lines: the value given for alloc 1 (an allocated or a boot
 * configuration), each range asked for when 0 (a requirement list). is64:
 * the x64 IRQ descriptor, whose affinity is 8 bytes. Returns 1 when the
 * data checked; else a line says it could not be read. */
int tr_res_text(TR_TEXT *t, TR_U32 type, const unsigned char *d, size_t n,
                int alloc, int is64);

/* ---- the report ---------------------------------------------------------- */

/* One node of the report: its label at depth, then its details indented
 * under it (the details' own lines, CR LF or LF). */
void tr_report_node(TR_TEXT *t, int depth, const char *label,
                    const char *details);

/* ---- the elevated stop of a unit not marked removable (section 6.6) ------ */

/* NT: 1 when a unit's stop is to be offered with administrator rights
 * before it is tried: the unit is not marked removable (inst_unit_not_marked)
 * and this process is not an administrator (Vista and 7: not elevated). */
int tr_elev_needed(int nt, int notMarked, int isAdmin);

/* The digest of the devnode list the window's user confirmed, passed on
 * the elevated run's command line (/confirmed), which nothing can change
 * after the start: two 32-bit FNV-1a hashes with different seeds over the
 * count and the instance IDs, upper-cased, as 16 hex digits. The elevated
 * run skips its own confirmation only when its own list has the same
 * digest, so the digest can spare a question, never add a devnode. */
#define TR_DIGEST_CHARS     17
void tr_res_digest(const char (*devs)[TR_ID_CHARS], int n, char *out);
int tr_res_digest_ok(const char *s);

/* The answer travels in a named section of TR_RES_BYTES that the window
 * creates ("Local\Xhci98TrAnswer-<digits and dashes>", tr_res_name_ok),
 * never in a file: "XHCITR ANSWER 1", "code <n>", then the text, NUL
 * ended. tr_res_answer returns the length, 0 when it does not fit; the
 * reader is given a terminated copy. */
#define TR_RES_PREFIX       "Local\\Xhci98TrAnswer-"
#define TR_RES_BYTES        4096
int tr_res_name_ok(const char *name);
size_t tr_res_answer(int code, const char *msg, char *out, size_t cap);
int tr_res_read_answer(const char *text, int *code, char *msg, size_t cap);

/* The window's result of an elevated run, as XHCIINST's exit code with its
 * text in out. launched: ShellExecuteEx started it, else launchErr is its
 * last error (ERROR_CANCELLED 1223: the user declined). finished: it ended
 * within the wait, with exitCode. haveAnswer: its answer was read. */
#define TR_ERROR_CANCELLED  1223UL
int tr_elev_result(int launched, TR_U32 launchErr, int finished,
                   TR_U32 exitCode, int haveAnswer, int answerCode,
                   const char *answerMsg, char *out, size_t cap);

/* The question before an elevated stop. */
const char *tr_elev_question(void);

/* ---- the drivers' logging levels (record 25 section 7.5) ----------------- */

/* The two drivers, each with its level in its devices' driver (software)
 * keys: xhci98's XhciLogVerbosity in every controller's, xhcicm's
 * XhciCmLogVerbosity in every NHI's; and, from 3.1.0.0, the same value in
 * the machine-wide key TR_LV_MACHINE_KEY, which both drivers read first and
 * which wins when it is there in any type (task 42-A.17). */
#define TR_LV_XHCI98            0
#define TR_LV_XHCICM            1
#define TR_LV_DRIVERS           2
#define TR_LV_DEFAULT           2
#define TR_LV_MAX               4
#define TR_LV_NAME_XHCI98       "XhciLogVerbosity"
#define TR_LV_NAME_XHCICM       "XhciCmLogVerbosity"
#define TR_LV_MACHINE_KEY       "System\\CurrentControlSet\\Services\\USB"

/* "xhci98" or "xhcicm"; NULL for anything else. */
const char *tr_level_driver(int driver);

/* The meaning of a level, one line each: "off", "counters only", "the log
 * (default)", "adds port status", "adds memory addresses (for
 * developers)"; NULL out of range. */
const char *tr_level_text(TR_U32 level);

/* One registry value as read. */
#define TR_RV_ABSENT            0   /* the key has no such value           */
#define TR_RV_DWORD             1   /* a 4-byte REG_DWORD, in Value         */
#define TR_RV_OTHER             2   /* there, but not a 4-byte REG_DWORD    */
#define TR_RV_UNREAD            3   /* the key or the value did not read    */

typedef struct _TR_RVAL {
    int State;
    TR_U32 Value;
} TR_RVAL;

/* One RegQueryValueEx answer for a 4-byte buffer as a reading, by the
 * drivers' rule (src/xhci_regsrc.h): not found is absent, and so is a
 * zero-length REG_NONE (type 0); a 4-byte REG_DWORD (type 4) is the DWORD;
 * any other stored type, or a value too long for the buffer, is another
 * type; any other error is not read. */
#define TR_Q_OK                 0   /* ERROR_SUCCESS                       */
#define TR_Q_NOT_FOUND          1   /* ERROR_FILE_NOT_FOUND                */
#define TR_Q_MORE_DATA          2   /* ERROR_MORE_DATA                     */
#define TR_Q_ERROR              3   /* anything else                       */
void tr_rv_from_query(int status, TR_U32 type, TR_U32 bytes, TR_U32 value,
                      TR_RVAL *out);

/* Where a device's level was taken from. */
#define TR_LV_FROM_NONE         0   /* neither key has the value            */
#define TR_LV_FROM_MACHINE      1   /* the machine-wide key                 */
#define TR_LV_FROM_DEVICE       2   /* the device's driver key              */

/* A device's level as read: the state of the value in force, and the level
 * that reading stands for - the DWORD when it is 0-4, TR_LV_DEFAULT when no
 * value is there, -1 otherwise (out of range, another type, not read); which
 * key it came from; and both readings. */
typedef struct _TR_LEVEL {
    int State;                  /* TR_RV_* of the value in force           */
    TR_U32 Value;
    int Level;
    int From;                   /* TR_LV_FROM_*                            */
    TR_RVAL Machine;            /* the machine-wide key's reading          */
    TR_RVAL Device;             /* the driver key's reading                */
} TR_LEVEL;

/* The level from the two readings, as the drivers take it: the machine-wide
 * one when it is not absent (another type, or not read, included), else the
 * driver key's. `machine` NULL is absent. */
void tr_level_pick(const TR_RVAL *machine, const TR_RVAL *cur, TR_LEVEL *out);

/* The value a driver's level is: XhciLogVerbosity or XhciCmLogVerbosity. */
const char *tr_level_value_name(int driver);

/* One line for a device's level, at most 72 characters: "3 - adds port
 * status (XhciCmLogVerbosity)", "3 - adds port status (machine-wide)",
 * "2 - the log (default) (no value set)", "7 - out of range, not 0-4
 * (XhciLogVerbosity)", "XhciLogVerbosity is not a REG_DWORD",
 * "XhciLogVerbosity machine-wide is not a REG_DWORD", "not read". */
void tr_level_line(int driver, const TR_LEVEL *l, char *out, size_t cap);

/* The levels block of the report and the dialog: per driver a heading
 * line, the machine-wide value's line (`machine` NULL is absent), then
 * each device's name and its line - with, under a machine-wide value in
 * force, the device's own value that it overrides - or that there is none.
 * A line stays within 79 columns but for a long device name. */
void tr_level_head(TR_TEXT *t, int driver, int count, const TR_RVAL *machine);
void tr_level_device(TR_TEXT *t, int driver, const char *name,
                     const TR_LEVEL *l);

/* 1 when every device of a driver reads the same level 0-4 (*level set);
 * 0 when they differ, one is not 0-4, or there is none. */
int tr_level_common(const TR_LEVEL *l, int n, int *level);

/* "xhci98=<n>" or "xhcicm=<n>", case aside, <n> one digit 0-4: 1 with
 * *driver and *level set, else 0. Anything longer, shorter or other is
 * refused. */
int tr_level_arg(const char *a, int *driver, int *level);

/* A driver key name as the configuration manager gives it, the only form
 * the level is written under: "<class>\<digits>", the class of letters,
 * digits, braces, dashes and underscores, the instance 1-8 digits, one
 * backslash and no other character. */
int tr_driver_key_ok(const char *key);

/* The window's result of an elevated /setlevel run, as tr_elev_result
 * reads an elevated stop's: XHCIINST's exit code, the text in out. */
int tr_level_elev_result(int launched, TR_U32 launchErr, int finished,
                         TR_U32 exitCode, int haveAnswer, int answerCode,
                         const char *answerMsg, char *out, size_t cap);

/* The offer after a level was written: the drivers read it only at
 * start. */
const char *tr_level_restart_question(void);

/* ---- 42-A.5: the safe-removal mark, shown (record 21 section 7.4,
 * revision 3, "The viewer") ------------------------------------------- */

/* A Thunderbolt unit's mark, from the marks answer (cm_query.h CM_QM_*)
 * and its devnode's LowerFilters. */
#define TR_MK_OLD       0   /* this xhcicm.sys does not answer the request */
#define TR_MK_FAILED    1   /* the request failed                          */
#define TR_MK_LISTED    2   /* Removable read set: Windows lists it        */
#define TR_MK_WRITTEN   3   /* the value present, no attachment yet        */
#define TR_MK_NOTLISTED 4   /* the value present and attached, yet Windows
                               does not list it                            */
#define TR_MK_NOT       5   /* no value, no attachment                     */

/* 1 when the multi-string msz (bytes long, possibly unterminated at the
 * end) holds `name` as one of its strings, without case. */
int tr_msz_has(const char *msz, size_t bytes, const char *name);
/* A PCI function's location as the marks answer gives a member: bus 15:8,
 * device 7:3, function 2:0, from its bus number and DevicePropertyAddress
 * (device 31:16, function 15:0). */
INST_U32 tr_mark_member(INST_U32 bus, INST_U32 address);
/* The one entry of m for a unit behind `member` at bus/address, reported
 * and located, and with `pending` set a departure pending too; -1 for
 * none or two. */
int tr_mark_find(const MK_MARKS *m, INST_U32 member, INST_U32 bus,
                 INST_U32 address, int pending);
/* The state (TR_MK_*): `answered` 1 read, 0 not served (an older driver),
 * -1 failed; `entry` tr_mark_find's, with pending 0; filterPresent the
 * devnode's LowerFilters names xhcicm. */
int tr_mark_state(int answered, const MK_MARKS *m, int entry,
                  int filterPresent);
const char *tr_mark_text(int state);

/* The mark service's state, as QueryServiceStatus read it. */
#define TR_SV_UNREAD    0
#define TR_SV_ABSENT    1   /* not installed (ERROR_SERVICE_DOES_NOT_EXIST) */
#define TR_SV_STOPPED   2
#define TR_SV_RUNNING   3
#define TR_SV_OTHER     4   /* starting, stopping, paused                  */
const char *tr_mksvc_text(int state);

#endif /* TR_CORE_H */
