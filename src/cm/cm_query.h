/*
 * cm_query.h - the answer of xhcicm.sys's query device to
 * IOCTL_XHCICM_QUERY (design record 25 section 4): the status record of
 * design record 24 (cm_status.h) as the connection manager's thread last
 * published it, framed by a header that says which start of which NHI it
 * belongs to and how old that pass is, then one extension per router
 * entry with what the record lacks, then the NHI's instance id. Shared
 * byte for byte by the driver (cm_query.c, cm_qryos.c), XHCITR.EXE
 * (xhcitr\tr_core.c) and the host tests, so it uses no DDK or Windows
 * type.
 *
 * Every field is a little-endian 32-bit unsigned value at a fixed offset,
 * or a byte string; the readers take bytes, never a structure overlay.
 * A layout change is a new CM_Q_VERSION: a reader refuses a version it
 * does not know. Version 2 (design record 25 revision 16, roadmap-usb4
 * 43.21) puts the removal block (CM_Q_RM_*) between the header and the
 * record; a reader of version 2 takes version 1 too, which has none.
 */

#ifndef CM_QUERY_H
#define CM_QUERY_H

/* The device: \Device\XhciCm<n>, linked as \DosDevices\XHCICM<n> (created
   in the start IRP's system context, so global on XP and later), n the
   first free of 0..15; its name "XHCICM<n>" as REG_SZ CM_Q_NAME_VALUE in
   the NHI's hardware key, written after the link and written empty before
   it goes (ZwDeleteValueKey has no Windows 98 evidence; cm_qryos.c). An
   empty value is no query device. A reader trusts the mapping only when the answer's instance id equals
   the devnode it read the value from (CM_Q_O_ID_*). */
#define CM_Q_MAX_DEVICES        16UL
#define CM_Q_NAME_VALUE         "XhciCmQueryName"

/* CTL_CODE(FILE_DEVICE_UNKNOWN 0x22, 0xA40, METHOD_BUFFERED, FILE_ANY_ACCESS),
   opened with no access rights; no input.
   - output buffer < CM_Q_HDR_BYTES: STATUS_BUFFER_TOO_SMALL, nothing
     returned;
   - output buffer < the whole answer: STATUS_SUCCESS with the header alone
     (Information = CM_Q_HDR_BYTES), CM_Q_O_BYTES the size needed; the
     reader asks again with that size (bounded by CM_Q_MAX_BYTES);
   - otherwise STATUS_SUCCESS, Information = CM_Q_O_BYTES.
   A reader parses no more than Information bytes. A query device whose NHI
   has stopped or gone answers every request with STATUS_DEVICE_REMOVED
   (STATUS_NO_SUCH_DEVICE on Windows 98 SE and ME) and no data. */
#define CM_Q_IOCTL              0x00222900UL
#define CM_Q_MAX_BYTES          65536UL
#define CM_Q_ASK_BYTES          8192UL  /* the reader's first buffer */

#define CM_Q_MAGIC              0x514D4358UL    /* 'XCMQ' */
#define CM_Q_VERSION            2UL
#define CM_Q_VERSION_1          1UL     /* no removal block               */

/* The header. */
#define CM_Q_HDR_BYTES          64UL
#define CM_Q_O_MAGIC            0UL
#define CM_Q_O_VERSION          4UL
#define CM_Q_O_BYTES            8UL     /* the whole answer */
#define CM_Q_O_PASS             12UL    /* snapshots published by the thread
                                           since this start, counted after
                                           each publish; 0 before the
                                           first; wraps past 0 to 1 */
#define CM_Q_O_PASS_AGE_MS      16UL    /* ms from that publish to this
                                           answer, from a running clock;
                                           CM_Q_UNKNOWN before the first */
