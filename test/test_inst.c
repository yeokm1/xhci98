/*
 * test_inst.c - host vectors for the install tool's pure half
 * (xhciinst\inst_core.c; design record 23 section 10; roadmap-usb4.md task
 * 40.6): the INF reader over the package's own INFs, the models section
 * each system reads, versions, ids, the action per device, the uninstall's
 * order, the small helpers, the registry value table with its
 * applicability and answers, the switches and the exit codes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xhciinst/inst_core.h"
#include "../src/cm/cm_status.h"
#include "../src/cm/cm_query.h"
#include "test_harness.h"

static char *load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static INST_ENV env(int nt, INST_U32 major, INST_U32 minor, int arch)
{
    INST_ENV e;

    memset(&e, 0, sizeof(e));
    e.Nt = nt;
    e.Major = major;
    e.Minor = minor;
    e.Arch = arch;
    return e;
}

static int has_id(const INST_IDLIST *ids, const char *id)
{
    int i;

    for (i = 0; i < ids->Count; i++) {
        if (inst_eq(ids->Id[i], id)) {
            return 1;
        }
    }
    return 0;
}

static void test_versions(void)
{
    INST_U32 a[4];
    INST_U32 b[4];

    CHECK(inst_ver_parse("2.4.0.0", a), "2.4.0.0 parses");
    CHECK_EQ(a[1], 4, "minor");
    CHECK(inst_ver_parse("3.0", b), "two fields parse");
    CHECK_EQ(b[2], 0, "missing field 0");
    CHECK(inst_ver_cmp(a, b) < 0, "2.4.0.0 < 3.0");
    CHECK(inst_ver_cmp(b, a) > 0, "3.0 > 2.4.0.0");
    CHECK(inst_ver_cmp(a, a) == 0, "equal");
    CHECK(!inst_ver_parse("2..0", a), "empty field refused");
    CHECK(!inst_ver_parse("1.2.3.4.5", a), "five fields refused");
    CHECK(!inst_ver_parse("2.4a", a), "junk refused");
    CHECK(!inst_ver_parse("70000", a), "field over 65535 refused");
    CHECK(!inst_ver_parse("", a), "empty refused");
}

static void test_models_rule(void)
{
    char out[128];
    INST_ENV e;

    e = env(0, 4, 10, INST_ARCH_X86);
    inst_models_section("XhciModels,NTx86.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels"), "98 reads the undecorated section");
    e = env(1, 5, 0, INST_ARCH_X86);
    inst_models_section("XhciModels,NTx86,NTx86.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels"), "2000 reads the undecorated section");
    e = env(1, 5, 1, INST_ARCH_X86);
    inst_models_section("XhciModels,NTx86.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels"), "XP: .6.0 does not apply");
    inst_models_section("XhciModels,NTx86,NTx86.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels.NTx86"), "XP: NTx86");
    inst_models_section("XhciModels,NT,NTx86", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels.NTx86"), "XP: arch-specific beats NT");
    inst_models_section("XhciModels,NTamd64", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels"), "XP x86: NTamd64 does not apply");
    e = env(1, 6, 1, INST_ARCH_X86);
    inst_models_section("XhciModels,NTx86.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels.NTx86.6.0"), "7 x86: .NTx86.6.0");
    inst_models_section("XhciModels,NTx86.6.0,NTx86.6.1", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels.NTx86.6.1"), "7: the highest version");
    e = env(1, 6, 0, INST_ARCH_X86);
    inst_models_section("XhciModels,NTx86.6.0,NTx86.6.1", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels.NTx86.6.0"), "Vista: not 6.1");
    e = env(1, 5, 2, INST_ARCH_AMD64);
    inst_models_section("XhciModels,NTamd64,NTamd64.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels.NTamd64"), "XP x64: NTamd64");
    e = env(1, 6, 1, INST_ARCH_AMD64);
    inst_models_section("XhciModels,NTamd64,NTamd64.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels.NTamd64.6.0"), "7 x64: NTamd64.6.0");
    inst_models_section("UasModels,NTamd64", &e, out, sizeof(out));
    CHECK(inst_eq(out, "UasModels.NTamd64"), "7 x64: NTamd64 alone");
    inst_models_section("XhciModels,ntAMD64.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels.ntAMD64.6.0"), "case kept, matched without");
    /* Section 4.2 item 5: the whole token, or the decoration is unknown. */
    e = env(1, 6, 1, INST_ARCH_X86);
    inst_models_section("XhciModels,NTx86garbage", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels"), "NTx86garbage is not NTx86");
    inst_models_section("XhciModels,NTx86.6.0.1", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels"), "a product-type field is not read");
    inst_models_section("XhciModels,NTx86.6.", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels"), "a trailing dot is not a version");
    /* Record 23 section 4.5: Windows 8 and 8.1 read the .6.0 sections. */
    e = env(1, 6, 2, INST_ARCH_X86);
    inst_models_section("CmModels,NTx86.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "CmModels.NTx86.6.0"), "8 x86: .NTx86.6.0");
    e = env(1, 6, 3, INST_ARCH_X86);
    inst_models_section("CmModels,NTx86.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "CmModels.NTx86.6.0"), "8.1 x86: .NTx86.6.0");
    e = env(1, 6, 2, INST_ARCH_AMD64);
    inst_models_section("CmModels,NTamd64,NTamd64.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "CmModels.NTamd64.6.0"), "8 x64: NTamd64.6.0");
    e = env(1, 6, 3, INST_ARCH_AMD64);
    inst_models_section("CmModels,NTamd64,NTamd64.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "CmModels.NTamd64.6.0"), "8.1 x64: NTamd64.6.0");
}

/* ---- record 23 sections 4.3 and 4.5: the system, the USB drivers -------- */

static void test_cm_only(void)
{
    INST_OPTS o;
    char err[128];
    char *argv[4];
    const char *why;
    int offered;

    /* The system's kind (revision 13). */
    CHECK(inst_os_kind(0, 4, 10, 2222) == INST_OS_FULL, "98 SE: full");
    CHECK(inst_os_kind(0, 4, 10, 1998) == INST_OS_REFUSED, "98: refused");
    CHECK(inst_os_kind(0, 4, 90, 3000) == INST_OS_FULL, "ME: full");
    CHECK(inst_os_kind(0, 4, 0, 950) == INST_OS_REFUSED, "95: refused");
    CHECK(inst_os_kind(1, 4, 0, 1381) == INST_OS_REFUSED, "NT 4: refused");
    CHECK(inst_os_kind(1, 5, 0, 2195) == INST_OS_FULL, "2000: full");
    CHECK(inst_os_kind(1, 5, 1, 2600) == INST_OS_FULL, "XP: full");
    CHECK(inst_os_kind(1, 5, 2, 3790) == INST_OS_FULL, "XP x64: full");
    CHECK(inst_os_kind(1, 6, 0, 6002) == INST_OS_FULL, "Vista: full");
    CHECK(inst_os_kind(1, 6, 1, 7601) == INST_OS_FULL, "7: full");
    CHECK(inst_os_kind(1, 6, 2, 9200) == INST_OS_CMONLY,
          "8: the connection manager alone");
    CHECK(inst_os_kind(1, 6, 3, 9600) == INST_OS_CMONLY,
          "8.1: the connection manager alone");
    CHECK(inst_os_kind(1, 6, 4, 9841) == INST_OS_REFUSED,
          "10's previews (6.4): refused");
    CHECK(inst_os_kind(1, 10, 0, 19045) == INST_OS_REFUSED, "10: refused");
    CHECK(inst_os_kind(1, 10, 0, 26100) == INST_OS_REFUSED, "11: refused");
    CHECK(inst_os_kind(1, 7, 0, 0) == INST_OS_REFUSED, "7.0: refused");
    CHECK(strcmp(inst_os_name(1, 6, 2), "Windows 8") == 0 &&
          strcmp(inst_os_name(1, 6, 3), "Windows 8.1") == 0 &&
          strcmp(inst_os_name(1, 6, 1), "Windows 7") == 0 &&
          strcmp(inst_os_name(1, 5, 2), "Windows XP x64") == 0 &&
          strcmp(inst_os_name(0, 4, 90), "Windows ME") == 0 &&
          strcmp(inst_os_name(0, 4, 10), "Windows 98 SE") == 0 &&
          strcmp(inst_os_name(1, 10, 0), "Windows") == 0, "the names");

    /* The USB drivers (section 4.5). */
    CHECK(inst_usb_decide(INST_OS_FULL, 0, 1, 0, &offered, &why) == 1 &&
          !offered && why == NULL, "98 SE and ME: required, not offered");
    CHECK(inst_usb_decide(INST_OS_FULL, 0, 1, 1, &offered, &why) == -1 &&
          why != NULL, "98 SE and ME: /cmonly refused");
    CHECK(inst_usb_decide(INST_OS_FULL, 1, 1, 0, &offered, &why) == 1 &&
          offered, "2000 to 7 with the connection manager: offered, in");
    CHECK(inst_usb_decide(INST_OS_FULL, 1, 1, 1, &offered, &why) == 0 &&
          offered, "2000 to 7, /cmonly: out");
    CHECK(inst_usb_decide(INST_OS_FULL, 1, 0, 0, &offered, &why) == 1 &&
          !offered, "2000 to 7 without the connection manager: required");
    CHECK(inst_usb_decide(INST_OS_FULL, 1, 0, 1, &offered, &why) == -1 &&
          why != NULL, "/cmonly with no connection manager: refused");
    CHECK(inst_usb_decide(INST_OS_CMONLY, 1, 1, 0, &offered, &why) == 0 &&
          !offered, "8 and 8.1: out, not offered");
    CHECK(inst_usb_decide(INST_OS_CMONLY, 1, 1, 1, &offered, &why) == 0 &&
          !offered, "8 and 8.1, /cmonly: the same");
    CHECK(inst_usb_decide(INST_OS_CMONLY, 1, 0, 0, &offered, &why) == -1 &&
          why != NULL, "8 and 8.1 with no connection manager: refused");
    CHECK(inst_usb_decide(INST_OS_REFUSED, 1, 1, 0, &offered, &why) == -1,
          "a refused system");

    /* /cmonly belongs to an install. */
    argv[0] = "xhciinst";
    argv[1] = "/cmonly";
    CHECK(inst_parse_args(2, argv, &o, err, sizeof(err)) == 0 && o.CmOnly,
          "/cmonly parsed");
    argv[2] = "/uninstall";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) != 0,
          "/cmonly /uninstall refused");
    argv[2] = "/devices";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) != 0,
          "/cmonly /devices refused");
    argv[2] = "/thunderbolt";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) == 0 &&
          o.CmOnly && o.Thunderbolt, "/cmonly /thunderbolt taken");
}

static void test_reg_hex(void)
{
    char name[32];
    unsigned char d[8];
    size_t n;
    static const char *const bad[] = {
        "\"0\"=hex:0g,09", "\"0\"=hex:8", "\"0\"=hex:80,", "\"0\"=hex:80,,09",
        "\"0\"=hex:", "\"0\"=dword:00000001", "0=hex:80", "\"\"=hex:80",
        "\"0\"=hex:80 09", "\"0\"=hex:01,02,03,04,05,06,07,08,09"
    };
    int i;

    CHECK(inst_reg_hex_line("\"0\"=hex:80,09,00,00", name, sizeof(name), d,
                            sizeof(d), &n) &&
          inst_eq(name, "0") && n == 4 && d[0] == 0x80 && d[1] == 0x09 &&
          d[3] == 0, "SILENT.REG's first line");
    CHECK(inst_reg_hex_line("\"3\"=hex:c3,08,06,50", name, sizeof(name), d,
                            sizeof(d), &n) &&
          n == 4 && d[0] == 0xC3 && d[3] == 0x50, "lower-case digits");
    for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
        CHECK(!inst_reg_hex_line(bad[i], name, sizeof(name), d, sizeof(d), &n),
              bad[i]);
    }
    {
        /* The shipped list itself, every value line. */
        char *text = load("..\\src\\silent.reg");
        char *p;
        char *nl;
        int lines = 0;
        CHECK(text != NULL, "src\\silent.reg read");
        for (p = text; p != NULL && *p != '\0'; p = nl) {
            nl = strchr(p, '\n');
            if (nl != NULL) {
                *nl++ = '\0';
            }
            if (strlen(p) > 0 && p[strlen(p) - 1] == '\r') {
                p[strlen(p) - 1] = '\0';
            }
            if (p[0] == '"') {
                CHECK(inst_reg_hex_line(p, name, sizeof(name), d, sizeof(d),
                                        &n) && n == 4, p);
                lines++;
            }
            if (nl == NULL) {
                break;
            }
        }
        CHECK_EQ(lines, 6, "six values in src\\silent.reg");
        free(text);
    }
}

static void test_decorations(void)
{
    int a;
    INST_U32 mj;
    INST_U32 mn;

    CHECK(inst_decoration_parse("NTx86", &a, &mj, &mn) && a == INST_DECO_X86,
          "NTx86");
    CHECK(inst_decoration_parse("NTamd64.6.0", &a, &mj, &mn) &&
          a == INST_DECO_AMD64 && mj == 6 && mn == 0, "NTamd64.6.0");
    CHECK(inst_decoration_parse("NT.5", &a, &mj, &mn) && a == INST_DECO_NONE &&
          mj == 5, "NT.5");
    CHECK(!inst_decoration_parse("NTx86garbage", &a, &mj, &mn), "trailing text");
    CHECK(!inst_decoration_parse("NTx86.6.0.1", &a, &mj, &mn), "product type");
    CHECK(!inst_decoration_parse("NTx86.6.0.1.2", &a, &mj, &mn), "suite");
    CHECK(!inst_decoration_parse("NTia64", &a, &mj, &mn), "another architecture");
    CHECK(!inst_decoration_parse("NTx86.", &a, &mj, &mn), "an empty version");
    CHECK(!inst_decoration_parse("NTx86.256", &a, &mj, &mn), "a version over 255");
    CHECK(!inst_decoration_parse("Win9x", &a, &mj, &mn), "not NT");
    CHECK(!inst_inf_arch_ok("[Manufacturer]\r\n%M%=X,NTx86garbage\r\n[X]\r\n",
                            INST_ARCH_X86), "the architecture check refuses it");
    CHECK(!inst_inf_arch_ok("[Manufacturer]\r\n%M%=X,NTx86.6.0.1\r\n[X]\r\n",
                            INST_ARCH_X86), "and a product-type field");
    CHECK(!inst_inf_arch_ok("[Manufacturer]\r\n%M%=X,NT\r\n[X]\r\n",
                            INST_ARCH_X86), "and a decoration naming no architecture");
    CHECK(inst_inf_arch_ok("[Manufacturer]\r\n%M%=X,NTx86.6.0\r\n[X]\r\n",
                           INST_ARCH_X86), "NTx86.6.0 passes");
}

/* The package's INFs: what each system reads. */
static void test_real_infs(void)
{
    char *x86 = load("..\\src\\xhci98.inf");
    char *a64 = load("..\\src\\xhci98-amd64.inf");
    char *u86 = load("..\\src\\uas\\xhciuas.inf");
    char *u64 = load("..\\src\\uas\\xhciuas-amd64.inf");
    INST_IDLIST ids;
    INST_ENV e;
    INST_U32 v[4];
    INST_U32 hver[4];
    char name[128];
    int i;
    int sawSys;

    CHECK(x86 != NULL && a64 != NULL && u86 != NULL && u64 != NULL,
          "the four INFs of src\\ read");
    if (x86 == NULL || a64 == NULL || u86 == NULL || u64 == NULL) {
        return;
    }

    CHECK(inst_inf_driverver(x86, v), "xhci98.inf DriverVer");
    CHECK(inst_inf_driverver(a64, hver), "xhci98-amd64.inf DriverVer");
    CHECK(inst_ver_cmp(v, hver) == 0, "both xhci98 INFs one version");
    CHECK(inst_inf_driverver(u86, hver) && inst_ver_cmp(v, hver) == 0,
          "xhciuas.inf the same version");
    CHECK(inst_inf_driverver(u64, hver) && inst_ver_cmp(v, hver) == 0,
          "xhciuas-amd64.inf the same version");

    CHECK(inst_inf_arch_ok(x86, INST_ARCH_X86), "xhci98.inf is x86");
    CHECK(!inst_inf_arch_ok(x86, INST_ARCH_AMD64), "xhci98.inf is not amd64");
    CHECK(inst_inf_arch_ok(a64, INST_ARCH_AMD64), "xhci98-amd64.inf is amd64");
    CHECK(!inst_inf_arch_ok(a64, INST_ARCH_X86), "xhci98-amd64.inf is not x86");
    CHECK(inst_inf_arch_ok(u86, INST_ARCH_X86), "xhciuas.inf is x86");
    CHECK(inst_inf_arch_ok(u64, INST_ARCH_AMD64), "xhciuas-amd64.inf is amd64");
    CHECK(!inst_inf_arch_ok(u64, INST_ARCH_X86), "xhciuas-amd64.inf not x86");

    e = env(0, 4, 10, INST_ARCH_X86);
    CHECK(inst_inf_models(x86, &e, &ids), "98: models read");
    CHECK(has_id(&ids, "PCI\\CC_0C0330"), "98: the controller");
    CHECK(has_id(&ids, "USB\\XHCI98_ROOT_HUB"), "98: the root hub");
    CHECK(has_id(&ids, "USB\\XHCI98_COMPOSITE"), "98: composite");
    CHECK_EQ(ids.Count, 5, "98: five ids");
    e = env(1, 5, 0, INST_ARCH_X86);
    CHECK(inst_inf_models(x86, &e, &ids) && ids.Count == 5, "2000: five ids");
    e = env(1, 6, 1, INST_ARCH_X86);
    CHECK(inst_inf_models(x86, &e, &ids) && ids.Count == 5 &&
          has_id(&ids, "USB\\XHCI98_HUB30"), "7 x86: five ids");
    e = env(1, 5, 2, INST_ARCH_AMD64);
    CHECK(inst_inf_models(a64, &e, &ids) && ids.Count == 5 &&
          has_id(&ids, "PCI\\CC_0C0330"), "XP x64: five ids");
    e = env(1, 6, 1, INST_ARCH_AMD64);
    CHECK(inst_inf_models(a64, &e, &ids) && ids.Count == 5, "7 x64: five ids");
    CHECK(inst_inf_models(u64, &e, &ids) &&
          has_id(&ids, "USB\\Class_08&SubClass_06&Prot_62"), "7 x64: UAS id");
    e = env(1, 5, 0, INST_ARCH_X86);
    CHECK(inst_inf_models(u86, &e, &ids) && ids.Count == 1, "2000: UAS id");

    sawSys = 0;
    for (i = 0; inst_inf_source_file(x86, i, name, sizeof(name)); i++) {
        if (inst_eq(name, "xhci98.sys")) {
            sawSys = 1;
        }
    }
    CHECK(sawSys, "xhci98.inf names xhci98.sys in [SourceDisksFiles]");
    CHECK(i >= 2, "xhci98.inf names the helper too");

    CHECK(inst_inf_is_ours(x86), "xhci98.inf is ours");
    CHECK(inst_inf_is_ours(a64), "xhci98-amd64.inf is ours");
    CHECK(inst_inf_is_ours(u86), "xhciuas.inf is ours");
    CHECK(inst_inf_is_ours(u64), "xhciuas-amd64.inf is ours");

    /* Every xhci98 row of the value table is written by both INFs, at the
     * default the table's meaning assumes. */
    for (i = 0; i < instValueCount; i++) {
        INST_U32 d86;
        INST_U32 d64;
        if (instValues[i].Inf != INST_INF_XHCI) {
            continue;
        }
        CHECK(inst_inf_addreg_default(x86, instValues[i].Name, &d86),
              instValues[i].Name);
        CHECK(inst_inf_addreg_default(a64, instValues[i].Name, &d64),
              instValues[i].Name);
        CHECK_EQ(d86, d64, instValues[i].Name);
        CHECK_EQ(d86, 0, instValues[i].Name);
        CHECK(instValues[i].Asked == 0 ||
              (instValues[i].Asked & (1UL << d86)) != 0,
              "the default is an offered answer");
        CHECK((instValues[i].Settable & (1UL << d86)) != 0,
              "the default is a value /set takes");
    }

    free(x86);
    free(a64);
    free(u86);
    free(u64);
}

/* The Thunderbolt INFs in src\cm\ (task 39-A.1). A missing one is a
 * failure, as a missing INF of src\ is above: the tree carries them. */
static void test_cm_infs(void)
{
    char *cm = load("..\\src\\cm\\xhcicm.inf");
    char *cf = load("..\\src\\cm\\xhcicmf.inf");
    char *c64 = load("..\\src\\cm\\xhcicm-amd64.inf");
    INST_ENV e;
    INST_IDLIST ids;
    int i;

    CHECK(cm != NULL && cf != NULL && c64 != NULL,
          "the three INFs of src\\cm\\ read");
    if (cm == NULL || cf == NULL || c64 == NULL) {
        free(cm);
        free(cf);
        free(c64);
        return;
    }
    CHECK(inst_inf_is_ours(cm), "xhcicm.inf is ours");
    CHECK(inst_inf_is_ours(cf), "xhcicmf.inf is ours");
    CHECK(inst_inf_arch_ok(cm, INST_ARCH_X86), "xhcicm.inf is x86");
    CHECK(inst_inf_arch_ok(c64, INST_ARCH_AMD64), "xhcicm-amd64.inf is amd64");
    e = env(1, 5, 1, INST_ARCH_X86);
    CHECK(inst_inf_models(cm, &e, &ids) &&
          has_id(&ids, "PCI\\VEN_8086&DEV_15BF"), "XP: the captured NHI");
    for (i = 0; i < instValueCount; i++) {
        INST_U32 d;
        INST_U32 d64;
        INST_U32 offered;
        if (instValues[i].Inf != INST_INF_CM) {
            continue;
        }
        if (instValues[i].NoInf) {
            /* Not written by the INF (record 26 section 5.5): the table's
             * default, the driver's built-in one, is an allowed value, and
             * neither INF writes it. */
            CHECK(!inst_inf_addreg_default(cm, instValues[i].Name, &d) &&
                  !inst_inf_addreg_default(c64, instValues[i].Name, &d64),
                  "a NoInf row is written by no INF");
            d = instValues[i].Def;
        } else {
            CHECK(inst_inf_addreg_default(cm, instValues[i].Name, &d),
                  instValues[i].Name);
            CHECK(inst_inf_addreg_default(c64, instValues[i].Name, &d64) &&
                      d64 == d, "the amd64 INF writes the same default");
        }
        CHECK(inst_value_default(i, cm, &d64) && d64 == d,
              "inst_value_default gives the same default");
        if (instValues[i].Hi != 0) {
            /* A range row: never asked, its default inside the range. */
            CHECK(instValues[i].Asked == 0 && d >= instValues[i].Lo &&
                  d <= instValues[i].Hi, "the cm default is in its range");
            continue;
        }
        /* A never-asked row (Asked 0) offers its default through /set. */
        offered = instValues[i].Asked != 0 ? instValues[i].Asked
                                           : instValues[i].Settable;
        CHECK(d < 32 && (offered & (1UL << d)) != 0,
              "the cm default is an offered answer");
    }
    free(cm);
    free(cf);
    free(c64);
}

