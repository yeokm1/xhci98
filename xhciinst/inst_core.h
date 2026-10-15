/*
 * inst_core.h - the pure half of XHCIINST.EXE and XHCIIN64.EXE (design
 * record 23 section 10; roadmap-usb4.md task 40.6).
 *
 * Everything here is a function of its arguments: no Windows header, no
 * registry, no device, no file. The tool links inst_core.c and so does the
 * host suite test\test_inst.c, under /Za, so what the suite checks is what
 * the tool runs. C89.
 */

#ifndef INST_CORE_H
#define INST_CORE_H

#include <stddef.h>

typedef unsigned long INST_U32;

/* ---- sizes ---------------------------------------------------------------- */

#define INST_ID_CHARS       208     /* MAX_DEVICE_ID_LEN 200, and a NUL     */
#define INST_LINE_CHARS     1024
#define INST_MAX_IDS        96
#define INST_NAME_CHARS     64

/* ---- architectures and systems -------------------------------------------- */

#define INST_ARCH_X86       0
#define INST_ARCH_AMD64     1

#define INST_PE_I386        0x014CUL
#define INST_PE_AMD64       0x8664UL

/* The system as the decisions see it. */
typedef struct _INST_ENV {
    int Nt;                 /* 1: Windows 2000 and later; 0: 98 SE, ME  */
    INST_U32 Major;
    INST_U32 Minor;
    int Arch;               /* INST_ARCH_*                              */
    int AnyXhci;            /* a present xHCI controller (PCI\CC_0C0330) */
    int PswController;      /* one of them passes XhciPswGate           */
    int CmNhi;              /* a present device xhcicm.inf matches      */
} INST_ENV;

/* ---- strings ---------------------------------------------------------------- */

int inst_upper(int c);
int inst_eq(const char *a, const char *b);
int inst_eqn(const char *a, const char *b, size_t n);
int inst_starts(const char *s, const char *prefix);
void inst_copy(char *dst, const char *src, size_t cap);

/* ---- the INF reader (record 23 section 5.2) ---------------------------------- */

/* The first line after "[section]" (compared without case), or NULL. */
const char *inst_inf_section(const char *text, const char *section);

/* The next non-empty logical line of the section p points into, comments
 * (';' outside quotes) removed and the line trimmed, copied to line; the
 * return points past it, or NULL at the section's end or the text's. */
const char *inst_inf_line(const char *p, char *line, size_t cap);

/* key = value. Returns 1 with both trimmed, 0 for a line with no '='. */
int inst_inf_kv(const char *line, char *key, size_t keyCap,
                char *value, size_t valueCap);

/* Field index (0-based) of a comma-separated value, trimmed and unquoted.
 * Returns 1 when the field exists. */
int inst_inf_field(const char *value, int index, char *out, size_t cap);

/* A %token% expanded from [Strings]; text without % is copied. Returns 1
 * when expanded or copied, 0 when the token is not in [Strings]. */
int inst_inf_expand(const char *text, const char *in, char *out, size_t cap);

/* [Version] DriverVer's version field parsed. Returns 1 on success. */
int inst_inf_driverver(const char *text, INST_U32 ver[4]);

/* ---- versions --------------------------------------------------------------- */

/* "a.b.c.d" (1 to 4 fields; missing ones 0). Returns 1 on success. */
int inst_ver_parse(const char *s, INST_U32 ver[4]);
int inst_ver_cmp(const INST_U32 a[4], const INST_U32 b[4]);

/* ---- models sections (record 23 section 5.2) ---------------------------------- */

/* The models section a system reads for one [Manufacturer] value
 * ("Models[,decoration...]"). Windows 2000 (5.0) reads the undecorated
 * name only; XP onward the most specific decoration whose architecture is
 * the system's (or none) and whose version is at most the system's, the
 * undecorated name when none applies. Returns 1. */
int inst_models_section(const char *mfgValue, const INST_ENV *env,
                        char *out, size_t cap);

/* One [Manufacturer] decoration, the whole token: "NT", then "x86" or
 * "amd64" or nothing, then ".major" and ".minor" (each 0 to 255) or less,
 * and nothing after. 1 valid, with its parts; 0 anything else (another
 * architecture, a product-type or suite field, trailing text). Shared by
 * the architecture check and the section choice. */
#define INST_DECO_NONE      0
#define INST_DECO_X86       1
#define INST_DECO_AMD64     2
int inst_decoration_parse(const char *d, int *arch, INST_U32 *major,
                          INST_U32 *minor);

/* The id list of every models section the system reads, from every
 * [Manufacturer] line: each model line's fields from the second on. */