#define CM_Q_O_LIVE             20UL    /* CM_LIVE_* (cm_live.h) */
#define CM_Q_O_FLAGS            24UL    /* CM_Q_F_* */
#define CM_Q_O_REC_OFFSET       28UL    /* the status record */
#define CM_Q_O_EXT_OFFSET       32UL    /* the first router extension */
#define CM_Q_O_EXT_SHAPE        36UL    /* count 15:0, entry bytes 31:16 */
#define CM_Q_O_EPOCH_LO         40UL    /* this start of this NHI: the system */
#define CM_Q_O_EPOCH_HI         44UL    /* time at the query device's creation,
                                           made unique per boot; never 0 */
#define CM_Q_O_NHI_BDF          48UL    /* the NHI's bus 15:8, device 7:3,
                                           function 2:0, as the record's
                                           port words */
#define CM_Q_O_ID_OFFSET        52UL    /* the NHI's device instance id, */
#define CM_Q_O_ID_BYTES         56UL    /* ASCII, no NUL, at most 200 */
#define CM_Q_O_CONTENT_GEN      60UL    /* bumped when anything in the answer
                                           but the pass counter, its age and
                                           the record's own generation,
                                           uptime, time and echo changes;
                                           never 0; per start */

#define CM_Q_F_INERT            0x01UL  /* the thread waits with no timeout */
#define CM_Q_F_TRUNCATED        0x02UL  /* more routers than the record holds */
#define CM_Q_F_STOPPING         0x04UL  /* the NHI is stopping: refuse ejects */

/* Display only: an answer older than this is shown as "not current".
   An eject never relies on age (design record 25 section 6.2): it waits
   for CM_Q_O_PASS to move at least CM_Q_NEW_PASSES past the value it
   read when it began (wrap-safe), so the snapshot was built after. */
#define CM_Q_NEW_PASSES         2UL
#define CM_Q_CURRENT_MS         3000UL
#define CM_Q_NEW_PASS_WAIT_MS   3000UL

/* One router extension, in the record's router order. */
#define CM_Q_EXT_BYTES          32UL
#define CM_Q_X_RATE             0UL     /* Gbit/s per lane: 10, 20, 40;
                                           0 unknown */
#define CM_Q_X_WIDTH            4UL     /* lanes: 1, 2; 0 unknown */
#define CM_Q_X_LINK_STATE       8UL     /* the lane adapter's link state as
                                           read, CM_Q_UNKNOWN where not */
#define CM_Q_X_GEN_RAW          12UL    /* the negotiated lane speed code as
                                           read, CM_Q_UNKNOWN where not */
#define CM_Q_X_IDS              16UL    /* vendor 15:0, device 31:16 the
                                           connection manager holds for a
                                           router the record has no ids
                                           for; 0 unknown */
#define CM_Q_X_FLAGS            20UL    /* CM_Q_XF_* */
/* 24..31 reserved, 0 */

#define CM_Q_XF_RATE_READ       0x01UL  /* rate and width read from the lane
                                           adapter by the connection manager
                                           on its own path, not assumed */

#define CM_Q_UNKNOWN            0xFFFFFFFFUL

/*
 * The removal block (version 2; design record 25 section 7.6): at
 * CM_Q_HDR_BYTES, before the record. What this driver's attachments saw
 * Windows do on NT: a surprise removal of a started device an active
 * member's bridge reported, counted once per PDO (cm_marks.h), and the
 * member (tunnel port) of the last one, shown for diagnosis: the notice a
 * reader gives names no unit.
 */
#define CM_Q_RM_BYTES           32UL
#define CM_Q_RM_FLAGS           0UL     /* CM_Q_RF_*                      */
#define CM_Q_RM_COUNT           4UL     /* unexpected removals counted
                                           since this start; wraps        */
#define CM_Q_RM_MEMBER          8UL     /* the last one's member: bus
                                           15:8, device 7:3, function 2:0;
                                           CM_Q_UNKNOWN before the first  */
#define CM_Q_RM_LOST            12UL    /* surprise removals in a loss of
                                           the host interface, counted
                                           apart; wraps                   */