static void test_parser(void)
{
    static const char inf[] =
        "; a comment\r\n"
        "[Version]\r\n"
        "Signature=\"$CHICAGO$\"\r\n"
        "Provider=%P%\r\n"
        "DriverVer=10/15/2026,3.0.0.0 ; trailing\r\n"
        "\r\n"
        "[Manufacturer]\r\n"
        "%M%=Models,NTx86.6.0\r\n"
        "[Models]\r\n"
        "%D%=Inst,PCI\\CC_0C0330, PCI\\VEN_1234&DEV_5678\r\n"
        "[Models.NTx86.6.0]\r\n"
        "%D%=Inst6,PCI\\CC_0C0330\r\n"
        "[Inst.AddReg]\r\n"
        "HKR,,NTMPDriver,,xhci98.sys\r\n"
        "HKR,,XhciThing,0x00010003,0x10\r\n"
        "[SourceDisksFiles]\r\n"
        "a.sys=1\r\n"
        "[SourceDisksFiles.x86]\r\n"
        "b.sys = 1,,\r\n"
        "[Strings]\r\n"
        "P=\"Yeo Kheng Meng\"\r\n"
        "D=\"x;y\"\r\n";
    char line[256];
    char k[64];
    char v[64];
    char f[64];
    INST_U32 ver[4];
    INST_U32 d;
    INST_IDLIST ids;
    INST_ENV e;
    const char *p;

    p = inst_inf_section(inf, "version");
    CHECK(p != NULL, "section found without case");
    p = inst_inf_line(p, line, sizeof(line));
    CHECK(p != NULL && inst_eq(line, "Signature=\"$CHICAGO$\""), "first line");
    CHECK(inst_inf_kv(line, k, sizeof(k), v, sizeof(v)) && inst_eq(k, "Signature"),
          "key");
    CHECK(inst_inf_field("a, \"b,c\" ,d", 1, f, sizeof(f)) && inst_eq(f, "b,c"),
          "quoted comma kept");
    CHECK(!inst_inf_field("a,b", 2, f, sizeof(f)), "no third field");
    CHECK(inst_inf_driverver(inf, ver) && ver[0] == 3, "DriverVer, comment cut");
    CHECK(inst_inf_expand(inf, "%D%", f, sizeof(f)) && inst_eq(f, "x;y"),
          "a ';' inside quotes is not a comment");
    CHECK(!inst_inf_expand(inf, "%Q%", f, sizeof(f)), "unknown token");
    CHECK(inst_inf_is_ours(inf), "provider and NTMPDriver");
    CHECK(inst_inf_addreg_default(inf, "XhciThing", &d) && d == 16, "hex default");
    CHECK(!inst_inf_addreg_default(inf, "Nope", &d), "absent value");
    CHECK(inst_inf_source_file(inf, 1, f, sizeof(f)) && inst_eq(f, "b.sys"),
          "decorated [SourceDisksFiles]");
    CHECK(!inst_inf_source_file(inf, 2, f, sizeof(f)), "two files");
    {
        static const char sd[] =
            "[SourceDisksNames]\r\n1=%D%,a.sys,,\r\n2=%D%,b.sys,,\\nt\r\n"
            "[SourceDisksFiles]\r\na.sys=1\r\nb.sys=2\r\nc.sys=1,sub\r\nd.sys=3\r\n"
            "[Strings]\r\nD=\"x\"\r\n";
        static const char up[] =
            "[SourceDisksNames]\r\n1=%D%,a.sys,,..\r\n[SourceDisksFiles]\r\na.sys=1\r\n";
        CHECK(inst_inf_source_path(sd, 0, f, sizeof(f)) && inst_eq(f, "a.sys"),
              "no disk path: beside the INF");
        CHECK(inst_inf_source_path(sd, 1, f, sizeof(f)) && inst_eq(f, "nt\\b.sys"),
              "the disk's path");
        CHECK(inst_inf_source_path(sd, 2, f, sizeof(f)) && inst_eq(f, "sub\\c.sys"),
              "the file's subdirectory");
        CHECK(inst_inf_source_path(sd, 3, f, sizeof(f)) && inst_eq(f, "d.sys"),
              "an unknown disk: beside the INF");
        CHECK(!inst_inf_source_path(sd, 4, f, sizeof(f)), "four files");
        CHECK(inst_inf_source_path(up, 0, f, sizeof(f)) && inst_eq(f, "..\\a.sys"),
              ".. kept for the caller to hold to the package");
        CHECK(inst_inf_source_path(sd, 4, f, sizeof(f)) == 0, "the end is 0");
        CHECK(inst_inf_source_path(sd, 1, f, 6) == -1,
              "a path too long is an error, not the end");
        CHECK(inst_inf_source_path("[SourceDisksNames]\r\n1=%D%,a,,c:\\x\r\n"
                                   "[SourceDisksFiles]\r\na.sys=1\r\nb.sys=1\r\n",
                                   0, f, sizeof(f)) == -1,
              "a drive letter is an error, not the end");
    }
    e = env(1, 5, 1, INST_ARCH_X86);
    CHECK(inst_inf_models(inf, &e, &ids) && ids.Count == 2, "XP: two ids");
    e = env(1, 6, 0, INST_ARCH_X86);
    CHECK(inst_inf_models(inf, &e, &ids) && ids.Count == 1, "Vista: one id");
    CHECK(!inst_inf_is_ours("[Version]\r\nProvider=Microsoft\r\n"
                            "[S]\r\nAddService=xhci98,2,X\r\n"), "not our provider");
    CHECK(!inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                            "[S]\r\nAddService=usbhub,2,X\r\n"), "not our service");
    CHECK(inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                           "[S]\r\nAddService = XHCICM , 2, X\r\n"), "xhcicm");
    /* Stor98 0.3.0.0's INF, in the shape it ships: the same Provider. */
    CHECK(!inst_inf_is_ours("[Version]\r\nSignature=\"$CHICAGO$\"\r\n"
                            "Provider=%Provider%\r\n"
                            "[Manufacturer]\r\n%Mfg%=Stor98_Models\r\n"
                            "[Stor98_Models]\r\n"
                            "%Stor98.DeviceDesc%=Stor98_Inst,PCI\\CC_010802\r\n"
                            "[Stor98_AddReg_9x]\r\nHKR,,PortDriver,,stor98.mpd\r\n"
                            "[Stor98_Inst.NTx86.Services]\r\n"
                            "AddService=stor98,%SPSVCINST_ASSOCSERVICE%,Stor98_Service\r\n"
                            "[Strings]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                            "Mfg=\"Stor98 Project\"\r\n"
                            "SPSVCINST_ASSOCSERVICE=0x00000002\r\n"),
          "Stor98's INF is not ours");
    CHECK(!inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                            "[S]\r\nHKR,,PortDriver,,STOR98.MPD\r\n"),
          "Stor98's 9x half is not ours");
    CHECK(!inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                            "[S]\r\nAddService=xhcicm,2,X\r\n"
                            "[T]\r\nAddService=stor98,2,Y\r\n"),
          "an INF that also installs stor98 is not ours");
    CHECK(!inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                            "[S]\r\nAddService=xhcicm,2,X\r\n"
                            "[T]\r\nAddService=%Svc%,2,Y\r\n"
                            "[Strings]\r\nSvc=\"stor98\"\r\n"),
          "stor98 named through a string is not ours");
    CHECK(!inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                            "[S]\r\nAddService=xhcicm,2,X\r\n"
                            "[T]\r\nHKR,,PortDriver,,%Mpd%\r\n"
                            "[Strings]\r\nMpd=stor98.mpd\r\n"),
          "stor98.mpd named through a string is not ours");
    CHECK(inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                           "[S]\r\nAddService=xhcicm,2,X\r\n"
                           "[T]\r\nHKR,,PortDriver,,%Mpd%\r\n"
                           "[Strings]\r\nMpd=xhci98.mpd\r\n"),
          "another file named through a string is not Stor98's");
    CHECK(!inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                            "[S]\r\nAddService=xhcicm,2,X\r\n"
                            "[T]\r\nHKR,,PortDriver,,%Driver%.mpd\r\n"
                            "[Strings]\r\nDriver=stor98\r\n"),
          "a token inside the file name");
    CHECK(!inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                            "[S]\r\nAddService=xhcicm,2,X\r\n"
                            "[T]\r\nAddService=%Prefix%98,2,Y\r\n"
                            "[Strings]\r\nPrefix=\"stor\"\r\n"),
          "a token inside the service name");
    CHECK(inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                           "[S]\r\nAddService=xhcicm,2,X\r\n"
                           "[T]\r\nHKR,,PortDriver,,100%%.mpd\r\n"),
          "%% is a percent sign, not Stor98's");
    CHECK(!inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                            "[S]\r\nAddService=xhcicm,2,X\r\n"
                            "[T]\r\nHKR,,%DriverValue%,,stor98.mpd\r\n"
                            "[Strings]\r\nDriverValue=PortDriver\r\n"),
          "a value name given as a token");
    CHECK(!inst_inf_is_ours("[Version]\r\nProvider=\"Yeo Kheng Meng\"\r\n"
                            "[S]\r\nAddService=xhcicm,2,X\r\n"
                            "[SourceDisksFiles]\r\nstor98.sys=1\r\n"),
          "a file named as a key");
}

static void test_ids(void)
{
    static const char hw[] = "PCI\\VEN_8086&DEV_8C31&SUBSYS_00000000&REV_04\0"
                             "PCI\\VEN_8086&DEV_8C31\0";
    static const char cc[] = "PCI\\VEN_8086&CC_0C0330\0PCI\\CC_0C0330\0PCI\\CC_0C03\0";
    INST_IDLIST ids;
    INST_U32 vd;

    CHECK(inst_msz_has(hw, sizeof(hw), "pci\\ven_8086&dev_8c31"), "msz, no case");
    CHECK(!inst_msz_has(hw, sizeof(hw), "PCI\\VEN_8086"), "whole entries only");
    CHECK(!inst_msz_has(NULL, 0, "x"), "NULL msz");
    {
        char s98[64];
        size_t nb;
        strcpy(s98, "PCI\\VEN_1B36&DEV_000D,PCI\\CC_0C0330");
        nb = inst_ids_split(s98, sizeof(s98));
        CHECK(inst_msz_has(s98, nb, "PCI\\CC_0C0330"), "98's comma list split");
        CHECK(inst_msz_has(s98, nb, "PCI\\VEN_1B36&DEV_000D"), "its first id");
        s98[0] = '\0';
        CHECK_EQ(inst_ids_split(s98, sizeof(s98)), 0, "an empty list");
    }
    ids.Count = 1;
    ids.Overflow = 0;
    strcpy(ids.Id[0], "PCI\\CC_0C0330");
    CHECK_EQ(inst_ids_match(hw, sizeof(hw), cc, sizeof(cc), &ids), 0,
             "matched by compatible id");
    strcpy(ids.Id[0], "PCI\\CC_0C0320");
    CHECK_EQ(inst_ids_match(hw, sizeof(hw), cc, sizeof(cc), &ids), -1,
             "EHCI not matched");
    CHECK(inst_pci_vendev("PCI\\VEN_8086&DEV_8C31&SUBSYS_1", &vd), "pci id");
    CHECK_EQ(vd, 0x8C318086UL, "the dword XhciPswGate takes");
    CHECK(inst_pci_vendev("PCI\\VEN_1b36&DEV_000d", &vd) && vd == 0x000D1B36UL,
          "lower case hex");
    CHECK(!inst_pci_vendev("PCI\\VEN_8086&DEV_8C3", &vd), "short");
    CHECK(!inst_pci_vendev("USB\\VID_8086&PID_8C31", &vd), "not PCI");
    CHECK(!inst_pci_vendev("PCI\\VEN_8086&DEV_8C31X", &vd), "trailing junk");

    CHECK_EQ(inst_bus_id("USB\\XHCI98_ROOT_HUB\\4&1"), 1, "current root hub");
    CHECK_EQ(inst_bus_id("USB\\XHCI98_HUB30&VID_0BDA&PID_0411\\5"), 1, "hub30");
    CHECK_EQ(inst_bus_id("USB\\XHCI98_NOXPORT&VID_1&PID_2\\x"), 0, "NOXPORT not");
    CHECK_EQ(inst_bus_id("XHCI98\\COMPOSITE&VID_1&PID_2\\x"), 2, "old composite");
    CHECK_EQ(inst_bus_id("XHCI98\\OTHER\\x"), 0, "unknown old form");
    CHECK_EQ(inst_bus_id("USB\\VID_046D&PID_C077\\5&1"), 0, "a device");
}

static void test_actions(void)
{
    INST_DEV d;
    INST_U32 pkg[4];

    inst_ver_parse("3.0.0.0", pkg);
    memset(&d, 0, sizeof(d));
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_INSTALL, "no service: install");
    d.HasService = 1;
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_FOREIGN, "another service");
    d.Problem = 28;
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_FOREIGN,
             "a problem code never makes a foreign device an install");
    d.Problem = 0;
    d.OwnService = 1;
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_UPDATE, "ours, version absent: update");
    d.VersionState = INST_VS_UNREADABLE;
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_UNKNOWN, "unreadable: unknown");
    CHECK(!inst_action_runs(INST_ACT_UNKNOWN, 1), "unknown never runs");
    d.VersionState = INST_VS_HAVE;
    inst_ver_parse("2.4.0.0", d.Version);
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_UPDATE, "older: update");
    inst_ver_parse("3.0.0.0", d.Version);
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_CURRENT, "equal: current");
    d.Problem = 28;
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_UPDATE, "ours, equal, Code 28: reinstall");
    d.Problem = 0;
    inst_ver_parse("3.0.0.1", d.Version);
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_NEWER, "newer: left");
    d.Problem = 28;
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_NEWER, "newer stays newer with a problem");

    CHECK_EQ(inst_group_verdict(0, 0, -1, 0), INST_GROUP_OK, "group older: ok");
    CHECK_EQ(inst_group_verdict(0, 0, -2, 0), INST_GROUP_OK, "no binary: ok");
    CHECK_EQ(inst_group_verdict(1, 0, -1, 0), INST_GROUP_REFUSED_NEWER,
             "one newer devnode refuses the group");
    CHECK_EQ(inst_group_verdict(0, 0, 1, 0), INST_GROUP_REFUSED_NEWER,
             "a newer binary refuses the group");
    CHECK_EQ(inst_group_verdict(1, 0, 1, 1), INST_GROUP_OK, "/force lifts newer");
    CHECK_EQ(inst_group_verdict(0, 1, 0, 1), INST_GROUP_REFUSED_UNKNOWN,
             "/force never lifts unknown");
    CHECK_EQ(inst_group_verdict(0, 0, 2, 1), INST_GROUP_REFUSED_UNKNOWN,
             "unreadable binary: unknown");
    CHECK_EQ(inst_uas_fix_9x(0, 90, 1, 2, 0), INST_UASFIX_ME_STICK,
             "ME, UAS at Code 2, no USBNTMAP.SYS: a stick, then a replug");
    CHECK_EQ(inst_uas_fix_9x(0, 10, 1, 2, 0), INST_UASFIX_NUSB,
             "98 SE, UAS at Code 2, no USBNTMAP.SYS: NUSB's storage half");
    CHECK_EQ(inst_uas_fix_9x(0, 90, 1, 2, 1), INST_UASFIX_NONE,
             "Code 2 with the filter present: another cause, no advice");
    CHECK_EQ(inst_uas_fix_9x(0, 90, 1, 0, 0), INST_UASFIX_NONE,
             "working UAS entry: nothing");
    CHECK_EQ(inst_uas_fix_9x(0, 90, 1, 28, 0), INST_UASFIX_NONE,
             "Code 28 is not the filter");
    CHECK_EQ(inst_uas_fix_9x(0, 90, 0, 2, 0), INST_UASFIX_NONE,
             "not the UAS INF: nothing");
    CHECK_EQ(inst_uas_fix_9x(1, 0, 1, 2, 0), INST_UASFIX_NONE,
             "NT: no filter, nothing");
    memset(&d, 0, sizeof(d));
    d.FilterInf = 1;
    d.HasService = 1;
    d.ServiceIsPci = 1;
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_FILTER, "pci port: add filter");
    d.ServiceIsPci = 0;
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_FOREIGN, "other bridge driver");
    d.FilterPresent = 1;
    CHECK_EQ(inst_action(&d, pkg), INST_ACT_UPDATE, "filter present: versioned");

    CHECK(inst_action_runs(INST_ACT_INSTALL, 0), "install runs");
    CHECK(inst_action_runs(INST_ACT_FILTER, 0), "filter runs");
    CHECK(!inst_action_runs(INST_ACT_CURRENT, 0), "current does not");
    CHECK(inst_action_runs(INST_ACT_CURRENT, 1), "current with /force");
    CHECK(inst_action_runs(INST_ACT_NEWER, 1), "newer with /force");
    CHECK(!inst_action_runs(INST_ACT_FOREIGN, 1), "foreign never, even /force");

    CHECK_EQ(inst_remove_rank("USB\\VID_1&PID_2\\x", "xhciuas", 0), 0, "UAS first");
    CHECK_EQ(inst_remove_rank("USB\\XHCI98_COMPOSITE&VID_1&PID_2\\x", "xhci98", 0),
             1, "composite");
    CHECK_EQ(inst_remove_rank("XHCI98\\HUB&VID_1&PID_2\\x", NULL, 0), 2,
             "old hub, no service");
    CHECK_EQ(inst_remove_rank("USB\\XHCI98_ROOT_HUB\\4&1", "xhci98", 0), 3, "root");
    CHECK_EQ(inst_remove_rank("PCI\\VEN_8086&DEV_15BF\\3", "xhcicm", 0), 4, "NHI");
    CHECK_EQ(inst_remove_rank("PCI\\VEN_1B36&DEV_000D\\3", "xhci98", 0), 5,
             "controller last");
    CHECK_EQ(inst_remove_rank("PCI\\VEN_8086&DEV_15C0\\3", "pci", 1), -1,
             "a filtered port is not removed");
    CHECK_EQ(inst_remove_rank("USB\\VID_046D&PID_C077\\5", "HidUsb", 0), -1,
             "another driver's device is left");
    CHECK_EQ(inst_remove_rank("USB\\XHCI98_NOXPORT&VID_1&PID_2\\x", "usbstor", 0),
             -1, "a refused interface is left");
}

static void test_helpers(void)
{
    unsigned char pe[0x100];
    INST_U32 m;
    char out[128];
    static const char filters[] = "other\0xhcicm\0third\0";
    static const char only[] = "XHCICM\0";
    char msz[64];
    int removed;
    size_t n;

    memset(pe, 0, sizeof(pe));
    pe[0] = 'M';
    pe[1] = 'Z';
    pe[0x3C] = 0x80;
    pe[0x80] = 'P';
    pe[0x81] = 'E';
    pe[0x84] = 0x64;
    pe[0x85] = 0x86;
    CHECK(inst_bundled_file("stor98.inf") && inst_bundled_file("STOR98.SYS") &&
          inst_bundled_file("stor98-LICENSE.txt") && inst_bundled_file("stor98.mpd") &&
          inst_bundled_file("stor98-test.cer"),
          "Stor98's files are the bundled driver's");
    CHECK(!inst_bundled_file("xhci98.inf") && !inst_bundled_file("xhcicm.sys") &&
          !inst_bundled_file("stor9.inf") && !inst_bundled_file("txtsetup.oem") &&
          !inst_bundled_file("xhci98-test.cer"),
          "this package's own files are not the bundled driver's");
    CHECK(inst_bundled_service("stor98") && inst_bundled_service("STOR98") &&
          !inst_bundled_service("xhci98") && !inst_bundled_service("stor98x"),
          "the bundled driver's service");
    CHECK(inst_pe_machine(pe, sizeof(pe), &m) && m == INST_PE_AMD64, "amd64 PE");
    pe[0x84] = 0x4C;
    pe[0x85] = 0x01;
    CHECK(inst_pe_machine(pe, sizeof(pe), &m) && m == INST_PE_I386, "i386 PE");
    pe[0x3C] = 0xFE;
    CHECK(!inst_pe_machine(pe, sizeof(pe), &m), "e_lfanew past the buffer end");
    pe[0] = 'X';
    CHECK(!inst_pe_machine(pe, sizeof(pe), &m), "not MZ");

    CHECK_EQ(inst_path_append("C:\\WINDOWS\\INF", "C:\\WINDOWS\\XHCI98", out,
                              sizeof(out)), 1, "appended");
    CHECK(inst_eq(out, "C:\\WINDOWS\\INF;C:\\WINDOWS\\XHCI98"), "append text");
    CHECK_EQ(inst_path_append("C:\\WINDOWS\\INF; c:\\windows\\xhci98 ",
                              "C:\\WINDOWS\\XHCI98", out, sizeof(out)), 0,
             "already there, without case");
    CHECK_EQ(inst_path_append("", "C:\\X", out, sizeof(out)), 1, "empty list");
    CHECK(inst_eq(out, "C:\\X"), "no leading ';'");
    CHECK_EQ(inst_path_append("C:\\A;", "C:\\X", out, sizeof(out)), 1, "ends in ;");
    CHECK(inst_eq(out, "C:\\A;C:\\X"), "no doubled ';'");
    CHECK_EQ(inst_path_append("C:\\A", "C:\\X", out, 6), -1, "too small");
    CHECK_EQ(inst_path_remove("C:\\A;C:\\X;C:\\B", "c:\\x", out, sizeof(out)), 1,
             "removed");
    CHECK(inst_eq(out, "C:\\A;C:\\B"), "rest kept in order");
    CHECK_EQ(inst_path_remove("C:\\A", "C:\\X", out, sizeof(out)), 0, "not there");
    CHECK_EQ(inst_path_remove_prefix("C:\\WINDOWS\\INF;c:\\windows\\xhci98\\2.4.0.0-x86;C:\\D",
                                     "C:\\WINDOWS\\XHCI98\\", out, sizeof(out)), 1,
             "an older local copy removed by prefix");
    CHECK(inst_eq(out, "C:\\WINDOWS\\INF;C:\\D"), "the rest kept");
    CHECK_EQ(inst_path_remove_prefix("C:\\WINDOWS\\INF", "C:\\WINDOWS\\XHCI98\\",
                                     out, sizeof(out)), 0, "none to remove");
    CHECK(inst_fnv1a(INST_FNV_START, (const unsigned char *)"a", 1) == 0xE40C292CUL,
          "FNV-1a of \"a\"");
    CHECK(inst_fnv1a(INST_FNV_START, (const unsigned char *)"", 0) == INST_FNV_START,
          "FNV-1a of nothing");

    n = inst_msz_remove(filters, sizeof(filters), "xhcicm", msz, sizeof(msz),
                        &removed);
    CHECK(removed, "filter name removed");
    CHECK_EQ(n, 13, "other\\0third\\0\\0");
    CHECK(memcmp(msz, "other\0third\0\0", 13) == 0, "order kept");
    n = inst_msz_remove(only, sizeof(only), "xhcicm", msz, sizeof(msz), &removed);
    CHECK(removed && n == 0, "nothing left: delete the value");
    n = inst_msz_remove(filters, sizeof(filters), "absent", msz, sizeof(msz),
                        &removed);
    CHECK(!removed && n == sizeof(filters), "absent: unchanged");
    n = inst_msz_remove(filters, sizeof(filters), "absent", msz, 4, &removed);
    CHECK(n == (size_t)-1, "too small");
    {
        /* The CriticalDeviceDatabase scrub (record 21 section 7.4): Windows
         * 2000's pci#cc_0604 and pci#cc_010802 read LowerFilters=xhcicm
         * after a marked unit's driver install (2026-10-19). */
        static const char cddbOnly[] = "xhcicm\0\0";
        static const char cddbMixed[] = "partmgr\0XHCICM\0other\0\0";
        static const char cddbOther[] = "partmgr\0\0";
        size_t ob;

        CHECK_EQ(inst_filter_drop(cddbOnly, sizeof(cddbOnly), "xhcicm", msz,
                                  sizeof(msz), &ob), INST_FD_DELETE,
                 "xhcicm alone: the value deleted, never written empty");
        CHECK_EQ(inst_filter_drop(cddbMixed, sizeof(cddbMixed), "xhcicm", msz,
                                  sizeof(msz), &ob), INST_FD_SET,
                 "with other filters: written back");
        CHECK(ob == 15 && memcmp(msz, "partmgr\0other\0\0", 15) == 0,
              "the other names kept in order, case ignored for ours");
        CHECK_EQ(inst_filter_drop(cddbOther, sizeof(cddbOther), "xhcicm", msz,
                                  sizeof(msz), &ob), INST_FD_NONE,
                 "another driver's filter alone: untouched");
        CHECK_EQ(inst_filter_drop(NULL, 0, "xhcicm", msz, sizeof(msz), &ob),
                 INST_FD_NONE, "no value: nothing");
        CHECK_EQ(inst_filter_drop(cddbMixed, sizeof(cddbMixed), "xhcicm", msz,
                                  4, &ob), INST_FD_ERROR, "too small: an error");
        CHECK_EQ(inst_filter_drop("xhcicmx\0\0", 10, "xhcicm", msz,
                                  sizeof(msz), &ob), INST_FD_NONE,
                 "a longer name is not ours");
    }

    CHECK(inst_testsigning(" NOEXECUTE=OPTIN  TESTSIGNING"), "testsigning on");
    CHECK(inst_testsigning("/testsigning /noexecute=optin"), "slash form");
    CHECK(!inst_testsigning(" NOEXECUTE=OPTIN"), "off");
    CHECK(!inst_testsigning(" NOTESTSIGNINGX"), "a longer word is not it");
    CHECK(!inst_testsigning(NULL), "NULL");

    {
        static const unsigned char one[] = "xx\0XHCI98_FLAVOUR_RELEASE\0yy";
        static const unsigned char two[] =
            "XHCI98_FLAVOUR_DEBUG\0XHCI98_FLAVOUR_QEMU\0";
        static const unsigned char none[] = "XHCI98_FLAVOUR_\0nothing";
        char fl[16];
        CHECK_EQ(inst_flavour_marker(one, sizeof(one), fl, sizeof(fl)), 1,
                 "one marker");
        CHECK(inst_eq(fl, "RELEASE"), "its flavour");
        CHECK_EQ(inst_flavour_marker(two, sizeof(two), fl, sizeof(fl)), 2,
                 "two markers counted");
        CHECK_EQ(inst_flavour_marker(none, sizeof(none), fl, sizeof(fl)), 0,
                 "a bare prefix is no marker");
    }
}

