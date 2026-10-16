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

/* The Thunderbolt INFs, once task 39-A.1 has put them in src\cm\. */
static void test_cm_infs(void)
{
    char *cm = load("..\\src\\cm\\xhcicm.inf");
    char *cf = load("..\\src\\cm\\xhcicmf.inf");
    char *c64 = load("..\\src\\cm\\xhcicm-amd64.inf");
    INST_ENV e;
    INST_IDLIST ids;
    int i;

    if (cm == NULL || cf == NULL || c64 == NULL) {
        printf("test_inst: src\\cm\\ INFs not in this tree; their checks skipped\n");
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
        CHECK(inst_inf_addreg_default(cm, instValues[i].Name, &d),
              instValues[i].Name);
        CHECK(inst_inf_addreg_default(c64, instValues[i].Name, &d64) &&
                  d64 == d, "the amd64 INF writes the same default");
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
    } else {
        printf("test_inst: xhcicmf.inf not in this tree; the table check skipped\n");
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
    CHECK(inst_eject_nt_fallback(INST_CR_REMOVE_VETOED, 8, 1, 0x0),
          "an illegal request on a unit that is not removable: query and remove");
    CHECK(!inst_eject_nt_fallback(INST_CR_REMOVE_VETOED, 8, 1, 0x4),
          "not for a removable unit");
    CHECK(!inst_eject_nt_fallback(INST_CR_REMOVE_VETOED, 8, 1, 0x80),
          "not for a unit that is surprise-removal OK");
    CHECK(!inst_eject_nt_fallback(INST_CR_REMOVE_VETOED, 8, 0, 0x0),
          "not when the capabilities are not known");
    CHECK(!inst_eject_nt_fallback(INST_CR_REMOVE_VETOED, 5, 1, 0x0),
          "never past an open handle");
    CHECK(!inst_eject_nt_fallback(0, 8, 1, 0x0), "not after a success");
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

int main(void)
{
    test_versions();
    test_models_rule();
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
    printf("test_inst: %d checks, %d failures\n", checks, failures);
    return failures;
}