#define CM_Q_RM_DRIVER          16UL    /* the running xhcicm.sys's
                                           version, a byte a field from
                                           the most significant
                                           (xhci_verpk.h); 0 from a driver
                                           before 3.3.0.0, whose word was
                                           reserved (roadmap-usb4 44.8;
                                           design record 25 section 18).
                                           Not a layout change: the
                                           version stays 2              */
/* 20..31 reserved, 0 */

#define CM_Q_RF_WATCHED         0x01UL  /* this domain's removals are
                                           watched (NT, the filter route) */

/*
 * ---- The log answer (design record 25 section 4.5, revision 9) ----
 *
 * A second request on the same device:
 * CTL_CODE(FILE_DEVICE_UNKNOWN 0x22, 0xA41, METHOD_BUFFERED,
 * FILE_ANY_ACCESS), no input, the same access rules and removed-device
 * answer as CM_Q_IOCTL, and its sizing rules with CM_QL_HDR_BYTES for
 * CM_Q_HDR_BYTES. A driver from before revision 9 answers it with
 * STATUS_INVALID_DEVICE_REQUEST.
 *
 * The answer: the header, then the counters, then the log. Both are ASCII
 * text, one record a line, "label=XXXXXXXX" (eight upper-case hex digits;
 * "--------" for a withheld value) ending CR LF, the log ring's own record
 * format (xhci_log.h). No byte outside 0x20..0x7E, CR and LF.
 */
#define CM_QL_IOCTL             0x00222904UL
#define CM_QL_MAGIC             0x4C4D4358UL    /* 'XCML' */
#define CM_QL_VERSION           1UL

#define CM_QL_HDR_BYTES         64UL
#define CM_QL_O_MAGIC           0UL
#define CM_QL_O_VERSION         4UL
#define CM_QL_O_BYTES           8UL     /* the whole answer; in a header
                                           alone, the size to ask for */
#define CM_QL_O_FLAGS           12UL    /* CM_QL_F_* */
#define CM_QL_O_VERBOSITY       16UL    /* XhciLogVerbosity applied */
#define CM_QL_O_VERBOSITY_READ  20UL    /* ... as read from the registry */
#define CM_QL_O_EPOCH_LO        24UL    /* CM_Q_O_EPOCH_* of this start */
#define CM_QL_O_EPOCH_HI        28UL
#define CM_QL_O_CTR_OFFSET      32UL    /* the counters' text */
#define CM_QL_O_CTR_BYTES       36UL
#define CM_QL_O_LOG_OFFSET      40UL    /* the log's text, oldest first */
#define CM_QL_O_LOG_BYTES       44UL
#define CM_QL_O_APPENDS         48UL    /* records the ring accepted since
                                           the NHI's device was added: the
                                           ring keeps its records across a
                                           stop and start */
#define CM_QL_O_DROPPED         52UL    /* bytes the ring's wrap overwrote */
#define CM_QL_O_PASS            56UL    /* the pass the counters are of;
                                           0 before the first */
#define CM_QL_O_PASS_AGE_MS     60UL    /* ms since it; CM_Q_UNKNOWN
                                           before the first */

#define CM_QL_F_RECORDING       0x01UL  /* verbosity >= 2: the ring fills */
#define CM_QL_F_REFUSED         0x02UL  /* the value read was out of range
                                           and refused (the default, 0,
                                           applied) */
#define CM_QL_F_STOPPING        0x04UL  /* the NHI is stopping */
#define CM_QL_F_EMITTED         0x08UL  /* a checked flavour: the thread
                                           hands the ring to DebugView as it
                                           fills, so the log is what it has
                                           not emitted yet */
#define CM_QL_F_LOG_CUT         0x10UL  /* the oldest bytes may be the tail
                                           of a record (the wrap overwrote
                                           bytes, or a checked flavour's
                                           drain took some): the log starts
                                           at the next record */
#define CM_QL_F_CTR_CUT         0x20UL  /* the counters did not all fit */
#define CM_QL_F_WITHHELD        0x40UL  /* a value that is an address was
                                           written "--------" */
