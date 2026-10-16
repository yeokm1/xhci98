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
 * "Yeo Kheng Meng", it installs one of the services xhci98, xhciuas,
 * xhcicm (AddService) or names one of their files as NTMPDriver, and it
 * neither installs the bundled driver's service nor names one of its files
 * as PortDriver or NTMPDriver (Stor98's INF has the same Provider). */
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

/* 98 SE and ME: a UAS entry at Code 2 (CM_PROB_DEVLOADER_FAILED) while
 * System32\Drivers holds no USBNTMAP.SYS, the upper filter xhciuas.inf names.
 * minor: 10 for 98 SE, 90 for ME. INST_UASFIX_ME_STICK: ME copies the file
 * from its own cabinets when its first ordinary stick installs, so a stick
 * and then a replug of the disk; INST_UASFIX_NUSB: 98 SE has it only from
 * NUSB's mass-storage component. Anything else: INST_UASFIX_NONE. */
#define INST_UASFIX_NONE            0
#define INST_UASFIX_ME_STICK        1
#define INST_UASFIX_NUSB            2

int inst_uas_fix_9x(int nt, INST_U32 minor, int isUas, INST_U32 problem,
                    int ntmapPresent);

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
    int Thunderbolt;        /* /thunderbolt: the Thunderbolt support, unasked */
    int NoTray;             /* /notray: XHCITR not started at login     */
    int NoPause;
    int SelfTest;
    int Devices;            /* /devices (record 24)                     */
    int Eject;              /* /eject                                   */
    INST_U32 EjectNum;      /* /eject N: N, 0 for none                  */
    char EjectId[INST_ID_CHARS];  /* /eject id=...: the instance id     */
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

/* 1 when a file of the flavour directory is the bundled Stor98 driver's
 * (a separate project: every name of its files there starts with "stor98",
 * package-common.ps1). The tool installs none of them and copies none into
 * its local copy, which is on the hardware wizard's search path. */
int inst_bundled_file(const char *name);

/* 1 when the service is the bundled Stor98 driver's (stor98). */
int inst_bundled_service(const char *service);

/* The removal rank: lower goes first. -1 when the devnode is not removed. */
int inst_remove_rank(const char *instanceId, const char *service,
                     int filterPresent);

/* ---- the Thunderbolt and USB4 device viewer and eject (record 24) ------------- */

/* The filter INF's model ids (src\cm\xhcicmf.inf), the candidate tunnel
 * ports: the host suite checks the table against the INF both ways. */
extern const char *const instTbPortIds[];
extern const int instTbPortIdCount;

/* 1 when the hardware id list holds one of instTbPortIds. */
int inst_tb_candidate(const char *hw, size_t hwBytes);

/* Record 24 section 4.3's host functions: inst_tb_is_nhi - the service
 * xhcicm, or an id of class 0880 or 0C0340; inst_tb_is_usbhc - an id of
 * class 0C03 (a USB host controller) that is not an NHI. */
int inst_tb_is_nhi(const char *hw, size_t hwBytes, const char *compat,
                   size_t compatBytes, const char *service);
/* 1 when an id of the list has CC_<cls...> at a field start. */
int inst_msz_has_class(const char *msz, size_t bytes, const char *cls);
int inst_tb_is_usbhc(const char *hw, size_t hwBytes, const char *compat,
                     size_t compatBytes, const char *service);

/* Windows 98's PCI instance ids (runs/run-39.md): the last component is
 * BUS_bb&DEV_dd&FUNC_ff on bus 0, else the devfn path in hex, leaf first,
 * ending with the root bus. inst_id_location: 1 with bus 15:8, device 7:3,
 * function 2:0 for the BUS_ form. inst_id_path: 1 with up to `room` path
 * elements for the path form. */
int inst_id_location(const char *id, INST_U32 *bdf);
int inst_id_path(const char *id, unsigned char *path, int *count, int room);

/* 1 when a 98 instance id names the member at `bdf` whose record path is
 * path[0..pathCount-1] (pathCount 0: unknown, so only the BUS_ form can
 * match). */
int inst_tb_loc_match(const char *id, INST_U32 bdf, const unsigned char *path,
                      int pathCount);

/* The status record (src\cm\cm_status.h). */
INST_U32 inst_get32(const unsigned char *b, size_t offset);

/* 1 when the record's magic, version, sizes and counts check, for `n`
 * bytes read. */
int inst_st_check(const unsigned char *b, size_t n);

/* Diagnostic only (record 24 section 3.3): 1 when the boot time a record
 * implies (its system time less its uptime) is within five minutes of the
 * one the tool computes (now less the tick count). Times are FILETIME
 * words (100 ns since 1601). Currency is the echo's, never this. */
int inst_st_fresh(INST_U32 recUptimeS, INST_U32 recTimeLo, INST_U32 recTimeHi,
                  INST_U32 nowLo, INST_U32 nowHi, INST_U32 tickMs);

/* The echo check (record 24 section 3.3): 1 when the record answers the
 * ask the tool wrote (a nonzero ask equal to the echo at offset 124). */
int inst_st_confirmed(const unsigned char *b, INST_U32 ask);

/* A walked tree, nodes in walk order (a parent before its children),
 * parent[i] -1 for the root (record 24 section 4.3).
 * inst_tb_protect: prot[i] set for every NHI and its ancestors, and for a
 * USB host controller whose grandparent is an NHI's grandparent.
 * inst_tb_units: unitOf[i] the index of the unit node i belongs to (i
 * itself for a unit: a child of a verified port that is not itself one),
 * or -1. inst_tb_unit_blocked: 1 when unit u's subtree holds a protected
 * node. */
void inst_tb_protect(const int *parent, const int *isNhi, const int *isUsbHc,
                     int n, int *prot);
void inst_tb_units(const int *parent, const int *verified, int n,
                   int *unitOf);
int inst_tb_unit_blocked(const int *unitOf, const int *prot, int n, int u);

/* The outcomes of an eject, with their exit codes (record 24 sections 7
 * and 8.2). */
#define INST_EJ_DONE            0   /* stopped: exit 0                       */
#define INST_EJ_VETO            1   /* refused by Windows: exit 4            */
#define INST_EJ_CHANGED         2   /* the list changed: exit 1              */
#define INST_EJ_UNSUPPORTED     3   /* the call is not offered: exit 1       */
#define INST_EJ_RIGHTS          4   /* access denied: exit 1                 */
#define INST_EJ_FAILED          5   /* failed after acceptance, or unknown: 2 */
int inst_eject_code(int outcome);

#define INST_CR_INVALID_DEVNODE         0x05UL
#define INST_CR_NO_SUCH_DEVNODE         0x0DUL
#define INST_CR_REMOVE_VETOED           0x17UL
#define INST_CR_ACCESS_DENIED           0x33UL
#define INST_CR_CALL_NOT_IMPLEMENTED    0x34UL

/* 98 SE and ME: the outcome of a query-remove that was not accepted, and
 * its text. */
int inst_eject9x_query(INST_U32 cr, const char **text);

/* NT: the outcome of CM_Request_Device_EjectA's answer and its text; for a
 * veto, `vetoType` (PNP_VETO_TYPE) chooses the text, and *withName says
 * whether the text takes the veto name (one %s). */
int inst_eject_nt(INST_U32 cr, INST_U32 vetoType, const char **text,
                  int *withName);

/* NT: 1 when an eject request refused as PNP_VetoIllegalDeviceRequest is
 * for a unit whose capabilities were read and say neither Removable nor
 * SurpriseRemovalOK, so the same query-remove and remove are asked by
 * CM_Query_And_Remove_SubTreeA (record 24 section 7.2; a veto still stops
 * that call). */
int inst_eject_nt_fallback(INST_U32 cr, INST_U32 vetoType, int capsKnown,
                           INST_U32 caps);

/* The step when that routine is not implemented either (Windows 2000, read
 * on the leg): exit 4, nothing changed. */
const char *inst_not_removable_text(void);

/* 98 SE and ME: the outcome of the removal that follows an accepted
 * query, and its text (record 24 section 7.1 item 4). */
int inst_eject9x_remove(INST_U32 cr, const char **text);

/* The advice after a successful eject (record 24 section 5.4):
 * INST_UNPLUG_NOW only when the walk after it was whole, no other unit
 * behind the port is still running, the unit was a single storage
 * function (singleFunction: no PCI function below it, not a bridge or a
 * USB controller, of class 01), and the router's USB3 tunnel is known to
 * be none (usb3: CM_ST_TUN_* of a confirmed record, 5 when unknown). */
#define INST_UNPLUG_NOW     0
#define INST_UNPLUG_OTHERS  1
#define INST_UNPLUG_CHECK   2
#define INST_UNPLUG_RESTARTED 3 /* the ejected unit started again: exit 4 */
int inst_unplug_kind(int walkWhole, int othersLeft, int singleFunction,
                     INST_U32 usb3);
const char *inst_unplug_text(int kind);

/* ---- the Thunderbolt 3 / USB4 question (record 23 section 8.5) ---------------- */

/* Whether the run installs and stages the Thunderbolt driver (xhcicm.inf,
 * nt\xhcicmf.inf, xhcicm.sys). haveCm: the package carries it; nhiPresent:
 * a device xhcicm.inf matches is present. Returns the decision when no
 * question is asked (*ask 0), else 0 with *ask 1 and *defaultYes the
 * answer Enter gives:
 *   no driver in the package     no question, not included;
 *   /thunderbolt                 no question, included;
 *   /yes, /list                  no question, the default (present: yes,
 *                                absent: no);
 *   otherwise                    asked, default yes when present, no when
 *                                absent (a laptop may show its controller
 *                                only when a device is connected).
 * A no never removes or updates a Thunderbolt driver already installed: it
 * only leaves this package's out of the copy, the store and the run. */
int inst_tb_decide(int haveCm, int nhiPresent, int optYes, int optThunderbolt,
                   int optList, int *ask, int *defaultYes);

/* The answer to the question: 1 yes, 0 no (Enter: the default), -1 not an
 * answer (asked again). */
int inst_tb_answer(const char *line, int defaultYes);

/* ---- the USB tree tool (record 23 section 9.5, record 25 section 8) ---------- */

/* The contract with XHCITR: its hidden tray window's class, the event that
 * asks every running copy to quit (NT: in the Global namespace, so a copy
 * in another session hears it too), and the Run value that starts it. */
#define INST_TRAY_CLASS         "Xhci98TrTray"
#define INST_TRAY_QUIT_NT       "Global\\Xhci98TrQuit"
#define INST_TRAY_QUIT_9X       "Xhci98TrQuit"
#define INST_TRAY_RUN_KEY       "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define INST_TRAY_RUN_VALUE     "xHCI98 USB Tree"
#define INST_TRAY_CLOSE_MS      15000UL

/* The tool's file name for an architecture: XHCITR.EXE (x86, 98 SE and ME
 * included) or XHCITR64.EXE. */
const char *inst_tray_file(int arch);

/* The Run value's data, "\"<exe>\" /tray": 1, or 0 when it does not fit
 * or exe holds a quote. */
int inst_tray_run_data(const char *exe, char *out, size_t cap);

/* Whether the package's tool is copied (record 25 section 8). installed:
 * the installed copy's version as file_version answers it (1 read, 0 the
 * file absent, -1 unreadable); pkg the same for the package's copy. Copied
 * when the installed copy is absent, or when the package's version is read
 * and is newer than the installed copy's (an unreadable one counts as
 * older); otherwise left. */
int inst_tray_copy_needed(int installed, const INST_U32 instVer[4], int pkg,
                          const INST_U32 pkgVer[4]);

/* A .BAK left by an interrupted replacement on 98 SE or ME (record 25
 * section 8): with no tool beside it, it is the tool and is renamed back
 * (a failure stops the tool's part of the run before any version is
 * compared); beside the tool it is deleted. */
#define INST_TRAY_BAK_NONE      0
#define INST_TRAY_BAK_RESTORE   1
#define INST_TRAY_BAK_DELETE    2
int inst_tray_leftover(int bakExists, int exeExists);

/* What a copy over the tool came to: in place; left, because the tool was
 * still running after it was asked to close (the run ends with steps
 * left); or failed. */
#define INST_TRAY_PUT_FAILED    0
#define INST_TRAY_PUT_DONE      1
#define INST_TRAY_PUT_IN_USE    2

/* What becomes of the Run value. runState: 0 absent, 1 the wanted data, 2
 * other data; inPlace: the tool's file is at its installed path (the value
 * is written only then). */
#define INST_TRAY_RUN_KEEP      0
#define INST_TRAY_RUN_SET       1
#define INST_TRAY_RUN_DELETE    2
int inst_tray_run_action(int noTray, int runState, int inPlace);

#endif /* INST_CORE_H */