static void test_values(void)
{
    INST_ENV e;
    int psw = inst_value_find("XhciIntelPortSwitch");
    int im = inst_value_find("xhciinterruptmode");
    int ft = inst_value_find("XhciPollFineTimer");
    int ss = inst_value_find("XhciSelectiveSuspend");
    int cm = inst_value_find("XhciCmMode");
    INST_U32 v;
    int idx;
    char text[64];

    CHECK(psw >= 0 && im >= 0 && ft >= 0 && ss >= 0 && cm >= 0, "rows found");
    CHECK_EQ(inst_value_find("XhciTolerance"), -1, "a quirk value is not asked");
    CHECK_EQ(inst_value_find("XhciLogVerbosity"), -1, "the log is SNAPON's");

    /* 98 SE with a QEMU controller: no switchover, no fine timer, no NHI. */
    e = env(0, 4, 10, INST_ARCH_X86);
    e.AnyXhci = 1;
    CHECK(!inst_value_applies(psw, &e), "98, no Intel 7-9: no port switch");
    CHECK(inst_value_applies(im, &e), "98: interrupt mode");
    CHECK(!inst_value_applies(ft, &e), "98: no fine timer");
    /* Owner, 2026-10-15: the fine timer and selective suspend are never
     * asked; /set Name=Value still sets them. */
    CHECK(!inst_value_applies(ss, &e), "98: selective suspend not asked");
    CHECK(inst_value_set("XhciSelectiveSuspend=1", &idx, &v) == 1 &&
          idx == ss && v == 1, "/set still sets selective suspend");
    CHECK(inst_value_set("XhciPollFineTimer=1", &idx, &v) == 1 &&
          idx == ft && v == 1, "/set still sets the fine timer");
    CHECK(!inst_value_applies(cm, &e), "no NHI: no cm values");
    CHECK_EQ(inst_value_asked_mask(im, &e), 0x7, "98: interrupt mode 0-2");
    e.PswController = 1;
    CHECK(inst_value_applies(psw, &e), "an Intel 7-9 controller: port switch");
    e.CmNhi = 1;
    CHECK(inst_value_applies(cm, &e), "an NHI: cm values");
    e = env(1, 5, 0, INST_ARCH_X86);
    e.AnyXhci = 1;
    CHECK(!inst_value_applies(ft, &e), "2000: the fine timer not asked");
    CHECK(!inst_value_applies(ss, &e), "2000: selective suspend not asked");
    /* Owner, 2026-10-15: the silent install is asked on 98 SE and ME only,
     * and /set refuses it on NT. */
    {
        int si = inst_value_find("XhciSilentInstall");
        INST_ENV e9 = env(0, 4, 10, INST_ARCH_X86);
        e9.AnyXhci = 1;
        CHECK(si >= 0, "the silent install's row");
        CHECK(!inst_value_applies(si, &e), "2000: the silent install not asked");
        CHECK_EQ(inst_value_set_mask(si, &e), 0, "2000: /set refuses it");
        CHECK(inst_value_applies(si, &e9), "98 SE: the silent install asked");
        CHECK_EQ(inst_value_asked_mask(si, &e9), 0x3, "98 SE: 0 and 1");
        CHECK_EQ(inst_value_set_mask(si, &e9), 0x3, "98 SE: /set takes 0 and 1");
        e9.Major = 4;
        e9.Minor = 90;
        CHECK(inst_value_applies(si, &e9), "ME: the silent install asked");
        e9.AnyXhci = 0;
        CHECK(!inst_value_applies(si, &e9), "no controller: not asked");
        CHECK(instValues[si].Caution != NULL &&
              instValues[si].CautionMask == 0, "its caveat shown every time");
    }
    CHECK_EQ(inst_value_asked_mask(im, &e), 0x7, "2000: no MSI");
    e = env(1, 6, 1, INST_ARCH_AMD64);
    e.AnyXhci = 1;
    CHECK_EQ(inst_value_asked_mask(im, &e), 0xF, "7: MSI offered");
    e.AnyXhci = 0;
    CHECK(!inst_value_applies(im, &e), "no controller: nothing asked");
    CHECK(!inst_value_applies(-1, &e), "bad index");

    CHECK_EQ(inst_value_answer("", 0x3, &v), 0, "Enter keeps");
    CHECK_EQ(inst_value_answer("  \r\n", 0x3, &v), 0, "blank keeps");
    CHECK(inst_value_answer(" 1 ", 0x3, &v) == 1 && v == 1, "1 taken");
    CHECK_EQ(inst_value_answer("2", 0x3, &v), -1, "2 not offered");
    CHECK_EQ(inst_value_answer("x", 0x3, &v), -1, "junk");
    CHECK_EQ(inst_value_answer("0x1", 0x3, &v), -1, "hex not taken");
    CHECK_EQ(inst_value_answer("99", 0xFFFFFFFFUL, &v), -1, "over 31");

    CHECK(inst_value_set("XhciIntelPortSwitch=2", &idx, &v) == 1 && idx == psw &&
          v == 2, "/set takes 2 for the port switch");
    CHECK(inst_value_set("XhciCmMode:3", &idx, &v) == 1 && v == 3, "':' form");
    CHECK_EQ(inst_value_set("XhciPollFineTimer=2", &idx, &v), -1, "not allowed");
    e = env(1, 5, 1, INST_ARCH_X86);
    CHECK_EQ(inst_value_set_mask(im, &e), 0x7, "XP: /set refuses mode 3");
    e = env(1, 6, 0, INST_ARCH_X86);
    CHECK_EQ(inst_value_set_mask(im, &e), 0xF, "Vista: /set takes mode 3");
    CHECK_EQ(inst_value_set_mask(psw, &e), 0x7, "/set takes port switch 2");
    CHECK_EQ(inst_value_set("XhciNope=1", &idx, &v), 0, "unknown name");
    for (idx = 0; idx < instValueCount; idx++) {
        CHECK(instValues[idx].Meaning != NULL && instValues[idx].Meaning[0] != '\0',
              "every value has a meaning");
        CHECK((instValues[idx].Asked & ~instValues[idx].Settable) == 0,
              "every asked value is settable");
    }
    CHECK(instValues[psw].Caution != NULL && instValues[psw].CautionMask == 0x4,
          "port switch 2 carries its warning");
    CHECK(instValues[ss].Caution != NULL, "selective suspend carries its caution");
    CHECK_EQ(inst_value_set("=1", &idx, &v), -2, "no name");
    CHECK_EQ(inst_value_set("XhciCmMode", &idx, &v), -2, "no value");

    /* Owner, 2026-10-15 (record 24 section 11): four connection-manager
     * values never asked, /set only, each allowed set cm_vals.c's. */
    {
        static const char *const names[4] = {
            "XhciCmReenum", "XhciCmUsb3Tunnel", "XhciCmLaneBonding",
            "XhciCmPreOsCleanup"
        };
        static const INST_U32 top[4] = { 1, 1, 1, 2 };
        char arg[64];
        int k;
        int ix;
        INST_ENV en = env(0, 4, 10, INST_ARCH_X86);
        en.CmNhi = 1;
        for (k = 0; k < 4; k++) {
            ix = inst_value_find(names[k]);
            CHECK(ix >= 0, names[k]);
            if (ix < 0) {
                continue;
            }
            CHECK_EQ(instValues[ix].Asked, 0, "never asked");
            CHECK(!inst_value_applies(ix, &en), "not asked with an NHI present");
            CHECK_EQ(instValues[ix].Settable, (2UL << top[k]) - 1,
                     "the allowed set is 0 to the rule's maximum");
            sprintf(arg, "%s=%lu", names[k], (unsigned long)top[k]);
            CHECK(inst_value_set(arg, &idx, &v) == 1 && idx == ix &&
                      v == top[k], "/set takes the maximum");
            sprintf(arg, "%s=0", names[k]);
            CHECK(inst_value_set(arg, &idx, &v) == 1 && v == 0, "/set takes 0");
            sprintf(arg, "%s=%lu", names[k], (unsigned long)(top[k] + 1));
            CHECK_EQ(inst_value_set(arg, &idx, &v), -1,
                     "/set refuses one past the maximum");
            CHECK_EQ(inst_value_set_mask(ix, &en), instValues[ix].Settable,
                     "settable on 98 SE");
        }
    }

    inst_mask_text(0x7, text, sizeof(text));
    CHECK(inst_eq(text, "0, 1, 2"), "mask text");
    inst_mask_text(0x0, text, sizeof(text));
    CHECK(inst_eq(text, ""), "empty mask text");
}

static int parse(int argc, const char **argv, INST_OPTS *o)
{
    char err[256];

    return inst_parse_args(argc, (char **)argv, o, err, sizeof(err));
}

static void test_args(void)
{
    INST_OPTS o;
    const char *a1[] = { "x", "/dir", "release-x86", "/list" };
    const char *a2[] = { "x", "-set", "XhciSelectiveSuspend=1", "/yes",
                         "/set", "XhciInterruptMode", "2" };
    const char *a3[] = { "x", "/frobnicate" };
    const char *a4[] = { "x", "/list", "/yes" };
    const char *a5[] = { "x", "/defaults", "/set", "XhciCmMode=0" };
    const char *a6[] = { "x", "/set", "XhciSelectiveSuspend=2" };
    const char *a7[] = { "x", "/selftest", "/list" };
    const char *a8[] = { "x", "/uninstall", "/set", "XhciCmMode=0" };
    const char *a9[] = { "x", "/set", "XhciCmMode=0", "/set", "xhcicmmode=1" };
    const char *a10[] = { "x", "/dir" };
    const char *a11[] = { "x", "/uninstall", "/yes", "/nopause", "/log", "C:\\L" };
    const char *a12[] = { "x", "/clean" };

    CHECK_EQ(parse(4, a1, &o), 0, "/dir /list");
    CHECK(o.List && inst_eq(o.Dir, "release-x86"), "fields");
    CHECK_EQ(parse(7, a2, &o), 0, "/set twice, one split by a batch file");
    CHECK_EQ(o.SetCount, 2, "two values");
    CHECK_EQ(o.SetValue[1], 2, "split form joined");
    CHECK(o.Yes, "/yes");
    CHECK_EQ(parse(2, a3, &o), 1, "unknown switch");
    CHECK_EQ(parse(3, a4, &o), 1, "/list /yes");
    CHECK_EQ(parse(4, a5, &o), 1, "/defaults /set");
    CHECK_EQ(parse(3, a6, &o), 1, "a value /set does not take");
    CHECK_EQ(parse(3, a7, &o), 1, "/selftest with company");
    CHECK_EQ(parse(4, a8, &o), 1, "/uninstall /set");
    CHECK_EQ(parse(5, a9, &o), 1, "a value set twice");
    CHECK_EQ(parse(2, a10, &o), 1, "/dir without a name");
    CHECK_EQ(parse(6, a11, &o), 0, "/uninstall /yes /nopause /log");
    CHECK(o.Uninstall && o.Yes && o.NoPause && inst_eq(o.Log, "C:\\L"), "fields");
    /* Owner, 2026-10-15: the installer never runs XHCICLN, so the switch
     * that once did is gone - an unknown argument. */
    CHECK_EQ(parse(2, a12, &o), 1, "/clean is no switch");

    CHECK_EQ(inst_exit_code(0, 0, 0, 0), 0, "done");
    CHECK_EQ(inst_exit_code(1, 1, 1, 1), 1, "refused first");
    CHECK_EQ(inst_exit_code(0, 1, 1, 1), 2, "failed beats restart and steps");
    CHECK_EQ(inst_exit_code(0, 0, 1, 1), 4, "steps beat restart");
    CHECK_EQ(inst_exit_code(0, 0, 1, 0), 3, "restart");
}

/* The last line agrees with the exit code; the settle; the busy retry; the
 * results' state text; staged again; the confirmation's wording. */
static void test_flow(void)
{
    int line = -1;
    INST_SETTLE s;
    char t[64];

    /* /defaults /yes over current devices, the INFs staged already: no
     * change, "No changes were made.", exit 0 (the 2000 leg read "Changes
     * were made" there). */
    CHECK_EQ(inst_final(0, 0, inst_exit_code(0, 0, 0, 0), &line), 0,
             "no change: 0");
    CHECK_EQ(line, INST_FINAL_NO_CHANGES, "no change: its line");
    CHECK(strcmp(inst_final_text(line), "No changes were made.") == 0,
          "no change: the text");
    CHECK_EQ(inst_final(1, 1, 0, &line), 0, "/list: 0");
    CHECK_EQ(line, INST_FINAL_NO_CHANGES, "/list: no change");
    CHECK_EQ(inst_final(0, 0, 4, &line), 4, "no change, steps: 4 kept");
    CHECK_EQ(line, INST_FINAL_NO_CHANGES, "no change, steps: its line");
    /* A change in a finished run asks for the restart and exits 3. */
    CHECK_EQ(inst_final(1, 0, 0, &line), 3, "change, done: 3");
    CHECK_EQ(line, INST_FINAL_RESTART, "change, done: restart line");
    CHECK(strstr(inst_final_text(line), "Please restart Windows now") != NULL,
          "restart text");
    CHECK_EQ(inst_final(1, 0, 3, &line), 3, "change, restart: 3");
    CHECK_EQ(line, INST_FINAL_RESTART, "change, restart: restart line");
    /* A change in a run that stopped: no bare restart request beside
     * "run INSTALL.BAT again". */
    CHECK_EQ(inst_final(1, 0, 2, &line), 2, "change, failed: 2");
    CHECK_EQ(line, INST_FINAL_UNFINISHED, "change, failed: unfinished");
    CHECK_EQ(inst_final(1, 0, 4, &line), 4, "change, steps: 4");
    CHECK_EQ(line, INST_FINAL_UNFINISHED, "change, steps: unfinished");
    CHECK(strstr(inst_final_text(line), "Please restart") == NULL,
          "unfinished: no restart request");
    CHECK_EQ(inst_final(1, 0, 1, &line), 2, "change under a refusal: 2");

    /* Settle: quiet for 3 s, nothing expected. */
    inst_settle_init(&s, 60000UL, 20000UL, 20000UL);
    CHECK_EQ(inst_settle_step(&s, 0, 0, 7, 1, 0), INST_SETTLE_WAIT, "quiet 0");
    CHECK_EQ(inst_settle_step(&s, 2500, 0, 7, 1, 0), INST_SETTLE_WAIT, "quiet 2.5");
    CHECK_EQ(inst_settle_step(&s, 3000, 0, 7, 1, 0), INST_SETTLE_DONE, "quiet 3");
    /* The tree moves: the quiet stretch starts again. */
    inst_settle_init(&s, 60000UL, 20000UL, 20000UL);
    inst_settle_step(&s, 0, 0, 7, 1, 0);
    CHECK_EQ(inst_settle_step(&s, 2500, 0, 8, 1, 0), INST_SETTLE_WAIT, "moved");
    CHECK_EQ(inst_settle_step(&s, 5000, 0, 8, 1, 0), INST_SETTLE_WAIT, "moved 2.5");
    CHECK_EQ(inst_settle_step(&s, 5500, 0, 8, 1, 0), INST_SETTLE_DONE, "moved 3");
    /* The 2000 race: the controller updated, no root hub yet (expected),
     * Windows picks it up (pending), its wizard waits, then ends. */
    inst_settle_init(&s, 600000UL, 20000UL, 20000UL);
    CHECK_EQ(inst_settle_step(&s, 0, 0, 1, 1, 1), INST_SETTLE_WAIT, "no hub yet");
    CHECK_EQ(inst_settle_step(&s, 3500, 0, 1, 1, 1), INST_SETTLE_WAIT,
             "quiet but a hub expected");
    CHECK_EQ(inst_settle_step(&s, 4000, 0, 2, 1, 0), INST_SETTLE_WAIT, "hub seen");
    CHECK_EQ(inst_settle_step(&s, 4500, 1, 2, 1, 0), INST_SETTLE_WAIT,
             "Windows installs it");
    CHECK_EQ(inst_settle_step(&s, 24000, 1, 2, 1, 0), INST_SETTLE_WAIT,
             "pending 19.5 s");
    CHECK_EQ(inst_settle_step(&s, 24500, 1, 2, 1, 0), INST_SETTLE_TELL,
             "pending 20 s: the wizard is named");
    CHECK_EQ(inst_settle_step(&s, 25000, 1, 2, 1, 0), INST_SETTLE_WAIT,
             "told once");
    CHECK_EQ(inst_settle_step(&s, 90000, 0, 3, 1, 0), INST_SETTLE_WAIT,
             "wizard finished");
    CHECK_EQ(inst_settle_step(&s, 93000, 0, 3, 1, 0), INST_SETTLE_DONE,
             "settled after it");
    /* An expected device that never comes: bounded by ExpectMs. */
    inst_settle_init(&s, 600000UL, 20000UL, 20000UL);
    inst_settle_step(&s, 0, 0, 1, 1, 1);
    CHECK_EQ(inst_settle_step(&s, 19500, 0, 1, 1, 1), INST_SETTLE_WAIT,
             "expected 19.5 s");
    CHECK_EQ(inst_settle_step(&s, 20000, 0, 1, 1, 1), INST_SETTLE_DONE,
             "expected: bound");
    /* Pending at the limit: timeout; not pending at the limit: done. */
    inst_settle_init(&s, 60000UL, 5000UL, 0UL);
    CHECK_EQ(inst_settle_step(&s, 0, 1, 1, 1, 0), INST_SETTLE_WAIT, "pending 0");
    CHECK_EQ(inst_settle_step(&s, 5000, 1, 1, 1, 0), INST_SETTLE_TELL, "tell 5");
    CHECK_EQ(inst_settle_step(&s, 60000, 1, 1, 1, 0), INST_SETTLE_TIMEOUT,
             "pending at the limit");
    inst_settle_init(&s, 60000UL, 5000UL, 0UL);
    inst_settle_step(&s, 59000, 0, 1, 1, 0);
    CHECK_EQ(inst_settle_step(&s, 60000, 0, 2, 1, 0), INST_SETTLE_UNSTEADY,
             "moving, not pending, at the limit: never settled unquiet");
    CHECK_EQ(inst_settle_step(&s, 60500, -1, 2, 1, 0), INST_SETTLE_FAILED,
             "question failed");

    /* A wizard that ends just before the deadline: no settle without the
     * quiet stretch. */
    inst_settle_init(&s, 600000UL, 20000UL, 0UL);
    inst_settle_step(&s, 0, 1, 1, 1, 0);
    CHECK_EQ(inst_settle_step(&s, 599500, 0, 2, 1, 0), INST_SETTLE_WAIT,
             "cleared 0.5 s before the deadline");
    CHECK_EQ(inst_settle_step(&s, 600000, 0, 2, 1, 0), INST_SETTLE_UNSTEADY,
             "deadline without the quiet stretch");
    CHECK(s.Moved, "pending seen: moved");
    /* A reading that failed restarts the quiet stretch. */
    inst_settle_init(&s, 60000UL, 20000UL, 0UL);
    inst_settle_step(&s, 0, 0, 5, 1, 0);
    CHECK(!s.Moved, "first reading: nothing moved");
    CHECK_EQ(inst_settle_step(&s, 2500, 0, 5, 0, 0), INST_SETTLE_WAIT,
             "incomplete");
    CHECK_EQ(inst_settle_step(&s, 3000, 0, 5, 1, 0), INST_SETTLE_WAIT,
             "whole again: the stretch starts now");
    CHECK_EQ(inst_settle_step(&s, 6000, 0, 5, 1, 0), INST_SETTLE_DONE,
             "3 s whole and quiet");
    CHECK(s.Moved, "an incomplete reading counts as moved");
    inst_settle_init(&s, 60000UL, 20000UL, 0UL);
    inst_settle_step(&s, 0, 0, 5, 1, 0);
    CHECK_EQ(inst_settle_step(&s, 3000, 0, 5, 1, 0), INST_SETTLE_DONE,
             "quiet from the start");
    CHECK(!s.Moved, "quiet from the start: not moved");
    inst_settle_init(&s, 4000UL, 20000UL, 0UL);
    CHECK_EQ(inst_settle_step(&s, 0, 0, 5, 0, 0), INST_SETTLE_WAIT,
             "never whole");
    CHECK_EQ(inst_settle_step(&s, 4000, 0, 5, 0, 0), INST_SETTLE_UNSTEADY,
             "never whole: unsteady at the limit");

    /* The busy answer: tried again twice, then a failure. */
    CHECK_EQ(inst_busy_retry(0x20UL, 1), 1, "busy once: again");
    CHECK_EQ(inst_busy_retry(0x20UL, 2), 1, "busy twice: again");
    CHECK_EQ(inst_busy_retry(0x20UL, 3), 0, "busy three times: failed");
    CHECK_EQ(inst_busy_retry(0x5UL, 1), 0, "another error: failed");
    {
        /* A spent member blocks a call a newer member would start. */
        int tries[3];
        tries[0] = 0; tries[1] = 2; tries[2] = 0;
        CHECK_EQ(inst_busy_spent(tries, 3), 0, "two busy answers: one more");
        tries[1] = 3;
        CHECK_EQ(inst_busy_spent(tries, 3), 1, "a spent member blocks");
        CHECK_EQ(inst_busy_spent(tries, 0), 0, "nobody");
    }

    /* A parent's job and the devices below it (98 SE, 2026-10-19). */
    CHECK_EQ(inst_parent_kind("USB\\XHCI98_ROOT_HUB\\0PCI&VEN_1B36", 0), 1,
             "the root hub is a parent");
    CHECK_EQ(inst_parent_kind("USB\\XHCI98_HUB&VID_0409&PID_55AA\\314159", 0),
             1, "a hub is a parent");
    CHECK_EQ(inst_parent_kind("USB\\XHCI98_HUB30&VID_1&PID_2\\1", 0), 1,
             "a SuperSpeed hub is a parent");
    CHECK_EQ(inst_parent_kind("USB\\XHCI98_COMPOSITE&VID_1&PID_2\\1", 0), 1,
             "a composite entry is a parent");
    CHECK_EQ(inst_parent_kind("PCI\\VEN_1B36&DEV_000D\\3", 1), 1,
             "a controller is a parent");
    CHECK_EQ(inst_parent_kind("USB\\VID_0627&PID_0001\\28754", 0), 0,
             "a function is not");
    CHECK_EQ(inst_parent_kind("USB\\XHCI98_HUBX\\1", 0), 0,
             "a near name is not");
    CHECK_EQ(inst_settle_after_job(1, INST_DI_OUT_OK, 1), 1,
             "root hub installed, hub next: settle");
    CHECK_EQ(inst_settle_after_job(1, INST_DI_OUT_RESTART, 1), 1,
             "installed, restart pending: settle");
    CHECK_EQ(inst_settle_after_job(1, INST_DI_OUT_FAILED, 1), 1,
             "a failed install may have restarted it: settle");
    CHECK_EQ(inst_settle_after_job(1, INST_DI_OUT_STEPS, 1), 1,
             "cancelled in a dialog: settle");
    CHECK_EQ(inst_settle_after_job(1, INST_DI_OUT_ABSENT, 1), 0,
             "nothing was reached: no settle");
    CHECK_EQ(inst_settle_after_job(1, INST_DI_OUT_REFUSED, 1), 0,
             "the helper refused: no settle");
    CHECK_EQ(inst_settle_after_job(1, INST_DI_OUT_OK, 0), 0,
             "the last job of the pass: the pass's own settle follows");
    CHECK_EQ(inst_settle_after_job(0, INST_DI_OUT_OK, 1), 0,
             "a function's job: no settle");
    CHECK_EQ(inst_absent_retry(INST_DI_OUT_ABSENT, 1, 0), 1,
             "not present after a parent's job: once more after a settle");
    CHECK_EQ(inst_absent_retry(INST_DI_OUT_ABSENT, 1, 1), 0,
             "still not present after the retry: not present");
    CHECK_EQ(inst_absent_retry(INST_DI_OUT_ABSENT, 0, 0), 0,
             "no parent's job this run: not present at once");
    CHECK_EQ(inst_absent_retry(INST_DI_OUT_OK, 1, 0), 0,
             "installed: no retry");
    CHECK_EQ(inst_absent_retry(INST_DI_OUT_FAILED, 1, 0), 0,
             "failed: no retry");

    /* Windows 2000 after a first filter install (owner, 2026-10-15): the
     * NHI's or a port's Code 12, or a device gone, waits for the restart;
     * nothing else does. */
    CHECK_EQ(inst_tunnel_restart(1, 1, INST_INF_CMF, 0, 12UL), 1,
             "upstream port at Code 12: the restart");
    CHECK_EQ(inst_tunnel_restart(1, 1, INST_INF_CM, 1, 0UL), 1,
             "a switch device gone: the restart");
    CHECK_EQ(inst_tunnel_restart(1, 1, INST_INF_CMF, 0, 10UL), 0,
             "Code 10 stays a failure");
    CHECK_EQ(inst_tunnel_restart(1, 0, INST_INF_CMF, 0, 12UL), 0,
             "no filter installed: a failure");
    CHECK_EQ(inst_tunnel_restart(0, 1, INST_INF_CMF, 0, 12UL), 0,
             "not Windows 2000: a failure");
    CHECK_EQ(inst_tunnel_restart(1, 1, INST_INF_XHCI, 0, 12UL), 0,
             "not a tunnel device: a failure");

    /* The results' state text: never "started" with a problem code, never
     * an empty field. */
    inst_state_text(t, sizeof(t), "", "", 1, 28UL);
    CHECK(strcmp(t, "no driver, problem code 28") == 0, "U23b's root hub");
    inst_state_text(t, sizeof(t), "xhci98", "3.0.0.0", 1, 0UL);
    CHECK(strcmp(t, "xhci98 3.0.0.0, started") == 0, "started");
    inst_state_text(t, sizeof(t), "xhci98", "2.3.1.0", 0, 0UL);
    CHECK(strcmp(t, "xhci98 2.3.1.0, not started") == 0, "not started");
    inst_state_text(t, sizeof(t), "xhci98", "3.0.0.0", 0, 10UL);
    CHECK(strcmp(t, "xhci98 3.0.0.0, problem code 10") == 0, "code 10");
    inst_state_text(t, 8, "xhci98", "3.0.0.0", 1, 0UL);
    CHECK(strcmp(t, "xhci98 ") == 0, "bounded");

    /* Staged again: the name, or a path ending in it; never after a new
     * local copy or without an identical oem*.inf. */
    CHECK_EQ(inst_staged_again(1, 0, "oem0.inf", "oem0.inf"), 1, "name");
    CHECK_EQ(inst_staged_again(1, 0, "oem0.inf", "C:\\WINNT\\inf\\OEM0.INF"), 1,
             "path");
    CHECK_EQ(inst_staged_again(1, 0, "oem0.inf", "oem4.inf"), 0, "new name");
    CHECK_EQ(inst_staged_again(1, 1, "oem0.inf", "oem0.inf"), 0, "new copy");
    CHECK_EQ(inst_staged_again(0, 0, "", "oem0.inf"), 0, "none before");

    /* The confirmation says what will happen. */
    CHECK(strcmp(inst_confirm_text(1, 0, 1, 0),
                 "Install and stage the package now") == 0, "runs");
    CHECK(strstr(inst_confirm_text(0, 1, 1, 0), "settings") != NULL, "a setting");
    CHECK(strstr(inst_confirm_text(0, 0, 1, 0), "Every device is current") != NULL,
          "nothing");
    CHECK(strcmp(inst_confirm_text(1, 0, 0, 0),
                 "Copy the package and update DevicePath now") == 0, "9x runs");
    CHECK(strstr(inst_confirm_text(0, 1, 0, 0), "settings") != NULL, "9x setting");
    CHECK(strstr(inst_confirm_text(0, 0, 0, 0), "DevicePath now") != NULL,
          "9x nothing");
    /* An entry that may be a cancelled wizard's controller: no claim that
     * every device is current or needs no driver. */
    CHECK(strstr(inst_confirm_text(0, 0, 0, 1), "current") == NULL &&
          strstr(inst_confirm_text(0, 0, 0, 1), "listed apart") != NULL,
          "9x unsure");
    CHECK(strstr(inst_confirm_text(0, 1, 0, 1), "no device") == NULL &&
          strstr(inst_confirm_text(0, 1, 0, 1), "settings") != NULL,
          "9x unsure, a setting");
}