#define CM_QL_F_INERT           0x80UL  /* the thread waits with no timeout:
                                           the counters' pass may be old */

#define CM_QL_CTR_MAX           8192UL
#define CM_QL_LOG_MAX           16384UL /* XHCI_LOG_RING_BYTES */
#define CM_QL_MAX_BYTES         (CM_QL_HDR_BYTES + CM_QL_CTR_MAX + \
                                 CM_QL_LOG_MAX)
#define CM_QL_ASK_BYTES         32768UL /* the reader's first buffer */

/*
 * ---- The PCI answer (design record 25 section 4.7, revision 12) ----
 *
 * A third request on the same device:
 * CTL_CODE(FILE_DEVICE_UNKNOWN 0x22, 0xA42, METHOD_BUFFERED,
 * FILE_ANY_ACCESS), no input, the same access rules, removed-device answer
 * and sizing rules as CM_Q_IOCTL with CM_QP_HDR_BYTES for CM_Q_HDR_BYTES.
 * A driver from before revision 12 answers it with
 * STATUS_INVALID_DEVICE_REQUEST.
 *
 * Read at the request, never written: the NHI, each member of its domain's
 * tunnel-port set, and on Windows 2000 and later every function found
 * below a member - its secondary bus and, through each bridge found
 * there, the buses within the member's secondary-to-subordinate range,
 * bounded by CM_QP_MAX_* (a bounded walk, each bus once). On Windows 98 SE
 * and ME (task 42-A.14) the walk below the members is made by the 98
 * CONFIGMG worker through PCI.VXD's guarded read, entering only a bus
 * whose bridge PCI.VXD itself reads with the same numbers or whose
 * numbers the worker's own numbering gave; where it cannot be made (no
 * worker, the worker busy past the request's bound, PCI.VXD's guard
 * refused, the member's controller lost) one entry per member says so
 * (CM_QP_S_NOT_READ_OS, its reason in CM_QP_E_INFO). Each entry is a
 * 32-byte head and the function's
 * configuration space 00h..FFh as read, byte for byte; the reader decodes
 * it. Bus addresses (the BARs and bridge windows) are what the bus holds,
 * the ranges Device Manager's Resources tab shows any user; no kernel
 * address is in the answer.
 */
#define CM_QP_IOCTL             0x00222908UL
#define CM_QP_MAGIC             0x504D4358UL    /* 'XCMP' */
#define CM_QP_VERSION           1UL

#define CM_QP_HDR_BYTES         64UL
#define CM_QP_O_MAGIC           0UL
#define CM_QP_O_VERSION         4UL
#define CM_QP_O_BYTES           8UL     /* the whole answer; in a header
                                           alone, the size to ask for */
#define CM_QP_O_FLAGS           12UL    /* CM_QP_F_* */
#define CM_QP_O_EPOCH_LO        16UL    /* CM_Q_O_EPOCH_* of this start */
#define CM_QP_O_EPOCH_HI        20UL
#define CM_QP_O_ENT_OFFSET      24UL    /* the first entry */
#define CM_QP_O_ENT_SHAPE       28UL    /* count 15:0, entry bytes 31:16 */
#define CM_QP_O_NHI_BDF         32UL    /* as CM_Q_O_NHI_BDF */
#define CM_QP_O_PORTS           36UL    /* tunnel-port members listed */
#define CM_QP_O_READS           40UL    /* configuration reads made */
#define CM_QP_O_FAILS           44UL    /* of them, refused */
/* 48..63 reserved, 0 */

#define CM_QP_F_STOPPING        0x01UL  /* the NHI is stopping */
#define CM_QP_F_TRUNCATED       0x02UL  /* a bound stopped the walk: more
                                           functions or buses than the
                                           answer holds */
#define CM_QP_F_BELOW_OS        0x04UL  /* this system: the functions below
                                           a member are not read */
#define CM_QP_F_NO_SET          0x08UL  /* the domain has no tunnel-port set
                                           (none found, or no route) */
