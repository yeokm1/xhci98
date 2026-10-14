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
        CHECK((instValues[i].Asked & (1UL << d86)) != 0,
              "the default is an offered answer");
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
        if (instValues[i].Inf != INST_INF_CM) {
            continue;
        }
        CHECK(inst_inf_addreg_default(cm, instValues[i].Name, &d),
              instValues[i].Name);
        CHECK((instValues[i].Asked & (1UL << d)) != 0,
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
    CHECK(inst_value_applies(ss, &e), "98: selective suspend");
    CHECK(!inst_value_applies(cm, &e), "no NHI: no cm values");
    CHECK_EQ(inst_value_asked_mask(im, &e), 0x7, "98: interrupt mode 0-2");
    e.PswController = 1;
    CHECK(inst_value_applies(psw, &e), "an Intel 7-9 controller: port switch");
    e.CmNhi = 1;
    CHECK(inst_value_applies(cm, &e), "an NHI: cm values");
    e = env(1, 5, 0, INST_ARCH_X86);
    e.AnyXhci = 1;
    CHECK(inst_value_applies(ft, &e), "2000: fine timer");
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

    CHECK_EQ(inst_exit_code(0, 0, 0, 0), 0, "done");
    CHECK_EQ(inst_exit_code(1, 1, 1, 1), 1, "refused first");
    CHECK_EQ(inst_exit_code(0, 1, 1, 1), 2, "failed beats restart and steps");
    CHECK_EQ(inst_exit_code(0, 0, 1, 1), 4, "steps beat restart");
    CHECK_EQ(inst_exit_code(0, 0, 1, 0), 3, "restart");
}

int main(void)
{
    test_versions();
    test_models_rule();
    test_real_infs();
    test_cm_infs();
    test_parser();
    test_ids();
    test_actions();
    test_helpers();
    test_values();
    test_args();
    printf("test_inst: %d checks, %d failures\n", checks, failures);
    return failures;
}