/* A PE image's embedded signature: the security directory, inside the
 * optional header, pointing to a table inside the file. */
static void pe_set32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void test_pe_signed(void)
{
    static unsigned char pe[1024];
    unsigned off = 0x80;
    unsigned opt = off + 24;

    memset(pe, 0, sizeof(pe));
    pe[0] = 'M';
    pe[1] = 'Z';
    pe[0x3C] = (unsigned char)off;
    pe[off] = 'P';
    pe[off + 1] = 'E';
    pe[off + 20] = 240;                 /* SizeOfOptionalHeader, PE32+ */
    pe[opt] = 0x0B;
    pe[opt + 1] = 0x02;                 /* PE32+ */
    pe[opt + 112 - 4] = 16;             /* NumberOfRvaAndSizes */
    CHECK_EQ(inst_pe_signed(pe, sizeof(pe)), 0, "PE32+ unsigned");
    pe_set32(pe + opt + 112 + 4 * 8, 0x300);    /* table at 0x300 */
    pe_set32(pe + opt + 112 + 4 * 8 + 4, 0x40);
    CHECK_EQ(inst_pe_signed(pe, sizeof(pe)), 1, "PE32+ signed");
    CHECK_EQ(inst_pe_signed(pe, 0x320), -1, "table cut off by the file's end");
    CHECK_EQ(inst_pe_signed(pe, 0x2F0), -1, "table past the file's end");
    pe[off + 20] = 100;
    CHECK_EQ(inst_pe_signed(pe, sizeof(pe)), -1,
             "directory outside the optional header");
    pe[off + 20] = 240;
    pe[opt + 112 - 4] = 4;
    CHECK_EQ(inst_pe_signed(pe, sizeof(pe)), 0, "four directories: none");
    memset(pe + opt, 0, sizeof(pe) - opt);
    pe[off + 20] = 224;                 /* PE32 */
    pe[opt] = 0x0B;
    pe[opt + 1] = 0x01;
    pe[opt + 96 - 4] = 16;
    CHECK_EQ(inst_pe_signed(pe, sizeof(pe)), 0, "PE32 unsigned");
    pe_set32(pe + opt + 96 + 4 * 8, 0x200);
    pe_set32(pe + opt + 96 + 4 * 8 + 4, 0x10);
    CHECK_EQ(inst_pe_signed(pe, sizeof(pe)), 1, "PE32 signed");
    CHECK_EQ(inst_pe_signed(pe, opt + 100), -1, "cut short");
    pe[0] = 'X';
    CHECK_EQ(inst_pe_signed(pe, sizeof(pe)), -1, "not MZ");
}

/* A mock device tree for inst_tree_sig, with faults to inject. Node 0 is
 * the controller; 1 the root hub (USB\XHCI98_ROOT_HUB); 2 and 3 its
 * children. */
typedef struct _MOCK_TREE {
    int ChildOf[4];         /* first child, -1 none                     */
    int Next[4];            /* next sibling, -1 none                    */
    const char *Id[4];
    INST_U32 Status[4];
    INST_U32 Problem[4];    /* left as is when no problem: garbage      */
    int FailChild;          /* node whose child read fails, -1 none     */
    int FailSibling;
    int FailId;
} MOCK_TREE;

static int mock_child(void *ctx, INST_U32 dn, INST_U32 *child)
{
    MOCK_TREE *t = (MOCK_TREE *)ctx;

    if ((int)dn == t->FailChild) {
        return INST_TREE_ERROR;
    }
    if (t->ChildOf[dn] < 0) {
        return INST_TREE_NONE;
    }
    *child = (INST_U32)t->ChildOf[dn];
    return INST_TREE_OK;
}

static int mock_sibling(void *ctx, INST_U32 dn, INST_U32 *sibling)
{
    MOCK_TREE *t = (MOCK_TREE *)ctx;

    if ((int)dn == t->FailSibling) {
        return INST_TREE_ERROR;
    }
    if (t->Next[dn] < 0) {
        return INST_TREE_NONE;
    }
    *sibling = (INST_U32)t->Next[dn];
    return INST_TREE_OK;
}

static int mock_id(void *ctx, INST_U32 dn, char *id, size_t cap)
{
    MOCK_TREE *t = (MOCK_TREE *)ctx;

    if ((int)dn == t->FailId) {
        return INST_TREE_ERROR;
    }
    inst_copy(id, t->Id[dn], cap);
    return INST_TREE_OK;
}

static int mock_status(void *ctx, INST_U32 dn, INST_U32 *status,
                       INST_U32 *problem)
{
    MOCK_TREE *t = (MOCK_TREE *)ctx;

    *status = t->Status[dn];
    *problem = t->Problem[dn];
    return INST_TREE_OK;
}

static void mock_init(MOCK_TREE *t)
{
    memset(t, 0, sizeof(*t));
    t->ChildOf[0] = 1;  t->Next[0] = -1;
    t->ChildOf[1] = 2;  t->Next[1] = -1;
    t->ChildOf[2] = -1; t->Next[2] = 3;
    t->ChildOf[3] = -1; t->Next[3] = -1;
    t->Id[0] = "PCI\\VEN_1B36&DEV_000D\\3&1";
    t->Id[1] = "USB\\XHCI98_ROOT_HUB\\4&1&0";
    t->Id[2] = "USB\\VID_0627&PID_0001\\1";
    t->Id[3] = "USB\\XHCI98_HUB&VID_0409&PID_55AA\\5&2";
    t->Status[1] = INST_DN_STARTED;
    t->Status[2] = INST_DN_STARTED;
    t->Status[3] = INST_DN_STARTED;
    t->Problem[1] = 28;     /* garbage: no DN_HAS_PROBLEM */
    t->Problem[2] = 1;
    t->Problem[3] = 18;
    t->FailChild = -1;
    t->FailSibling = -1;
    t->FailId = -1;
}

static void test_tree_sig(void)
{
    MOCK_TREE t;
    INST_TREE_OPS ops;
    INST_U32 h0;
    INST_U32 h;
    int expect;
    int nodes;

    ops.Ctx = &t;
    ops.Child = mock_child;
    ops.Sibling = mock_sibling;
    ops.Id = mock_id;
    ops.Status = mock_status;

    CHECK_EQ(inst_dn_problem(INST_DN_STARTED, 28), 0, "problem masked");
    CHECK_EQ(inst_dn_problem(INST_DN_HAS_PROBLEM, 28), 28, "problem kept");

    /* Healthy, with garbage problem numbers: read whole, nothing expected,
     * and the garbage does not reach the signature. */
    mock_init(&t);
    h = INST_FNV_START; expect = 0; nodes = 0;
    CHECK_EQ(inst_tree_sig(&ops, 0, 0, &h, &expect, &nodes), 1, "whole");
    CHECK_EQ(expect, 0, "healthy: nothing expected");
    CHECK_EQ(nodes, 3, "three below the controller");
    h0 = h;
    t.Problem[1] = 99;
    h = INST_FNV_START; expect = 0; nodes = 0;
    inst_tree_sig(&ops, 0, 0, &h, &expect, &nodes);
    CHECK(h == h0, "masked problem: same signature");

    /* A hub of the bus not configured yet: expected. A foreign device at
     * Code 1 is not. */
    t.Status[3] = INST_DN_HAS_PROBLEM;
    h = INST_FNV_START; expect = 0; nodes = 0;
    inst_tree_sig(&ops, 0, 0, &h, &expect, &nodes);
    CHECK_EQ(expect, 1, "bus hub at Code 18: expected");
    CHECK(h != h0, "the problem moves the signature");
    t.Status[3] = INST_DN_STARTED;
    t.Status[2] = INST_DN_HAS_PROBLEM;
    h = INST_FNV_START; expect = 0; nodes = 0;
    inst_tree_sig(&ops, 0, 0, &h, &expect, &nodes);
    CHECK_EQ(expect, 0, "foreign device at Code 1: not ours to expect");

    /* Faults: a failed child, sibling or id read is never a whole tree. */
    mock_init(&t);
    t.FailChild = 1;
    h = INST_FNV_START; expect = 0; nodes = 0;
    CHECK_EQ(inst_tree_sig(&ops, 0, 0, &h, &expect, &nodes), 0, "child fails");
    mock_init(&t);
    t.FailSibling = 2;
    h = INST_FNV_START; expect = 0; nodes = 0;
    CHECK_EQ(inst_tree_sig(&ops, 0, 0, &h, &expect, &nodes), 0, "sibling fails");
    mock_init(&t);
    t.FailId = 3;
    t.Status[3] = INST_DN_HAS_PROBLEM;
    h = INST_FNV_START; expect = 0; nodes = 0;
    CHECK_EQ(inst_tree_sig(&ops, 0, 0, &h, &expect, &nodes), 0, "id fails");
    CHECK_EQ(expect, 0, "an unread id is never inspected");
    /* Cut short: a subtree below the depth bound, or more devnodes than
     * the bound, is never whole. */
    mock_init(&t);
    h = INST_FNV_START; expect = 0; nodes = 0;
    CHECK_EQ(inst_tree_sig(&ops, 0, INST_TREE_DEPTH, &h, &expect, &nodes), 0,
             "children below the depth bound: not whole");
    h = INST_FNV_START; expect = 0; nodes = 0;
    CHECK_EQ(inst_tree_sig(&ops, 2, INST_TREE_DEPTH, &h, &expect, &nodes), 1,
             "a leaf at the depth bound: whole");
    h = INST_FNV_START; expect = 0; nodes = INST_TREE_NODES - 1;
    CHECK_EQ(inst_tree_sig(&ops, 0, 0, &h, &expect, &nodes), 0,
             "the node bound reached: not whole");
    /* No children at all: whole. */
    mock_init(&t);
    t.ChildOf[0] = -1;
    h = INST_FNV_START; expect = 0; nodes = 0;
    CHECK_EQ(inst_tree_sig(&ops, 0, 0, &h, &expect, &nodes), 1, "leaf");
}

/* ---- record 24: the Thunderbolt and USB4 device viewer and eject ---- */

static void put32(unsigned char *b, size_t o, unsigned long v)
{
    b[o] = (unsigned char)(v & 0xFF);
    b[o + 1] = (unsigned char)((v >> 8) & 0xFF);
    b[o + 2] = (unsigned char)((v >> 16) & 0xFF);
    b[o + 3] = (unsigned char)((v >> 24) & 0xFF);
}

static void test_tb_ids(void)
{
    char *cf = load("..\\src\\cm\\xhcicmf.inf");
    char msz[64];
    INST_U32 bdf;
    unsigned char path[16];
    unsigned char want[4];
    int n;
    int i;
    int found;

    /* The candidate table against xhcicmf.inf, both ways. */
    CHECK(cf != NULL, "xhcicmf.inf read");
    if (cf != NULL) {
        const char *p = cf;
        int lines = 0;
        while ((p = strstr(p, "=CmF.Dev,")) != NULL) {
            char id[64];
            size_t m = 0;
            p += 9;
            while (p[m] != '\r' && p[m] != '\n' && p[m] != '\0' &&
                   p[m] != ' ' && p[m] != ';' && m + 1 < sizeof(id)) {
                id[m] = p[m];
                m++;
            }
            id[m] = '\0';
            lines++;
            found = 0;
            for (i = 0; i < instTbPortIdCount; i++) {
                if (inst_eq(instTbPortIds[i], id)) {
                    found = 1;
                }
            }
            CHECK(found, "every xhcicmf.inf model id is in the tool's table");
        }
        CHECK_EQ(lines, instTbPortIdCount,
                 "the table holds exactly the INF's undecorated model ids");
        for (i = 0; i < instTbPortIdCount; i++) {
            char pat[80];
            strcpy(pat, "=CmF.Dev,");
            strcat(pat, instTbPortIds[i]);
            CHECK(strstr(cf, pat) != NULL, "every table id is an INF model id");
        }
        free(cf);
    }
    memset(msz, 0, sizeof(msz));
    strcpy(msz, "PCI\\VEN_8086&DEV_15C0&CC_0604");
    CHECK(inst_tb_candidate(msz, sizeof(msz)), "a candidate");
    strcpy(msz, "PCI\\VEN_8086&DEV_15C0");
    CHECK(!inst_tb_candidate(msz, sizeof(msz)), "the bare id is not");

    /* Host functions. */
    memset(msz, 0, sizeof(msz));
    strcpy(msz, "PCI\\VEN_8086&DEV_15BF&CC_088000");
    CHECK(inst_tb_is_nhi(msz, sizeof(msz), NULL, 0, ""), "class 0880 is an NHI");
    CHECK(!inst_tb_is_usbhc(msz, sizeof(msz), NULL, 0, ""), "not a USB controller");
    strcpy(msz, "PCI\\VEN_1B36&DEV_00F4&CC_0C0340");
    CHECK(inst_tb_is_nhi(msz, sizeof(msz), NULL, 0, ""), "class 0C0340 is an NHI");
    CHECK(!inst_tb_is_usbhc(msz, sizeof(msz), NULL, 0, ""), "an NHI is no USB controller");
    strcpy(msz, "PCI\\VEN_8086&DEV_15C1&CC_0C0330");
    CHECK(inst_tb_is_usbhc(msz, sizeof(msz), NULL, 0, ""), "an xHCI");
    CHECK(!inst_tb_is_nhi(msz, sizeof(msz), NULL, 0, ""), "not an NHI");
    strcpy(msz, "PCI\\VEN_1B36&DEV_0010&CC_010802");
    CHECK(!inst_tb_is_nhi(msz, sizeof(msz), NULL, 0, "") &&
              !inst_tb_is_usbhc(msz, sizeof(msz), NULL, 0, ""),
          "an NVMe controller is neither");
    CHECK(inst_tb_is_nhi(msz, sizeof(msz), NULL, 0, "XHCICM"),
          "bound to xhcicm is an NHI");
    strcpy(msz, "PCI\\VEN_1234&DEV_0C03");
    CHECK(!inst_tb_is_usbhc(msz, sizeof(msz), NULL, 0, ""),
          "a device id 0C03 is no class");

    /* Windows 98's instance ids (runs/run-39.md). */
    CHECK(inst_id_location("PCI\\VEN_1B36&DEV_0001&SUBSYS_00000000&REV_00\\BUS_00&DEV_06&FUNC_00",
                           &bdf) && bdf == 0x0030, "BUS_00&DEV_06&FUNC_00");
    CHECK(!inst_id_location("PCI\\VEN_8086&DEV_15C0\\08083000", &bdf),
          "a path is no BUS_ form");
    CHECK(inst_id_path("PCI\\VEN_8086&DEV_15C0&SUBSYS_00000000&REV_01\\08083000",
                       path, &n, 16) && n == 4 && path[0] == 0x08 &&
              path[2] == 0x30 && path[3] == 0, "08 08 30 00");
    CHECK(!inst_id_path("PCI\\X\\0808300", path, &n, 16), "odd length");
    CHECK(!inst_id_path("PCI\\X\\08G83000", path, &n, 16), "not hex");
    want[0] = 0x08;
    want[1] = 0x08;
    want[2] = 0x30;
    want[3] = 0x00;
    CHECK(inst_tb_loc_match("PCI\\VEN_8086&DEV_15C0&SUBSYS_00000000&REV_01\\08083000",
                            0x0208, want, 4), "02:01.0 by its path");
    CHECK(!inst_tb_loc_match("PCI\\VEN_8086&DEV_15C0&SUBSYS_00000000&REV_01\\10083000",
                             0x0210, want, 4), "02:02.0 is not that path");
    CHECK(!inst_tb_loc_match("PCI\\VEN_8086&DEV_15C0&SUBSYS_00000000&REV_01\\08083000",
                             0x0208, want, 0), "an unknown path matches nothing");
    CHECK(inst_tb_loc_match("PCI\\VEN_1B36&DEV_0001\\BUS_00&DEV_05&FUNC_00",
                            0x0028, want, 0), "a bus 0 member by its BUS_ form");
}

static void test_tb_record(void)
{
    unsigned char r[CM_ST_MAX_BYTES];
    size_t n;

    /* The pure half's numbers against cm_status.h. */
    CHECK_EQ(CM_ST_HDR_BYTES, 128, "header size");
    CHECK_EQ(CM_ST_ENTRY_BYTES, 128, "entry size");
    CHECK_EQ(CM_ST_O_ROUTER_COUNT, 72, "count offset");
    CHECK_EQ(CM_ST_O_ENTRY_BYTES, 76, "entry size offset");
    CHECK_EQ(CM_ST_O_PATHS, 84, "paths offset");
    CHECK_EQ(CM_ST_O_ECHO, 124, "echo offset");
    CHECK_EQ(CM_ST_MAX_ROUTERS, 24, "router limit");

    memset(r, 0, sizeof(r));
    put32(r, CM_ST_O_MAGIC, CM_ST_MAGIC);
    put32(r, CM_ST_O_VERSION, CM_ST_VERSION);
    put32(r, CM_ST_O_ROUTER_COUNT, 1);
    put32(r, CM_ST_O_ENTRY_BYTES, CM_ST_ENTRY_BYTES);
    put32(r, CM_ST_O_BYTES, CM_ST_HDR_BYTES + CM_ST_ENTRY_BYTES);
    put32(r, CM_ST_O_PORT_COUNT, 2);
    n = CM_ST_HDR_BYTES + CM_ST_ENTRY_BYTES;
    CHECK(inst_st_check(r, n), "a record checks");
    CHECK(!inst_st_check(r, n - 1), "a short read");
    put32(r, CM_ST_O_VERSION, 2);
    CHECK(!inst_st_check(r, n), "an unknown version");
    put32(r, CM_ST_O_VERSION, 1);
    put32(r, CM_ST_O_ROUTER_COUNT, 25);
    CHECK(!inst_st_check(r, n), "too many routers");
    put32(r, CM_ST_O_ROUTER_COUNT, 1);
    put32(r, CM_ST_O_PORT_COUNT, 5);
    CHECK(!inst_st_check(r, n), "too many ports");
    put32(r, CM_ST_O_PORT_COUNT, 2);
    r[CM_ST_O_PATHS + 8] = 8;
    CHECK(!inst_st_check(r, n), "a path count over 7");
    r[CM_ST_O_PATHS + 8] = 4;
    put32(r, CM_ST_O_MAGIC, 0);
    CHECK(!inst_st_check(r, n), "no magic");
    put32(r, CM_ST_O_MAGIC, CM_ST_MAGIC);

    /* The echo is the confirmation; the generation alone is not. */
    CHECK(!inst_st_confirmed(r, 0x1234), "no echo");
    put32(r, CM_ST_O_ECHO, 0x1234);
    CHECK(inst_st_confirmed(r, 0x1234), "the echo of this ask");
    CHECK(!inst_st_confirmed(r, 0x1235), "another session's echo");
    CHECK(!inst_st_confirmed(r, 0), "a zero ask is never confirmed");

    /* The diagnostic boot-time label. Now = 2026-10-15 at FILETIME
     * 0x01DD3D5F:00000000 (any value), uptime 600 s in the record and
     * 600,000 ms in the tick count: the same boot. */
    CHECK(inst_st_fresh(600, 0x00000000UL, 0x01DD3D5FUL, 0x00000000UL,
                        0x01DD3D5FUL, 600000UL), "the same boot");
    /* Written 10 minutes before a restart that happened 2 minutes ago:
     * the boots are 12 minutes apart. */
    CHECK(!inst_st_fresh(1200, 0x00000000UL, 0x01DD3D5FUL,
                         0x00000000UL + 0x8F0D1800UL, 0x01DD3D5FUL + 1UL,
                         120000UL), "an earlier boot");
    /* Boots three minutes apart pass the label: it is diagnostic only. */
    CHECK(inst_st_fresh(60, 0x00000000UL, 0x01DD3D5FUL, 0x6B49D200UL,
                        0x01DD3D5FUL, 60000UL), "three minutes apart: labelled same");
}