#define CM_QP_F_UNWALKED_SAID   0x10UL  /* every member read whose secondary
                                           bus was not entered, but for one
                                           with no bus numbers (nothing can
                                           be below it), has its
                                           CM_QP_S_NOT_READ_OS entry with a
                                           reason (task 43.63); clear from
                                           an older driver */

/* One entry: the head, then the configuration space. */
#define CM_QP_CFG_BYTES         256UL
#define CM_QP_EHDR_BYTES        32UL
#define CM_QP_ENT_BYTES         (CM_QP_EHDR_BYTES + CM_QP_CFG_BYTES)
#define CM_QP_E_BDF             0UL     /* bus 15:8, device 7:3, function
                                           2:0; CM_Q_UNKNOWN for an entry
                                           that names no function */
#define CM_QP_E_ROLE            4UL     /* CM_QP_R_* */
#define CM_QP_E_STATE           8UL     /* CM_QP_S_* */
#define CM_QP_E_PORT            12UL    /* the member it is below (BELOW),
                                           its own BDF (PORT), else
                                           CM_Q_UNKNOWN */
#define CM_QP_E_PARENT          16UL    /* the bridge whose secondary bus it
                                           is on (BELOW), else CM_Q_UNKNOWN */
#define CM_QP_E_DEPTH           20UL    /* BELOW: 0 on the member's
                                           secondary bus, one more per bridge */
#define CM_QP_E_INFO            24UL    /* PORT: source 7:0 (XHCI_TBT_SRC_*),
                                           flags 15:8 (CM_PORT_*); BELOW in
                                           CM_QP_S_NOT_READ_OS: the reason,
                                           CM_QP_NR_* (0 from a driver
                                           before 42-A.14: this system) */
#define CM_QP_E_VALID           28UL    /* READ: the bytes of configuration
                                           space read, CM_QP_CFG_BYTES or
                                           fewer where the HAL answered
                                           fewer (whole DWORDs); else 0 */
/* The configuration space at CM_QP_EHDR_BYTES: zero past the bytes read,
   all zeros unless the state is CM_QP_S_READ. */

#define CM_QP_R_NHI             1UL
#define CM_QP_R_PORT            2UL     /* a tunnel-port member */
#define CM_QP_R_BELOW           3UL     /* a function below a member */

#define CM_QP_S_READ            1UL     /* the 256 bytes as read */
#define CM_QP_S_NOT_READ_OS     2UL     /* not read on this system */
#define CM_QP_S_FAILED          3UL     /* a read was refused */
#define CM_QP_S_ABSENT          4UL     /* no function answers (all ones) */

/* Why the functions below a member were not read (CM_QP_E_INFO). */
#define CM_QP_NR_SYSTEM         0UL     /* not read on this system        */
#define CM_QP_NR_NO_WORKER      1UL     /* 98 SE / ME: no CONFIGMG worker
                                           running (no route, or not yet
                                           started)                        */
#define CM_QP_NR_BUSY           2UL     /* ... the worker did not answer
                                           within the request's bound (a
                                           configuration-manager call of
                                           its own still running)          */
#define CM_QP_NR_GUARD          3UL     /* ... PCI.VXD's read refused by its
                                           guard                           */
#define CM_QP_NR_HELD           4UL     /* ... the member's controller lost:
                                           no bridge below it is read       */
#define CM_QP_NR_UNADMITTED     5UL     /* ... the member's own bus numbers
                                           not admitted by the walk's rule
                                           (CM_QP_ADMIT, task 43.63): its
                                           secondary bus not entered       */

#define CM_QP_MAX_PORTS         4UL     /* XHCI_TBT_MAX_TUNNELS */
#define CM_QP_MAX_BELOW         32UL    /* functions below, all members */
#define CM_QP_MAX_BUSES         16UL    /* buses walked, per member */
#define CM_QP_MAX_DEPTH         8UL     /* bridges below a member */
#define CM_QP_MAX_ENTRIES       (1UL + CM_QP_MAX_PORTS + CM_QP_MAX_BELOW)
#define CM_QP_MAX_BYTES         (CM_QP_HDR_BYTES + \
                                 CM_QP_MAX_ENTRIES * CM_QP_ENT_BYTES)
