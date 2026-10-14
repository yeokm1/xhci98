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

/* The index of a value by name (without case), or -1. */
int inst_value_find(const char *name);

/* 1 when the question is shown on this system with this hardware. */
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
    int Clean;
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

/* ---- the uninstall's selection and order (record 23 section 9.2) ---------------------- */

/* 1 when the service is one of this package's (xhci98, xhciuas, xhcicm). */
int inst_own_service(const char *service);

/* The removal rank: lower goes first. -1 when the devnode is not removed. */
int inst_remove_rank(const char *instanceId, const char *service,
                     int filterPresent);

#endif /* INST_CORE_H */