static void test_tb_units(void)
{
    /* The emulator's Alpine Ridge tree on 98 SE:
     *  0 root
     *  1  PCI bus
     *  2   00:06.0 root bridge (candidate 1B36:0001)
     *  3    01:01.0 upstream port (15C0)
     *  4     02:00.0 (15C0)
     *  5      03:00.0 NHI
     *  6     02:01.0 (15C0) - the verified tunnel port
     *  7      04:00.0 NVMe
     *  8     02:02.0 (15C0)
     *  9      05:00.0 xHCI (the host's)
     * 10       its root hub
     */
    int parent[16] = { -1, 0, 1, 2, 3, 4, 3, 6, 3, 8, 9 };
    int isNhi[16] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
    int isHc[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
    int verified[16] = { 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 };
    int prot[16];
    int unitOf[16];
    int n = 11;

    inst_tb_protect(parent, isNhi, isHc, n, prot);
    CHECK(prot[5] && prot[4] && prot[3] && prot[2] && prot[1] && prot[0],
          "the NHI and its ancestors are protected");
    CHECK(prot[9], "the host's xHCI beside the NHI is protected");
    CHECK(!prot[7] && !prot[6], "the tunnelled NVMe and its port are not");
    inst_tb_units(parent, verified, n, unitOf);
    CHECK_EQ(unitOf[7], 7, "the NVMe is a unit");
    CHECK_EQ(unitOf[9], -1, "the xHCI is no unit");
    CHECK(!inst_tb_unit_blocked(unitOf, prot, n, 7), "the NVMe is offered");

    /* An override naming the host xHCI's port 02:02.0. */
    verified[8] = 1;
    inst_tb_units(parent, verified, n, unitOf);
    CHECK_EQ(unitOf[9], 9, "the xHCI becomes a unit under the override");
    CHECK_EQ(unitOf[10], 9, "its root hub is inside it");
    CHECK(inst_tb_unit_blocked(unitOf, prot, n, 9),
          "but it holds a protected node and is not offered");
    verified[8] = 0;

    /* An override naming the NHI's own port 02:00.0. */
    verified[4] = 1;
    inst_tb_units(parent, verified, n, unitOf);
    CHECK_EQ(unitOf[5], 5, "the NHI as a unit");
    CHECK(inst_tb_unit_blocked(unitOf, prot, n, 5), "is never offered");
    verified[4] = 0;

    /* A dock: 02:01.0 -> dock upstream port 6.. with its own xHCI two
     * levels down, and an NHI nested inside (a dock carrying a host). */
    {
        int dp[16] = { -1, 0, 1, 2, 3, 4, 3, 6, 7, 8, 7, 10 };
        int dn[16] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 };
        int dh[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 };
        int dv[16] = { 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
        /* 7 dock upstream, 8 its downstream, 9 the dock's xHCI, 10 its
         * other downstream, 11 a network controller. */
        inst_tb_protect(dp, dn, dh, 12, prot);
        CHECK(!prot[9], "a dock's own xHCI is not the host's");
        inst_tb_units(dp, dv, 12, unitOf);
        CHECK(unitOf[7] == 7 && unitOf[9] == 7 && unitOf[11] == 7,
              "the dock is one unit with everything below it");
        CHECK(!inst_tb_unit_blocked(unitOf, prot, 12, 7), "and is offered");
        dn[11] = 1;
        inst_tb_protect(dp, dn, dh, 12, prot);
        CHECK(inst_tb_unit_blocked(unitOf, prot, 12, 7),
              "an NHI nested in a unit blocks it");
    }
}

/* Record 23 section 8.5: the Thunderbolt 3 / USB4 question (owner,
 * 2026-10-15). */
static void test_tb_question(void)
{
    int ask;
    int def;
    char *argv1[4];
    INST_OPTS o;
    char err[256];

    /* A controller present: asked, default yes; /yes installs it. */
    CHECK_EQ(inst_tb_decide(1, 1, 0, 0, 0, &ask, &def), 0, "present: asked");
    CHECK(ask && def, "present: default yes");
    CHECK_EQ(inst_tb_decide(1, 1, 1, 0, 0, &ask, &def), 1, "present, /yes: yes");
    CHECK(!ask, "present, /yes: not asked");
    /* None found: still asked, default no; /yes skips it. */
    CHECK_EQ(inst_tb_decide(1, 0, 0, 0, 0, &ask, &def), 0, "absent: asked");
    CHECK(ask && !def, "absent: default no");
    CHECK_EQ(inst_tb_decide(1, 0, 1, 0, 0, &ask, &def), 0, "absent, /yes: no");
    CHECK(!ask, "absent, /yes: not asked");
    /* /thunderbolt: yes, unasked, present or not, /yes or not. */
    CHECK_EQ(inst_tb_decide(1, 0, 0, 1, 0, &ask, &def), 1, "/thunderbolt, absent");
    CHECK(!ask, "/thunderbolt: not asked");
    CHECK_EQ(inst_tb_decide(1, 0, 1, 1, 0, &ask, &def), 1, "/thunderbolt /yes");
    CHECK_EQ(inst_tb_decide(1, 1, 0, 1, 0, &ask, &def), 1, "/thunderbolt, present");
    /* /list: not asked, the default shown. */
    CHECK_EQ(inst_tb_decide(1, 1, 0, 0, 1, &ask, &def), 1, "/list, present");
    CHECK_EQ(inst_tb_decide(1, 0, 0, 0, 1, &ask, &def), 0, "/list, absent");
    CHECK(!ask, "/list: not asked");
    /* A package without the Thunderbolt driver: nothing, whatever the
     * switches. */
    CHECK_EQ(inst_tb_decide(0, 1, 0, 1, 0, &ask, &def), 0, "no driver: no");
    CHECK(!ask, "no driver: not asked");

    /* The answers. */
    CHECK_EQ(inst_tb_answer("", 1), 1, "Enter: the default yes");
    CHECK_EQ(inst_tb_answer("", 0), 0, "Enter: the default no");
    CHECK_EQ(inst_tb_answer(" y", 0), 1, "y");
    CHECK_EQ(inst_tb_answer("YES", 0), 1, "YES");
    CHECK_EQ(inst_tb_answer("n", 1), 0, "n");
    CHECK_EQ(inst_tb_answer("No", 1), 0, "No");
    CHECK_EQ(inst_tb_answer("maybe", 1), -1, "not an answer");
    CHECK_EQ(inst_tb_answer("yx", 1), -1, "not an answer either");

    /* Output redirected while the keyboard answers: the text is also on
     * the screen (standard error), so no question waits unseen. */
    CHECK_EQ(inst_out_mirror(0, 1, 1), 1, "mirror: > file, keyboard answers");
    CHECK_EQ(inst_out_mirror(1, 1, 1), 0, "mirror: not redirected");
    CHECK_EQ(inst_out_mirror(0, 0, 1), 0, "mirror: standard error redirected too");
    CHECK_EQ(inst_out_mirror(0, 1, 0), 0, "mirror: answers from a file");
    CHECK_EQ(inst_out_mirror(1, 1, 0), 0, "mirror: answers from a file, screen");
    CHECK_EQ(inst_out_mirror(0, 0, 0), 0, "mirror: nothing on the console");

    /* The switch. */
    argv1[0] = "x";
    argv1[1] = "/thunderbolt";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 && o.Thunderbolt,
          "/thunderbolt");
    argv1[2] = "/yes";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 0 && o.Yes,
          "/thunderbolt /yes");
    argv1[2] = "/list";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 0,
          "/thunderbolt /list");
    argv1[2] = "/uninstall";
    CHECK_EQ(inst_parse_args(3, argv1, &o, err, sizeof(err)), 1,
             "/thunderbolt /uninstall refused");
    argv1[2] = "/devices";
    CHECK_EQ(inst_parse_args(3, argv1, &o, err, sizeof(err)), 1,
             "/thunderbolt /devices refused");
}

/* The longest line of a text, for the 79-column rule. */
static size_t longest_line(const char *s)
{
    size_t best = 0;
    size_t n = 0;

    for (; *s != '\0'; s++) {
        if (*s == '\n') {
            n = 0;
            continue;
        }
        n++;
        if (n > best) {
            best = n;
        }
    }
    return best;
}

/* NT: a unit not marked removable (whatever the cause: roadmap EJRB.1). */
static void test_tb_fallback(void)
{
    const char *text;
    int withName;

    /* Not marked: capabilities read, neither Removable nor
     * SurpriseRemovalOK. */
    CHECK(inst_unit_not_marked(1, 0x0), "neither bit: not marked");
    CHECK(inst_unit_not_marked(1, 0x2), "other bits do not count");
    CHECK(!inst_unit_not_marked(1, 0x4), "Removable: marked");
    CHECK(!inst_unit_not_marked(1, 0x80), "SurpriseRemovalOK: not this case");
    CHECK(!inst_unit_not_marked(1, 0x84), "both: not this case");
    CHECK(!inst_unit_not_marked(0, 0x0), "capabilities not known");

    /* XP answers a vetoed eject CR_SUCCESS with the veto type set. */
    CHECK_EQ(inst_eject_cr(0, 0), 0, "a success with no veto stays one");
    CHECK_EQ(inst_eject_cr(0, 5), INST_CR_REMOVE_VETOED,
             "a success with a veto type is a veto");
    CHECK_EQ(inst_eject_cr(0, 8), INST_CR_REMOVE_VETOED,
             "an illegal request answered as success is a veto");
    CHECK_EQ(inst_eject_cr(INST_CR_ACCESS_DENIED, 12), INST_CR_ACCESS_DENIED,
             "another answer is kept");
    CHECK_EQ(inst_eject_cr(INST_CR_REMOVE_VETOED, 5), INST_CR_REMOVE_VETOED,
             "a veto stays a veto");
    CHECK_EQ(inst_eject_nt(inst_eject_cr(0, 5), 5, &text, &withName),
             INST_EJ_VETO, "XP's open-handle answer: refused, exit 4");
    CHECK(strstr(text, "program") != NULL, "XP's open-handle answer: says a program");
    CHECK_EQ(inst_eject_code(inst_eject_nt(inst_eject_cr(0, 6), 6, &text,
                                           &withName)),
             INST_EXIT_STEPS, "XP's device veto: exit 4");
    CHECK(withName, "XP's device veto: names the device");

    /* The step after the eject request. */
    CHECK_EQ(inst_eject_nt_step(INST_CR_REMOVE_VETOED, 8, 1, 1),
             INST_NT_FALLBACK,
             "an illegal request on a unit not marked: query and remove");
    CHECK_EQ(inst_eject_nt_step(inst_eject_cr(0, 8), 8, 1, 1), INST_NT_FALLBACK,
             "XP's success-with-veto illegal request: query and remove");
    CHECK_EQ(inst_eject_nt_step(INST_CR_REMOVE_VETOED, 8, 1, 0), INST_NT_ADMIN,
             "an unelevated caller on Vista and 7: the administrator refusal");
    CHECK_EQ(inst_eject_nt_step(INST_CR_ACCESS_DENIED, 0, 1, 1), INST_NT_ADMIN,
             "access denied on a unit not marked: the administrator refusal");
    CHECK_EQ(inst_eject_nt_step(INST_CR_REMOVE_VETOED, 12, 1, 1), INST_NT_ADMIN,
             "insufficient rights on a unit not marked: the administrator refusal");
    CHECK_EQ(inst_eject_nt_step(INST_CR_REMOVE_VETOED, 8, 0, 1), INST_NT_ANSWER,
             "a marked unit: the answer stands");
    CHECK_EQ(inst_eject_nt_step(INST_CR_ACCESS_DENIED, 0, 0, 1), INST_NT_ANSWER,
             "a marked unit's access refusal: the rights text");
    CHECK_EQ(inst_eject_nt_step(INST_CR_REMOVE_VETOED, 5, 1, 1), INST_NT_ANSWER,
             "never past an open handle");
    CHECK_EQ(inst_eject_nt_step(INST_CR_REMOVE_VETOED, 6, 1, 1), INST_NT_ANSWER,
             "never past a device veto");
    CHECK_EQ(inst_eject_nt_step(0, 0, 1, 1), INST_NT_ANSWER,
             "an accepted eject is done");
    CHECK_EQ(inst_eject_nt_step(INST_CR_NO_SUCH_DEVNODE, 0, 1, 1),
             INST_NT_ANSWER, "the list changed");

    /* The fallback's answers. */
    CHECK_EQ(inst_eject_nt_removal(0, 0, &text, &withName), INST_EJ_DONE,
             "removed");
    CHECK_EQ(inst_eject_nt_removal(INST_CR_REMOVE_VETOED, 5, &text, &withName),
             INST_EJ_VETO, "a file open: refused");
    CHECK(strstr(text, "program") != NULL && !withName,
          "a file open: says a program");
    CHECK_EQ(inst_eject_code(inst_eject_nt_removal(inst_eject_cr(0, 7), 7,
                                                   &text, &withName)),
             INST_EXIT_STEPS, "a driver veto answered as success: exit 4");
    CHECK(withName, "a driver veto: names it");
    CHECK_EQ(inst_eject_nt_removal(INST_CR_ACCESS_DENIED, 0, &text, &withName),
             INST_EJ_RIGHTS, "access denied: the administrator refusal");
    CHECK(text == inst_admin_text() && !withName, "the administrator text");
    CHECK_EQ(inst_eject_code(INST_EJ_RIGHTS), INST_EXIT_REFUSED,
             "the administrator refusal is exit 1");
    CHECK_EQ(inst_eject_nt_removal(INST_CR_REMOVE_VETOED, 12, &text, &withName),
             INST_EJ_RIGHTS, "insufficient rights: the administrator refusal");
    CHECK(text == inst_admin_text(), "insufficient rights: the administrator text");
    CHECK_EQ(inst_eject_nt_removal(INST_CR_CALL_NOT_IMPLEMENTED, 0, &text,
                                   &withName),
             INST_EJ_VETO, "not implemented: a step, exit 4");
    CHECK(text == inst_not_removable_text(), "not implemented: its text");
    CHECK_EQ(inst_eject_nt_removal(INST_CR_NO_SUCH_DEVNODE, 0, &text, &withName),
             INST_EJ_CHANGED, "the list changed");
    CHECK_EQ(inst_eject_nt_removal(0x1F, 0, &text, &withName), INST_EJ_FAILED,
             "an unknown answer: the state is not known, exit 2");

    /* The flags: UI_NOT_OK | NO_RESTART, then UI_NOT_OK alone after
     * CR_INVALID_FLAG, once. */
    CHECK(inst_removal_retry(INST_CR_INVALID_FLAG,
                             INST_REMOVE_UI_NOT_OK | INST_REMOVE_NO_RESTART),
          "an invalid flag: asked again without NO_RESTART");
    CHECK(!inst_removal_retry(INST_CR_INVALID_FLAG, INST_REMOVE_UI_NOT_OK),
          "never a second time");
    CHECK(!inst_removal_retry(0, INST_REMOVE_UI_NOT_OK | INST_REMOVE_NO_RESTART),
          "not after a success");
    CHECK(!inst_removal_retry(INST_CR_REMOVE_VETOED,
                              INST_REMOVE_UI_NOT_OK | INST_REMOVE_NO_RESTART),
          "never after a veto");

    /* The texts: the owner's words, at most 79 columns. */
    CHECK(strcmp(inst_admin_text(), "Windows does not list this device for "
                 "safe removal. Run this as an\nadministrator to stop it.") == 0,
          "the administrator text");
    CHECK(strstr(inst_admin_text(), "plug") == NULL,
          "the administrator text promises nothing of a re-plug");
    CHECK(longest_line(inst_admin_text()) <= 79, "the administrator text fits 79");
    CHECK(longest_line(inst_not_removable_text()) <= 79,
          "the not-implemented text fits 79");
}

static void test_tb_eject(void)
{
    const char *text;
    int withName;
    char *argv1[6];
    INST_OPTS o;
    char err[256];

    /* 98 SE and ME: a refused query is never followed by a removal; an
     * accepted one always is. */
    CHECK_EQ(inst_eject9x_query(0, &text), INST_EJ_DONE, "accepted: remove next");
    CHECK_EQ(inst_eject9x_query(INST_CR_REMOVE_VETOED, &text), INST_EJ_VETO,
             "vetoed");
    CHECK(strstr(text, "in use") != NULL && strstr(text, "program is using") == NULL,
          "the 9x veto names no cause");
    CHECK_EQ(inst_eject9x_query(INST_CR_NO_SUCH_DEVNODE, &text), INST_EJ_CHANGED,
             "gone");
    CHECK_EQ(inst_eject9x_query(INST_CR_CALL_NOT_IMPLEMENTED, &text),
             INST_EJ_UNSUPPORTED, "not offered");
    CHECK_EQ(inst_eject9x_query(0x1F, &text), INST_EJ_VETO, "anything else");
    CHECK_EQ(inst_eject9x_remove(0, &text), INST_EJ_DONE, "removed");
    CHECK_EQ(inst_eject9x_remove(0x1F, &text), INST_EJ_FAILED,
             "a removal failing after an accepted query");
    CHECK(strstr(text, "Shut down Windows") != NULL, "says shut down first");

    /* NT. */
    CHECK_EQ(inst_eject_nt(0, 5, &text, &withName), INST_EJ_DONE,
             "the veto type is read only with CR_REMOVE_VETOED");
    CHECK_EQ(inst_eject_nt(INST_CR_REMOVE_VETOED, 5, &text, &withName),
             INST_EJ_VETO, "an open handle");
    CHECK(!withName && strstr(text, "program") != NULL, "says a program");
    CHECK_EQ(inst_eject_nt(INST_CR_REMOVE_VETOED, 7, &text, &withName),
             INST_EJ_VETO, "a driver");
    CHECK(withName, "names the driver");
    CHECK_EQ(inst_eject_nt(INST_CR_REMOVE_VETOED, 12, &text, &withName),
             INST_EJ_RIGHTS, "insufficient rights: exit 1, as access denied");
    CHECK(strstr(text, "administrator") != NULL, "says administrator");
    CHECK_EQ(inst_eject_nt(INST_CR_ACCESS_DENIED, 0, &text, &withName),
             INST_EJ_RIGHTS, "access denied");
    CHECK_EQ(inst_eject_nt(INST_CR_INVALID_DEVNODE, 0, &text, &withName),
             INST_EJ_CHANGED, "the list changed");
    CHECK_EQ(inst_eject_nt(0x1F, 0, &text, &withName), INST_EJ_FAILED,
             "unknown state");
    CHECK(strstr(text, "shut down") != NULL, "an unknown state: shut down first");
    test_tb_fallback();
    CHECK(strstr(inst_unplug_text(INST_UNPLUG_RESTARTED), "not safe") != NULL,
          "a unit that started again is not safe to unplug");
    CHECK_EQ(inst_eject_code(INST_EJ_DONE), INST_EXIT_DONE, "done 0");
    CHECK_EQ(inst_eject_code(INST_EJ_VETO), INST_EXIT_STEPS, "veto 4");
    CHECK_EQ(inst_eject_code(INST_EJ_CHANGED), INST_EXIT_REFUSED, "changed 1");
    CHECK_EQ(inst_eject_code(INST_EJ_RIGHTS), INST_EXIT_REFUSED, "rights 1");
    CHECK_EQ(inst_eject_code(INST_EJ_UNSUPPORTED), INST_EXIT_REFUSED, "unsupported 1");
    CHECK_EQ(inst_eject_code(INST_EJ_FAILED), INST_EXIT_FAILED, "uncertain 2");

    /* The advice after an eject. */
    CHECK_EQ(inst_unplug_kind(1, 0, 1, CM_ST_TUN_NONE), INST_UNPLUG_NOW,
             "an enclosure alone, no USB3 tunnel: unplug now");
    CHECK_EQ(inst_unplug_kind(1, 1, 1, CM_ST_TUN_NONE), INST_UNPLUG_OTHERS,
             "another unit left");
    CHECK_EQ(inst_unplug_kind(1, 0, 0, CM_ST_TUN_NONE), INST_UNPLUG_CHECK,
             "a dock-shaped unit");
    CHECK_EQ(inst_unplug_kind(1, 0, 1, CM_ST_TUN_UNKNOWN), INST_UNPLUG_CHECK,
             "USB3 not known");
    CHECK_EQ(inst_unplug_kind(1, 0, 1, CM_ST_TUN_UP), INST_UNPLUG_CHECK,
             "USB3 up");
    CHECK_EQ(inst_unplug_kind(0, 0, 1, CM_ST_TUN_NONE), INST_UNPLUG_CHECK,
             "the walk after it incomplete");
    CHECK(inst_eq(inst_unplug_text(INST_UNPLUG_NOW),
                  "You can now unplug the device."), "the owner's words");

    /* The switches. */
    argv1[0] = "x";
    argv1[1] = "/devices";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 && o.Devices,
          "/devices");
    argv1[1] = "/eject";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 && o.Eject &&
              o.EjectNum == 0, "/eject alone");
    argv1[2] = "2";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 0 &&
              o.EjectNum == 2, "/eject 2");
    argv1[3] = "/yes";
    CHECK_EQ(inst_parse_args(4, argv1, &o, err, sizeof(err)), 1,
             "/eject 2 /yes refused");
    argv1[2] = "id=PCI\\VEN_1B36&DEV_0010\\0008083000";
    CHECK(inst_parse_args(4, argv1, &o, err, sizeof(err)) == 0 && o.Yes &&
              inst_eq(o.EjectId, "PCI\\VEN_1B36&DEV_0010\\0008083000"),
          "/eject id=... /yes");
    argv1[2] = "id";
    argv1[3] = "PCI\\X\\00";
    argv1[4] = "/yes";
    CHECK(inst_parse_args(5, argv1, &o, err, sizeof(err)) == 0 &&
              inst_eq(o.EjectId, "PCI\\X\\00"), "COMMAND.COM's split id= joined");
    argv1[2] = "0";
    CHECK_EQ(inst_parse_args(3, argv1, &o, err, sizeof(err)), 1, "number 0");
    argv1[2] = "x";
    CHECK_EQ(inst_parse_args(3, argv1, &o, err, sizeof(err)), 1, "a word");
    argv1[1] = "/devices";
    argv1[2] = "/list";
    CHECK_EQ(inst_parse_args(3, argv1, &o, err, sizeof(err)), 1,
             "/devices /list refused");
    argv1[2] = "/eject";
    CHECK_EQ(inst_parse_args(3, argv1, &o, err, sizeof(err)), 1,
             "/devices /eject refused");
    argv1[2] = "/dir";
    argv1[3] = "release-x86";
    CHECK(inst_parse_args(4, argv1, &o, err, sizeof(err)) == 0,
          "INSTALL.BAT's /dir is accepted");
    argv1[1] = "/eject";
    argv1[2] = "/uninstall";
    CHECK_EQ(inst_parse_args(3, argv1, &o, err, sizeof(err)), 1,
             "/eject /uninstall refused");
}

/* The USB tree tool (record 23 section 9.5, record 25 section 8): its name
 * per architecture, the Run value's data, when it is copied, the leftover
 * .BAK, what becomes of the Run value, and /notray. */
static void test_tray(void)
{
    static const INST_U32 v300[4] = { 3, 0, 0, 0 };
    static const INST_U32 v301[4] = { 3, 0, 0, 1 };
    char buf[512];
    char small[8];
    INST_OPTS o;
    const char *a1[] = { "x", "/notray", "/yes" };
    const char *a2[] = { "x", "/notray", "/uninstall" };
    const char *a3[] = { "x", "/notray", "/devices" };
    const char *a4[] = { "x", "/eject", "/notray" };
    const char *a5[] = { "x", "/list", "/notray" };

    CHECK(inst_eq(inst_tray_file(INST_ARCH_X86), "XHCITR.EXE"), "x86 name");
    CHECK(inst_eq(inst_tray_file(INST_ARCH_AMD64), "XHCITR64.EXE"), "x64 name");
    CHECK(strcmp(INST_TRAY_CLASS, "Xhci98TrTray") == 0, "the window class");
    CHECK(strcmp(INST_TRAY_RUN_VALUE, "xHCI98 USB Tree") == 0, "the Run value");
    CHECK_EQ(INST_TRAY_CLOSE_MS, 15000UL, "15 s to close");

    CHECK_EQ(inst_tray_run_data("C:\\WINDOWS\\XHCI98\\XHCITR.EXE", buf,
                                sizeof(buf)), 1, "Run data");
    CHECK(strcmp(buf, "\"C:\\WINDOWS\\XHCI98\\XHCITR.EXE\" /tray") == 0,
          "Run data text");
    CHECK_EQ(inst_tray_run_data("C:\\A.EXE", small, sizeof(small)), 0,
             "Run data too long");
    CHECK_EQ(inst_tray_run_data("C:\\A\"B.EXE", buf, sizeof(buf)), 0,
             "a quote in the path");
    CHECK_EQ(inst_tray_run_data("", buf, sizeof(buf)), 0, "an empty path");

    CHECK_EQ(inst_tray_copy_needed(0, NULL, 1, v300), 1, "absent: copied");
    CHECK_EQ(inst_tray_copy_needed(0, NULL, -1, NULL), 1,
             "absent, package unread: copied");
    CHECK_EQ(inst_tray_copy_needed(1, v300, 1, v301), 1, "older: copied");
    CHECK_EQ(inst_tray_copy_needed(1, v300, 1, v300), 0, "equal: left");
    CHECK_EQ(inst_tray_copy_needed(1, v301, 1, v300), 0, "newer: left");
    CHECK_EQ(inst_tray_copy_needed(-1, NULL, 1, v300), 1,
             "installed unreadable: copied");
    CHECK_EQ(inst_tray_copy_needed(1, v300, -1, NULL), 0,
             "package unreadable: left");
    CHECK_EQ(inst_tray_copy_needed(-1, NULL, -1, NULL), 0,
             "both unreadable: left");

    CHECK_EQ(inst_tray_leftover(0, 0), INST_TRAY_BAK_NONE, "no .BAK, no tool");
    CHECK_EQ(inst_tray_leftover(0, 1), INST_TRAY_BAK_NONE, "no .BAK");
    CHECK_EQ(inst_tray_leftover(1, 0), INST_TRAY_BAK_RESTORE,
             ".BAK alone is the tool: renamed back");
    CHECK_EQ(inst_tray_leftover(1, 1), INST_TRAY_BAK_DELETE,
             ".BAK beside the tool: deleted");

    CHECK_EQ(inst_tray_run_action(0, 0, 1), INST_TRAY_RUN_SET, "absent: set");
    CHECK_EQ(inst_tray_run_action(0, 2, 1), INST_TRAY_RUN_SET, "other: set");
    CHECK_EQ(inst_tray_run_action(0, 1, 1), INST_TRAY_RUN_KEEP, "same: kept");
    CHECK_EQ(inst_tray_run_action(0, 0, 0), INST_TRAY_RUN_KEEP,
             "no file in place: not written");
    CHECK_EQ(inst_tray_run_action(1, 0, 1), INST_TRAY_RUN_KEEP,
             "/notray, none: nothing");
    CHECK_EQ(inst_tray_run_action(1, 1, 1), INST_TRAY_RUN_DELETE,
             "/notray: deleted");
    CHECK_EQ(inst_tray_run_action(1, 2, 0), INST_TRAY_RUN_DELETE,
             "/notray, other data: deleted");
    /* A copy left in use keeps the installed tool and its Run value. */
    CHECK_EQ(inst_tray_run_action(0, 1, 1), INST_TRAY_RUN_KEEP,
             "left in use: the Run value kept");
    CHECK_EQ(inst_exit_code(0, 0, 0, 1), INST_EXIT_STEPS,
             "left in use: steps left");

    CHECK_EQ(parse(3, a1, &o), 0, "/notray /yes");
    CHECK(o.NoTray && o.Yes, "/notray field");
    CHECK_EQ(parse(3, a2, &o), 1, "/notray /uninstall refused");
    CHECK_EQ(parse(3, a3, &o), 1, "/notray /devices refused");
    CHECK_EQ(parse(3, a4, &o), 1, "/eject /notray refused");
    CHECK_EQ(parse(3, a5, &o), 0, "/list /notray");
}

/* The console (owner, 2026-10-16): no line wider than 79 columns, a long
 * line broken at a space and hung under its text, a word longer than a
 * line broken after a backslash, the spaces at a prompt's end kept, and the
 * wrap's state across pieces fed one at a time. */