#define CM_QP_ASK_BYTES         16384UL /* the reader's first buffer */

/*
 * ---- The marks answer (design record 21 section 7.4, revision 3; task
 * 42-A.5) ----
 *
 * A fourth request on the same device, IOCTL_XHCICM_MARKS:
 * CTL_CODE(FILE_DEVICE_UNKNOWN 0x22, 0xA43, METHOD_BUFFERED,
 * FILE_ANY_ACCESS), no input, the same access rules, removed-device
 * answer and sizing rules as CM_Q_IOCTL with CM_QM_HDR_BYTES for
 * CM_Q_HDR_BYTES. A driver from before 42-A.5 answers it with
 * STATUS_INVALID_DEVICE_REQUEST. What it carries are facts the domain's
 * attachments saw, never an inference about what a user did: one entry per
 * child PDO the domain has seen on an active member since this start, the
 * current ones (in the member bridge's last snapshot) first, then the
 * tombstones (left it). The mark service (xhcimark\) is its reader; the
 * thread keeps a tombstone CM_QM_TOMB_MS and longer while no answer of a
 * marks generation past its departure has been read, up to the set's room.
 */
#define CM_QM_IOCTL             0x0022290CUL
#define CM_QM_MAGIC             0x4D4D4358UL    /* 'XCMM' */
#define CM_QM_VERSION           1UL

#define CM_QM_HDR_BYTES         64UL
#define CM_QM_O_MAGIC           0UL
#define CM_QM_O_VERSION         4UL
#define CM_QM_O_BYTES           8UL     /* the whole answer; in a header
                                           alone, the size to ask for */
#define CM_QM_O_PASS            12UL    /* as CM_Q_O_PASS */
#define CM_QM_O_EPOCH_LO        16UL    /* CM_Q_O_EPOCH_* of this start */
#define CM_QM_O_EPOCH_HI        20UL
#define CM_QM_O_GEN             24UL    /* the marks generation: bumped on
                                           every change of an entry; 0 only
                                           before the first */
#define CM_QM_O_COUNT           28UL    /* entries */
#define CM_QM_O_ENT_BYTES       32UL    /* CM_QM_ENT_BYTES */
#define CM_QM_O_ENT_OFFSET      36UL    /* the first entry */
#define CM_QM_O_FLAGS           40UL    /* CM_QM_F_* */
#define CM_QM_O_DROPPED         44UL    /* tombstones evicted before any
                                           answer showed them */
#define CM_QM_O_NHI_BDF         48UL    /* as CM_Q_O_NHI_BDF */
#define CM_QM_O_CURRENT         52UL    /* of the entries, current ones */
/* 56..63 reserved, 0 */

#define CM_QM_F_INCOMPLETE      0x01UL  /* a current PDO could not be listed
                                           (a set full, a truncated
                                           snapshot): no unit may be read
                                           as departed from this answer */
#define CM_QM_F_STOPPING        0x02UL  /* the NHI is stopping */
#define CM_QM_F_SAFE_EJECT_OFF  0x04UL  /* XhciCmSafeEject 0 on this domain */

/* One entry. */
#define CM_QM_ENT_BYTES         64UL
#define CM_QM_E_MEMBER          0UL     /* the member's bus 15:8, device
                                           7:3, function 2:0 */
#define CM_QM_E_BUS             4UL     /* the child's bus number and */
#define CM_QM_E_ADDRESS         8UL     /* DevicePropertyAddress (device
                                           31:16, function 15:0), once the
                                           location is known, else 0 */
#define CM_QM_E_SERIAL          12UL    /* given at the PDO's first
                                           appearance, per start; never 0 */