typedef struct _INST_IDLIST {
    int Count;
    int Overflow;
    char Id[INST_MAX_IDS][INST_ID_CHARS];
} INST_IDLIST;

int inst_inf_models(const char *text, const INST_ENV *env, INST_IDLIST *ids);

/* 1 when the INF's [Manufacturer] lines fit the architecture (record 23
 * section 4.2 item 5): x86 - an undecorated or NTx86 section and no
 * NTamd64; amd64 - only NTamd64 decorations. */
int inst_inf_arch_ok(const char *text, int arch);

/* Every file [SourceDisksFiles] (and its decorated forms) names, one by
 * one: the index-th, copied to out. Returns 1 when it exists. */
int inst_inf_source_file(const char *text, int index, char *out, size_t cap);

/* The index-th [SourceDisksFiles] file's path relative to the INF's own
 * directory: the [SourceDisksNames] path of its disk (field 4), its own
 * subdirectory (field 2), then its name, joined with '\'. Returns 1 when it
 * exists, 0 past the last file, -1 for an entry that cannot be used (no
 * name, a path too long, a drive letter): an error, never the list's end.
 * The caller holds the result to the package directory. */
int inst_inf_source_path(const char *text, int index, char *out, size_t cap);

/* An HKR,,<name>,flags,value line's value in any section: the INF's default
 * for that value. Returns 1 when found. */
int inst_inf_addreg_default(const char *text, const char *name,
                            INST_U32 *value);

/* 1 when the INF is this project's: its [Version] Provider (expanded) is
 * "Yeo Kheng Meng", and it installs one of the services xhci98, xhciuas,
 * xhcicm (AddService) or names one of their files as NTMPDriver. */
int inst_inf_is_ours(const char *text);

/* ---- ids --------------------------------------------------------------------- */

/* 1 when the multi-string (bytes long, double-NUL ended or bounded by
 * bytes) holds id, compared without case. */
int inst_msz_has(const char *msz, size_t bytes, const char *id);

/* Windows 98 SE and ME answer a hardware or compatible id list as one
 * comma-separated string: split it into a multi-string in place (ids hold no
 * comma). cap is the buffer's size; returns the multi-string's length in
 * bytes, the final NUL pair included. */
size_t inst_ids_split(char *buf, size_t cap);

/* The first id of the hardware or compatible list that ids holds, or -1. */
int inst_ids_match(const char *hw, size_t hwBytes, const char *compat,
                   size_t compatBytes, const INST_IDLIST *ids);

/* The vendor and device of a PCI id ("PCI\VEN_vvvv&DEV_dddd..."), as
 * (device << 16) | vendor, the dword XhciPswGate takes. 1 on success. */
int inst_pci_vendev(const char *id, INST_U32 *vendorDevice);

/* The bus's own device ids (record 13 sections 10.13.1 and 10.13.8):
 * 1 current (USB\XHCI98_ROOT_HUB, _HUB, _HUB30, _COMPOSITE and their
 * &VID_ forms), 2 old (XHCI98\...), 0 neither. */
int inst_bus_id(const char *instanceId);

/* ---- the action per device (record 23 section 5.4) ----------------------------- */

#define INST_ACT_INSTALL    0   /* no driver                               */
#define INST_ACT_UPDATE     1   /* this INF's service, older               */
#define INST_ACT_CURRENT    2   /* this INF's service, equal               */
#define INST_ACT_NEWER      3   /* this INF's service, newer: not without /force */
#define INST_ACT_FOREIGN    4   /* another driver: left                    */
#define INST_ACT_FILTER     5   /* tunnel port on pci without our filter   */
#define INST_ACT_UNKNOWN    6   /* ours, version unreadable: refused        */

/* A driver key's DriverVersion: three answers (record 23 section 5.3). */
#define INST_VS_ABSENT      0
#define INST_VS_HAVE        1
#define INST_VS_UNREADABLE  2

typedef struct _INST_DEV {
    int HasService;
    int OwnService;         /* the service is this INF's               */
    int ServiceIsPci;
    int FilterPresent;      /* UpperFilters holds xhcicm               */
    int FilterInf;          /* the matching INF is xhcicmf.inf         */
    INST_U32 Problem;       /* CM_PROB_*, 0 for none                   */
    int VersionState;       /* INST_VS_*                               */
    INST_U32 Version[4];
} INST_DEV;

int inst_action(const INST_DEV *dev, const INST_U32 pkg[4]);

/* Whether the action changes the device: install, update, filter; a
 * current or newer one with force. */
int inst_action_runs(int action, int force);

const char *inst_action_text(int action);