static void test_wrap(void)
{
    char out[4096];
    char text[1024];
    INST_WRAP w;
    size_t i;
    size_t n;

    inst_wrap_text("short line\nand another\n", out, sizeof(out));
    CHECK(strcmp(out, "short line\nand another\n") == 0, "short lines pass");
    /* Exactly 79 columns stays one line; 80 wraps. */
    memset(text, 'x', 79);
    text[79] = '\0';
    text[40] = ' ';
    inst_wrap_text(text, out, sizeof(out));
    CHECK(strchr(out, '\n') == NULL && strlen(out) == 79, "79 columns: kept");
    memset(text, 'x', 80);
    text[80] = '\0';
    text[40] = ' ';
    inst_wrap_text(text, out, sizeof(out));
    CHECK(inst_text_width(out) <= INST_COLUMNS, "80 columns: wrapped");
    CHECK(strncmp(strchr(out, '\n'), "\n  x", 4) == 0,
          "a plain line goes on two spaces in");
    /* A marker's text hangs under itself. */
    inst_wrap_text("  NOTE: aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj "
                   "kkkk llll mmmm nnnn oooo pppp", out, sizeof(out));
    CHECK(strstr(out, "\n        ") != NULL &&
          strstr(out, "\n         ") == NULL, "NOTE: hangs at column 8");
    inst_wrap_text("  3. aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj "
                   "kkkk llll mmmm nnnn oooo pppp", out, sizeof(out));
    CHECK(strstr(out, "\n     p") != NULL || strstr(out, "\n     o") != NULL,
          "a numbered step hangs under its text");
    inst_wrap_text("  /to path      install the xHCI98 tools in that directory "
                   "without asking at all ever", out, sizeof(out));
    CHECK(strstr(out, "\n                ") != NULL,
          "a key and two spaces: hangs under the text after it");
    /* A word longer than a line: broken after a backslash. */
    inst_wrap_text("    C:\\AAAAAAAAAAAAAAAAAAAA\\BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
                   "\\CCCCCCCCCCCCCCCCCCCCCCCCCCCC\\DDDDDDDDDDDDDDDDDDDDDD",
                   out, sizeof(out));
    CHECK(inst_text_width(out) <= INST_COLUMNS, "a long path kept to 79");
    CHECK(strstr(out, "\\\n") != NULL, "broken after a backslash");
    memset(text, 'Z', 300);
    text[300] = '\0';
    inst_wrap_text(text, out, sizeof(out));
    CHECK(inst_text_width(out) <= INST_COLUMNS, "a word of 300 kept to 79");
    for (i = 0, n = 0; out[i] != '\0'; i++) {
        n += out[i] == 'Z';
    }
    CHECK_EQ(n, 300, "nothing of it lost");
    /* A prompt's trailing space is printed at the flush, not before. */
    inst_wrap_init(&w);
    inst_wrap_feed(&w, "  Enter for [1]: ", out, sizeof(out));
    CHECK(strcmp(out, "  Enter for [1]:") == 0, "the space held back");
    inst_wrap_flush(&w, out, sizeof(out));
    CHECK(strcmp(out, " ") == 0, "and printed at the flush");
    inst_wrap_input(&w);
    CHECK_EQ(w.Col, 0, "the answer's Enter starts a new line");
    /* Pieces fed apart wrap as the whole would. */
    inst_wrap_init(&w);
    text[0] = '\0';
    for (i = 0; i < 20; i++) {
        inst_wrap_feed(&w, "word ", out, sizeof(out));
        strcat(text, out);
    }
    inst_wrap_feed(&w, "\n", out, sizeof(out));
    strcat(text, out);
    CHECK(inst_text_width(text) <= INST_COLUMNS && w.Wraps == 1,
          "fed in pieces: one wrap");
    CHECK(w.MaxCol <= INST_COLUMNS, "the widest column measured");
    CHECK_EQ(inst_text_width("ab\nabcd\r\nabc"), 4, "the width of a text");
}

/* Every line of a text within the width. */
static int fits(const char *text)
{
    return inst_text_width(text) <= INST_COLUMNS;
}

/* The questions (owner, 2026-10-16): each choice on its own line with its
 * meaning, "Enter for [x]" with x what Enter gives, "default: d (set now:
 * x)" when they differ. */
static void test_questions(void)
{
    char out[4096];
    char longName[160];
    INST_VNOW now[3];
    INST_U32 enter;
    int approve = inst_value_find("XhciCmApprove");
    int mode = inst_value_find("XhciInterruptMode");
    int clean = inst_value_find("XhciCmPreOsCleanup");
    int i;

    for (i = 0; i < instValueCount; i++) {
        int b;
        int named = 0;
        for (b = 0; b < 4; b++) {
            named += instValues[i].Choice[b] != NULL;
            if ((instValues[i].Settable & (1UL << b)) == 0) {
                CHECK(instValues[i].Choice[b] == NULL,
                      "a choice has a meaning only where the value is allowed");
            }
        }
        CHECK(named > 0 || instValues[i].Hi != 0,
              "every value names its choices, or is a range");
        CHECK(strchr(instValues[i].Meaning, '(') == NULL,
              "the meaning carries no choice list");
        CHECK(strchr(instValues[i].Meaning, '\n') == NULL &&
              (instValues[i].Caution == NULL ||
               strchr(instValues[i].Caution, '\n') == NULL),
              "no hand-made line breaks: the console wraps");
    }
    memset(now, 0, sizeof(now));
    now[0].State = 1;
    now[0].Value = 1;
    now[0].Desc = "xHCI98 USB4 Host Router";
    CHECK(inst_value_question(out, sizeof(out), approve, 0x3, 1, now, 1, 0, 0,
                              &enter) == 1 && enter == 1, "Enter keeps 1");
    CHECK(strstr(out, "XhciCmApprove - approve Thunderbolt devices "
                      "automatically\n") == out, "the name and meaning first");
    CHECK(strstr(out, "  now: 1 (xHCI98 USB4 Host Router)\n") != NULL,
          "now, with the device");
    CHECK(strstr(out, "  allowed:\n    0  no\n    1  yes\n") != NULL,
          "each choice on its own line");
    CHECK(strstr(out, "  default: 1\n") != NULL, "the default");
    CHECK(strlen(out) > 17 &&
          strcmp(out + strlen(out) - 17, "  Enter for [1]: ") == 0,
          "the prompt ends it");
    /* Set now to other than the default: Enter keeps what is set. */
    now[0].Value = 0;
    inst_value_question(out, sizeof(out), approve, 0x3, 1, now, 1, 0, 0,
                        &enter);
    CHECK(enter == 0 && strstr(out, "  default: 1 (set now: 0)\n") != NULL &&
          strstr(out, "Enter for [0]: ") != NULL, "set now: 0, Enter for [0]");
    /* Not set, and a new install: Enter gives the default. */
    now[0].State = 0;
    inst_value_question(out, sizeof(out), approve, 0x3, 1, now, 1, 0, 0,
                        &enter);
    CHECK(enter == 1 && strstr(out, "not set, the install writes 1") != NULL &&
          strstr(out, "Enter for [1]: ") != NULL, "not set: the default");
    now[0].State = -1;
    inst_value_question(out, sizeof(out), approve, 0x3, 1, now, 1, 1, 0,
                        &enter);
    CHECK(strstr(out, "new install, the default 1") != NULL, "a new install");
    CHECK(strstr(out, "NOTE: a new install starts with the default") != NULL,
          "and the approval's note");
    /* Two devices that differ: each keeps its own. */
    now[0].State = 1;
    now[0].Value = 0;
    now[1].State = 1;
    now[1].Value = 1;
    now[1].Desc = "xHCI98 USB4 Host Router #2";
    CHECK(inst_value_question(out, sizeof(out), approve, 0x3, 1, now, 2, 0, 0,
                              &enter) == 0, "two devices that differ");
    CHECK(strstr(out, "Enter for [each keeps its own]: ") != NULL &&
          strstr(out, "(set now: not the same on every device)") != NULL,
          "each keeps its own");
    /* The caution, shown where it is about an offered value. */
    now[0].State = 0;
    inst_value_question(out, sizeof(out), mode, 0x7, 0, now, 1, 0, 0, &enter);
    CHECK(strstr(out, "  NOTE: with 1, a controller") != NULL,
          "the caution names only the offered value it is about");
    inst_value_question(out, sizeof(out), mode, 0xF, 0, now, 1, 0, 0, &enter);
    CHECK(strstr(out, "  NOTE: with 1 or 3, a controller") != NULL,
          "both, where 3 is offered");
    inst_value_question(out, sizeof(out), mode, 0x5, 0, now, 1, 0, 0, &enter);
    CHECK(strstr(out, "NOTE") == NULL, "no caution without 1 or 3");
    inst_caution_text(mode, 1UL << 3, longName, sizeof(longName));
    CHECK(strncmp(longName, "with 3, ", 8) == 0, "/set 3: the caution of 3");
    inst_caution_text(inst_value_find("XhciForceBulkOnly"), 0x3, longName,
                      sizeof(longName));
    CHECK(strncmp(longName, "read as each", 12) == 0, "a plain caution as is");
    CHECK(strstr(out, "    2  polling always\n") != NULL &&
          strstr(out, "    3  ") == NULL, "3 not offered: not listed");
    inst_value_question(out, sizeof(out), clean, 0x7, 0, now, 1, 0, 1, &enter);
    CHECK(strstr(out, "    2  all cleaned\n") != NULL, "a three-way choice");
    CHECK(strstr(out, "  /list: not asked; Enter would give [0].\n") != NULL,
          "/list says what Enter would give");
    /* Long names stay within the width, on their own line. */
    memset(longName, 'N', sizeof(longName) - 1);
    longName[sizeof(longName) - 1] = '\0';
    memcpy(longName, "xHCI98 USB 3.x eXtensible Host Controller ", 42);
    now[0].Desc = longName;
    now[0].State = 1;
    now[0].Value = 0;
    for (i = 0; i < instValueCount; i++) {
        inst_value_question(out, sizeof(out), i, instValues[i].Settable, 0,
                            now, 1, 1, 0, &enter);
        CHECK(fits(out), "a value's question fits 79 columns");
    }
    CHECK(strstr(out, "  now: 0\n    (xHCI98") != NULL,
          "a long name on its own line");
    /* Sixteen devices with names of 127 characters: the prompt still ends
     * the question. */
    {
        static char big[16384];
        INST_VNOW many[16];
        char name127[128];
        memset(name127, 'N', 127);
        name127[127] = '\0';
        for (i = 0; i < 16; i++) {
            many[i].State = 1;
            many[i].Value = 0;
            many[i].Desc = name127;
        }
        inst_value_question(big, sizeof(big), approve, 0x3, 1, many, 16, 1, 0,
                            &enter);
        CHECK(strlen(big) > 17 &&
              strcmp(big + strlen(big) - 17, "  Enter for [0]: ") == 0 &&
              fits(big), "sixteen long names: the prompt is still there");
    }
    /* Yes or no. */
    inst_yesno_question(out, sizeof(out), "Install Thunderbolt 3 / USB4 "
                        "support?", "install it", "leave it out", 0);
    CHECK(strcmp(out, "Install Thunderbolt 3 / USB4 support?\n  Y  install it"
                      "\n  N  leave it out\n  Enter for [N]: ") == 0,
          "a yes-or-no question");
    inst_yesno_question(out, sizeof(out), "Open Device Manager now?",
                        "open it", "not now", 1);
    CHECK(strstr(out, "Enter for [Y]: ") != NULL, "Enter for [Y]");
    /* The install directory's question. */
    inst_dir_question(out, sizeof(out), "XHCITR.EXE", NULL, NULL, NULL,
                      "C:\\XHCI98", 1, 0);
    CHECK(strstr(out, "Install directory for the xHCI98 tools\n") == out &&
          strstr(out, "  XHCITR.EXE  the xHCI98 USB Tree") != NULL &&
          strstr(out, "8 characters") == NULL &&
          strcmp(out + strlen(out) - 25, "  Enter for [C:\\XHCI98]: ") == 0,
          "NT: the tool, then Enter for [C:\\XHCI98]");
    inst_dir_question(out, sizeof(out), "XHCITR.EXE", "XHCIWIZ.EXE",
                      "*.INF, *.SYS",
                      "C:\\WINDOWS\\XHCI98", "C:\\WINDOWS\\XHCI98", 0, 0);
    CHECK(strstr(out, "  *.INF, *.SYS  this package's drivers, for the hardware "
                      "wizards\n") != NULL &&
          strstr(out, "  now: C:\\WINDOWS\\XHCI98\n") != NULL &&
          strstr(out, "  XHCIWIZ.EXE   this installer, which Add/Remove "
                      "Programs runs to remove it\n") != NULL &&
          strstr(out, "no spaces") != NULL &&
          strstr(out, "Enter for [C:\\WINDOWS\\XHCI98]: ") != NULL,
          "98: the copy, where it is now, the 8.3 rule");
    CHECK(fits(out), "the directory question fits");
    inst_dir_question(out, sizeof(out), "XHCITR64.EXE", NULL, NULL, NULL,
                      "D:\\XHCI98", 1, 1);
    CHECK(strstr(out, "/list: not asked; Enter would give [D:\\XHCI98].\n")
          != NULL, "the directory under /list");
}

/* The install directory as text (record 23 section 9.6). */
static void test_dir_check(void)
{
    char out[260];
    const char *win = "C:\\WINDOWS";
    const char *pkg = "D:\\Downloads\\xhci98-3.0.0.0\\";

    CHECK_EQ(inst_dir_check("C:\\XHCI98", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_OK, "C:\\XHCI98");
    CHECK(strcmp(out, "C:\\XHCI98") == 0, "kept as given");
    CHECK_EQ(inst_dir_check(" \"d:/Tools/xhci98/\" ", 1, win, pkg, NULL, out,
                            sizeof(out)), INST_DIR_OK, "quotes, slashes");
    CHECK(strcmp(out, "D:\\Tools\\xhci98") == 0, "normalised");
    CHECK_EQ(inst_dir_check("C:\\Program Files\\xHCI98", 1, win, pkg, NULL, out,
                            sizeof(out)), INST_DIR_OK, "NT takes spaces");
    CHECK_EQ(inst_dir_check("C:\\Program Files\\xHCI98", 0, win, pkg, NULL, out,
                            sizeof(out)), INST_DIR_NAME83, "98 does not");
    CHECK_EQ(inst_dir_check("C:\\LONGFOLDERNAME", 0, win, pkg, NULL, out,
                            sizeof(out)), INST_DIR_NAME83, "nine characters");
    CHECK_EQ(inst_dir_check("C:\\TOOLS.ABC\\X98", 0, win, pkg, NULL, out,
                            sizeof(out)), INST_DIR_OK, "an 8.3 extension");
    CHECK_EQ(inst_dir_check("C:\\TOOLS.ABCD", 0, win, pkg, NULL, out,
                            sizeof(out)), INST_DIR_NAME83, "a long extension");
    CHECK_EQ(inst_dir_check("XHCI98", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_FORM, "relative");
    CHECK_EQ(inst_dir_check("\\\\server\\share\\x", 1, win, pkg, NULL, out,
                            sizeof(out)), INST_DIR_FORM, "a network path");
    CHECK_EQ(inst_dir_check("C:XHCI98", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_FORM, "a drive-relative path");
    CHECK_EQ(inst_dir_check("C:\\", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_ROOT, "a drive's root");
    CHECK_EQ(inst_dir_check("C:", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_ROOT, "a bare drive");
    CHECK_EQ(inst_dir_check("C:\\a;b", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_CHAR, "a semicolon (DevicePath's separator)");
    CHECK_EQ(inst_dir_check("C:\\a?b", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_CHAR, "a wildcard");
    CHECK_EQ(inst_dir_check("C:\\a:b", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_CHAR, "a second colon");
    CHECK_EQ(inst_dir_check("C:\\a\\..\\b", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_PART, "..");
    CHECK_EQ(inst_dir_check("C:\\a.\\b", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_PART, "a trailing dot");
    CHECK_EQ(inst_dir_check("C:\\a\\\\b", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_FORM, "an empty part");
    CHECK_EQ(inst_dir_check("C:\\WINDOWS\\XHCI98", 1, win, pkg, NULL, out,
                            sizeof(out)), INST_DIR_WINDOWS, "in Windows");
    CHECK_EQ(inst_dir_check("c:\\windows", 1, win, pkg, NULL, out, sizeof(out)),
             INST_DIR_WINDOWS, "Windows itself, any case");
    CHECK_EQ(inst_dir_check("C:\\WINDOWS2\\X", 1, win, pkg, NULL, out,
                            sizeof(out)), INST_DIR_OK, "a sibling is not inside");
    CHECK_EQ(inst_dir_check("C:\\WINDOWS\\XHCI98", 0, win, pkg,
                            "C:\\WINDOWS\\XHCI98", out, sizeof(out)),
             INST_DIR_OK, "where an earlier build put it: kept");
    CHECK_EQ(inst_dir_check("D:\\Downloads\\xhci98-3.0.0.0\\tools", 1, win, pkg,
                            NULL, out, sizeof(out)), INST_DIR_PACKAGE,
             "inside the package");
    CHECK_EQ(inst_dir_check("D:\\Downloads\\xhci98-3.0.0.0", 1, win, pkg, NULL,
                            out, sizeof(out)), INST_DIR_PACKAGE, "the package");
    {
        char longPath[200];
        memset(longPath, 'a', sizeof(longPath) - 1);
        longPath[sizeof(longPath) - 1] = '\0';
        memcpy(longPath, "C:\\", 3);
        CHECK_EQ(inst_dir_check(longPath, 1, win, pkg, NULL, out, sizeof(out)),
                 INST_DIR_LONG, "too long");
    }
    CHECK(strstr(inst_dir_reason(INST_DIR_NAME83), "no spaces") != NULL,
          "the 8.3 reason");
    CHECK(inst_path_within("C:\\A\\B", "c:\\a") && inst_path_within("C:\\A",
                                                                    "C:\\A\\") &&
          !inst_path_within("C:\\AB", "C:\\A"), "within");
    inst_dir_default("D:", "C:\\WINDOWS", out, sizeof(out));
    CHECK(strcmp(out, "D:\\XHCI98") == 0, "the system drive's root");
    inst_dir_default("", "E:\\WIN98", out, sizeof(out));
    CHECK(strcmp(out, "E:\\XHCI98") == 0, "98: the Windows directory's drive");
    inst_dir_default(NULL, NULL, out, sizeof(out));
    CHECK(strcmp(out, "C:\\XHCI98") == 0, "else C:");
    /* The local copies' names, and DevicePath's entries of them. */
    CHECK(inst_copy_name("1A2B3C4D") && inst_copy_name("1a2b3c4d.NEW") &&
          !inst_copy_name("1A2B3C4") && !inst_copy_name("TOOLS") &&
          !inst_copy_name("1A2B3C4G"), "a copy's name");
    CHECK_EQ(inst_path_remove_copies("C:\\WINDOWS\\INF;C:\\XHCI98\\1A2B3C4D;"
                                     "C:\\XHCI98\\MINE;C:\\XHCI98X\\1A2B3C4D",
                                     "C:\\XHCI98", out, sizeof(out)), 1,
             "a copy removed");
    CHECK(strcmp(out, "C:\\WINDOWS\\INF;C:\\XHCI98\\MINE;C:\\XHCI98X\\1A2B3C4D")
          == 0, "only the copy");
    CHECK_EQ(inst_path_remove_copies("C:\\WINDOWS\\INF", "C:\\XHCI98", out,
                                     sizeof(out)), 0, "none there");
}

/* /to (record 23 section 9.6). */
static void test_to_switch(void)
{
    INST_OPTS o;
    char err[128];
    char *a[4];

    a[0] = "x";
    a[1] = "/to";
    a[2] = "D:\\Tools\\xhci98";
    CHECK_EQ(inst_parse_args(3, a, &o, err, sizeof(err)), 0, "/to path");
    CHECK(strcmp(o.To, "D:\\Tools\\xhci98") == 0 && o.Dir[0] == '\0',
          "/to is not /dir");
    CHECK_EQ(inst_parse_args(2, a, &o, err, sizeof(err)), 1, "/to needs a path");
    a[3] = "/uninstall";
    CHECK_EQ(inst_parse_args(4, a, &o, err, sizeof(err)), 1,
             "/to with /uninstall refused");
    a[3] = "/list";
    CHECK_EQ(inst_parse_args(4, a, &o, err, sizeof(err)), 0, "/to with /list");
    a[3] = "/devices";
    CHECK_EQ(inst_parse_args(4, a, &o, err, sizeof(err)), 1,
             "/to with /devices refused");
}

/* ---- record 26 (task 42-A.2): the helper's interface, record 27 section
 * 4 and 5.4 ---------------------------------------------------------------- */

static const char *diFile(const char *body)
{
    static char buf[2048];

    inst_copy(buf, body, sizeof(buf));
    return buf;
}

static void test_di_helper(void)
{
    char tail[512];
    char longId[260];
    INST_DI_RESULT r;
    const char *id = "PCI\\VEN_8086&DEV_15EC&SUBSYS_00000000&REV_00\\"
                     "BUS_00&DEV_0D&FUNC_00";
    const char *inf = "C:\\XHCI98\\XHCICM.INF";
    const char *res = "C:\\XHCI98\\XHCIDI.RES";

    /* The command line (record 27 section 4.1): "<version> <job file>". */
    CHECK(inst_di_cmdline(tail, sizeof(tail), "3.1.0.0",
                          "C:\\XHCI98\\DIJOB.TXT") &&
          strcmp(tail, "3.1.0.0 C:\\XHCI98\\DIJOB.TXT") == 0,
          "the command line: the version and the job file");
    CHECK(!inst_di_cmdline(tail, sizeof(tail), "3.1.0.0",
                           "C:\\Program Files\\DIJOB.TXT"),
          "a job file with a space is not passed");
    CHECK(!inst_di_cmdline(tail, sizeof(tail), "3.1.0.0 x",
                           "C:\\XHCI98\\DIJOB.TXT"),
          "a version with a space is not passed");
    CHECK(!inst_di_cmdline(tail, sizeof(tail),
                           "12345678901234567890123456789012",
                           "C:\\XHCI98\\DIJOB.TXT"),
          "a version over 31 characters is not passed");
    memset(longId, 0, sizeof(longId));
    strcpy(longId, "C:");
    while (strlen(longId) < 76) {
        strcat(longId, "\\ABCDEFGH");
    }
    strcat(longId, "\\DIJOB.TXT");
    CHECK(strlen(longId) > INST_DI_JOB_PATH_CHARS &&
          !inst_di_cmdline(tail, sizeof(tail), "3.1.0.0", longId),
          "a job file over 80 characters is not passed");
    CHECK(strlen("C:\\ABCDEFGH.IJK\\ABCDEFGH.IJK\\ABCDEFGH.IJK\\"
                 "ABCDEFGH.IJK\\ABCDEFGH.IJK\\ABCDEFGH.TXT") == 80 &&
          inst_di_cmdline(tail, sizeof(tail),
                          "1234567890123456789012345678901",
                          "C:\\ABCDEFGH.IJK\\ABCDEFGH.IJK\\ABCDEFGH.IJK\\"
                          "ABCDEFGH.IJK\\ABCDEFGH.IJK\\ABCDEFGH.TXT") &&
          strlen(tail) == 112 && strlen(tail) <= INST_DI_TAIL_CHARS,
          "a 31-character version and an 80-character job file: 112");
    CHECK(strlen("C:\\ABCDEFGH.IJK\\ABCDEFGH.IJK\\ABCDEFGH.IJK\\"
                 "ABCDEFGH.IJK\\ABCDEFGH.IJK\\ABCDE\\ABC.TXT") == 81 &&
          !inst_di_cmdline(tail, sizeof(tail), "3.1.0.0",
                           "C:\\ABCDEFGH.IJK\\ABCDEFGH.IJK\\ABCDEFGH.IJK\\"
                           "ABCDEFGH.IJK\\ABCDEFGH.IJK\\ABCDE\\ABC.TXT"),
          "an 81-character 8.3 job file is not passed");

    /* The job file's path. */
    CHECK(inst_di_jobpath(tail, sizeof(tail), "C:\\XHCI98") &&
          strcmp(tail, "C:\\XHCI98\\DIJOB.TXT") == 0,
          "the job file beside the installer");
    CHECK(inst_di_jobpath(tail, sizeof(tail), "C:\\XHCI98\\") &&
          strcmp(tail, "C:\\XHCI98\\DIJOB.TXT") == 0,
          "a trailing backslash is not doubled");
    CHECK(inst_di_jobpath(tail, sizeof(tail), "D:\\") &&
          strcmp(tail, "D:\\DIJOB.TXT") == 0, "a drive's root");
    longId[strlen(longId) - 10] = '\0';
    CHECK(inst_di_jobpath(tail, sizeof(tail), longId) &&
          strcmp(tail, "C:\\XHCIDIJ.TXT") == 0,
          "too long: the root of the install drive");
    CHECK(inst_di_jobpath(tail, sizeof(tail), "E:\\Program Files\\xhci98") &&
          strcmp(tail, "E:\\XHCIDIJ.TXT") == 0,
          "not 8.3: the root of the install drive");
    CHECK(!inst_di_jobpath(tail, sizeof(tail), NULL) &&
          !inst_di_jobpath(tail, sizeof(tail), "C:"),
          "no directory, no job file");

    /* The job file (record 27 section 4.1, format 1). */
    CHECK(inst_di_jobfile(tail, sizeof(tail), INST_DI_PROBE, res, 1234UL,
                          NULL, NULL, 0, 0) &&
          strcmp(tail, "XHCIDI-JOB 1\r\nJOB probe\r\nRESULT "
                       "C:\\XHCI98\\XHCIDI.RES\r\nHWND 1234\r\nEND\r\n") == 0,
          "the probe's job file");
    CHECK(inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL, id,
                          inf, 0, 1) &&
          strcmp(tail, "XHCIDI-JOB 1\r\nJOB install\r\nRESULT "
                       "C:\\XHCI98\\XHCIDI.RES\r\nHWND 0\r\nDEVICE "
                       "PCI\\VEN_8086&DEV_15EC&SUBSYS_00000000&REV_00\\"
                       "BUS_00&DEV_0D&FUNC_00\r\nINF C:\\XHCI98\\XHCICM.INF"
                       "\r\nFLAGS quiet\r\nEND\r\n") == 0,
          "an install's job file, quiet");
    CHECK(inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL, id,
                          inf, 1, 1) &&
          strstr(tail, "\r\nFLAGS force quiet\r\nEND\r\n") != NULL,
          "force before quiet");
    CHECK(inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL, id,
                          inf, 0, 0) &&
          strstr(tail, "FLAGS") == NULL &&
          strstr(tail, ".INF\r\nEND\r\n") != NULL,
          "no flag: no FLAGS line");
    CHECK(inst_di_jobfile(tail, sizeof(tail), INST_DI_REMOVE, res, 65535UL,
                          id, NULL, 1, 1) &&
          strstr(tail, "INF") == NULL && strstr(tail, "FLAGS") == NULL &&
          strstr(tail, "\r\nHWND 65535\r\nDEVICE PCI\\") != NULL &&
          strstr(tail, "FUNC_00\r\nEND\r\n") != NULL,
          "a removal takes no INF and no flag");
    CHECK(!inst_di_jobfile(tail, sizeof(tail), INST_DI_PROBE, res, 65536UL,
                           NULL, NULL, 0, 0),
          "a handle over 65535 is not written");
    CHECK(!inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL,
                           "C:\\Program Files\\X\\XHCIDI.RES", 0UL, id, inf,
                           0, 1), "a result path with a space is not written");
    CHECK(!inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL,
                           "C:\\XHCI98\\LONGNAME98\\XHCIDI.RES", 0UL, id, inf,
                           0, 1), "a part longer than 8.3 is not written");
    CHECK(!inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL,
                           "USB\\A B", inf, 0, 1),
          "an id with a space is not written");
    CHECK(!inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL,
                           "USB\\A\tB", inf, 0, 1),
          "an id with a tab is not written");
    CHECK(!inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL, id,
                           "C:\\XHCI98\\XHCICM.TXT", 0, 1),
          "an INF not ending .INF is not written");
    CHECK(!inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL, id,
                           NULL, 0, 1), "an install without an INF");
    CHECK(!inst_di_jobfile(tail, sizeof(tail), INST_DI_REMOVE, res, 0UL,
                           NULL, NULL, 0, 0), "a removal without a device");
    CHECK(!inst_di_jobfile(tail, sizeof(tail), 99, res, 0UL, NULL, NULL, 0,
                           0), "an unknown job");
    memset(longId, 'I', 200);
    memcpy(longId, "PCI\\", 4);
    longId[200] = '\0';
    CHECK(inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL,
                          longId, inf, 1, 1), "a 200-character id is written");
    longId[200] = 'I';
    longId[201] = '\0';
    CHECK(!inst_di_jobfile(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL,
                           longId, inf, 1, 1),
          "a 201-character id is not written");
    CHECK(!inst_di_jobfile(tail, 20, INST_DI_PROBE, res, 0UL, NULL, NULL, 0,
                           0) && tail[0] == '\0',
          "a buffer too small: nothing written");

    /* The helper's refusal of an unusable job file, and its DEFAULT line. */
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\r\nRESULT refused command "
                               "job-file\r\nEND 2\r\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_REFUSED,
          "a refused job file is a refusal");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\r\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\r\nDEFAULT "
                               "DIF_SELECTBESTCOMPATDRV lowest-rank\r\n"
                               "RESULT installed\r\nEND 4\r\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_INSTALLED,
          "a DEFAULT line is read past");

    /* The result file (record 27 section 4.2). */
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\r\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\r\nFOUND 1\r\n"
                               "CALL DiCallClassInstaller DIF_INSTALLDEVICE 0"
                               "\r\nFLAGS 00000080\r\nRESULT installed-restart"
                               "\r\nEND 6\r\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) ==
              INST_DI_V_INSTALLED_RESTART && r.HaveFlags &&
          r.Flags == 0x80UL && r.Ended, "installed-restart with its flags");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nRESULT installed\n"
                               "END 3\n"),
                        "3.1.0.0", INST_DI_INSTALL, "pci\\x\\1",
                        "c:\\xhci98\\xhci98.inf", &r) == INST_DI_V_INSTALLED,
          "installed; the id and INF compared without case");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nRESULT installed\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_NONE &&
          !r.Ended, "no END: the helper did not finish");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nRESULT installed\n"
                               "END 4\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_NONE,
          "an END count that differs");
    CHECK(inst_di_parse(diFile("XHCIDI 3.0.0.0\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nRESULT installed\n"
                               "END 3\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_NONE,
          "another version");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB install PCI\\Y\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nRESULT installed\n"
                               "END 3\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_NONE,
          "another job's device");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nRESULT splendid\n"
                               "END 3\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_NONE,
          "an unknown word");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nRESULT installed\n"
                               "END 3\nRESULT failed\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_NONE,
          "a line after END");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nCALL DiInstallDevice "
                               "1027\nERROR ERR_DI_NOFILECOPY 1027\nRESULT "
                               "failed ERR_DI_NOFILECOPY 1027\nEND 5\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_FAILED &&
          strcmp(r.Error, "ERR_DI_NOFILECOPY") == 0 && r.Number == 1027UL,
          "failed, with the error's name and number");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nRESULT refused version "
                               "3.0.0.0\nEND 2\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_REFUSED &&
          strstr(r.Detail, "version") != NULL, "refused, with its reason");
    CHECK(inst_di_parse(diFile("XHCIDI 3.0.0.0\nRESULT refused version\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_REFUSED,
          "refused whatever the version line says");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nFOUND 0\nRESULT "
                               "not-present\nEND 4\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) ==
              INST_DI_V_NOT_PRESENT, "not-present");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nRESULT cancelled\n"
                               "END 3\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_CANCELLED,
          "cancelled");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB install PCI\\X\\1 "
                               "C:\\XHCI98\\XHCI98.INF\nRESULT no-match\n"
                               "END 3\n"),
                        "3.1.0.0", INST_DI_INSTALL, "PCI\\X\\1",
                        "C:\\XHCI98\\XHCI98.INF", &r) == INST_DI_V_NO_MATCH,
          "no-match");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB remove USB\\A\\1\n"
                               "RESULT removed-restart\nEND 3\n"),
                        "3.1.0.0", INST_DI_REMOVE, "USB\\A\\1", NULL, &r) ==
              INST_DI_V_REMOVED_RESTART, "removed-restart");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB remove USB\\A\\1\n"
                               "RESULT not-found\nEND 3\n"),
                        "3.1.0.0", INST_DI_REMOVE, "USB\\A\\1", NULL, &r) ==
              INST_DI_V_NOT_FOUND, "not-found");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB remove USB\\A\\1\n"
                               "RESULT installed\nEND 3\n"),
                        "3.1.0.0", INST_DI_REMOVE, "USB\\A\\1", NULL, &r) ==
              INST_DI_V_NONE, "an install's word for a removal");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB probe\nRESULT probed\n"
                               "END 3\n"),
                        "3.1.0.0", INST_DI_PROBE, NULL, NULL, &r) ==
              INST_DI_V_PROBED, "the probe's file");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB probe\nRESULT ok\nEND 3\n"),
                        "3.1.0.0", INST_DI_PROBE, NULL, NULL, &r) ==
              INST_DI_V_NONE, "a probe's unknown word");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB probe\nRESULT failed "
                               "ERR_DI_BAD_PATH 1024\nEND 3\n"),
                        "3.1.0.0", INST_DI_PROBE, NULL, NULL, &r) ==
              INST_DI_V_NONE, "a probe that failed");
    CHECK(inst_di_parse(diFile("XHCIDI 3.1.0.0\nJOB probe\nEND 2\n"),
                        "3.1.0.0", INST_DI_PROBE, NULL, NULL, &r) ==
              INST_DI_V_NONE, "a probe with no RESULT line");
    CHECK(inst_di_parse(NULL, "3.1.0.0", INST_DI_PROBE, NULL, NULL, &r) ==
          INST_DI_V_NONE, "no file");
    CHECK(inst_di_ended("XHCIDI 3.1.0.0\r\nEND 1\r\n") &&
          !inst_di_ended("XHCIDI 3.1.0.0\r\nJOB probe\r\n"), "the END line");

    /* The verdicts' outcomes (record 26 section 7). */
    CHECK(inst_di_outcome(INST_DI_INSTALL, INST_DI_V_INSTALLED) ==
          INST_DI_OUT_OK, "installed: read back");
    CHECK(inst_di_outcome(INST_DI_INSTALL, INST_DI_V_INSTALLED_RESTART) ==
          INST_DI_OUT_RESTART, "the restart pending");
    CHECK(inst_di_outcome(INST_DI_INSTALL, INST_DI_V_CANCELLED) ==
          INST_DI_OUT_STEPS && inst_di_outcome(INST_DI_INSTALL,
                                               INST_DI_V_NO_MATCH) ==
          INST_DI_OUT_STEPS, "a cancel and no match: the steps, exit 4");
    CHECK(inst_di_outcome(INST_DI_INSTALL, INST_DI_V_FAILED) ==
          INST_DI_OUT_FAILED && inst_di_outcome(INST_DI_INSTALL,
                                                INST_DI_V_NONE) ==
          INST_DI_OUT_FAILED, "failed, or not finished: the steps, exit 2");
    CHECK(inst_di_outcome(INST_DI_INSTALL, INST_DI_V_NOT_PRESENT) ==
          INST_DI_OUT_ABSENT, "not present: recorded, not running now");
    CHECK(inst_di_outcome(INST_DI_INSTALL, INST_DI_V_REFUSED) ==
          INST_DI_OUT_REFUSED, "refused: the route fails this run");
    CHECK(strlen(inst_di_verdict_text(INST_DI_V_NONE)) > 0, "a text for each");
}