#define CM_QM_E_FLAGS           16UL    /* CM_QM_EF_* */
#define CM_QM_E_COUNTS0         20UL    /* query-remove 15:0, cancel 31:16 */
#define CM_QM_E_COUNTS1         24UL    /* surprise removal 15:0, removal
                                           after a query-remove 31:16 */
#define CM_QM_E_COUNTS2         28UL    /* removal with no query-remove
                                           since the last start 15:0, a
                                           start that failed 31:16; each
                                           half saturates at FFFFh */
#define CM_QM_E_PENDING         32UL    /* the thread's clock (ms, never 0)
                                           at the departure after a
                                           query-remove of the last
                                           attachment, 0 for none */
#define CM_QM_E_PENDING_GEN     36UL    /* the port's connection generation
                                           at that departure (USB4 profile;
                                           0 under the ICM client) */
/* 40..43 reserved, 0 */
#define CM_QM_E_CHANGED         44UL    /* the thread's clock at the
                                           entry's last change */
/* 48..63 reserved, 0 */

#define CM_QM_EF_LOCATED        0x01UL  /* bus and address known */
#define CM_QM_EF_REPORTED       0x02UL  /* in the bridge's last snapshot */
#define CM_QM_EF_BUS_ATT        0x04UL  /* an attachment by BusRelations */
#define CM_QM_EF_ADD_ATT        0x08UL  /* an attachment by AddDevice (the
                                           LowerFilters child, or a bridge
                                           filter on the PDO) */
#define CM_QM_EF_REMOVABLE      0x10UL  /* Removable answered set */
#define CM_QM_EF_SAFE_EJECT_OFF 0x20UL  /* the domain's XhciCmSafeEject 0 */

/* The sets are a member's (record 21 section 7.4, revision 3: two sets a
 * member keeps apart, so that neither can crowd the other out), and no
 * member's can crowd another's: CM_QM_MAX_CURRENT current entries and
 * CM_QM_MAX_TOMBS tombstones per member, CM_QM_MAX_MEMBERS members
 * (XHCI_TBT_MAX_TUNNELS). */
#define CM_QM_TOMB_MS           60000UL
#define CM_QM_MAX_MEMBERS       4UL
#define CM_QM_MAX_CURRENT       32UL    /* CM_SNAP_MAX, per member */
#define CM_QM_MAX_TOMBS         32UL    /* per member */
#define CM_QM_MAX_ENTRIES       (CM_QM_MAX_MEMBERS * \
                                 (CM_QM_MAX_CURRENT + CM_QM_MAX_TOMBS))
#define CM_QM_MAX_BYTES         (CM_QM_HDR_BYTES + \
                                 CM_QM_MAX_ENTRIES * CM_QM_ENT_BYTES)

/*
 * The confirmation of an eject (record 21 section 7.4, revision 3,
 * decision M16): XhciCmEjectConfirm, REG_BINARY of CM_QC_BYTES, in the
 * NHI's driver key, written by the mark service for a unit it read held
 * for eject (problem 47, Windows XP and later) and by an administrator's
 * tool for its own successful eject; read by the thread with the status
 * ask, checked, the eject posted once, and the value written empty. A
 * writer writes it only while the value is absent or empty.
 */
#define CM_QC_VALUE             "XhciCmEjectConfirm"
#define CM_QC_BYTES             28UL
#define CM_QC_O_EPOCH_LO        0UL     /* CM_Q_O_EPOCH_* */
#define CM_QC_O_EPOCH_HI        4UL
#define CM_QC_O_MEMBER          8UL     /* CM_QM_E_MEMBER */
#define CM_QC_O_BUS             12UL    /* CM_QM_E_BUS */
#define CM_QC_O_ADDRESS         16UL    /* CM_QM_E_ADDRESS */
#define CM_QC_O_SERIAL          20UL    /* CM_QM_E_SERIAL */
#define CM_QC_O_GEN             24UL    /* CM_QM_E_PENDING_GEN */

#endif /* CM_QUERY_H */