/* The verdict per service group (record 23 section 5.4): every devnode of a
 * service runs one binary. anyNewer / anyUnknown over the group's devnodes,
 * present or not; binary: the installed .sys's version against the package,
 * -1 older, 0 equal, 1 newer, 2 unknown, -2 absent. */
#define INST_GROUP_OK               0
#define INST_GROUP_REFUSED_NEWER    1
#define INST_GROUP_REFUSED_UNKNOWN  2

int inst_group_verdict(int anyNewer, int anyUnknown, int binary, int force);

/* The XHCI98_FLAVOUR_* markers in an image: their count, and the first's
 * suffix (RELEASE, DEBUG, QEMU) in out. */
int inst_flavour_marker(const unsigned char *buf, size_t len, char *out,
                        size_t cap);

/* ---- small pure helpers -------------------------------------------------------- */

/* The PE header's machine field from the file's first bytes. 1 on success. */
int inst_pe_machine(const unsigned char *buf, size_t len, INST_U32 *machine);

/* Whether a PE image carries an embedded (Authenticode) signature: its
 * security directory, inside the optional header, has a size and points to
 * a certificate table inside the buffer (the whole file). 1 yes, 0 no, -1
 * not a readable image or a table outside it. Structure only: no
 * cryptographic check. */
int inst_pe_signed(const unsigned char *buf, size_t len);

/* list;dir appended unless dir is already an entry (without case).
 * Returns 0 present (out = list), 1 appended, -1 out too small. */
int inst_path_append(const char *list, const char *dir, char *out,
                     size_t cap);

/* The list with every entry equal to dir removed. 1 removed, 0 not there,
 * -1 out too small. */
int inst_path_remove(const char *list, const char *dir, char *out,
                     size_t cap);

/* The list with every entry that begins with prefix (without case) removed.
 * 1 removed, 0 none, -1 out too small. */
int inst_path_remove_prefix(const char *list, const char *prefix, char *out,
                            size_t cap);

/* The multi-string with every entry equal to name removed, the rest kept in
 * order. Returns the new length in bytes including the final NUL pair, or 0
 * when nothing is left; *removed says whether name was there. (size_t)-1
 * when out is too small. */
size_t inst_msz_remove(const char *in, size_t inBytes, const char *name,
                       char *out, size_t cap, int *removed);

/* 1 when a SystemStartOptions value holds the word TESTSIGNING. */
int inst_testsigning(const char *options);

/* ---- registry values (record 23 section 8) ---------------------------------------- */

#define INST_INF_XHCI       0
#define INST_INF_UAS        1
#define INST_INF_CM         2
#define INST_INF_CMF        3
#define INST_INF_COUNT      4

#define INST_APPLY_ANY_XHCI 0
#define INST_APPLY_PSW      1
#define INST_APPLY_NT_XHCI  2
#define INST_APPLY_CM       3
#define INST_APPLY_9X_XHCI  4   /* 98 SE and ME with an xHCI controller */

typedef struct _INST_VALUE {
    const char *Name;
    int Inf;                /* INST_INF_*: whose driver key            */
    int Apply;              /* INST_APPLY_*                            */
    INST_U32 Asked;         /* bit n: value n offered when asked       */
    INST_U32 Settable;      /* bit n: value n accepted by /set         */
    const char *Meaning;
    const char *Caution;    /* shown with the question and with /set, or NULL */
    INST_U32 CautionMask;   /* the values the caution is about (0: every one) */
} INST_VALUE;

/* FNV-1a over bytes, continuing from h (start with INST_FNV_START): the
 * local copy's content tag (record 23 section 4.4). */
#define INST_FNV_START 0x811C9DC5UL
INST_U32 inst_fnv1a(INST_U32 h, const unsigned char *buf, size_t len);

extern const INST_VALUE instValues[];
extern const int instValueCount;

/* One value line of a REGEDIT4 file, "name"=hex:aa,bb,... (the silent
 * install's SILENT.REG): 1 with the name and the bytes, 0 for anything
 * else (two hex digits per byte, commas between, nothing after). */
int inst_reg_hex_line(const char *line, char *name, size_t nameCap,
                      unsigned char *data, size_t dataCap, size_t *n);

/* The index of a value by name (without case), or -1. */
int inst_value_find(const char *name);

/* 1 when the question is shown on this system with this hardware (never
 * for a value whose Asked mask is 0: /set only). */
int inst_value_applies(int index, const INST_ENV *env);

/* The values offered when asked on this system (the Asked mask narrowed:
 * XhciInterruptMode 3 on Vista and 7 only). */
