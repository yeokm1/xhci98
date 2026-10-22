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
 * xhcicm (AddService) or names one of their files as NTMPDriver, and none
 * of its fields, %tokens% expanded, names the bundled driver (starts with
 * "stor98": Stor98's INF has the same Provider). */
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

/* 1 when name is a local copy's directory name (record 23 section 4.4):
 * eight hex digits, or the same with ".NEW" while it is built. */
int inst_copy_name(const char *name);

/* The list with every entry that is a local copy directly under root
 * (root\<8 hex digits>, without case) removed; another entry under root is
 * kept, so a directory the user chose is never taken out wholesale. 1
 * removed, 0 none, -1 out too small. */
int inst_path_remove_copies(const char *list, const char *root, char *out,
                            size_t cap);

/* The multi-string with every entry equal to name removed, the rest kept in
 * order. Returns the new length in bytes including the final NUL pair, or 0
 * when nothing is left; *removed says whether name was there. (size_t)-1
 * when out is too small. */
size_t inst_msz_remove(const char *in, size_t inBytes, const char *name,
                       char *out, size_t cap, int *removed);

/* A filter list value (LowerFilters or UpperFilters, read as a
 * multi-string of inBytes; a REG_SZ is passed with its NUL pair) with
 * every entry equal to `name` taken out, the other names kept in order
 * (record 21 section 7.4, the CriticalDeviceDatabase scrub, and the
 * uninstall's step 0): INST_FD_NONE when `name` is not in it (nothing to
 * write), INST_FD_SET with out[0..*outBytes) the value to write back,
 * INST_FD_DELETE when nothing else is left (the value is deleted, never
 * written empty), INST_FD_ERROR when out (cap bytes) is too small. */
#define INST_FD_NONE        0
#define INST_FD_SET         1
#define INST_FD_DELETE      2
#define INST_FD_ERROR       3
int inst_filter_drop(const char *in, size_t inBytes, const char *name,
                     char *out, size_t cap, size_t *outBytes);

/* The CriticalDeviceDatabase key, under HKEY_LOCAL_MACHINE. Windows 2000
 * and XP copy a boot-critical class's device-specific filter values into
 * its entries there (pci#cc_0604, pci#cc_010802) when they install such a
 * device, and every new device of that class then gets them. */
#define INST_CDDB_KEY "SYSTEM\\CurrentControlSet\\Control\\CriticalDeviceDatabase"

/* Whether a CriticalDeviceDatabase entry goes with an uninstall: 1 when
 * its Service value (service, NULL when it has none or it is not a
 * string) names, ignoring case, one of the gone[0..n-1] services this run
 * removed or found absent. Windows writes such an entry when it installs a
 * boot-critical device (the NHI's id and its class, read on 2000, XP and
 * 7); left behind it makes the device's next install start from a service
 * that no longer exists (Code 19). An entry naming another driver's
 * service is never one. */
int inst_cddb_service_gone(const char *service, const char *const *gone,
                           int n);

/* 1 when a SystemStartOptions value holds the word TESTSIGNING. */
int inst_testsigning(const char *options);

/* ---- registry values (record 23 section 8) ---------------------------------------- */

#define INST_INF_XHCI       0
#define INST_INF_UAS        1
#define INST_INF_CM         2
#define INST_INF_CMF        3
#define INST_INF_S98        4   /* the bundled Stor98, when ticked */
#define INST_INF_COUNT      5

#define INST_APPLY_ANY_XHCI 0
#define INST_APPLY_PSW      1
#define INST_APPLY_NT_XHCI  2
#define INST_APPLY_CM       3
#define INST_APPLY_9X_XHCI  4   /* 98 SE and ME with an xHCI controller */
#define INST_APPLY_9X_CM    5   /* 98 SE and ME with a device xhcicm.inf matches */

/* The values a choice list names (0 to 4: XhciFastPollFsLs's 4). */
#define INST_CHOICES        5

typedef struct _INST_VALUE {
    const char *Name;
    int Inf;                /* INST_INF_*: whose driver key            */
    int Apply;              /* INST_APPLY_*                            */
    INST_U32 Asked;         /* bit n: value n offered when asked       */
    INST_U32 Settable;      /* bit n: value n accepted by /set         */
    const char *Meaning;
    const char *Caution;    /* shown with the question and with /set, or NULL */
    INST_U32 CautionMask;   /* the values the caution is about (0: every one) */
    const char *Choice[INST_CHOICES]; /* what value n means, or NULL (owner,
                               2026-10-16: each choice on its own line with
                               its meaning) */
    /* Record 26 section 11 (task 42-A.2): a value whose allowed set is a
     * range rather than a list (Hi nonzero: Lo to Hi, the masks unused),
     * and a value the INFs do not write (NoInf: Def is its default, the
     * driver's built-in one). */
    INST_U32 Lo;
    INST_U32 Hi;
    int NoInf;
    INST_U32 Def;
} INST_VALUE;

/* The most values the table holds (the answers' arrays). */
#define INST_MAX_VALUES     32

/* Where the installer writes a value (record 26 section 5.4, task 42-A.17;
 * owner, 2026-10-18): the machine-wide key both drivers read first, under
 * HKLM. A per-controller value is left as it is. */
#define INST_MW_KEY         "System\\CurrentControlSet\\Services\\USB"

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

/* 1 when v is a value /set (and the window's Advanced dialog) may give
 * value `index` on this system: in its range, or in its /set mask. */
int inst_value_allowed(int index, const INST_ENV *env, INST_U32 v);

/* 1 when value `index` concerns this system and hardware, asked or not
 * (the window's Advanced rows): its Apply rule's hardware and system. */
int inst_value_relevant(int index, const INST_ENV *env);

/* 1 when value `index` is shown greyed on this system (owner, 2026-10-19):
 * its hardware is here and its system is not (a 98 SE and ME value on
 * 2000 to 7, a 2000-to-7 value on 98 SE and ME), or its controller is not
 * one the port switch is for. The window shows such a row disabled, with
 * inst_value_only's words, and it never writes. */
int inst_value_greyed(int index, const INST_ENV *env);

/* What a greyed row says it is for ("Windows 98 SE and ME only"), or NULL
 * for a value every system takes. */
const char *inst_value_only(int index);

/* The value's default: the INF's AddReg line, or, for a value no INF
 * writes (NoInf), the table's. 1 when there is one. */
int inst_value_default(int index, const char *infText, INST_U32 *def);

/* An answer: 0 keep (empty line), 1 *value set, -1 not an allowed value. */
int inst_value_answer(const char *line, INST_U32 mask, INST_U32 *value);

/* "Name=Value" for /set: 1 with *index and *value, 0 unknown name, -1 a
 * value /set does not accept, -2 malformed. */
int inst_value_set(const char *arg, int *index, INST_U32 *value);

/* The allowed values of a mask as text ("0, 1, 2"). */
void inst_mask_text(INST_U32 mask, char *out, size_t cap);

/* ---- the console (owner, 2026-10-16) ------------------------------------------------- */

/* Every line the tool prints is at most INST_COLUMNS wide: a line of 80
 * wraps an extra blank line on the 98 and NT consoles. The tool's output
 * goes through a streaming word wrap: a line too long is broken at its
 * last space within the width, the rest indented under the line's text (a
 * line that starts with "- ", "1. ", "[1] " or "NOTE: " hangs under what
 * follows the marker; a first word followed by two spaces, as a choice's
 * key is, hangs under the text after it; any other line two spaces in). A
 * word longer than the room left is broken after its last backslash that
 * fits, or at the width. Spaces at the end of what was fed are held back
 * until a word follows or the text is flushed, so a wrap never leaves
 * them dangling. */
#define INST_COLUMNS        79

typedef struct _INST_WRAP {
    size_t Col;             /* the column the next character goes to     */
    size_t Lead;            /* leading spaces of this logical line       */
    size_t Cont;            /* the indent of its continuation lines      */
    int InLead;             /* still reading the leading spaces          */
    int Words;              /* words placed on this logical line         */
    int Wrapped;            /* this logical line has wrapped             */
    int Hung;               /* its hanging indent is set by a key        */
    size_t Spaces;          /* spaces held before the next word          */
    size_t WordLen;
    char Word[INST_LINE_CHARS];
    INST_U32 Wraps;         /* lines broken so far (a test's measure)    */
    size_t MaxCol;          /* the widest line so far                    */
} INST_WRAP;

void inst_wrap_init(INST_WRAP *w);

/* Text fed through the wrap: what is to be printed now is appended to out
 * (NUL-terminated; cap counts the NUL, and what does not fit is dropped).
 * Returns the length of out. */
size_t inst_wrap_feed(INST_WRAP *w, const char *text, char *out, size_t cap);

/* The word and spaces held back, printed: before input is read, and at the
 * end. */
size_t inst_wrap_flush(INST_WRAP *w, char *out, size_t cap);

/* The line was ended by the user's Enter (the console echoes it): the next
 * character starts a new line. */
void inst_wrap_input(INST_WRAP *w);

/* The whole text wrapped at once (a fresh wrap, flushed). */
size_t inst_wrap_text(const char *text, char *out, size_t cap);

/* The widest line of a text, in columns. */
size_t inst_text_width(const char *text);

/* ---- the questions (owner, 2026-10-16) ------------------------------------------------ */

/* Every question lists its choices one per line, indented, each with its
 * meaning, and its last line says what Enter gives: "Enter for [x]: ",
 * where x is what Enter results in. */

/* One device a value question is about: State 1 the value is set (Value),
 * 0 not set (the install writes the INF's default), -1 a new install (the
 * default). */
typedef struct _INST_VNOW {
    int State;
    INST_U32 Value;
    const char *Desc;
} INST_VNOW;

/* A registry value's question (record 23 section 8.3), wrapped, into out.
 * mask: the values offered; def: the INF's default; cmNewNote: a new
 * install of the Thunderbolt driver, so XhciCmMode and XhciCmApprove take
 * the answer only from the restart; list: /list, so the last line says
 * what Enter would give instead of asking. Enter keeps each device's value:
 * returns 1 with *enter the value it gives when that is one value for
 * every device, 0 when they differ ("each keeps its own"). */
int inst_value_question(char *out, size_t cap, int index, INST_U32 mask,
                        INST_U32 def, const INST_VNOW *now, int n,
                        int cmNewNote, int list, INST_U32 *enter);

/* A value's caution as shown: a "%s" in it names the values it is about
 * that are among values (CautionMask & values), "1" or "1 or 3", so the
 * note never speaks of a value the question does not offer. */
void inst_caution_text(int index, INST_U32 values, char *out, size_t cap);

/* A yes-or-no question: the question, a line each for Y and N with their
 * meaning, and "Enter for [Y]: " or "Enter for [N]: ". */
size_t inst_yesno_question(char *out, size_t cap, const char *question,
                           const char *yes, const char *no, int defaultYes);

/* The install directory's question (record 23 section 9.6): a line for
 * each thing that goes there - toolName, the USB tree tool, when the
 * package carries it; setupName, the installer itself (record 26 section
 * 6.2), NULL for none; copyName, on 98 SE and ME the package copy the
 * hardware wizards search - where they are now (NULL on a first install),
 * the 98 SE and ME naming rule, and "Enter for [<def>]: " (or, with list,
 * what Enter would give). */
size_t inst_dir_question(char *out, size_t cap, const char *toolName,
                         const char *setupName, const char *copyName,
                         const char *now, const char *def, int nt, int list);

/* The install directory checked as text (record 23 section 9.6). path is
 * what was typed or given with /to; on success out holds it in the form
 * kept: surrounding quotes and spaces removed, '/' as '\', no trailing
 * backslash. Refused: anything but "X:\name[\name...]"; a drive's root;
 * the characters " * ? < > | ; and a control character, and ':' after the
 * drive; a "." or ".." part, or a part ending in a space or a dot; longer
 * than INST_DIR_CHARS; inside (or equal to) the Windows directory, unless
 * it is exactly `allowed` (the directory an earlier install used, kept);
 * inside (or equal to) the package directory pkgRoot; and on 98 SE and ME
 * (nt 0) any part that is not an 8.3 name of letters, digits and _ - $ ~
 * ! # & @ ^ ' ( ) { } - no spaces - because the hardware wizards' "Copy
 * files from" and DevicePath take it as typed. Returns INST_DIR_OK or the
 * reason, which inst_dir_reason words. */
#define INST_DIR_CHARS      100
#define INST_DIR_OK         0
#define INST_DIR_FORM       1
#define INST_DIR_ROOT       2
#define INST_DIR_CHAR       3
#define INST_DIR_PART       4
#define INST_DIR_LONG       5
#define INST_DIR_WINDOWS    6
#define INST_DIR_PACKAGE    7
#define INST_DIR_NAME83     8
int inst_dir_check(const char *path, int nt, const char *windir,
                   const char *pkgRoot, const char *allowed, char *out,
                   size_t cap);
const char *inst_dir_reason(int code);

/* 1 when path is dir or lies inside it (without case; a whole part). */
int inst_path_within(const char *path, const char *dir);

/* The default install directory: the root of the drive the system is on
 * (sysDrive "C:", or the Windows directory's drive) and XHCI98. */
void inst_dir_default(const char *sysDrive, const char *windir, char *out,
                      size_t cap);

/* The registry record of the install directory (record 23 section 9.6). */
#define INST_DIR_KEY        "Software\\xhci98"
#define INST_DIR_VALUE      "InstallDir"
#define INST_DIR_MOVEFROM   "MoveFrom"

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
    int CmOnly;             /* /cmonly: the connection manager alone (record
                               23 section 4.5)                           */
    int Stor98;             /* /stor98: the bundled Stor98, unasked      */
    int NoStor98;           /* /nostor98: Stor98 left out of a run that
                               has the Thunderbolt support (record 23
                               section 7.1, revision 23)                */
    int NoTray;             /* /notray: XHCITR not started at login     */
    int NoUas;              /* /nouas: xhciuas.inf left out of the run  */
    int Storage;            /* /storage: the USB storage drivers, 98 SE
                               only (record 26 section 6.5.9)          */
    int NoStorage;          /* /nostorage: left out, on any system      */
    int NoPause;
    int SelfTest;
    int Devices;            /* /devices (record 24)                     */
    int Eject;              /* /eject                                   */
    INST_U32 EjectNum;      /* /eject N: N, 0 for none                  */
    char EjectId[INST_ID_CHARS];  /* /eject id=...: the instance id     */
    char Dir[INST_NAME_CHARS];
    char Log[260];
    char To[260];           /* /to <path>: the install directory, unasked */
    char From[260];         /* /from <dir>: the uninstall's relaunch from the
                               install directory (record 26 section 9.2) */
    int Debug;              /* /debug: the debug- directory (the window's
                               own choice; INSTALL.BAT's for the console) */
    int Console;            /* /console: INSTALL.BAT's word, the console's */
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

/* The same line where the run asks for a shutdown and a fresh start
 * instead of a restart (98 SE and ME after a Stor98 update, record 26
 * section 5.9); the exit codes are unchanged. */
const char *inst_final_text_cold(int line, int shutdown);

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

/* A parent's job and the devices below it (98 SE update over 3.0.0.0 with
 * an external hub, 2026-10-19: the root hub's reinstall restarted it, its
 * hub was gone for about four seconds, and the hub's job, made at once,
 * found it not present). A parent is a controller or a device of the bus
 * (root hub, hub, composite entry): its job may restart the devices below
 * it. Outcomes are INST_DI_OUT_* (NT's install call mapped onto them). */
int inst_parent_kind(const char *instanceId, int controller);

/* 1 when the tree settles before the next job of the same pass: the job
 * just made was a parent's, it reached its device (not absent, not
 * refused), and another job of the pass follows. */
int inst_settle_after_job(int parent, int outcome, int more);

/* 1 when a job that found its device not present is made once more after
 * a settle: a parent's job ran earlier in this run (parentJobs), and this
 * device has not been tried again yet (retries). A device that is still
 * not present then is "not present"; one settle is all it costs. */
int inst_absent_retry(int outcome, int parentJobs, int retries);

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

/* The window's Summary headline (matrix N1, 2026-10-19): runs, a device
 * installed or updated; writes, a setting the pages chose (the drivers'
 * or Stor98's); other, any other change the page lists (a component
 * added, the tools folder moved). "Every device is current" only when
 * nothing at all changes. */
const char *inst_summary_head(int runs, int writes, int other, int nt);

/* XhciCmMode 0 turns the Thunderbolt 3 and USB4 support off (owner,
 * 2026-10-19: a wheel turned it off unnoticed on real hardware): 1 when
 * the value written is that one, so the Summary and the console say
 * instCmOffText. */
int inst_cm_mode_off(const char *name, INST_U32 value);

/* The Thunderbolt row's note when a controller was found (owner,
 * 2026-10-19): "A Thunderbolt 3 / USB4 controller was found: <name>."
 * with the name cut to INST_TB_NAME_SHOWN characters, then the memory
 * sentence of INST_PT_TB_T; with no name, INST_PT_TB_T as it is. */
#define INST_TB_NAME_SHOWN  36
void inst_tb_found_text(char *out, size_t cap, const char *name);
extern const char instCmOffText[];

/* ---- the uninstall's selection and order (record 23 section 9.2) ---------------------- */

/* 1 when the service is one of this package's (xhci98, xhciuas, xhcicm). */
int inst_own_service(const char *service);

/* 1 when a file of the flavour directory is the bundled Stor98 driver's
 * (every name of its files there starts with "stor98", package-common.ps1).
 * The tool installs none of them and copies none into
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

#define INST_CR_INVALID_FLAG            0x04UL

/* NT: 1 for a unit not marked removable now - its capabilities were read
 * and say neither Removable nor SurpriseRemovalOK, whatever the cause (the
 * first plug of a device Windows has not seen behind a tunnel, a stack
 * rebuilt since, roadmap EJRB.1): Safely Remove Hardware does not list
 * it. */
int inst_unit_not_marked(int capsKnown, INST_U32 caps);

/* NT: an eject routine's answer as the outcome tables read it. Windows XP
 * answers a vetoed CM_Request_Device_EjectA with CR_SUCCESS and the veto
 * type set, so a veto type other than PNP_VetoTypeUnknown (0) turns
 * success into CR_REMOVE_VETOED. The caller sets the type to 0 first. */
INST_U32 inst_eject_cr(INST_U32 cr, INST_U32 vetoType);

/* NT: what follows CM_Request_Device_EjectA's answer (through
 * inst_eject_cr) for a unit (record 24 section 7.2). notMarked: from
 * inst_unit_not_marked; mayRemove: 0 when the caller is known to lack
 * the right the fallback needs (an unelevated token on Vista and 7). For
 * a unit not marked removable, Windows' refusal as an illegal request
 * leads to CM_Query_And_Remove_SubTree - the same query-remove and
 * remove, a veto still stopping it - and a rights refusal to
 * inst_admin_text. */
#define INST_NT_ANSWER      0   /* the answer is the outcome: inst_eject_nt */
#define INST_NT_FALLBACK    1   /* ask CM_Query_And_Remove_SubTree         */
#define INST_NT_ADMIN      2   /* refuse: inst_admin_text, exit 1        */
int inst_eject_nt_step(INST_U32 cr, INST_U32 vetoType, int notMarked,
                       int mayRemove);

/* NT: the outcome of CM_Query_And_Remove_SubTree's answer (through
 * inst_eject_cr) and its text, as inst_eject_nt, except that a rights
 * refusal gives inst_admin_text (exit 1) and CR_CALL_NOT_IMPLEMENTED
 * inst_not_removable_text (exit 4, nothing changed). */
int inst_eject_nt_removal(INST_U32 cr, INST_U32 vetoType, const char **text,
                          int *withName);

/* The fallback's flags: CM_REMOVE_UI_NOT_OK | CM_REMOVE_NO_RESTART, which
 * 2000, XP, Vista and 7 accept. inst_removal_retry: 1 when an answer of
 * CR_INVALID_FLAG to `flags` is to be asked again with
 * CM_REMOVE_UI_NOT_OK alone (that refusal changes nothing). */
#define INST_REMOVE_UI_NOT_OK       0x00000001UL
#define INST_REMOVE_NO_RESTART      0x00000002UL
int inst_removal_retry(INST_U32 cr, INST_U32 flags);

/* The refusal for a unit not marked removable when the caller lacks the
 * right the fallback needs: exit 1, nothing changed. Promises nothing of
 * a re-plug. */
const char *inst_admin_text(void);

/* The system's kind (record 23 sections 4.3 and 4.5, revision 13): a full
 * target, Windows 8 and 8.1 for the connection manager alone, or refused.
 * nt and the version as RtlGetVersion (or GetVersionEx before 8) reads
 * them; build is the build number's low word. */
#define INST_OS_REFUSED     0
#define INST_OS_FULL        1
#define INST_OS_CMONLY      2
int inst_os_kind(int nt, INST_U32 major, INST_U32 minor, INST_U32 build);
const char *inst_os_name(int nt, INST_U32 major, INST_U32 minor);

/* The USB drivers (xhci98.sys, xhciuas.sys; record 23 section 4.5): 1 in
 * the run, 0 left out, -1 refused (why in *refusal). *offered: the window's
 * box can be unticked. */
int inst_usb_decide(int osKind, int nt, int haveCm, int cmOnly, int *offered,
                    const char **refusal);

/* The UAS disk driver, xhciuas.sys (owner, 2026-10-19): a child of the
 * USB drivers, in the run only with them and unless /nouas or its
 * unticked box leaves it out. Left out, xhciuas.inf is neither staged nor
 * installed and an xhciuas already installed stays as it is. */
int inst_uas_decide(int usbInclude, int noUas);

/* The step when that routine is not implemented either: exit 4, nothing
 * changed. */
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
/* The text of a kind, for the tool the user is in (roadmap 43.8): only
 * INST_UNPLUG_RESTARTED's differs, naming how to eject again. */
#define INST_TOOL_CONSOLE   0   /* XHCIINST /eject */
#define INST_TOOL_TREE      1   /* the xHCI98 USB Tree */
const char *inst_unplug_text(int kind, int tool);

/* The USB3 tunnel of the one router behind an active port of a checked
 * status record (inst_st_check): CM_ST_TUN_*, 5 when the port is not an
 * active member or not exactly one router is behind it. */
INST_U32 inst_port_usb3(const unsigned char *rec, INST_U32 portBdf);

/* Before the eject (roadmap 43.8): 1 when the confirmation carries
 * inst_eject_remind_text, i.e. when the unit as it is now would not get
 * INST_UNPLUG_NOW after it. */
int inst_eject_remind(int singleFunction, INST_U32 usb3);
const char *inst_eject_remind_text(void);

/* ---- the Thunderbolt 3 / USB4 question (record 23 section 8.5) ---------------- */

/* Where a flavour directory keeps the tunnel-port filter's INF, relative to
 * it: nt\xhcicmf.inf in the x86 package, out of the way of Windows 98 and
 * ME, which read a $Windows NT$ INF (task 39-0.4); xhcicmf.inf, beside
 * xhcicm.inf and xhcicm.sys, in the x64 package, which 98 and ME never
 * read. inst_cmf_pnf is the device installer's precompiled copy of it, and
 * inst_cmf_where the place as the run's text names it for Have Disk. */
const char *inst_cmf_rel(int arch);
const char *inst_cmf_pnf(int arch);
const char *inst_cmf_where(int arch);

/* Whether the run installs and stages the Thunderbolt driver (xhcicm.inf,
 * the filter INF, xhcicm.sys). haveCm: the package carries it; nhiPresent:
 * a device xhcicm.inf matches is present. Returns the decision when no
 * question is asked (*ask 0), else 0 with *ask 1 and *defaultYes the
 * answer Enter gives:
 *   no driver in the package     no question, not included;
 *   /thunderbolt                 no question, included;
 *   /yes, /list                  no question, the default: no;
 *   otherwise                    asked, default no, present or not (owner,
 *                                2026-10-19: the support is experimental
 *                                and ticked only on purpose; nhiPresent
 *                                is said, it no longer decides).
 * A no never removes or updates a Thunderbolt driver already installed: it
 * only leaves this package's out of the copy, the store and the run. */
int inst_tb_decide(int haveCm, int nhiPresent, int optYes, int optThunderbolt,
                   int optList, int *ask, int *defaultYes);

/* The answer to the question: 1 yes, 0 no (Enter: the default), -1 not an
 * answer (asked again). */
int inst_tb_answer(const char *line, int defaultYes);

/* Whether the run's text is also written to standard error (record 23
 * section 7.6): 1 when standard output is not the console (redirected to
 * a file or a pipe) while the answers come from the console's keyboard
 * and standard error is the console - otherwise each question, and the
 * closing "Press Enter", waits for a key with its text only in the file.
 * Each argument is 1 when that handle is the console. */
int inst_out_mirror(int outIsConsole, int errIsConsole, int inIsConsole);

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

/* /list's words for the start at login (98 SE leg, 2026-10-19: after an
 * update with the tool unticked, /list said "started at login" with no Run
 * value): what the value is now (runState as above), then what a run with
 * the same switches would do to it (action, INST_TRAY_RUN_*). */
const char *inst_tray_list_text(int runState, int action);

/* 98 SE and ME: SETUPX records each INF\OTHER copy it makes as a value of
 * this key, named by the copy's path, the INF's own name its data. 1 when
 * value name is the path of a copy the uninstall deleted (path, or its
 * short alias shortPath; either may be NULL or empty), case aside. */
#define INST_OEM_NAME_KEY \
    "Software\\Microsoft\\Windows\\CurrentVersion\\Setup\\SetupX\\INF\\OEM Name"
int inst_oem_name_ours(const char *valueName, const char *path,
                       const char *shortPath);

/* ---- the machine-wide value (record 26 section 5.4; task 42-A.17) ------------ */

/* The value question with the machine-wide value beside each device's own:
 * mwState 1 set (mwValue), 0 not set, -1 unreadable (said, and treated as
 * not set). Set, it is the value in force on every device: "now: N, set
 * machine-wide", each device whose own value differs listed under it as
 * not used, and Enter gives N. Not set: inst_value_question's form, a
 * mixed state "each keeps its own". Returns as inst_value_question. */
int inst_value_question_mw(char *out, size_t cap, int index, INST_U32 mask,
                           INST_U32 def, const INST_VNOW *now, int n,
                           int mwState, INST_U32 mwValue, int cmNewNote,
                           int list, INST_U32 *enter);

/* 1 when an answer writes the machine-wide value: it differs from the
 * value in force - the machine-wide one where set, else each device's
 * own (its value, or the default where it is not set). 0: nothing to
 * write, every device runs with the answer already. */
int inst_mw_write_needed(int mwState, INST_U32 mwValue, const INST_VNOW *now,
                         int n, INST_U32 def, INST_U32 answer);

/* ---- the 98 SE and ME helper, XHCIDI.EXE (record 27 sections 4 and 5.4) ---- */

#define INST_DI_NAME            "XHCIDI.EXE"
#define INST_DI_RESULT_NAME     "XHCIDI.RES"

#define INST_DI_PROBE           0
#define INST_DI_INSTALL         1
#define INST_DI_REMOVE          2

/* A 16-bit program's command tail holds 126 characters (record 27 section
 * 4.1, read on 98 SE: CreateProcessA failed at 127), so the job travels in
 * a job file and the command line is "<version> <job file>": the version
 * at most 31 characters, the job file's path at most 80, 8.3 in every
 * part. */
#define INST_DI_TAIL_CHARS      126
#define INST_DI_JOB_PATH_CHARS  80
#define INST_DI_JOB_BYTES       2048
#define INST_DI_JOB_NAME        "DIJOB.TXT"
#define INST_DI_JOB_ROOT_NAME   "XHCIDIJ.TXT"

/* 1 when p is drive-rooted and every part of it an 8.3 name. */
int inst_di_path(const char *p);

/* The job file's path: <dir>\DIJOB.TXT when that fits in 80 characters
 * and is 8.3, else XHCIDIJ.TXT at the root of dir's drive. 1, or 0. */
int inst_di_jobpath(char *out, size_t cap, const char *dir);

/* The helper's command tail: "<version> <job file>", one space between,
 * no quoting. 1, or 0 when either cannot be passed. */
int inst_di_cmdline(char *out, size_t cap, const char *version,
                    const char *jobFile);

/* The job file for one job (record 27 section 4.1, format 1), CRLF:
 * XHCIDI-JOB 1, JOB, RESULT, HWND, and for an install and a removal
 * DEVICE, for an install INF and FLAGS (force, quiet; the line left out
 * for neither), then END. Every operand checked: the paths 8.3 in every
 * part and at most 255 characters, the INF ending .INF, the id at most
 * 200 characters with no space, the handle 0 to 65535; the whole at most
 * 2048 bytes. 1 with the text in out; 0 when the job cannot be written. */
int inst_di_jobfile(char *out, size_t cap, int job, const char *result,
                    unsigned long hwnd, const char *devId, const char *inf,
                    int force, int quiet);

/* The verdicts (record 27 section 5.4 and section 9), and NONE: the
 * helper did not finish (no END line, a count that differs, another
 * version or job, an unknown word). */
#define INST_DI_V_NONE              0
#define INST_DI_V_INSTALLED         1
#define INST_DI_V_INSTALLED_RESTART 2
#define INST_DI_V_CANCELLED         3
#define INST_DI_V_NO_MATCH          4
#define INST_DI_V_NOT_PRESENT       5
#define INST_DI_V_FAILED            6
#define INST_DI_V_REFUSED           7
#define INST_DI_V_REMOVED           8
#define INST_DI_V_REMOVED_RESTART   9
#define INST_DI_V_NOT_FOUND         10
#define INST_DI_V_PROBED            11

typedef struct _INST_DI_RESULT {
    int Verdict;            /* INST_DI_V_*                                 */
    int Ended;              /* the END line is there                       */
    int Lines;              /* the lines before END                        */
    int HaveFlags;
    INST_U32 Flags;         /* the FLAGS line: DEVICE_INFO.Flags after it  */
    INST_U32 Number;        /* failed: the RETERR                          */
    char Error[48];         /* failed: the ERR_DI_* name                   */
    char Detail[128];       /* refused: the reason; failed: what follows   */
} INST_DI_RESULT;

/* 1 when the text holds an END line (the end the wait looks for). */
int inst_di_ended(const char *text);

/* The result file read for the job that was started: its first line
 * "XHCIDI <version>" equal to the installer's, its JOB line naming the
 * job and its device id (and, for an install, the INF; later fields not
 * compared), one RESULT line with a known word for the job, and END with
 * the number of lines above it. Returns the verdict (r filled), or
 * INST_DI_V_NONE. A RESULT refused is taken whatever the version line
 * says. A probe's one RESULT word is "probed" (the helper's; record 27
 * names none). */
int inst_di_parse(const char *text, const char *version, int job,
                  const char *devId, const char *inf, INST_DI_RESULT *r);

/* What the installer does with a verdict (record 26 section 7, record 27
 * section 5.4): */
#define INST_DI_OUT_OK          0   /* done: read the device back           */
#define INST_DI_OUT_RESTART     1   /* done, a restart pending              */
#define INST_DI_OUT_STEPS       2   /* the steps page, exit 4               */
#define INST_DI_OUT_FAILED      3   /* the steps page, exit 2               */
#define INST_DI_OUT_ABSENT      4   /* not present: "recorded, not running" */
#define INST_DI_OUT_REFUSED     5   /* the helper's route fails this run    */
int inst_di_outcome(int job, int verdict);

/* 1 when the verdict is not one the helper reported: the helper did not
 * finish (no END line, a count that differs), which is an install left
 * unfinished with steps, never a failure of the device and never
 * "nothing to do" (98 SE update, 2026-10-19). */
int inst_di_unfinished(int verdict);

/* The verdict as one phrase ("installed", "the helper did not finish"). */
const char *inst_di_verdict_text(int verdict);

/* ---- the installer's own files and the uninstall (record 26 sections 6.2, 9) --- */

/* The window and the console tool of an architecture: XHCIWIZ.EXE and
 * XHCIINST.EXE (x86, 98 SE and ME included), XHCIWZ64.EXE and
 * XHCIIN64.EXE (x64). which 0 the window, 1 the console tool. */
#define INST_OWN_WINDOW         0
#define INST_OWN_CONSOLE        1
const char *inst_self_file(int arch, int which);

/* The Add/Remove Programs entry (record 26 section 9.1). */
#define INST_ARP_KEY \
    "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\xhci98"
#define INST_PUBLISHER          "Yeo Kheng Meng"

/* "xHCI98 USB 3.x drivers <version>", ending " (incomplete: run again)"
 * for an uninstall that stopped part-way; with cmOnly (no xhci98 service
 * registered, xhcicm's registered: record 26 section 9.1, draft 8) "xHCI98
 * Thunderbolt 3 / USB4 connection manager <version>". */
void inst_arp_name(char *out, size_t cap, const char *version, int incomplete,
                   int cmOnly);
/* 1 when the entry names the connection manager alone: the xhci98 service
 * read as absent (0), and xhcicm's registered (1) or this run one without
 * the USB drivers (runCmOnly: a staging-only run registers no service);
 * -1 is unreadable. The uninstall's warning takes runCmOnly 0. */
int inst_arp_cm_only(int xhciService, int cmService, int runCmOnly);

/* The UninstallString: the window in dir, /uninstall. NT quotes the path;
 * 98 SE and ME, whose install directory is 8.3 with no space, take it as
 * it is. 1, or 0 when it does not fit or holds a quote. */
int inst_arp_command(char *out, size_t cap, const char *dir, const char *exe,
                     int nt);

/* The uninstall's route (record 26 section 9.2): run here, relaunch a
 * temporary copy (started from the install directory, no /from), or
 * refuse (a /from that does not name the install directory, or given to
 * a run that is itself in it). */
#define INST_UN_RUN             0
#define INST_UN_RELAUNCH        1
#define INST_UN_REFUSE          2
int inst_uninstall_route(int selfInInstallDir, int fromGiven,
                         int fromIsInstallDir);

/* The temporary copy's command line: "<exe>" /uninstall /from "<dir>",
 * and /log "<log>", /nopause and /yes when this run had them (the console
 * tool's unattended uninstall stays unattended). 1, or 0 when it does not
 * fit. */
int inst_relaunch_cmd(char *out, size_t cap, const char *exe,
                      const char *fromDir, const char *log, int nopause,
                      int yes);

/* ---- the window's switches (record 26 section 10) ------------------------- */

/* NULL when the window takes the switches parsed into opts, else the
 * refusal: the console's switches ("use INSTALL /console ..."). argv as
 * given, for the switches the parser does not keep (/console). */
const char *inst_window_refusal(const INST_OPTS *opts, int argc, char **argv);

/* ---- the window's texts (record 26 sections 5 and 11) --------------------- */

/* Every fixed text a page shows, with the size of the control it sits in
 * (dialog units, xhciwiz.rc): the host suite checks each fits its control
 * at the dialog font's average width, and the window's self-test renders
 * them. Width 0: shown in a message box, or in a box that scrolls. */
#define INST_PT_WELCOME         1
#define INST_PT_WAIT            2
#define INST_PT_FOUND           3
#define INST_PT_DRIVERS         4
#define INST_PT_DRIVERS_T       5
#define INST_PT_TB              6
#define INST_PT_TB_T            7
#define INST_PT_TB_ABSENT       8
#define INST_PT_TRAY            9
#define INST_PT_TRAY_T          10
#define INST_PT_DIR             11
#define INST_PT_DIR_9X          12
#define INST_PT_USB             13
#define INST_PT_TBSET           14
#define INST_PT_TB_LATER        15
#define INST_PT_ADVANCED        16
#define INST_PT_SUMMARY         17
#define INST_PT_PROGRESS        18
#define INST_PT_STEPS           19
#define INST_PT_NOW             20
#define INST_PT_LATER           21
#define INST_PT_UN_TITLE        22
#define INST_PT_UN_WARN         23
#define INST_PT_UN_AGREE        24
#define INST_PT_UN_SUMMARY      25
#define INST_PT_GATE            26
#define INST_PT_S98             27
#define INST_PT_S98_T           28
#define INST_PT_S98SET          29
#define INST_PT_S98_UNUSED      30
#define INST_PT_SHUT_NOW        31
#define INST_PT_SHUT_LATER      32
/* Record 26 draft 8: the connection manager alone (record 23 section 4.5). */
#define INST_PT_WELCOME_CM      33
#define INST_PT_DRIVERS_T8      34  /* 8 and 8.1: not offered              */
#define INST_PT_DRIVERS_OFF     35  /* 2000 to 7: unticked                 */
#define INST_PT_UN_TITLE_CM     36
#define INST_PT_UN_WARN_CM      37
#define INST_PT_UN_AGREE_CM     38
#define INST_PT_UAS             39  /* the UAS box (owner, 2026-10-19)    */
#define INST_PT_UAS_T           40
/* Record 26 section 6.5 (task 43.4): the USB storage drivers, 98 SE. */
#define INST_PT_STOR            41
#define INST_PT_STOR_T          42

typedef struct _INST_PAGE_TEXT {
    int Id;
    const char *Text;
    INST_U32 Width;         /* the control, in dialog units             */
    INST_U32 Height;
} INST_PAGE_TEXT;

extern const INST_PAGE_TEXT instPageTexts[];
extern const int instPageTextCount;

const char *inst_page_text(int id);

/* The lines a text takes, word-wrapped at cols characters. */
INST_U32 inst_text_lines(const char *text, INST_U32 cols);

/* What a control of that many dialog units holds: the characters a line
 * (a quarter of the average character a unit, less a margin), the lines. */
INST_U32 inst_page_cols(INST_U32 widthDlu);
INST_U32 inst_page_rows(INST_U32 heightDlu);

/* ---- Stor98, the bundled NVMe driver (record 26 sections 5.6 and 6.3) ---- */

/* Stor98's settings as its readme.txt ("SETTINGS") gives them at the
 * version the package bundles (STOR98_TABLE_VERSION): the 98 SE and ME
 * value name, the 2000-to-7 DriverParameter key, the unit, the defaults,
 * the range. The host suite holds every row to that readme's table
 * (test\stor98-settings.txt, a copy make-release.ps1 compares with the
 * bundled readme), so a Stor98 release that renames or retypes a value
 * fails the build rather than the user. */
#define INST_S98_TABLE_VERSION  "0.4.0.0"

#define INST_S98_WORDS      0   /* a word of a list (StorWriteThrough)       */
#define INST_S98_NUMBER     1   /* a decimal number in a range               */

#define INST_S98_WRITETHROUGH   0
#define INST_S98_POLLMODE       1
#define INST_S98_MAXTRANSFERKB  2
#define INST_S98_POLLBUSYUS     3
#define INST_S98_POLLIDLEMS     4
#define INST_S98_POLLSPINUS     5
#define INST_S98_GONE           6
#define INST_S98_IDLE           7
#define INST_S98_COUNT          8

#define INST_S98_WORDS_MAX      4

typedef struct _INST_S98_VALUE {
    const char *Name9x;     /* the value under Services\stor98 (98 SE, ME);
                               NULL: not settable there                    */
    const char *KeyNt;      /* the DriverParameter key, without '='      */
    int Kind;               /* INST_S98_*                                */
    int Asked;              /* on the page (1) or behind Advanced (0)     */
    const char *Unit;       /* "KiB", "us", "ms" or ""                   */
    const char *Def9x;      /* the built-in default on 98 SE and ME       */
    const char *DefNt;      /* ... and on 2000 to 7                      */
    INST_U32 Lo;            /* a number's range; Step its multiple        */
    INST_U32 Hi;
    INST_U32 Step;
    int ZeroOk;             /* 0 is a value of its own (Zero says what)   */
    const char *Zero;
    const char *Words[INST_S98_WORDS_MAX];
    const char *Means[INST_S98_WORDS_MAX];   /* a word's or a number's sense */
    const char *What;       /* one line: what the setting does            */
} INST_S98_VALUE;

extern const INST_S98_VALUE instS98Values[INST_S98_COUNT];

/* 1 when row i is settable on this system (the NT-only two are not on
 * 98 SE and ME). */
int inst_s98_applies(int i, int nt);

/* 1 when Stor98's row i is shown greyed on this system: settable on the
 * other systems only (gone and idle on 98 SE and ME). */
int inst_s98_greyed(int i, int nt);

/* What such a row says it is for, or NULL for a row of every system. */
const char *inst_s98_only(int i);

/* The built-in default as text ("auto", "512"), and its one-line sense
 * for the system ("cacheoff on ME, off elsewhere"); me: Windows ME. */
const char *inst_s98_default(int i, int nt);
void inst_s98_default_text(int i, int nt, int me, char *out, size_t cap);

/* A value typed or chosen for row i, checked (the installer offers only
 * what the readme admits, rather than leaving the driver to clamp or
 * refuse): 1 with the canonical text in out (a word in lower case, a
 * number in decimal), 0 refused with why. */
int inst_s98_check(int i, int nt, const char *text, char *out, size_t cap,
                   const char **why);

/* The 2000-to-7 DriverParameter string: lower-case key=value pairs
 * separated by semicolons. get: 1 with the value of key in out, 0 when the
 * key is not there. set: the string with key's pair replaced in place (its
 * position kept), appended when absent, or removed when value is NULL;
 * every pair it does not know kept, in the order found, empty segments
 * dropped. 1, or 0 when the result would not fit cap. */
int inst_s98_param_get(const char *param, const char *key, char *out,
                       size_t cap);
int inst_s98_param_set(const char *param, const char *key, const char *value,
                       char *out, size_t cap);

/* A row's "now" as the page shows it: "not set (the built-in default is
 * <d>)" or "set: <value>"; state 1 set, 0 not set, -1 unreadable. */
void inst_s98_now_text(int i, int nt, int me, int state, const char *value,
                       char *out, size_t cap);

/* The install record's two rules for a device first bound to Stor98 by
 * this installer (record 26 section 6.3, "The record"). By the
 * installer's own call or the helper's: the reading before showed no
 * Stor98 (before: 0 no driver, 1 another driver, 2 Stor98), the reading
 * after shows it. During a settle (Windows bound it from the staged INF):
 * newly bound, and on NT the driver key's InfPath names an INF this
 * installer staged, on 98 SE and ME no Stor98 at the baseline. */
int inst_s98_owned_by_call(int before, int afterS98);
int inst_s98_owned_by_settle(int before, int afterS98, int nt,
                             int infStaged, int preExisting);

/* The uninstall's last Stor98 step: the service, its files and the
 * recorded settings go only when no devnode is bound to Stor98 any more
 * and no Stor98 was there before this installer's first run. */
int inst_s98_service_removable(int devnodesLeft, int preExisting);

/* An update's precautions (Stor98's readme, "Updating"): on 98 SE and ME
 * nothing may be using the drive's volume and the machine is shut down,
 * not restarted; on 2000 to 7 an update of a drive behind a tunnel under
 * this package's connection manager is "updated, awaiting reconnection"
 * whatever the first read-back says (record 24 section 5.2). */
int inst_s98_cold_start(int nt);

/* SHA-256 of len bytes, as 64 upper-case hex digits and a NUL. */
void inst_sha256(const unsigned char *data, size_t len, char *hex65);

/* The question "Install Stor98, the bundled NVMe driver?" (record 26
 * section 6.4, draft 16). Part of the Thunderbolt 3 / USB4 support and
 * chosen with it (owner, 2026-10-19, replacing "never unasked"): with tb
 * (that support in the run) 0, neither asked nor installed; with it,
 * Enter and /yes give Y, /stor98 installs it without asking, /nostor98
 * leaves it out without asking, /list asks nothing. Returns the decision
 * when it is not asked; *asked whether it is, *def what Enter gives. */
int inst_s98_decide(int haveS98, int tb, int yes, int stor98Switch,
                    int noStor98, int list, int *asked, int *def);

/* Record 23 section 4.4 (revision 23): a device bound to Stor98 that is a
 * known tunnel drive, for the shared-file rule. Present: behind a tunnel.
 * Not present: on NT one the install record names; on 98 SE and ME any
 * (an internal drive there is always present, so an entry with no
 * devnode is a drive gone with its tunnel, as the uninstall's adoption
 * reads it). */
int inst_s98_known_tunnel(int nt, int present, int place, int recorded);

/* 98 SE and ME (record 26 section 6.3, draft 16): the bundled Stor98 goes
 * in as a file, never by reinstalling a running device. 1 when the run
 * replaces stor98.mpd at the next start: Stor98 in the run, the shared
 * file free to change (inst_s98_share), and the installed file there
 * (installed 1; 0 absent, -1 unreadable) at an older release (cmp, the
 * installed version against the bundled one, < 0). */
int inst_s98_file_update(int nt, int include, int share, int installed,
                         int cmp);

/* The text of the 98 SE and ME confirmation the file update needs (the
 * Summary's box and the console's question, owner 2026-10-19). */
extern const char instS98DisconnectText[];

/* The window's Stor98 box (owner, 2026-10-19): under the Thunderbolt 3 /
 * USB4 box, and open only while that box can be and is ticked. */
int inst_s98_box_open(int haveS98, int tbHave, int tbTicked);
int inst_s98_await_reconnect(int nt, int updatedOk, int ownCm,
                             int behindTunnel);

/* Where a Stor98 drive sits (owner, 2026-10-19: the installer installs,
 * updates and reconfigures Stor98 only on NVMe drives behind a
 * Thunderbolt or USB4 tunnel). From the connection manager's confirmed
 * reading (record 24 sections 3.3 and 4): found, the device is in the
 * walk; changing, a record moved during it, so nothing is confirmed;
 * whole, every devnode was read; cmAll, at least one connection
 * manager's record was confirmed and every one found was; verifiedAbove,
 * an ancestor is a tunnel port that record names; portAbove, an ancestor
 * is a Thunderbolt or USB4 port by its id, or carries the tunnel-port
 * filter, without being verified. A tunnel needs a verified port; an
 * internal drive needs all of a whole reading; anything else is unknown,
 * never either. */
#define INST_S98_AT_UNKNOWN     0
#define INST_S98_AT_TUNNEL      1
#define INST_S98_AT_INTERNAL    2
int inst_s98_place(int found, int changing, int whole, int cmAll,
                   int verifiedAbove, int portAbove);

/* What the installer may do with a Stor98 device: eligible only behind a
 * tunnel and with the Thunderbolt component in the run; an internal or
 * unknown place is left, and so is every device when another Stor98
 * device shares the file at another release (inst_s98_share). */
#define INST_S98_ELIGIBLE       0
#define INST_S98_SKIP_INTERNAL  1
#define INST_S98_SKIP_UNKNOWN   2
#define INST_S98_NEEDS_TB       3
#define INST_S98_SKIP_SHARED    4
int inst_s98_eligible(int tbInRun, int place);
/* The sentence for a reason (long: the console and the log; short: the
 * window's found page). NULL for INST_S98_ELIGIBLE. */
const char *inst_s98_skip_text(int why, int brief);

/* The shared file and service (owner, 2026-10-19): every Stor98 device
 * runs one stor98.sys or stor98.mpd and reads one service's settings.
 * otherBound: devices bound to Stor98, present or not, that are not known
 * tunnel drives (internal, unknown, or not present and not recorded as
 * this installer's); fileSame: the installed file is the bundled
 * release's version. FREE: the run may change both; SAME: the devices
 * may be installed (the file stays what it is) but no setting is
 * written; HOLD: no Stor98 change at all. */
/* The uninstall's side (owner's coordinator, 2026-10-19). With Stor98
 * brought by this package's installer (PreExisting clear), a device
 * Windows bound to Stor98 after the run is the package's too when it sits
 * behind a tunnel or is gone with its tunnel (not present); one internal
 * or of unknown place is not, and keeps Stor98 for every device. And an
 * INF copy that appeared outside a Stor98 step of the 98 SE and ME install
 * is the package's when its text names Stor98 and PreExisting is clear. */
int inst_s98_adoptable(int preExisting, int present, int place);
/* 1 when an INF's active text (each line up to its ';') names the file
 * as a word of its own, case aside: a line's start, a blank, ',', '=',
 * '"' or '\' before it; a blank, ',', '=', '"', ';' or the line's end
 * after it. */
int inst_inf_names_file(const char *text, size_t n, const char *file);
int inst_s98_stray_inf_ours(int preExisting, int namesStor98);
/* 98 SE and ME: xhcicm.sys makes Services\stor98 itself for Stor98's
 * link-state locator, its value LinkState; 1 when a value of that key is
 * another, so Stor98's (the baseline's "Stor98 there before"). */
int inst_s98_svc_value_stor98(const char *name);

#define INST_S98_SHARE_FREE     0
#define INST_S98_SHARE_SAME     1
#define INST_S98_SHARE_HOLD     2
int inst_s98_share(int otherBound, int fileSame);

/* ---- the machine-wide value as the drivers read it (task 42-A.17) ------ */

/* xhci_regsrc.c's rule (XhciRegCallSeen, XhciRegPick): `found` is whether
 * the query found the name at all; a zero-length REG_NONE is absent; a
 * REG_DWORD of four bytes is its number; any other type or size is present
 * and wins with the driver's own fallback, which is not always the INF's
 * default (XhciForceBulkOnly's is 1). 1 present with *value its number;
 * 2 present and not a number (*value def, a guess the caller must not
 * decide by); 0 absent. */
#define INST_REG_NONE       0UL
#define INST_REG_DWORD      4UL
int inst_mw_reading(int found, INST_U32 type, INST_U32 bytes, INST_U32 dword,
                    INST_U32 def, INST_U32 *value);

/* ---- the safe-removal mark service (record 21 section 7.4, revision 3;
 * task 42-A.5) ------------------------------------------------------------- */

/* Where the service is installed: Windows 2000 to 7 (decision M5). */
int inst_mk_system(const INST_ENV *env);

/* What the end of an install or update run does about the marks (decision
 * M13, by the state the run ends in): cmService is the xhcicm service's
 * state (1 registered, 0 not, -1 unreadable), tbInRun whether the
 * Thunderbolt component was in this run, markSystem inst_mk_system,
 * safeEject the XhciCmSafeEject in force after the run (inst_mk_safe_eject;
 * owner, 2026-10-19: with 0 the service is neither installed nor kept). */
#define INST_MK_LEAVE       0   /* nothing changes                         */
#define INST_MK_INSTALL     1   /* the service installed or updated, started */
#define INST_MK_REMOVE      2   /* every mark removed, the service deleted */
int inst_mk_end_action(int cmService, int tbInRun, int markSystem,
                       int safeEject);

/* The XhciCmSafeEject in force after a run, by task 42-A.17's precedence:
 * the machine-wide value when present (mwState 1, mwValue; -1 present and
 * not a DWORD, which the driver reads as its default 1), else each
 * connection manager device's own (now[0..nNow-1], as value_targets reads
 * them: State 1 set, 0 not set or -1 a new install, both the default 1).
 * 0 only when the value in force is 0 for every device; with no device
 * and no machine-wide value, the default 1. */
int inst_mk_safe_eject(int mwState, INST_U32 mwValue, const INST_VNOW *now,
                       int nNow);

/* The confirmation of an administrator tool's own eject (decision M16;
 * cm_query.h CM_QC_*), from the marks answer b[0..n) (cm_query.h CM_QM_*):
 * the one located entry at member location `member`, bus `bus` and address
 * `address` whose departure is pending, built into out[INST_QC_BYTES].
 * 1 built; 0 no such entry, or not an answer; -1 more than one such entry
 * (nothing is built); -2 a header asking for *need bytes. */
#define INST_QC_BYTES       28
int inst_qc_build(const unsigned char *b, size_t n, INST_U32 member,
                  INST_U32 bus, INST_U32 address, unsigned char *out,
                  INST_U32 *need);

/* ---- the USB storage drivers on Windows 98 SE (record 26 section 6.5;
 * roadmap-usb4 task 43.4) ----------------------------------------------- */

/* NUSB 3.3's seven files, from the download's INST_STOR_PACK_DIR folder
 * (task 43.3), each with where its INF sends it and its SHA-256 (the same
 * as legal-provenance.md section 5's table and package-common.ps1's
 * allowlist, which test-package.ps1 holds equal). The binaries come first,
 * the two INFs last. */
#define INST_STOR_COUNT         7
#define INST_STOR_BINARIES      5
#define INST_STOR_IDX_USBSTOR_INF   5
#define INST_STOR_IDX_USBNTMAP_INF  6
#define INST_STOR_DIR_DRIVERS   0   /* %windir%\SYSTEM32\DRIVERS (dirid 10) */
#define INST_STOR_DIR_IOS       1   /* the system directory's IOSUBSYS (12) */
#define INST_STOR_DIR_INF       2   /* INF or INF\OTHER, the engine's copy  */
#define INST_STOR_PACK_DIR      "NUSB33\\W98SE"
#define INST_STOR_KEY           "Software\\xhci98\\Storage"
#define INST_STOR_USB20U_KEY \
    "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\USB20U"

typedef struct _INST_STOR_FILE {
    const char *Name;
    int Dir;                /* INST_STOR_DIR_*                          */
    const char *Sha256;     /* hex, upper case                         */
    const char *KeyFile;    /* an INF: the file its text names (found
                               under a renamed name by it); else NULL   */
} INST_STOR_FILE;

extern const INST_STOR_FILE instStorFile[INST_STOR_COUNT];

/* The index of a name among the seven, -1 none. */
int inst_stor_index(const char *name);

/* A file's state (record 26 section 6.5.3). */
#define INST_STOR_ABSENT        0
#define INST_STOR_OWN           1   /* recorded, its bytes the recorded   */
#define INST_STOR_SAME          2   /* present, the download's bytes      */
#define INST_STOR_OTHER         3   /* other bytes, or unreadable         */
/* present: found where it installs; readable: its bytes read; recordHash:
 * the install record's hash for it (NULL none); hash: its SHA-256 now. */
int inst_stor_state(int present, int readable, const char *recordHash,
                    const char *packHash, const char *hash);
/* An INF's state over its candidates (each already a state): absent with
 * none, other when any is other, own when every one is own, else same. */
int inst_stor_inf_state(const int *candidates, int n);

/* 1 when a device INF's text (one with a [Manufacturer] section) names the
 * key file of the INF at index i as a word: the INF found under another
 * name (record 26 section 6.5.3). */
int inst_stor_inf_is(const char *text, size_t n, int i);

/* The run's case (record 26 section 6.5.4). */
#define INST_STOR_CASE_NONE     0
#define INST_STOR_CASE_COMPLETE 1
#define INST_STOR_CASE_SAME     2   /* partial, the same bytes            */
#define INST_STOR_CASE_OTHER    3   /* partial, another file present      */
#define INST_STOR_CASE_OWN      4   /* every present file this installer's */
/* source: the record's Source (an earlier run deployed the seven): with
 * it, a file of other bytes is partial-other however many are present. */
int inst_stor_case(const int st[INST_STOR_COUNT], int source);
/* 1 when the case admits the same-bytes route (copies, the package's copy
 * of the seven, the device jobs). */
int inst_stor_route(int storCase);
/* The row's default: unticked while NUSB is installed (usb20u, its
 * Uninstall\USB20U entry), whatever was recorded or found (owner,
 * 2026-10-22); else a recorded choice when there is one, else ticked only
 * when none of the seven is found (absent or this installer's). */
int inst_stor_default(int usb20u, int haveChoice, int choice,
                      const int st[INST_STOR_COUNT]);
/* The binaries a ticked run copies, a bit per index: on the route, each
 * absent one; never a present one, never an INF. */
INST_U32 inst_stor_copy_set(int ticked, const int st[INST_STOR_COUNT],
                            int source);
/* The detection's text: none, some ("not known to be ready"), all, or
 * this installer's. */
void inst_stor_found_text(char *out, size_t cap,
                          const int st[INST_STOR_COUNT]);
/* The row's note on every page and in the console: the detection's text,
 * and with usb20u (NUSB's Uninstall\USB20U entry) a sentence saying NUSB
 * is installed and the row can be left unticked (owner, 2026-10-22). */
#define INST_STOR_NUSB_TEXT \
    "NUSB is installed on this computer (found its Uninstall\\USB20U " \
    "entry). It already provides the USB storage drivers, so you can " \
    "leave this unticked."
void inst_stor_note_text(char *out, size_t cap,
                         const int st[INST_STOR_COUNT], int usb20u);

/* A device's storage kind by its ids (record 26 section 6.5.5). */
#define INST_STOR_KIND_NONE     0
#define INST_STOR_KIND_BULK     1   /* USB\Class_08&SubClass_0{2,5,6}&Prot_50 */
#define INST_STOR_KIND_UNIT     2   /* USBSTOR\GenDisk, GenCdRom, GenSfloppy */
int inst_stor_kind(const char *hw, size_t hwBytes, const char *compat,
                   size_t compatBytes);
/* A unit's binding: 1 when the helper takes it - no driver, or Windows'
 * generic disk binding, every value read (readable): class DiskDrive, no
 * PortDriver and no NTMPDriver ("" for none), its InfPath exactly
 * DISKDRV.INF and that file in %windir%\INF itself; 0 when it is left
 * (USBMPHLP.PDR's already, or anything else, or a value unread). */
int inst_stor_unit_takes(int hasDriver, int readable, const char *cls,
                         const char *portDriver, const char *ntmpDriver,
                         const char *infPath, int infInWindowsInf);
/* With the record's Source (an earlier run put the seven in the package
 * directory), a file of the download's bytes is this installer's own
 * deployment, not another installation: made own, in place, before the
 * case, the default and the text are taken. Without it, nothing changes. */
void inst_stor_norm(int st[INST_STOR_COUNT], int source);
/* The uninstall's adoption of a storage device Windows bound after the
 * run (owner, 2026-10-20; record 26 section 6.5.8 step 1): copy is
 * stor_infpath_copy's answer for its InfPath (1 exactly one copy of the
 * pack's own INF bytes), recorded whether the record's Infs holds that
 * copy, beforeOk whether the list of such copies already there before the
 * first deployment is whole, inBefore whether this copy is on it,
 * published whether a run ever made the package's copy with the seven and
 * put it on DevicePath (the record's Published). Adopted only after such
 * a publication, when it is the pack's copy, NUSB is not installed, and
 * the copy is recorded, or known not to have been there before. */
int inst_stor_adoptable(int published, int beforeOk, int inBefore,
                        int usb20u, int copy, int recorded);
/* What the uninstall does with a recorded file. */
#define INST_STOR_RM_DELETE     0
#define INST_STOR_RM_CHANGED    1   /* its bytes changed: kept, forgotten  */
#define INST_STOR_RM_NUSB       2   /* USB20U present: kept, forgotten     */
#define INST_STOR_RM_USED       3   /* a devnode uses it: kept, forgotten  */
#define INST_STOR_RM_GONE       4   /* not there: forgotten               */
int inst_stor_remove(int present, int hashSame, int usb20u, int users);
const char *inst_stor_remove_text(int why);

#endif /* INST_CORE_H */
