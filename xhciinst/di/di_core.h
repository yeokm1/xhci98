/*
 * di_core.h - the pure part of XHCIDI.EXE, the Windows 98 SE and ME device
 * install helper (roadmap-usb4.md task 42-A.1, design record 27 section 11).
 *
 * C89 with no Windows header and no far pointer: Open Watcom's 16-bit
 * compiler builds it into the helper (large model, where every pointer is
 * far by default and an int is 16 bits), and MSVC 6.0 builds it into the
 * host suite test\test_di.c. Nothing here assumes an int wider than 16
 * bits; a value that may exceed 65535 is an unsigned long.
 *
 * What it holds: the command line of record 27 section 4.1, the result
 * file's lines of section 4.2 (formatted one at a time into a caller's
 * buffer, the system part writing and flushing each before the next), the
 * setup engine's error names and the values the helper tests (read from the
 * 98 SE DDK's setupx.h, which test_di.c checks against the header where it is
 * present), and the verdict rule of section 5.4.
 */

#ifndef XHCI_DI_CORE_H
#define XHCI_DI_CORE_H

/*
 * Lengths. A device id is at most cfgmgr32's MAX_DEVICE_ID_LEN (200); a path
 * at most 255 characters, the length of the one atom the engine keeps the
 * INF's path in (DEVICE_INFO.atDriverPath); a version string at most 31.
 */
#define DI_DEVID_MAX        200
#define DI_PATH_MAX         255
#define DI_VERSION_MAX      31
#define DI_LINE_MAX         1024

#define DI_JOB_NONE         0
#define DI_JOB_INSTALL      1
#define DI_JOB_REMOVE       2
#define DI_JOB_PROBE        3

#define DI_FLAG_FORCE       0x0001
#define DI_FLAG_QUIET       0x0002

/*
 * What di_parse found, in the order it looks: the result file first (with
 * no usable path nothing can be reported, and the helper exits without a
 * file), then the version (section 4.1: compared before anything else is
 * read), then the rest of the job.
 */
#define DI_PARSE_OK         0
#define DI_PARSE_NOFILE     1
#define DI_PARSE_VERSION    2
#define DI_PARSE_COMMAND    3

typedef struct _DI_CMD {
    int job;
    char version[DI_VERSION_MAX + 1];
    char result[DI_PATH_MAX + 1];
    unsigned long hwnd;
    char devid[DI_DEVID_MAX + 1];
    char inf[DI_PATH_MAX + 1];
    unsigned flags;
    const char *why;
} DI_CMD;

int di_parse(const char *cmdline, const char *own_version, DI_CMD *cmd);

int di_path_ok(const char *path, int want_inf);
int di_devid_ok(const char *devid);

int di_enumerator(const char *devid, char *out, unsigned size);
int di_subkey_matches(const char *subkey, const char *devid);
int di_inf_dir(const char *inf, char *out, unsigned size);
int di_same_inf(const char *atom_text, const char *inf);
const char *di_basename(const char *path);
int di_system_supported(const char *version_number);
int di_ieq(const char *a, const char *b);

/*
 * The setup engine's values the helper tests or writes, as setupx.h has them
 * (the 98 SE DDK's inc16\setupx.h, at the SHA-256 docs\references\README.md
 * records). test_di.c reads each again from the header.
 */
#define DI_OK                       0
#define DI_ERR_DO_DEFAULT           506
#define DI_ERR_USER_CANCEL          507
#define DI_ERR_DONT_INSTALL         524
#define DI_FL_NEEDRESTART           0x00000080UL
#define DI_FL_NEEDREBOOT            0x00000100UL
#define DI_FL_ENUMSINGLEINF         0x00010000UL
#define DI_FL_CLASSINSTALLPARAMS    0x00100000UL
#define DI_FL_QUIETINSTALL          0x00800000UL
#define DI_DNF_OLDDRIVER            0x00000002UL
#define DI_DNF_CURRENT_DRIVER       0x00000100UL

typedef struct _DI_ERRNAME {
    unsigned value;
    const char *name;
} DI_ERRNAME;

extern const DI_ERRNAME di_errnames[];
extern const unsigned di_errname_count;
const char *di_err_name(unsigned err);

/*
 * The verdict words of section 5.4 and section 9, and the probe's. The
 * probe (section 4.1) does nothing but write its file; its verdict, probed,
 * says it ran to its end on a system it accepts.
 */
#define DI_V_INSTALLED          "installed"
#define DI_V_INSTALLED_RESTART  "installed-restart"
#define DI_V_REMOVED            "removed"
#define DI_V_REMOVED_RESTART    "removed-restart"
#define DI_V_CANCELLED          "cancelled"
#define DI_V_NO_MATCH           "no-match"
#define DI_V_NOT_PRESENT        "not-present"
#define DI_V_NOT_FOUND          "not-found"
#define DI_V_FAILED             "failed"
#define DI_V_REFUSED            "refused"
#define DI_V_PROBED             "probed"

const char *di_verdict(int job, unsigned err, unsigned long flags);

/*
 * The helper's own failures that are no engine error: the class
 * installer's choice left no selected driver, or one from another INF (with
 * DI_ENUMSINGLEINF neither can happen if the engine does what the flag's
 * block says, so each is reported, never worked around); and a node that is
 * the installed driver's, met without the force flag (section 5.2: the
 * reinstall of a current driver is the installer's decision, which it
 * states with force). Written as "failed <name> 0".
 */
#define DI_SELF_NOSELECT        "XHCIDI_NO_SELECTION"
#define DI_SELF_OTHERINF        "XHCIDI_OTHER_INF"
#define DI_SELF_NOATOM          "XHCIDI_NO_ATOM"

/*
 * The result file, one line at a time. di_out_* format the next line into
 * out->line (CRLF-terminated), count it and return it. A field that would
 * not fit is left out whole and out->overflow is set; the lengths above
 * keep every line of section 4.2 well inside DI_LINE_MAX. di_out_end
 * writes "END <n>", n being the number of lines before it.
 */
typedef struct _DI_OUT {
    char line[DI_LINE_MAX];
    unsigned len;
    unsigned count;
    int overflow;
} DI_OUT;

void di_out_init(DI_OUT *out);
const char *di_out_header(DI_OUT *out, const char *version);
const char *di_out_job(DI_OUT *out, const DI_CMD *cmd);
const char *di_out_found(DI_OUT *out, int found);
const char *di_out_driver(DI_OUT *out, const char *inf_name,
                          const char *section, const char *description,
                          unsigned rank, unsigned dos_date);
const char *di_out_call(DI_OUT *out, const char *routine, const char *arg,
                        unsigned err);
const char *di_out_flags(DI_OUT *out, unsigned long flags);
const char *di_out_node(DI_OUT *out, unsigned long node_flags, int force);
const char *di_out_ids(DI_OUT *out, const char *value_name,
                       const char *value);
const char *di_out_error(DI_OUT *out, const char *name, unsigned err);
const char *di_out_result(DI_OUT *out, const char *verdict, unsigned err);
const char *di_out_result_text(DI_OUT *out, const char *verdict,
                               const char *detail);
const char *di_out_refused(DI_OUT *out, const char *reason,
                           const char *detail);
const char *di_out_end(DI_OUT *out);

#endif