INST_U32 inst_value_asked_mask(int index, const INST_ENV *env);

/* The values /set accepts on this system (XhciInterruptMode 3 on Vista and
 * 7 only, as asked). */
INST_U32 inst_value_set_mask(int index, const INST_ENV *env);

/* An answer: 0 keep (empty line), 1 *value set, -1 not an allowed value. */
int inst_value_answer(const char *line, INST_U32 mask, INST_U32 *value);

/* "Name=Value" for /set: 1 with *index and *value, 0 unknown name, -1 a
 * value /set does not accept, -2 malformed. */
int inst_value_set(const char *arg, int *index, INST_U32 *value);

/* The allowed values of a mask as text ("0, 1, 2"). */
void inst_mask_text(INST_U32 mask, char *out, size_t cap);

/* ---- switches and exit codes (record 23 section 7) ----------------------------------- */

#define INST_MAX_SET        16

typedef struct _INST_OPTS {
    int List;
    int Yes;
    int Defaults;
    int Force;
    int Restage;
    int Uninstall;
    int NoPause;
    int SelfTest;
    char Dir[INST_NAME_CHARS];
    char Log[260];
    int SetCount;
    int SetIndex[INST_MAX_SET];
    INST_U32 SetValue[INST_MAX_SET];
} INST_OPTS;

/* Returns 0 with opts filled, 1 refused with why in err. */
int inst_parse_args(int argc, char **argv, INST_OPTS *opts, char *err,
                    size_t errCap);

#define INST_EXIT_DONE      0
#define INST_EXIT_REFUSED   1
#define INST_EXIT_FAILED    2
#define INST_EXIT_RESTART   3
#define INST_EXIT_STEPS     4

int inst_exit_code(int refused, int failed, int restart, int steps);

/* The last line before the exit code, and the code it goes with (record 23
 * section 7.4): nothing changed (or /list) - "No changes were made.", the
 * code as it is; a change and a run that finished (0 or 3) - "Changes were
 * made. Please restart Windows now.", always 3; a change and a run that
 * did not finish (2 or 4) - "Changes were made, but not everything is
 * done ...", the code as it is; a change under a refusal's code (1) is
 * reported as failed part-way (2). Returns the final code. */
#define INST_FINAL_NO_CHANGES   0
#define INST_FINAL_RESTART      1
#define INST_FINAL_UNFINISHED   2
int inst_final(int changed, int list, int code, int *line);
const char *inst_final_text(int line);

/* ---- installing beside Windows' own installer (record 23 section 5.8) ------------ */

/* The settle: CMP_WaitNoPendingInstallEvents asked with no wait every
 * INST_SETTLE_POLL_MS, and a signature of the devices below this package's
 * controllers read each time. Settled once nothing has been pending and the
 * signature has been read whole and has not moved for INST_SETTLE_QUIET_MS
 * - and, while a device is expected (a controller this run updated shows
 * no root hub yet, a device of the bus not configured yet), not before
 * ExpectMs. At LimitMs: pending is a timeout; not pending but not settled
 * (moving, or not readable whole) is unsteady. Never settled without the
 * quiet stretch. */
#define INST_SETTLE_POLL_MS     500UL
#define INST_SETTLE_QUIET_MS    3000UL

#define INST_SETTLE_WAIT        0   /* poll again                            */
#define INST_SETTLE_DONE        1   /* settled                               */
#define INST_SETTLE_TELL        2   /* poll again; say why the wait, once    */
#define INST_SETTLE_TIMEOUT     3   /* still pending at the limit            */
#define INST_SETTLE_FAILED      4   /* the question failed                   */
#define INST_SETTLE_UNSTEADY    5   /* not pending, never quiet, at the limit */

typedef struct _INST_SETTLE {
    INST_U32 LimitMs;       /* overall bound                              */
    INST_U32 TellMs;        /* pending this long: TELL, once              */
    INST_U32 ExpectMs;      /* how long an expected device is waited for  */
    INST_U32 QuietFrom;     /* when the quiet stretch began               */
    INST_U32 PendingFrom;   /* when the pending stretch began             */
    INST_U32 Sig;
    int Quiet;              /* in a quiet stretch                         */
    int Pending;            /* in a pending stretch                       */
    int Told;
    int Polls;
    int Moved;              /* pending seen, or the signature moved, or a
                               reading incomplete: something happened     */
} INST_SETTLE;

void inst_settle_init(INST_SETTLE *s, INST_U32 limitMs, INST_U32 tellMs,
                      INST_U32 expectMs);