/* ---- record 26 section 5.4 (task 42-A.17): the machine-wide value ------- */

static void test_mw_values(void)
{
    char out[4096];
    INST_VNOW now[2];
    INST_U32 enter;
    int mode = inst_value_find("XhciInterruptMode");
    INST_ENV e = env(1, 6, 1, INST_ARCH_X86);
    int poll = inst_value_find("XhciCmPollIdleMs");
    int br = inst_value_find("XhciCmBridgeNumbering");
    INST_U32 d;

    memset(now, 0, sizeof(now));
    now[0].State = 1;
    now[0].Value = 0;
    now[0].Desc = "xHCI98 USB 3.x eXtensible Host Controller";
    now[1].State = 1;
    now[1].Value = 2;
    now[1].Desc = "a second controller";
    CHECK(inst_value_question_mw(out, sizeof(out), mode, 0xFUL, 0, now, 2, 1,
                                 1, 0, 0, &enter) == 1 && enter == 1 &&
          strstr(out, "now: 1, set machine-wide") != NULL &&
          strstr(out, "own value: 2, not used") != NULL &&
          strstr(out, "own value: 0, not used") != NULL &&
          strstr(out, "Enter for [1]: ") != NULL,
          "the machine-wide value in force, each device's own beside it");
    CHECK(inst_value_question_mw(out, sizeof(out), mode, 0xFUL, 0, now, 2, 0,
                                 0, 0, 0, &enter) == 0 &&
          strstr(out, "Enter for [each keeps its own]: ") != NULL,
          "none set machine-wide: the mixed state");
    CHECK(inst_value_question_mw(out, sizeof(out), mode, 0xFUL, 0, now, 1, -1,
                                 0, 0, 0, &enter) == 1 &&
          strstr(out, "could not be read") != NULL,
          "an unreadable machine-wide value is said");
    CHECK(fits(out), "the machine-wide question fits");
    /* What a choice writes. */
    CHECK(!inst_mw_write_needed(1, 2, now, 2, 0, 2) &&
          inst_mw_write_needed(1, 2, now, 2, 0, 0),
          "machine-wide set: written only when it differs from it");
    now[1].Value = 0;
    CHECK(!inst_mw_write_needed(0, 0, now, 2, 0, 0) &&
          inst_mw_write_needed(0, 0, now, 2, 0, 1),
          "none set: written only when a device runs another value");
    now[0].State = 0;
    CHECK(!inst_mw_write_needed(0, 0, now, 1, 0, 0) &&
          inst_mw_write_needed(0, 0, now, 1, 0, 1),
          "not set on the device: its default counts");
    CHECK(inst_mw_write_needed(0, 0, now, 0, 0, 0),
          "no device yet (a controller to come): written machine-wide");
    /* The ranges and the value no INF writes (record 26 section 5.5). */
    CHECK(poll >= 0 && inst_value_allowed(poll, &e, 50) &&
          inst_value_allowed(poll, &e, 5000) &&
          !inst_value_allowed(poll, &e, 49) &&
          !inst_value_allowed(poll, &e, 5001), "a range's bounds");
    CHECK(inst_value_set("XhciCmCmdTimeoutMs=60000", &mode, &d) == 1 && d == 60000UL &&
          inst_value_set("XhciCmCmdTimeoutMs=60001", &mode, &d) == -1,
          "/set takes a range's values");
    mode = inst_value_find("XhciInterruptMode");
    CHECK(br >= 0 && inst_value_default(br, NULL, &d) && d == 1UL,
          "the value no INF writes has the table's default");
    CHECK(inst_value_set_mask(br, &e) == 0, "a 98 SE and ME value on NT");
    e = env(0, 4, 10, INST_ARCH_X86);
    e.CmNhi = 1;
    CHECK(inst_value_relevant(br, &e) && !inst_value_applies(br, &e),
          "98 SE with a host interface: an Advanced row, never asked");
    CHECK(inst_value_allowed(mode, &e, 2) && !inst_value_allowed(mode, &e, 3),
          "a list's values by the system");
}

/* ---- record 26 sections 9.1 and 9.2: the entry, the relaunch ------------ */

static void test_uninstall_entry(void)
{
    char out[512];
    INST_OPTS o;
    char err[128];
    char *argv[5];

    inst_arp_name(out, sizeof(out), "3.1.0.0", 0, 0);
    CHECK(strcmp(out, "xHCI98 USB 3.x drivers 3.1.0.0") == 0, "the name");
    inst_arp_name(out, sizeof(out), "3.1.0.0", 1, 0);
    CHECK(strcmp(out, "xHCI98 USB 3.x drivers 3.1.0.0 (incomplete: run "
                      "again)") == 0, "an incomplete uninstall's name");
    /* Record 26 section 9.1, draft 8: named by what is registered. */
    inst_arp_name(out, sizeof(out), "3.1.0.0", 0, 1);
    CHECK(strcmp(out, "xHCI98 Thunderbolt 3 / USB4 connection manager "
                      "3.1.0.0") == 0, "the connection manager alone's name");
    inst_arp_name(out, sizeof(out), "3.1.0.0", 1, 1);
    CHECK(strcmp(out, "xHCI98 Thunderbolt 3 / USB4 connection manager "
                      "3.1.0.0 (incomplete: run again)") == 0,
          "the connection manager alone, incomplete");
    CHECK(inst_arp_cm_only(0, 1, 0) && !inst_arp_cm_only(1, 1, 0) &&
          !inst_arp_cm_only(-1, 1, 0) && !inst_arp_cm_only(0, 0, 0) &&
          !inst_arp_cm_only(0, -1, 0) && !inst_arp_cm_only(1, 0, 0),
          "the connection manager alone: xhci98 absent, xhcicm registered");
    /* A run without the USB drivers that only staged (no controller yet,
     * no service registered) is named for the connection manager; an
     * xhci98 still registered keeps the USB drivers' name. */
    CHECK(inst_arp_cm_only(0, 0, 1) && inst_arp_cm_only(0, -1, 1) &&
          !inst_arp_cm_only(1, 0, 1) && !inst_arp_cm_only(-1, 0, 1),
          "the connection manager alone: a staging-only run");
    /* The owner's rule (2026-10-19): one entry for every set of
     * components, an install over any of them an update in place. The
     * key is one constant, whatever the name says. */
    CHECK(strcmp(INST_ARP_KEY, "Software\\Microsoft\\Windows\\"
                 "CurrentVersion\\Uninstall\\xhci98") == 0,
          "one Add/Remove Programs key, whichever components");
    CHECK(inst_arp_command(out, sizeof(out), "C:\\Tools\\My xHCI98",
                           "XHCIWZ64.EXE", 1) &&
          strcmp(out, "\"C:\\Tools\\My xHCI98\\XHCIWZ64.EXE\" /uninstall") == 0,
          "NT: the window quoted, /uninstall");
    CHECK(inst_arp_command(out, sizeof(out), "C:\\XHCI98", "XHCIWIZ.EXE", 0) &&
          strcmp(out, "C:\\XHCI98\\XHCIWIZ.EXE /uninstall") == 0,
          "98 SE and ME: as it is, the folder 8.3 with no space");
    CHECK(!inst_arp_command(out, sizeof(out), "C:\\A B", "XHCIWIZ.EXE", 0),
          "98 SE and ME: a space is refused");
    CHECK(strcmp(inst_self_file(INST_ARCH_X86, INST_OWN_WINDOW),
                 "XHCIWIZ.EXE") == 0 &&
          strcmp(inst_self_file(INST_ARCH_AMD64, INST_OWN_WINDOW),
                 "XHCIWZ64.EXE") == 0 &&
          strcmp(inst_self_file(INST_ARCH_AMD64, INST_OWN_CONSOLE),
                 "XHCIIN64.EXE") == 0, "the installer's own files");
    CHECK(inst_uninstall_route(1, 0, 0) == INST_UN_RELAUNCH,
          "from the install directory: the relaunch");
    CHECK(inst_uninstall_route(0, 0, 0) == INST_UN_RUN, "from the package");
    CHECK(inst_uninstall_route(0, 1, 1) == INST_UN_RUN,
          "the temporary copy with /from");
    CHECK(inst_uninstall_route(0, 1, 0) == INST_UN_REFUSE &&
          inst_uninstall_route(1, 1, 1) == INST_UN_REFUSE,
          "a /from that is not the install directory, or given in it");
    CHECK(inst_relaunch_cmd(out, sizeof(out), "C:\\WINDOWS\\TEMP\\XHCI98UN\\"
                            "XHCIWIZ.EXE", "C:\\XHCI98", NULL, 0, 0) &&
          strcmp(out, "\"C:\\WINDOWS\\TEMP\\XHCI98UN\\XHCIWIZ.EXE\" /uninstall "
                      "/from \"C:\\XHCI98\"") == 0, "the relaunch's command");
    CHECK(inst_relaunch_cmd(out, sizeof(out), "C:\\T\\X.EXE", "C:\\X",
                            "D:\\L.LOG", 1, 1) &&
          strstr(out, " /log \"D:\\L.LOG\" /nopause /yes") != NULL,
          "the relaunch keeps /log, /nopause and /yes");
    argv[0] = "x";
    argv[1] = "/uninstall";
    argv[2] = "/from";
    argv[3] = "C:\\XHCI98";
    CHECK(inst_parse_args(4, argv, &o, err, sizeof(err)) == 0 &&
          strcmp(o.From, "C:\\XHCI98") == 0, "/from with /uninstall");
    CHECK(inst_parse_args(3, argv + 1, &o, err, sizeof(err)) == 1,
          "/from with no directory");
    argv[1] = "/from";
    argv[2] = "C:\\XHCI98";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) == 1,
          "/from without /uninstall");
    /* The window's switches (record 26 section 10). */
    argv[1] = "/console";
    CHECK(inst_parse_args(2, argv, &o, err, sizeof(err)) == 0 && o.Console &&
          inst_window_refusal(&o, 2, argv) != NULL,
          "/console: the console takes it, the window refuses it");
    argv[1] = "/list";
    CHECK(inst_parse_args(2, argv, &o, err, sizeof(err)) == 0 &&
          inst_window_refusal(&o, 2, argv) != NULL, "/list is the console's");
    argv[1] = "/debug";
    CHECK(inst_parse_args(2, argv, &o, err, sizeof(err)) == 0 && o.Debug &&
          inst_window_refusal(&o, 2, argv) == NULL, "/debug is the window's");
    argv[1] = "/thunderbolt";
    argv[2] = "/to";
    argv[3] = "C:\\XHCI98";
    CHECK(inst_parse_args(4, argv, &o, err, sizeof(err)) == 0 &&
          inst_window_refusal(&o, 4, argv) == NULL,
          "/thunderbolt and /to preselect the window's pages");
}

/* ---- record 26 section 11: the window's texts ------------------------- */

static void test_page_texts(void)
{
    int i;

    for (i = 0; i < instPageTextCount; i++) {
        const INST_PAGE_TEXT *t = &instPageTexts[i];
        CHECK(t->Text != NULL && t->Text[0] != '\0' &&
              strchr(t->Text, '\n') == NULL, "a page text, unbroken");
        if (t->Width != 0) {
            CHECK(inst_text_lines(t->Text, inst_page_cols(t->Width)) <=
                  inst_page_rows(t->Height + 2), "a page text fits its "
                  "control at the dialog font's average width");
        }
        CHECK(strcmp(inst_page_text(t->Id), t->Text) == 0, "by its id");
    }
    CHECK(inst_text_lines("aaa bbb ccc", 7) == 2 &&
          inst_text_lines("aaa bbb", 7) == 1 &&
          inst_text_lines("", 7) == 0 &&
          inst_text_lines("aaaaaaaaaaaaaaa", 7) == 3, "the line count");
    CHECK(inst_page_text(9999)[0] == '\0', "no such text");
}

/* The block of the readme's table that row i is: from the line naming it
 * to the next row's first line. */
static int s98_block(const char *table, int i, char *out, size_t cap)
{
    const INST_S98_VALUE *v = &instS98Values[i];
    char head[48];
    const char *s;
    const char *e;
    const char *p;
    size_t n;

    if (v->Name9x != NULL) {
        strcpy(head, "  ");
        strcat(head, v->Name9x);
        strcat(head, " ");
    } else {
        strcpy(head, "  (NT only) ");
        strcat(head, v->KeyNt);
        strcat(head, "=");
    }
    s = strstr(table, head);
    if (s == NULL) {
        return 0;
    }
    /* The block ends at the next line that starts a row: two spaces and a
     * letter or '(' in column 3. */
    e = s + 1;
    for (;;) {
        p = strchr(e, '\n');
        if (p == NULL) {
            e = s + strlen(s);
            break;
        }
        e = p + 1;
        if (e[0] == ' ' && e[1] == ' ' &&
            ((e[2] >= 'A' && e[2] <= 'Z') || e[2] == '(')) {
            break;
        }
        if (e[0] == '\0') {
            break;
        }
    }
    n = (size_t)(e - s);
    if (n + 1 > cap) {
        return 0;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return 1;
}

static int has_text(const char *block, const char *text)
{
    return text == NULL || strstr(block, text) != NULL;
}

static void test_s98(void)
{
    char *table = load("stor98-settings.txt");
    char block[2048];
    char t[256];
    char w[64];
    const char *why;
    int i;

    /* Every row against Stor98's own table (record 26 section 5.6). */
    CHECK(table != NULL, "test\\stor98-settings.txt, Stor98's readme table");
    for (i = 0; table != NULL && i < INST_S98_COUNT; i++) {
        const INST_S98_VALUE *v = &instS98Values[i];
        char key[32];
        char num[16];
        CHECK(s98_block(table, i, block, sizeof(block)),
              "each Stor98 row names a row of its readme's table");
        strcpy(key, v->KeyNt);
        strcat(key, "=");
        CHECK(has_text(block, key), "the row's DriverParameter key");
        CHECK(has_text(block, v->Unit[0] ? v->Unit : NULL), "the row's unit");
        CHECK(has_text(block, v->Def9x) && has_text(block, v->DefNt),
              "the row's defaults");
        if (v->Kind == INST_S98_NUMBER && i != INST_S98_POLLMODE) {
            sprintf(num, "%lu", (unsigned long)v->Lo);
            CHECK(has_text(block, num), "the row's range, its bottom");
            if (i != INST_S98_MAXTRANSFERKB && i != INST_S98_IDLE) {
                sprintf(num, "%lu", (unsigned long)v->Hi);
                CHECK(has_text(block, num), "the row's range, its top");
            }
        }
        if (v->Kind == INST_S98_WORDS) {
            CHECK(has_text(block, "auto, off, cacheoff, fua"),
                  "the write-through words");
        }
    }
    free(table);
    CHECK(strcmp(INST_S98_TABLE_VERSION, "0.4.0.0") == 0,
          "the table is the bundled Stor98's");

    /* Which system each row is for. */
    CHECK(inst_s98_applies(INST_S98_GONE, 1) &&
          !inst_s98_applies(INST_S98_GONE, 0) &&
          !inst_s98_applies(INST_S98_IDLE, 0) &&
          inst_s98_applies(INST_S98_WRITETHROUGH, 0),
          "gone and idle are 2000-to-7 only");
    CHECK(strcmp(inst_s98_default(INST_S98_MAXTRANSFERKB, 0), "512") == 0 &&
          strcmp(inst_s98_default(INST_S98_MAXTRANSFERKB, 1), "256") == 0 &&
          strcmp(inst_s98_default(INST_S98_POLLSPINUS, 0), "0") == 0 &&
          strcmp(inst_s98_default(INST_S98_POLLSPINUS, 1), "500") == 0,
          "per-system defaults");
    inst_s98_default_text(INST_S98_WRITETHROUGH, 0, 1, t, sizeof(t));
    CHECK(strcmp(t, "auto (here: cacheoff)") == 0, "auto on ME");
    inst_s98_default_text(INST_S98_WRITETHROUGH, 1, 0, t, sizeof(t));
    CHECK(strcmp(t, "auto (here: off)") == 0, "auto elsewhere");
    inst_s98_default_text(INST_S98_POLLIDLEMS, 1, 0, t, sizeof(t));
    CHECK(strcmp(t, "1000 ms") == 0, "a default with its unit");

    /* SHA-256, FIPS 180-2 appendix B's vectors and the empty string. */
    inst_sha256((const unsigned char *)"abc", 3, t);
    CHECK(strcmp(t, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61"
                    "F20015AD") == 0, "SHA-256 of abc");
    inst_sha256((const unsigned char *)"", 0, t);
    CHECK(strcmp(t, "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B"
                    "7852B855") == 0, "SHA-256 of nothing");
    inst_sha256((const unsigned char *)"abcdbcdecdefdefgefghfghighijhijkijkl"
                "jklmklmnlmnomnopnopq", 56, t);
    CHECK(strcmp(t, "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD4"
                    "19DB06C1") == 0, "SHA-256 of 56 bytes: two padding blocks");

    /* Values checked as the readme admits them. */
    CHECK(inst_s98_check(INST_S98_WRITETHROUGH, 0, " FUA ", w, sizeof(w),
                         &why) && strcmp(w, "fua") == 0,
          "a write-through word, any case");
    CHECK(!inst_s98_check(INST_S98_WRITETHROUGH, 1, "on", w, sizeof(w),
                          &why) && why != NULL, "an unknown word refused");
    CHECK(inst_s98_check(INST_S98_MAXTRANSFERKB, 0, "512", w, sizeof(w),
                         &why) &&
          !inst_s98_check(INST_S98_MAXTRANSFERKB, 1, "512", w, sizeof(w),
                          &why) &&
          inst_s98_check(INST_S98_MAXTRANSFERKB, 1, "128", w, sizeof(w),
                         &why) &&
          !inst_s98_check(INST_S98_MAXTRANSFERKB, 1, "130", w, sizeof(w),
                          &why) &&
          !inst_s98_check(INST_S98_MAXTRANSFERKB, 0, "60", w, sizeof(w),
                          &why),
          "the transfer size: 64 to the system's default, in steps of 4");
    CHECK(inst_s98_check(INST_S98_POLLBUSYUS, 1, "0", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_POLLBUSYUS, 1, "999", w, sizeof(w),
                          &why) &&
          inst_s98_check(INST_S98_POLLIDLEMS, 0, "0", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_POLLIDLEMS, 0, "99", w, sizeof(w), &why),
          "0 where the readme gives it a meaning, else the range");
    CHECK(inst_s98_check(INST_S98_POLLMODE, 0, "1", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_POLLMODE, 0, "2", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_POLLMODE, 0, "0x1", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_POLLMODE, 0, "-1", w, sizeof(w), &why),
          "decimal only");
    CHECK(!inst_s98_check(INST_S98_GONE, 0, "5000", w, sizeof(w), &why),
          "an NT-only row refused on 98 SE and ME");
    CHECK(!inst_s98_check(INST_S98_POLLIDLEMS, 1, "   ", w, sizeof(w),
                          &why) &&
          !inst_s98_check(INST_S98_WRITETHROUGH, 1, " ", w, sizeof(w), &why),
          "spaces alone are no value, not 0");
    CHECK(inst_s98_check(INST_S98_IDLE, 1, "0010", w, sizeof(w), &why) &&
          strcmp(w, "10") == 0, "a number written canonically");
    CHECK(inst_s98_check(INST_S98_IDLE, 1, "4294967295", w, sizeof(w),
                         &why) && strcmp(w, "4294967295") == 0 &&
          !inst_s98_check(INST_S98_IDLE, 1, "4294967296", w, sizeof(w),
                          &why) &&
          !inst_s98_check(INST_S98_IDLE, 1, "9", w, sizeof(w), &why),
          "idle: 10 and up, to the DWORD's top");
    CHECK(inst_s98_check(INST_S98_GONE, 1, "600000", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_GONE, 1, "600001", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_GONE, 1, "0", w, sizeof(w), &why),
          "gone: 1 to 600000");

    /* DriverParameter, edited one key at a time. */
    CHECK(inst_s98_param_get("maxtransferkb=128;pollmode=1", "pollmode", w,
                             sizeof(w)) && strcmp(w, "1") == 0,
          "a key read");
    CHECK(!inst_s98_param_get("maxtransferkb=128", "pollmode", w, sizeof(w)),
          "an absent key");
    CHECK(inst_s98_param_get("x=1; PollMode = 0 ;y", "pollmode", w,
                             sizeof(w)) && strcmp(w, "0") == 0,
          "a key in another case, with spaces");
    CHECK(!inst_s98_param_get("pollmodex=1", "pollmode", w, sizeof(w)),
          "a longer key is another key");
    CHECK(inst_s98_param_set("maxtransferkb=128;pollmode=1;foo=bar",
                             "pollmode", "0", t, sizeof(t)) &&
          strcmp(t, "maxtransferkb=128;pollmode=0;foo=bar") == 0,
          "a key replaced in place, the unknown pair kept");
    CHECK(inst_s98_param_set("maxtransferkb=128", "gone", "9000", t,
                             sizeof(t)) &&
          strcmp(t, "maxtransferkb=128;gone=9000") == 0, "a key appended");
    CHECK(inst_s98_param_set("a=1;;gone=2;b=3", "gone", NULL, t, sizeof(t)) &&
          strcmp(t, "a=1;b=3") == 0, "a key removed, empty segments dropped");
    CHECK(inst_s98_param_set("", "pollmode", "1", t, sizeof(t)) &&
          strcmp(t, "pollmode=1") == 0, "the first key");
    CHECK(inst_s98_param_set(NULL, "pollmode", NULL, t, sizeof(t)) &&
          strcmp(t, "") == 0, "nothing to remove");
    CHECK(inst_s98_param_set("pollmode=1;x=2;pollmode=0", "pollmode", "1", t,
                             sizeof(t)) && strcmp(t, "pollmode=1;x=2") == 0,
          "a doubled key made one");
    CHECK(!inst_s98_param_set("maxtransferkb=128", "pollmode", "1", t, 10),
          "a result that does not fit");

    /* What the page shows. */
    inst_s98_now_text(INST_S98_POLLMODE, 0, 0, 0, NULL, t, sizeof(t));
    CHECK(strcmp(t, "not set (the built-in default is 0)") == 0,
          "not set, with the default");
    inst_s98_now_text(INST_S98_POLLMODE, 0, 0, 1, "1", t, sizeof(t));
    CHECK(strcmp(t, "set: 1") == 0, "set");
    inst_s98_now_text(INST_S98_POLLMODE, 0, 0, -1, NULL, t, sizeof(t));
    CHECK(strcmp(t, "cannot be read") == 0, "unreadable");

    /* The switch, the question and the final line (record 26 sections 5.9
     * and 6.4). */
    {
        char *argv1[4];
        INST_OPTS o;
        char err[128];
        int asked;
        int def;
        argv1[0] = "x";
        argv1[1] = "/stor98";
        CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 && o.Stor98,
              "/stor98");
        argv1[2] = "/uninstall";
        CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1,
              "/stor98 belongs to an install");
        CHECK(!inst_s98_decide(1, 0, 0, 0, &asked, &def) && asked && !def,
              "asked, Enter gives N");
        CHECK(!inst_s98_decide(1, 1, 0, 0, &asked, &def) && !asked,
              "/yes: N, not asked");
        CHECK(inst_s98_decide(1, 1, 1, 0, &asked, &def) && !asked,
              "/stor98: installed unasked");
        CHECK(!inst_s98_decide(0, 0, 1, 0, &asked, &def) && !asked,
              "no Stor98 in the package: nothing");
        CHECK(!inst_s98_decide(1, 0, 0, 1, &asked, &def) && !asked,
              "/list asks nothing");
        CHECK(strcmp(inst_final_text_cold(INST_FINAL_RESTART, 1),
                     "Changes were made. Please shut down and start Windows "
                     "again.") == 0 &&
              strstr(inst_final_text_cold(INST_FINAL_UNFINISHED, 1),
                     "then shut down and start Windows again.") != NULL &&
              strcmp(inst_final_text_cold(INST_FINAL_RESTART, 0),
                     inst_final_text(INST_FINAL_RESTART)) == 0 &&
              strcmp(inst_final_text_cold(INST_FINAL_NO_CHANGES, 1),
                     "No changes were made.") == 0,
              "the shutdown's final lines");
    }

    /* The install record's two rules (record 26 section 6.3). */
    CHECK(inst_s98_owned_by_call(0, 1) && inst_s98_owned_by_call(1, 1) &&
          !inst_s98_owned_by_call(2, 1) && !inst_s98_owned_by_call(0, 0),
          "bound by the installer's call: no Stor98 before, Stor98 after");
    CHECK(inst_s98_owned_by_settle(0, 1, 1, 1, 1) &&
          !inst_s98_owned_by_settle(0, 1, 1, 0, 0),
          "NT, during a settle: the InfPath an INF this installer staged");
    CHECK(inst_s98_owned_by_settle(0, 1, 0, 0, 0) &&
          !inst_s98_owned_by_settle(0, 1, 0, 1, 1),
          "98 SE and ME, during a settle: no Stor98 at the baseline");
    CHECK(!inst_s98_owned_by_settle(2, 1, 1, 1, 0),
          "a device Stor98 ran before is never added");
    CHECK(inst_s98_service_removable(0, 0) &&
          !inst_s98_service_removable(1, 0) &&
          !inst_s98_service_removable(0, 1),
          "the service goes only when nothing is bound and none was there");
    CHECK(inst_s98_cold_start(0) && !inst_s98_cold_start(1),
          "98 SE and ME: shut down, not restart");
    CHECK(inst_s98_await_reconnect(1, 1, 1, 1) &&
          !inst_s98_await_reconnect(0, 1, 1, 1) &&
          !inst_s98_await_reconnect(1, 0, 1, 1) &&
          !inst_s98_await_reconnect(1, 1, 0, 1) &&
          !inst_s98_await_reconnect(1, 1, 1, 0),
          "a tunnelled drive's update awaits its reconnection");

    /* Stor98's place and eligibility (owner, 2026-10-19): tunnel drives
     * only, with the Thunderbolt component; unknown is never internal and
     * never a tunnel. */
    CHECK(inst_s98_place(1, 0, 1, 1, 1, 0) == INST_S98_AT_TUNNEL &&
          inst_s98_place(1, 0, 0, 0, 1, 1) == INST_S98_AT_TUNNEL,
          "below a verified tunnel port: a tunnel drive");
    CHECK(inst_s98_place(1, 0, 1, 1, 0, 0) == INST_S98_AT_INTERNAL,
          "a whole, confirmed reading, below no port: internal");
    CHECK(inst_s98_place(0, 0, 1, 1, 0, 0) == INST_S98_AT_UNKNOWN &&
          inst_s98_place(1, 1, 1, 1, 1, 0) == INST_S98_AT_UNKNOWN &&
          inst_s98_place(1, 0, 0, 1, 0, 0) == INST_S98_AT_UNKNOWN &&
          inst_s98_place(1, 0, 1, 0, 0, 0) == INST_S98_AT_UNKNOWN &&
          inst_s98_place(1, 0, 1, 1, 0, 1) == INST_S98_AT_UNKNOWN,
          "not found, changing, not whole, a manager unconfirmed, an "
          "unverified port above: unknown");
    CHECK(inst_s98_eligible(1, INST_S98_AT_TUNNEL) == INST_S98_ELIGIBLE,
          "eligible: a tunnel drive with the Thunderbolt component");
    CHECK(inst_s98_eligible(1, INST_S98_AT_INTERNAL) ==
              INST_S98_SKIP_INTERNAL,
          "skip-internal");
    CHECK(inst_s98_eligible(1, INST_S98_AT_UNKNOWN) == INST_S98_SKIP_UNKNOWN,
          "skip-unknown");
    CHECK(inst_s98_eligible(0, INST_S98_AT_TUNNEL) == INST_S98_NEEDS_TB &&
          inst_s98_eligible(0, INST_S98_AT_INTERNAL) == INST_S98_NEEDS_TB &&
          inst_s98_eligible(0, INST_S98_AT_UNKNOWN) == INST_S98_NEEDS_TB,
          "needs-TB, wherever the drive sits");
    CHECK(inst_s98_skip_text(INST_S98_ELIGIBLE, 0) == NULL &&
          inst_s98_skip_text(INST_S98_SKIP_INTERNAL, 0) != NULL &&
          strstr(inst_s98_skip_text(INST_S98_SKIP_INTERNAL, 1),
                 "internal") != NULL &&
          inst_s98_skip_text(INST_S98_SKIP_UNKNOWN, 1) != NULL &&
          inst_s98_skip_text(INST_S98_NEEDS_TB, 1) != NULL &&
          inst_s98_skip_text(INST_S98_SKIP_SHARED, 1) != NULL,
          "every reason has its sentence");
    CHECK(inst_s98_share(0, 0) == INST_S98_SHARE_FREE &&
          inst_s98_share(0, 1) == INST_S98_SHARE_FREE &&
          inst_s98_share(1, 1) == INST_S98_SHARE_SAME &&
          inst_s98_share(1, 0) == INST_S98_SHARE_HOLD,
          "the shared file: changed only with no other Stor98, or kept at "
          "its release");
    CHECK(inst_s98_adoptable(0, 1, INST_S98_AT_TUNNEL) &&
          inst_s98_adoptable(0, 0, INST_S98_AT_UNKNOWN) &&
          inst_s98_adoptable(0, 0, INST_S98_AT_INTERNAL),
          "uninstall: bound after the run, behind a tunnel or gone with it");
    CHECK(!inst_s98_adoptable(0, 1, INST_S98_AT_INTERNAL) &&
          !inst_s98_adoptable(0, 1, INST_S98_AT_UNKNOWN),
          "uninstall: an internal or unknown drive keeps Stor98");
    CHECK(!inst_s98_adoptable(1, 1, INST_S98_AT_TUNNEL) &&
          !inst_s98_adoptable(1, 0, INST_S98_AT_UNKNOWN),
          "uninstall: a Stor98 there before this installer is never adopted");
    {
        static const char a[] = "[Files]\r\nstor98.mpd\r\n";
        static const char b[] = "[SourceDisksFiles]\r\nSTOR98.MPD=1\r\n";
        static const char c[] = "PortDriver=stor98.mpd";
        static const char d[] = "; stor98.mpd in a comment\r\nx=1\r\n";
        static const char e[] = "x=mystor98.mpd\r\nstor98.mpdx\r\n";
        static const char f[] = "  stor98.mpd,,,2 ; copy\r\n";
        CHECK(inst_inf_names_file(a, sizeof(a) - 1, "stor98.mpd") &&
              inst_inf_names_file(b, sizeof(b) - 1, "stor98.mpd") &&
              inst_inf_names_file(c, sizeof(c) - 1, "stor98.mpd") &&
              inst_inf_names_file(f, sizeof(f) - 1, "stor98.mpd"),
              "an INF names the file: a line's start, a key, a value, a "
              "list entry");
        CHECK(!inst_inf_names_file(d, sizeof(d) - 1, "stor98.mpd") &&
              !inst_inf_names_file(e, sizeof(e) - 1, "stor98.mpd") &&
              !inst_inf_names_file(c, sizeof(c) - 1, "stor98.sys"),
              "a comment, a longer word or another file is not the file");
    }
    CHECK(!inst_s98_svc_value_stor98("LinkState") &&
          !inst_s98_svc_value_stor98("linkstate") &&
          inst_s98_svc_value_stor98("StorInterrupt") &&
          inst_s98_svc_value_stor98("StorWriteThrough"),
          "98 SE and ME: xhcicm's LinkState alone is not Stor98 there "
          "before");
    CHECK(inst_s98_stray_inf_ours(0, 1) && !inst_s98_stray_inf_ours(1, 1) &&
          !inst_s98_stray_inf_ours(0, 0),
          "a stray INF copy: the package's when Stor98's and none before");
    CHECK(strstr(inst_page_text(INST_PT_S98_T), "tunnel drives only") !=
              NULL &&
          strstr(inst_page_text(INST_PT_S98_T), "internal") == NULL,
          "the Components page's sentence");
}

/* ---- task 42-A.17's reading rule; task 42-A.5's installer hooks --------- */

static void test_marks(void)
{
    static unsigned char a[CM_QM_HDR_BYTES + 3 * CM_QM_ENT_BYTES];
    unsigned char c[INST_QC_BYTES];
    unsigned char *e;
    INST_U32 v = 0;
    INST_U32 need;
    int i;

    /* The machine-wide reading, as xhci_regsrc.c decides. */
    CHECK(inst_mw_reading(0, INST_REG_DWORD, 4, 1, 0, &v) == 0,
          "machine-wide: a name not found is absent");
    CHECK(inst_mw_reading(1, INST_REG_NONE, 0, 1, 0, &v) == 0,
          "machine-wide: a zero-length REG_NONE is absent");
    CHECK(inst_mw_reading(1, INST_REG_DWORD, 4, 2, 0, &v) == 1 && v == 2,
          "machine-wide: a DWORD is its number");
    v = 9;
    CHECK(inst_mw_reading(1, 1UL /* REG_SZ */, 4, 2, 3, &v) == 2,
          "machine-wide: another type is present and not a number");
    CHECK(inst_mw_reading(1, INST_REG_NONE, 2, 2, 1, &v) == 2,
          "machine-wide: a REG_NONE with data is present");
    CHECK(inst_mw_reading(1, INST_REG_DWORD, 2, 2, 0, &v) == 2,
          "machine-wide: a short DWORD is present and not a number");

    /* Where the service goes, and what a run's end does. */
    {
        INST_ENV w2k = env(1, 5, 0, INST_ARCH_X86);
        INST_ENV w7 = env(1, 6, 1, INST_ARCH_AMD64);
        INST_ENV w8 = env(1, 6, 2, INST_ARCH_AMD64);
        INST_ENV w81 = env(1, 6, 3, INST_ARCH_X86);
        INST_ENV w10 = env(1, 10, 0, INST_ARCH_AMD64);
        INST_ENV w64 = env(1, 6, 4, INST_ARCH_AMD64);
        INST_ENV w98 = env(0, 4, 10, INST_ARCH_X86);
        CHECK(inst_mk_system(&w2k) && inst_mk_system(&w7) &&
              inst_mk_system(&w8) && inst_mk_system(&w81) &&
              !inst_mk_system(&w10) && !inst_mk_system(&w64) &&
              !inst_mk_system(&w98),
              "the mark service: Windows 2000 to 8.1 (8 and 8.1 for the "
              "connection manager alone)");
    }
    CHECK(inst_mk_end_action(1, 1, 1) == INST_MK_INSTALL &&
          inst_mk_end_action(1, 0, 1) == INST_MK_LEAVE &&
          inst_mk_end_action(1, 1, 0) == INST_MK_LEAVE &&
          inst_mk_end_action(0, 1, 1) == INST_MK_REMOVE &&
          inst_mk_end_action(0, 0, 0) == INST_MK_REMOVE &&
          inst_mk_end_action(-1, 1, 1) == INST_MK_LEAVE,
          "a run's end: the service with xhcicm, every mark gone without it");

    /* The confirmation, against cm_query.h's layout. */
    CHECK(INST_QC_BYTES == CM_QC_BYTES && CM_QC_O_GEN + 4 == CM_QC_BYTES,
          "the confirmation's size is cm_query.h's");
    memset(a, 0, sizeof(a));
    put32(a, CM_QM_O_MAGIC, CM_QM_MAGIC);
    put32(a, CM_QM_O_VERSION, CM_QM_VERSION);
    put32(a, CM_QM_O_BYTES, sizeof(a));
    put32(a, CM_QM_O_EPOCH_LO, 0x11112222UL);
    put32(a, CM_QM_O_EPOCH_HI, 0x33334444UL);
    put32(a, CM_QM_O_COUNT, 3);
    put32(a, CM_QM_O_ENT_BYTES, CM_QM_ENT_BYTES);
    put32(a, CM_QM_O_ENT_OFFSET, CM_QM_HDR_BYTES);
    for (i = 0; i < 3; i++) {
        e = a + CM_QM_HDR_BYTES + i * CM_QM_ENT_BYTES;
        put32(e, CM_QM_E_MEMBER, 0x0108UL);
        put32(e, CM_QM_E_BUS, 5UL + (INST_U32)i);
        put32(e, CM_QM_E_ADDRESS, 0x00000000UL);
        put32(e, CM_QM_E_SERIAL, 40UL + (INST_U32)i);
        put32(e, CM_QM_E_FLAGS, CM_QM_EF_LOCATED);
        put32(e, CM_QM_E_PENDING_GEN, 7UL);
    }
    /* Entry 1 departed after a query-remove. */
    put32(a + CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, CM_QM_E_PENDING, 1234UL);
    CHECK(inst_qc_build(a, sizeof(a), 0x0108UL, 6, 0, c, &need) == 1 &&
          inst_get32(c, CM_QC_O_EPOCH_LO) == 0x11112222UL &&
          inst_get32(c, CM_QC_O_EPOCH_HI) == 0x33334444UL &&
          inst_get32(c, CM_QC_O_MEMBER) == 0x0108UL &&
          inst_get32(c, CM_QC_O_BUS) == 6UL &&
          inst_get32(c, CM_QC_O_ADDRESS) == 0UL &&
          inst_get32(c, CM_QC_O_SERIAL) == 41UL &&
          inst_get32(c, CM_QC_O_GEN) == 7UL,
          "the confirmation names the entry's epoch, serial and generation");
    CHECK(inst_qc_build(a, sizeof(a), 0x0108UL, 5, 0, c, &need) == 0,
          "no confirmation without a pending departure");
    CHECK(inst_qc_build(a, sizeof(a), 0x0110UL, 6, 0, c, &need) == 0,
          "no confirmation behind another member");
    put32(a + CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, CM_QM_E_FLAGS, 0);
    CHECK(inst_qc_build(a, sizeof(a), 0x0108UL, 6, 0, c, &need) == 0,
          "no confirmation for an entry whose location is not known");
    put32(a + CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, CM_QM_E_FLAGS,
          CM_QM_EF_LOCATED);
    put32(a + CM_QM_HDR_BYTES + 2 * CM_QM_ENT_BYTES, CM_QM_E_BUS, 6UL);
    put32(a + CM_QM_HDR_BYTES + 2 * CM_QM_ENT_BYTES, CM_QM_E_PENDING, 9UL);
    CHECK(inst_qc_build(a, sizeof(a), 0x0108UL, 6, 0, c, &need) == -1,
          "two pending entries at one place: none confirmed");
    CHECK(inst_qc_build(a, CM_QM_HDR_BYTES, 0x0108UL, 6, 0, c, &need) == -2 &&
          need == sizeof(a), "a header alone asks for the whole answer");
    put32(a, CM_QM_O_MAGIC, CM_Q_MAGIC);
    CHECK(inst_qc_build(a, sizeof(a), 0x0108UL, 6, 0, c, &need) == 0,
          "another answer is no marks answer");
}

int main(void)
{
    test_versions();
    test_models_rule();
    test_cm_only();
    test_decorations();
    test_reg_hex();
    test_real_infs();
    test_cm_infs();
    test_parser();
    test_ids();
    test_actions();
    test_helpers();
    test_values();
    test_args();
    test_flow();
    test_pe_signed();
    test_tree_sig();
    test_tb_ids();
    test_tb_record();
    test_tb_units();
    test_tb_eject();
    test_tb_question();
    test_tray();
    test_wrap();
    test_questions();
    test_dir_check();
    test_to_switch();
    test_di_helper();
    test_mw_values();
    test_uninstall_entry();
    test_page_texts();
    test_s98();
    test_marks();
    printf("test_inst: %d checks, %d failures\n", checks, failures);
    return failures;
}