/* One poll: nowMs since the settle began, pending 1 / 0 / -1 (the question
 * failed), sig the signature now and complete 1 when it was read whole,
 * expect 1 while a device is expected. */
int inst_settle_step(INST_SETTLE *s, INST_U32 nowMs, int pending,
                     INST_U32 sig, int complete, int expect);

/* The device tree as the settle reads it, through the configuration
 * manager's routines (or a test's): each returns INST_TREE_OK,
 * INST_TREE_NONE (CR_NO_SUCH_DEVINST: no child, no further sibling) or
 * INST_TREE_ERROR (anything else). Status's problem is only meaningful
 * with INST_DN_HAS_PROBLEM set, so the walk masks it. */
#define INST_TREE_OK            0
#define INST_TREE_NONE          1
#define INST_TREE_ERROR         2

#define INST_DN_STARTED         0x00000008UL
#define INST_DN_HAS_PROBLEM     0x00000400UL
#define INST_PROB_NOT_CONFIGURED 1UL
#define INST_PROB_REINSTALL     18UL

typedef struct _INST_TREE_OPS {
    void *Ctx;
    int (*Child)(void *ctx, INST_U32 dn, INST_U32 *child);
    int (*Sibling)(void *ctx, INST_U32 dn, INST_U32 *sibling);
    int (*Id)(void *ctx, INST_U32 dn, char *id, size_t cap);
    int (*Status)(void *ctx, INST_U32 dn, INST_U32 *status, INST_U32 *problem);
} INST_TREE_OPS;

/* The problem number as reported: 0 unless the status has a problem. */
INST_U32 inst_dn_problem(INST_U32 status, INST_U32 problem);

/* The devnodes below dn into the signature *h (ids, started, problem);
 * *expect set for a device of the bus (USB\XHCI98_...) not configured yet
 * (Code 1 or 18). 1 when read whole; 0 when a reading failed or the walk
 * was cut short (a subtree below INST_TREE_DEPTH, more than
 * INST_TREE_NODES devnodes in *nodes): the signature is then not to be
 * trusted. */
#define INST_TREE_DEPTH         8
#define INST_TREE_NODES         2048
int inst_tree_sig(const INST_TREE_OPS *ops, INST_U32 dn, int depth,
                  INST_U32 *h, int *expect, int *nodes);

/* An install call that failed because Windows was installing the same
 * device at that moment (ERROR_SHARING_VIOLATION, 0x20, read on the 2000
 * leg): 1 when it is tried again after a settle; tries counts the busy
 * answers so far, this one included. */
#define INST_BUSY_TRIES         3
int inst_busy_retry(INST_U32 error, int tries);

/* 1 when a device a call would reach has had INST_BUSY_TRIES busy answers
 * already: the whole call is not made (one call reaches the whole id
 * group, a member that joined later included). */
int inst_busy_spent(const int *tries, int n);

/* Windows 2000 after a tunnel port's first filter install succeeded in
 * this run (owner, 2026-10-15): a device of the connection manager's INFs
 * (inf INST_INF_CM or INST_INF_CMF) that is gone, or at Code 12, is waiting
 * for the restart - read on the 2000 leg, all started after it - not a
 * failure. 1 when it is so. */
int inst_tunnel_restart(int win2000, int filterInstalled, int inf, int gone,
                        INST_U32 problem);

/* A device's state as the results print it: "xhci98 3.0.0.0, started",
 * "no driver, problem code 28", "xhci98 2.3.1.0, not started". A device
 * with a problem code is never called started. */
void inst_state_text(char *out, size_t cap, const char *service,
                     const char *version, int started, INST_U32 problem);

/* 1 when SetupCopyOEMInf's answer (a name, or a path whose last part is the
 * name) is the oem*.inf found byte-identical before the call, from a local
 * copy this run did not make: staged already, which is no change. */
int inst_staged_again(int hadIdentical, int copiedNow, const char *before,
                      const char *answer);

/* The confirmation question: devices to change, else option values to
 * write, else neither; nt 0 for 98 SE and ME (the copy and DevicePath);
 * unsure 1 when an entry listed apart may still need a step (98 SE and
 * ME, record 23 section 6.3): then no claim that every device is current. */
const char *inst_confirm_text(int runs, int writes, int nt, int unsure);

/* ---- the uninstall's selection and order (record 23 section 9.2) ---------------------- */

/* 1 when the service is one of this package's (xhci98, xhciuas, xhcicm). */
int inst_own_service(const char *service);

/* The removal rank: lower goes first. -1 when the devnode is not removed. */
int inst_remove_rank(const char *instanceId, const char *service,
                     int filterPresent);

#endif /* INST_CORE_H */
