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
#include "../xhciinst/inst_board.h"
#include "../src/cm/cm_status.h"
#include "../src/cm/cm_query.h"
#include "../src/cm/cm_ports.h"
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
    /* Revision 28: Windows 10 reads them too, with no INF change. */
    e = env(1, 10, 0, INST_ARCH_X86);
    inst_models_section("CmModels,NTx86.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "CmModels.NTx86.6.0"), "10 x86: .NTx86.6.0");
    inst_models_section("XhciModels,NTx86.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels.NTx86.6.0"), "10 x86: xhci98's .NTx86.6.0");
    inst_models_section("UasModels", &e, out, sizeof(out));
    CHECK(inst_eq(out, "UasModels"), "10 x86: xhciuas's undecorated");
    e = env(1, 10, 0, INST_ARCH_AMD64);
    inst_models_section("CmModels,NTamd64,NTamd64.6.0", &e, out, sizeof(out));
    CHECK(inst_eq(out, "CmModels.NTamd64.6.0"), "10 x64: NTamd64.6.0");
    inst_models_section("Stor98_Models,NTamd64", &e, out, sizeof(out));
    CHECK(inst_eq(out, "Stor98_Models.NTamd64"), "10 x64: Stor98's NTamd64");
    inst_models_section("XhciModels,NTamd64,NTamd64.6.0,NTamd64.10.0", &e,
                        out, sizeof(out));
    CHECK(inst_eq(out, "XhciModels.NTamd64.10.0"),
          "10 x64: a .10.0 decoration would outrank .6.0");
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
    /* Revision 28 (task 43.34): 10.0 below build 22000, Server 2016 to
     * 2022 among them with no Server check; 11 refused. */
    CHECK(inst_os_kind(1, 10, 0, 10240) == INST_OS_CMONLY, "10 1507");
    CHECK(inst_os_kind(1, 10, 0, 14393) == INST_OS_CMONLY,
          "10 1607 / Server 2016");
    CHECK(inst_os_kind(1, 10, 0, 17763) == INST_OS_CMONLY,
          "10 1809 / Server 2019");
    CHECK(inst_os_kind(1, 10, 0, 19045) == INST_OS_CMONLY, "10 22H2");
    CHECK(inst_os_kind(1, 10, 0, 20348) == INST_OS_CMONLY, "Server 2022");
    CHECK(inst_os_kind(1, 10, 0, 21999) == INST_OS_CMONLY,
          "the last build below 11");
    CHECK(inst_os_kind(1, 10, 0, 22000) == INST_OS_REFUSED,
          "11 (22000): refused");
    CHECK(inst_os_kind(1, 10, 0, 26100) == INST_OS_REFUSED, "11: refused");
    CHECK(inst_os_kind(1, 10, 1, 19045) == INST_OS_REFUSED,
          "10.1: refused");
    CHECK(inst_os_kind(1, 11, 0, 19045) == INST_OS_REFUSED,
          "11.0: refused");
    CHECK(inst_os_kind(0, 10, 0, 19045) == INST_OS_REFUSED,
          "not NT: refused");
    CHECK(inst_os_kind(1, 7, 0, 0) == INST_OS_REFUSED, "7.0: refused");
    CHECK(strcmp(inst_os_name(1, 6, 2, 9200), "Windows 8") == 0 &&
          strcmp(inst_os_name(1, 6, 3, 9600), "Windows 8.1") == 0 &&
          strcmp(inst_os_name(1, 6, 1, 7601), "Windows 7") == 0 &&
          strcmp(inst_os_name(1, 5, 2, 3790), "Windows XP x64 / Server 2003") == 0 &&
          strcmp(inst_os_name(0, 4, 90, 3000), "Windows ME") == 0 &&
          strcmp(inst_os_name(0, 4, 10, 2222), "Windows 98 SE") == 0 &&
          strcmp(inst_os_name(1, 10, 0, 19045), "Windows 10") == 0 &&
          strcmp(inst_os_name(1, 10, 0, 20348), "Windows 10") == 0 &&
          strcmp(inst_os_name(1, 10, 0, 21999), "Windows 10") == 0 &&
          strcmp(inst_os_name(1, 10, 0, 22000), "Windows 11") == 0 &&
          strcmp(inst_os_name(1, 11, 0, 0), "Windows") == 0, "the names");
    {
        /* The Welcome page's system line (owner, 2026-11-04): 98 SE's
         * " A " trimmed, a blank CSD left out. */
        char l[96];
        int ok;

        inst_os_line(l, sizeof(l), "Windows 98 SE", 4, 10, 2222, " A ",
                     "32-bit");
        ok = strcmp(l, "Windows: Windows 98 SE (4.10.2222 A), 32-bit") == 0;
        inst_os_line(l, sizeof(l), "Windows ME", 4, 90, 3000, " ", "32-bit");
        ok = ok && strcmp(l, "Windows: Windows ME (4.90.3000), 32-bit") == 0;
        inst_os_line(l, sizeof(l), "Windows XP", 5, 1, 2600, "Service Pack 3",
                     "32-bit");
        ok = ok && strcmp(l, "Windows: Windows XP (5.1.2600 Service Pack 3), "
                          "32-bit") == 0;
        inst_os_line(l, sizeof(l), "Windows 10", 10, 0, 19045, "", "x64");
        ok = ok && strcmp(l, "Windows: Windows 10 (10.0.19045), x64") == 0;
        CHECK(ok, "the system line: the CSD trimmed, a blank one left out");
    }

    /* The signature note (section 5.10, revision 30; task 43.22). The
     * gates take the kind inst_os_kind gives, so a system admitted to the
     * connection-manager-alone kind later (10, task 43.34) takes them. */
    {
        int k98 = inst_os_kind(0, 4, 10, 2222);
        int kXp = inst_os_kind(1, 5, 1, 2600);
        int kXp64 = inst_os_kind(1, 5, 2, 3790);
        int kVista = inst_os_kind(1, 6, 0, 6002);
        int k7 = inst_os_kind(1, 6, 1, 7601);
        int k8 = inst_os_kind(1, 6, 2, 9200);
        int k81 = inst_os_kind(1, 6, 3, 9600);
        int k10 = inst_os_kind(1, 10, 0, 19045);
        int k11 = inst_os_kind(1, 10, 0, 22631);

        CHECK(k8 == INST_OS_CMONLY && k81 == INST_OS_CMONLY &&
              k10 == INST_OS_CMONLY && k11 == INST_OS_REFUSED &&
              k7 == INST_OS_FULL && kVista == INST_OS_FULL,
              "the kinds the gates key on");
        /* Windows 10 below build 22000 (task 43.34) takes every gate. */
        CHECK(inst_sig_note_kind(INST_ARCH_X86, 1, 10, k10) ==
              INST_SIG_X86_SB &&
              inst_sig_note_kind(INST_ARCH_AMD64, 1, 10, k10) ==
              INST_SIG_X64_8 &&
              inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_AMD64, k10)
              != NULL &&
              inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_X86, k10)
              == NULL &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 10, k10, 1, 1) ==
              INST_CMBOOT_WARN &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 10, k10, 1,
                                inst_ts_confirmed(1, INST_SB_ON)) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 10, k10, 0, 1) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_X86, 1, 10, k10, 0, 0) ==
              INST_CMBOOT_NONE &&
              inst_sb_applies(1, k10) && !inst_sb_applies(1, k11) &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 10, k11, 0, 0) ==
              INST_CMBOOT_NONE &&
              inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_AMD64, k11)
              == NULL,
              "10 below 22000 gated as 8 and 8.1; 11 refused, not gated");
        CHECK(inst_sig_note_kind(INST_ARCH_X86, 0, 4, k98) == INST_SIG_NONE &&
              inst_sig_note_kind(INST_ARCH_X86, 1, 5, kXp) == INST_SIG_NONE &&
              inst_sig_note_kind(INST_ARCH_X86, 1, 6, kVista) ==
              INST_SIG_NONE &&
              inst_sig_note_kind(INST_ARCH_X86, 1, 6, k7) == INST_SIG_NONE,
              "x86 98 SE to 7: no signature note");
        CHECK(inst_sig_note_kind(INST_ARCH_X86, 1, 6, k8) == INST_SIG_X86_SB &&
              inst_sig_note_kind(INST_ARCH_X86, 1, 6, k81) == INST_SIG_X86_SB,
              "x86 8 and 8.1: the Secure Boot note");
        CHECK(inst_sig_note_kind(INST_ARCH_X86, 1, 10, INST_OS_CMONLY) ==
              INST_SIG_X86_SB &&
              inst_sig_note_kind(INST_ARCH_X86, 1, 10, INST_OS_REFUSED) ==
              INST_SIG_NONE, "x86 10: as its kind says");
        CHECK(inst_sig_note_kind(INST_ARCH_AMD64, 1, 5, kXp64) ==
              INST_SIG_NONE, "XP x64: no note");
        CHECK(inst_sig_note_kind(INST_ARCH_AMD64, 1, 6, kVista) ==
              INST_SIG_X64 &&
              inst_sig_note_kind(INST_ARCH_AMD64, 1, 6, k7) == INST_SIG_X64 &&
              inst_sig_note_kind(INST_ARCH_AMD64, 1, 6, k8) ==
              INST_SIG_X64_8 &&
              inst_sig_note_kind(INST_ARCH_AMD64, 1, 6, k81) ==
              INST_SIG_X64_8 &&
              inst_sig_note_kind(INST_ARCH_AMD64, 1, 10, INST_OS_CMONLY) ==
              INST_SIG_X64_8, "x64 Vista and 7: the test-signing note; 8 and "
              "later its own words (record 26 section 5.3.3)");
        CHECK(strstr(inst_sig_x86_sb_text(), "Secure Boot") != NULL &&
              strstr(inst_sig_x86_sb_text(), "test-signing mode does not help")
              != NULL && strstr(inst_sig_x86_sb_text(), "Windows 8") == NULL,
              "the Secure Boot note's words, no version named");
        CHECK(inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_AMD64, k8)
              != NULL &&
              inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_AMD64, k81)
              != NULL &&
              inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_AMD64,
                                    INST_OS_CMONLY) != NULL,
              "8 and 8.1 x64, and the kind: no catalog explained");
        CHECK(inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_AMD64, k7)
              == NULL &&
              inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_AMD64, kXp64)
              == NULL &&
              inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_X86, k8)
              == NULL &&
              inst_stage_error_text(0xE0000203UL, INST_ARCH_AMD64, k8)
              == NULL &&
              inst_stage_error_text(5UL, INST_ARCH_AMD64, k81) == NULL,
              "other systems and errors: nothing added");
        CHECK(strstr(inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_AMD64,
                                           k8), "complete package") != NULL &&
              strstr(inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_AMD64,
                                           k8), ".cat files") != NULL &&
              strstr(inst_stage_error_text(INST_E_NO_CATALOG, INST_ARCH_AMD64,
                                           k8), "Startup Settings") == NULL &&
              inst_text_width(inst_stage_error_text(INST_E_NO_CATALOG,
                                                    INST_ARCH_AMD64, k8)) <=
              INST_COLUMNS,
              "no catalog: restore the package with its .cat files");
        /* 8 and 8.1 x64 make xhcicm a boot driver (task 43.22). */
        CHECK(inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k8, 1, 1) == INST_CMBOOT_WARN &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k81, 1, 1) ==
              INST_CMBOOT_WARN,
              "8 and 8.1 x64, test-signed, test-signing on: the warning");
        CHECK(inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k8, 1, 0) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k81, 0, 1) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k8, -1, 1) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k81, 0, 0) ==
              INST_CMBOOT_REFUSE,
              "8 and 8.1 x64: refused without test-signing or a test signature");
        CHECK(inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, INST_OS_CMONLY, 1, 1) ==
              INST_CMBOOT_WARN &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, INST_OS_CMONLY, 1, 0) ==
              INST_CMBOOT_REFUSE, "any system of the kind: gated alike");
        /* Vista and 7 x64 make xhcicm a boot driver too (the 3.2.0.0 cut,
         * task 44.32's reading): gated as 8 to 10 (task 44.12). */
        CHECK(inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k7, 0, 0) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, kVista, 1, 0) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k7, 0, 1) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, kVista, -1, 1) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k7, 1, 1) ==
              INST_CMBOOT_WARN &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, kVista, 1, 1) ==
              INST_CMBOOT_WARN, "Vista and 7 x64: gated as 8 to 10");
        CHECK(inst_cm_boot_gate(INST_ARCH_AMD64, 0, 4, k98, 0, 0) ==
              INST_CMBOOT_NONE &&
              inst_cm_boot_gate(INST_ARCH_X86, 1, 6, k7, 0, 0) ==
              INST_CMBOOT_NONE &&
              inst_cm_boot_gate(INST_ARCH_X86, 1, 6, kVista, 0, 0) ==
              INST_CMBOOT_NONE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 5, kXp64, 0, 0) ==
              INST_CMBOOT_NONE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, INST_OS_REFUSED, 0, 0) ==
              INST_CMBOOT_NONE &&
              inst_cm_boot_gate(INST_ARCH_X86, 1, 6, k8, 0, 0) == INST_CMBOOT_NONE &&
              inst_cm_boot_gate(INST_ARCH_X86, 1, 6, k81, 0, 0) == INST_CMBOOT_NONE,
              "XP x64, the 32-bit systems and the refused: not gated");
        CHECK(strstr(inst_cm_boot_refuse_text(), "bcdedit /set testsigning on")
              != NULL &&
              strstr(inst_cm_boot_warn_text(), "Automatic Repair") != NULL &&
              strstr(inst_cm_boot_refuse_text(), "Windows 8") == NULL &&
              strstr(inst_cm_boot_warn_text(), "Windows 8") == NULL,
              "the gate's words, no version named");

        /* The connection manager off the boot path (record 23 section
         * 5.14; task 44.32): 0, 1 and 2 set back to demand start; 3, 4,
         * another type or nothing read left alone. */
        CHECK(inst_cm_start_fix(1, INST_REG_DWORD_TYPE, 0UL) &&
              inst_cm_start_fix(1, INST_REG_DWORD_TYPE, 1UL) &&
              inst_cm_start_fix(1, INST_REG_DWORD_TYPE, 2UL) &&
              !inst_cm_start_fix(1, INST_REG_DWORD_TYPE, 3UL) &&
              !inst_cm_start_fix(1, INST_REG_DWORD_TYPE, 4UL) &&
              !inst_cm_start_fix(1, INST_REG_DWORD_TYPE, 0xFFFFFFFFUL) &&
              !inst_cm_start_fix(1, 1UL, 0UL) &&
              !inst_cm_start_fix(0, INST_REG_DWORD_TYPE, 0UL) &&
              INST_CM_START_DEMAND == 3UL, "xhcicm's start type put back");
        /* Only where no devnode on the system disk's path names it. */
        CHECK(inst_cm_start_action(1, 0) == INST_CMST_SET &&
              inst_cm_start_action(1, 1) == INST_CMST_KEEP_PATH &&
              inst_cm_start_action(1, -1) == INST_CMST_KEEP_UNREAD &&
              inst_cm_start_action(0, 0) == INST_CMST_NONE &&
              inst_cm_start_action(0, 1) == INST_CMST_NONE &&
              inst_cm_start_action(0, -1) == INST_CMST_NONE,
              "set back only off the system disk's path");
        CHECK(inst_bp_prop_verdict(INST_CR_NO_SUCH_VALUE, 0UL, NULL, 0,
                                   "xhcicm") == 0 &&
              inst_bp_prop_verdict(INST_CR_BUFFER_SMALL, 0UL, NULL, 0,
                                   "xhcicm") == INST_BP_GROW &&
              inst_bp_prop_verdict(0x13UL, 0UL, NULL, 0, "xhcicm") == -1 &&
              inst_bp_prop_verdict(0x05UL, 0UL, NULL, 0, "xhcicm") == -1 &&
              inst_bp_prop_verdict(0UL, INST_REG_MULTI_SZ_TYPE, NULL, 0,
                                   "xhcicm") == -1,
              "the system disk's path: no value, a small buffer, errors");
        /* A whole REG_MULTI_SZ: a name matches whole, in any case. */
        CHECK(inst_bp_prop_verdict(0UL, INST_REG_MULTI_SZ_TYPE,
                                   "pcifilt\0xhcicm\0", 16, "xhcicm") == 1 &&
              inst_bp_prop_verdict(0UL, INST_REG_MULTI_SZ_TYPE,
                                   "XHCICM\0", 8, "xhcicm") == 1 &&
              inst_bp_prop_verdict(0UL, INST_REG_MULTI_SZ_TYPE,
                                   "xhcicmx\0", 9, "xhcicm") == 0 &&
              inst_bp_prop_verdict(0UL, INST_REG_MULTI_SZ_TYPE,
                                   "xhci", 4, "xhcicm") == 0 &&
              inst_bp_prop_verdict(0UL, 1UL, "xhcicm\0", 7, "xhcicm") ==
              -1, "the system disk's path: a filter value's names");
        CHECK(strstr(inst_cm_boot_warn_text(), "demand") != NULL &&
              strstr(inst_cm_boot_refuse_text(), "Windows 7") == NULL &&
              inst_text_width(inst_cm_boot_warn_text()) <= INST_COLUMNS &&
              inst_text_width(inst_cm_boot_refuse_text()) <= INST_COLUMNS,
              "the gate's words: the start type put back, no version, "
              "the width");

        /* Secure Boot's state (record 26 section 5.3.2; owner,
         * 2026-10-22): three answers, read on 8, 8.1 and 10 alone. */
        CHECK(inst_sb_state(1, INST_REG_DWORD_TYPE, 1UL) == INST_SB_ON &&
              inst_sb_state(1, INST_REG_DWORD_TYPE, 0UL) == INST_SB_OFF &&
              inst_sb_state(1, INST_REG_DWORD_TYPE, 2UL) == INST_SB_UNKNOWN &&
              inst_sb_state(1, 1UL, 1UL) == INST_SB_UNKNOWN &&
              inst_sb_state(0, INST_REG_DWORD_TYPE, 1UL) == INST_SB_UNKNOWN &&
              inst_sb_state(0, 0UL, 0UL) == INST_SB_UNKNOWN,
              "the state: on, off, not known");
        CHECK(inst_sb_applies(1, k8) && inst_sb_applies(1, k81) &&
              inst_sb_applies(1, INST_OS_CMONLY) &&
              !inst_sb_applies(1, k7) && !inst_sb_applies(1, kVista) &&
              !inst_sb_applies(1, kXp64) && !inst_sb_applies(0, k98) &&
              !inst_sb_applies(1, INST_OS_REFUSED),
              "read on 8, 8.1 and the kind (10) only");
        CHECK(strstr(inst_sb_text(INST_ARCH_AMD64, INST_SB_ON),
                     "turn Secure Boot off") != NULL &&
              strstr(inst_sb_text(INST_ARCH_AMD64, INST_SB_OFF),
                     "bcdedit /set testsigning on") != NULL &&
              inst_sb_text(INST_ARCH_AMD64, INST_SB_UNKNOWN) == NULL,
              "x64: the three sentences");
        CHECK(strstr(inst_sb_text(INST_ARCH_X86, INST_SB_ON),
                     "unsigned 32-bit drivers") != NULL &&
              strstr(inst_sb_text(INST_ARCH_X86, INST_SB_OFF),
                     "load as on Windows 7") != NULL &&
              inst_sb_text(INST_ARCH_X86, INST_SB_UNKNOWN) == NULL,
              "x86: the three sentences");
        CHECK(inst_ts_confirmed(1, INST_SB_OFF) &&
              inst_ts_confirmed(1, INST_SB_UNKNOWN) &&
              !inst_ts_confirmed(1, INST_SB_ON) &&
              !inst_ts_confirmed(0, INST_SB_OFF) &&
              !inst_ts_confirmed(0, INST_SB_ON),
              "Secure Boot on: TESTSIGNING never confirmed");
        /* Secure Boot on, TESTSIGNING in the options, 8, 8.1 and the kind
         * x64: not confirmed, so the connection manager is refused. */
        CHECK(inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k8, 1,
                                inst_ts_confirmed(1, INST_SB_ON)) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k81, 1,
                                inst_ts_confirmed(1, INST_SB_ON)) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, INST_OS_CMONLY, 1,
                                inst_ts_confirmed(1, INST_SB_ON)) ==
              INST_CMBOOT_REFUSE &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k81, 1,
                                inst_ts_confirmed(1, INST_SB_OFF)) ==
              INST_CMBOOT_WARN &&
              inst_cm_boot_gate(INST_ARCH_AMD64, 1, 6, k8, 1,
                                inst_ts_confirmed(1, INST_SB_UNKNOWN)) ==
              INST_CMBOOT_WARN,
              "the state through the gate, each system");
    }

    /* Record 26 section 5.3.9 (2026-11-04): over controllers xhci98 runs,
     * 8 to 10 take the USB drivers as an update, /cmonly leaving them out;
     * other systems and a fresh install as before. */
    CHECK(inst_usb_opt(INST_OS_CMONLY, 0, 0, 1) == 1 &&
          inst_usb_decide(INST_OS_CMONLY, 1, 1, 0,
                          inst_usb_opt(INST_OS_CMONLY, 0, 0, 1), &offered,
                          &why) == 1 && offered,
          "8 to 10 over installed USB drivers: in, the box ticked");
    CHECK(inst_usb_opt(INST_OS_CMONLY, 0, 1, 1) == 0 &&
          inst_usb_decide(INST_OS_CMONLY, 1, 1, 1,
                          inst_usb_opt(INST_OS_CMONLY, 0, 1, 1), &offered,
                          &why) == 0, "8 to 10 installed, /cmonly: out");
    CHECK(inst_usb_opt(INST_OS_CMONLY, 0, 0, 0) == 0 &&
          inst_usb_opt(INST_OS_CMONLY, 1, 0, 0) == 1 &&
          inst_usb_opt(INST_OS_FULL, 0, 0, 1) == 0,
          "8 to 10 fresh: out unless /usb; other systems unchanged");
    /* Merge review (2026-11-04): only a present controller xhci98 runs
     * counts; a phantom controller or a stale hub entry does not. */
    CHECK(inst_usb_runs_ctrl("xhci98", 1, 1) == 1 &&
          inst_usb_runs_ctrl("XHCI98", 1, 1) == 1,
          "a present controller on xhci98 counts");
    CHECK(inst_usb_runs_ctrl("xhci98", 0, 1) == 0,
          "a phantom controller on xhci98 does not count");
    CHECK(inst_usb_runs_ctrl("xhci98", 1, 0) == 0 &&
          inst_usb_runs_ctrl("xhci98", 0, 0) == 0,
          "a root hub or hub entry on xhci98 does not count");
    CHECK(inst_usb_runs_ctrl("USBXHCI", 1, 1) == 0 &&
          inst_usb_runs_ctrl(NULL, 1, 1) == 0,
          "a controller on Windows' driver does not count");
    /* The USB drivers (section 4.5). */
    /* Draft 20 (task 43.43): every box on every system. */
    CHECK(inst_usb_decide(INST_OS_FULL, 0, 1, 0, 0, &offered, &why) == 1 &&
          offered && why == NULL,
          "98 SE and ME: in, and offered (the connection manager alone)");
    CHECK(inst_usb_decide(INST_OS_FULL, 0, 1, 1, 0, &offered, &why) == 0 &&
          offered && why == NULL, "98 SE and ME: /cmonly accepted");
    CHECK(inst_usb_decide(INST_OS_FULL, 0, 0, 0, 0, &offered, &why) == 1 &&
          !offered, "98 SE and ME without the connection manager: required");
    CHECK(inst_usb_decide(INST_OS_FULL, 1, 1, 0, 0, &offered, &why) == 1 &&
          offered, "2000 to 7 with the connection manager: offered, in");
    CHECK(inst_usb_decide(INST_OS_FULL, 1, 1, 1, 0, &offered, &why) == 0 &&
          offered, "2000 to 7, /cmonly: out");
    CHECK(inst_usb_decide(INST_OS_FULL, 1, 0, 0, 0, &offered, &why) == 1 &&
          !offered, "2000 to 7 without the connection manager: required");
    CHECK(inst_usb_decide(INST_OS_FULL, 1, 0, 1, 0, &offered, &why) == -1 &&
          why != NULL, "/cmonly with no connection manager: refused");
    CHECK(inst_usb_decide(INST_OS_FULL, 1, 1, 0, 1, &offered, &why) == 1 &&
          offered, "/usb on 2000 to 7: changes nothing");
    CHECK(inst_usb_decide(INST_OS_FULL, 1, 1, 1, 1, &offered, &why) == -1 &&
          why != NULL, "/usb with /cmonly: refused");
    CHECK(inst_usb_decide(INST_OS_CMONLY, 1, 1, 0, 0, &offered, &why) == 0 &&
          offered, "8 and 8.1: out by default, offered");
    CHECK(inst_usb_decide(INST_OS_CMONLY, 1, 1, 0, 1, &offered, &why) == 1 &&
          offered, "8 and 8.1, /usb: in");
    CHECK(inst_usb_decide(INST_OS_CMONLY, 1, 1, 1, 0, &offered, &why) == 0 &&
          offered, "8 and 8.1, /cmonly: out");
    CHECK(inst_usb_decide(INST_OS_CMONLY, 1, 0, 0, 0, &offered, &why) == 0 &&
          offered && why == NULL,
          "8 and 8.1 with no connection manager: not refused (case 5)");
    CHECK(inst_usb_decide(INST_OS_CMONLY, 1, 0, 0, 1, &offered, &why) == 1,
          "8 and 8.1 with no connection manager, /usb: the USB drivers");
    CHECK(inst_usb_decide(INST_OS_CMONLY, 1, 0, 1, 0, &offered, &why) == -1,
          "8 and 8.1 with no connection manager, /cmonly: refused");
    CHECK(inst_usb_decide(INST_OS_REFUSED, 1, 1, 0, 0, &offered, &why) == -1,
          "a refused system");

    /* The warnings of record 26 section 5.3.1 (task 43.43). */
    {
        int sys;
        int box;
        int bad = 0;
        char text[2048];
        for (sys = 0; sys < 4 && !bad; sys++) {
            /* 0 98 SE, 1 ME, 2 2000 to 7, 3 8 and 8.1 */
            for (box = 0; box < 16 && !bad; box++) {
                int usb = (box & 1) != 0;
                int stor = (box & 2) != 0;
                int s98 = (box & 4) != 0;
                INST_U32 w = inst_comp_warnings(sys == 0, sys <= 1, sys == 3,
                                                1, usb, stor, s98, 1, 0);
                INST_U32 want = 0;
                if (stor && sys != 0) {
                    want |= INST_CW_STOR;
                }
                if (!usb && sys <= 1) {
                    want |= INST_CW_CMALONE;
                }
                if (usb && sys == 3) {
                    want |= INST_CW_USB8;
                }
                if (w != want) {
                    printf("warnings: system %d boxes %d: %lu\n", sys, box,
                           (unsigned long)w);
                    bad = 1;
                }
            }
        }
        CHECK(!bad, "the warnings: exactly the owner's four cases");
        CHECK(inst_comp_warnings(0, 0, 1, 0, 0, 0, 0, 0, 0) ==
              INST_CW_NOCM8 &&
              inst_comp_warnings(0, 0, 0, 0, 1, 0, 0, 0, 0) == 0,
              "8 and 8.1 without the connection manager: warned (case 5)");
        CHECK(inst_comp_warnings(0, 0, 1, 1, 0, 0, 0, 1, 1) ==
              INST_CW_INTELTB &&
              inst_comp_warnings(0, 0, 0, 1, 1, 0, 0, 1, 1) ==
              INST_CW_INTELTB &&
              inst_comp_warnings(0, 0, 1, 1, 0, 0, 0, 0, 1) == 0 &&
              inst_comp_warnings(0, 0, 1, 1, 0, 0, 0, 1, 0) == 0 &&
              inst_comp_warnings(0, 0, 1, 0, 0, 0, 0, 1, 1) ==
              INST_CW_NOCM8,
              "over Intel's Thunderbolt driver: warned only when ticked");
        inst_comp_warn_text(0, "C:\\XHCI98", text, sizeof(text));
        CHECK(text[0] == '\0', "no warning: no text");
        inst_comp_warn_text(INST_CW_STOR | INST_CW_CMALONE | INST_CW_USB8 |
                            INST_CW_NOCM8, "C:\\XHCI98", text,
                            sizeof(text));
        CHECK(strstr(text, "C:\\XHCI98\\USBDRV\\NUSB33") != NULL &&
              strstr(text, "connection manager will be installed") != NULL &&
              strstr(text, "On Windows 8, 8.1 and 10, the xHCI98 USB drivers "
                     "replace Windows' own USB 3 driver on all xHCI "
                     "controllers") != NULL &&
              strstr(text, "may need a restart and a second run") == NULL &&
              strstr(text, "Uninstalling gives the controllers back") != NULL &&
              strstr(text, "tested") == NULL &&
              strstr(text, "UAS disks use Windows' own UAS driver.") !=
                  NULL &&
              strstr(text, "staged") == NULL &&
              strstr(text, "xhciuas") == NULL &&
              strstr(text, "only staged") == NULL &&
              strstr(text, "Stor98 has not been tested") == NULL &&
              strlen(text) < sizeof(text) - 1,
              "every warning, whole, in one text");
    }

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
    /* /usb (task 43.43): an install's, and not with /cmonly. */
    argv[1] = "/usb";
    CHECK(inst_parse_args(2, argv, &o, err, sizeof(err)) == 0 && o.Usb,
          "/usb parsed");
    argv[2] = "/cmonly";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) != 0,
          "/usb /cmonly refused");
    argv[2] = "/uninstall";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) != 0,
          "/usb /uninstall refused");
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
    /* And the name table's controllers by id (task 44.5). */
    CHECK(has_id(&ids, "PCI\\VEN_1B36&DEV_000D"), "98: QEMU's controller by id");
    CHECK_EQ(ids.Count, 5 + instXhciNameCount, "98: five ids and the table's");
    e = env(1, 5, 0, INST_ARCH_X86);
    CHECK(inst_inf_models(x86, &e, &ids) &&
          ids.Count == 5 + instXhciNameCount, "2000: the same section");
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
     * default the table's meaning assumes (task 45.54: docs\using\
     * tuning.md's "Written by the install" column, a range row's within
     * its range and on its step) - but XhciResumeKeepBot, which neither
     * writes (design record 20 section 4.3.4, M18): its default is the
     * driver's for each Windows, 1 on 2000 alone. */
    {
        static const struct {
            const char *Name;
            INST_U32 Def;
        } want[] = {
            { "XhciIntelPortSwitch", 0 }, { "XhciInterruptMode", 0 },
            { "XhciLinkKeep", 1 }, { "XhciSsFlapFallback", 1 },
            { "XhciResumeKeep", 1 },
            { "XhciUsb2Bos", 1 }, { "XhciImodInterval250ns", 160 },
            { "XhciLogVerbosity", 2 }, { "XhciLogDebugView", 0 },
            { "XhciPollIdleMs", 50 }, { "XhciPollActiveMs", 1 },
            { "XhciPollFineTimer", 0 }, { "XhciSelectiveSuspend", 0 },
            { "XhciSilentInstall", 0 }, { "XhciForceBulkOnly", 0 },
            { "XhciBulkOnlyOwnId", 1 },
            { "XhciFastPollFsLs", 0 }, { "XhciMaxTransferKB", 1024 },
            { "XhciWideMap", 0 }, { "XhciFirstEnumWaitMs", 5000 },
            { "XhciFirstEnumPortMs", 2000 },
            /* Task 49.38: every other value, in groups. */
            { "XhciIntelIrqRoute", 1 }, { "XhciIntelNoRouteIrq", 1 },
            { "XhciMsiStrategy", 0 }, { "XhciMsiIsaIrq", 0 },
            { "XhciMsiShare", 8 }, { "XhciMsiPools", 0 },
            { "XhciMsiInputFirst", 0 }, { "XhciMsiInputLast", 0 },
            { "XhciMsiInputBudget", 0 }, { "XhciDmaStaging", 0 },
            { "XhciStagePoolKiB", 2048 }, { "XhciEventRingTrbs", 256 },
            { "XhciTolerance", 1 },
            { "XhciCommandCheck", 1 }, { "XhciStreamDequeueRepair", 1 },
            { "XhciAccept096", 1 },
            { "XhciAcpiTables", 1 }, { "XhciIntervalCap", 1 },
            { "XhciAvgTrbEsit", 0 }, { "XhciUsb2HubSpeedFallback", 1 },
            { "XhciMsiAvoid", 1 }, { "XhciStreamsOff", 1 },
            { "XhciResumeReinit", 1 }, { "XhciIntelShutdownToEhci", 1 },
            { "XhciShutdownReset", 0 }, { "XhciSuspendHaltDelay", 1 },
            { "XhciSuspendHaltTimeout", 1 }, { "XhciAsmediaFlowControl", 1 },
            { "XhciTiIntervalCap", 1 }, { "XhciStuckLinkReset", 1 },
            { "XhciStreamRingPad", 1 }, { "XhciControlLinkNoOp", 1 },
            { "XhciIsochSpuriousSuccess", 1 }, { "XhciAmdSuspendBitClear", 1 }
        };
        int rowsSeen = 0;
        for (i = 0; i < instValueCount; i++) {
            INST_U32 d86;
            INST_U32 d64;
            int w;
            if (instValues[i].Inf != INST_INF_XHCI) {
                continue;
            }
            if (inst_eq(instValues[i].Name, "XhciResumeKeepBot")) {
                INST_U32 d;
                CHECK(instValues[i].NoInf &&
                          !inst_inf_addreg_default(x86, instValues[i].Name,
                                                   &d86) &&
                          !inst_inf_addreg_default(a64, instValues[i].Name,
                                                   &d64),
                      "XhciResumeKeepBot: NoInf, written by neither INF");
                e = env(1, 5, 0, INST_ARCH_X86);
                CHECK(inst_keepbot_default(&e) == 1 &&
                          inst_value_default_env(i, x86, &e, &d) && d == 1,
                      "XhciResumeKeepBot: 1 on 2000");
                e = env(1, 5, 1, INST_ARCH_X86);
                CHECK(inst_value_default_env(i, x86, &e, &d) && d == 0,
                      "XhciResumeKeepBot: 0 on XP");
                e = env(1, 5, 2, INST_ARCH_AMD64);
                CHECK(inst_value_default_env(i, a64, &e, &d) && d == 0,
                      "XhciResumeKeepBot: 0 on XP x64");
                e = env(1, 6, 1, INST_ARCH_X86);
                CHECK(inst_value_default_env(i, x86, &e, &d) && d == 0,
                      "XhciResumeKeepBot: 0 on 7");
                e = env(0, 4, 90, INST_ARCH_X86);
                CHECK(inst_value_default_env(i, x86, &e, &d) && d == 0,
                      "XhciResumeKeepBot: 0 on ME");
                CHECK(instValues[i].Asked == 0x3UL &&
                          instValues[i].Settable == 0xFUL &&
                          instValues[i].Apply == INST_APPLY_NT_XHCI,
                      "XhciResumeKeepBot: 0 or 1 asked on 2000 and later, "
                      "0 to 3 by /set");
                {
                    char q[2048];
                    INST_VNOW nv[1];
                    INST_U32 en;
                    nv[0].State = 0;
                    nv[0].Value = 0;
                    nv[0].Desc = "";
                    inst_value_question(q, sizeof(q), i, 0x3UL, 1UL, nv, 1,
                                        0, 0, &en);
                    CHECK(en == 1 &&
                              strstr(q, "not set, the driver uses 1") != NULL &&
                              strstr(q, "the install writes") == NULL,
                          "XhciResumeKeepBot unset: the driver's default, "
                          "not an install's");
                }
                {
                    /* M19: Setup writes it itself where nothing is set. */
                    INST_VNOW sv[2];
                    sv[0].State = 0;
                    sv[0].Value = 0;
                    sv[0].Desc = "";
                    sv[1] = sv[0];
                    sv[1].State = -1;
                    CHECK(inst_keepbot_seed(1, 2, 0, 0, sv, 2),
                          "seed: NT, unset everywhere, unanswered");
                    CHECK(!inst_keepbot_seed(0, 2, 0, 0, sv, 2),
                          "no seed on 98 SE and ME");
                    CHECK(!inst_keepbot_seed(1, 0, 0, 0, sv, 0),
                          "no seed without an xhci98 controller");
                    CHECK(!inst_keepbot_seed(1, 2, 1, 0, sv, 2),
                          "no seed over a user's answer (the engine asks "
                          "it with answered 0 for an answer equal to the "
                          "default)");
                    CHECK(!inst_keepbot_seed(1, 2, 0, 1, sv, 2),
                          "an update keeps a machine-wide value");
                    CHECK(!inst_keepbot_seed(1, 2, 0, -1, sv, 2),
                          "a machine-wide value not a number is kept");
                    sv[1].State = 1;
                    sv[1].Value = 0;
                    CHECK(!inst_keepbot_seed(1, 2, 0, 0, sv, 2),
                          "an update keeps a controller's own value");
                }
                {
#define TEST_NOW_MANY 20  /* past inst_eng.c's INST_MAX_NOW, 16 */
                    /* M19's second test: every controller, present or not,
                     * past the presentation list's cap. */
                    static INST_VNOW all[TEST_NOW_MANY];
                    int k;
                    for (k = 0; k < TEST_NOW_MANY; k++) {
                        all[k].State = 0;
                        all[k].Value = 0;
                        all[k].Desc = "";
                    }
                    CHECK(inst_keepbot_seed_all(1, all, TEST_NOW_MANY),
                          "seed_all: none holds it, more than the cap");
                    all[TEST_NOW_MANY - 2].State = 1;    /* past the cap */
                    CHECK(!inst_keepbot_seed_all(1, all, TEST_NOW_MANY),
                          "a controller past the cap holding it");
                    all[TEST_NOW_MANY - 2].State = 0;
                    all[1].State = 1;   /* an unplugged dock's explicit 0 */
                    all[1].Value = 0;
                    CHECK(!inst_keepbot_seed_all(1, all, 2),
                          "an absent controller's explicit 0 on 2000 kept");
                    all[1].State = -1;
                    CHECK(!inst_keepbot_seed_all(1, all, 2),
                          "an unreadable key: no seed");
                    all[1].State = 0;
                    CHECK(!inst_keepbot_seed_all(0, all, 2),
                          "a device list not whole: no seed");
                    CHECK(inst_keepbot_seed_all(1, all, 0),
                          "no xhci98 controller key at all: nothing holds it");
                    /* Task 49.47 (owner, 2026-11-14): Setup proposes the
                     * port switchover at a first install on a listed
                     * board, and only there. */
                    CHECK(inst_psw_propose(1, 0, 1, all, 2),
                          "psw: a first install on a listed board proposes 1");
                    CHECK(inst_psw_propose(1, 0, 1, all, 0),
                          "psw: no xhci98 key yet (a first install) proposes 1");
                    CHECK(!inst_psw_propose(0, 0, 1, all, 2),
                          "psw: no listed controller installed now: never");
                    CHECK(!inst_psw_propose(1, 1, 1, all, 2),
                          "psw: a machine-wide value set (0, 1 or 2) is kept");
                    CHECK(!inst_psw_propose(1, -1, 1, all, 2),
                          "psw: a machine-wide value not a number is kept");
                    CHECK(!inst_psw_propose(1, 0, 0, all, 2),
                          "psw: a device list not whole: no proposal");
                    all[1].State = 1;
                    all[1].Value = 0;
                    CHECK(!inst_psw_propose(1, 0, 1, all, 2),
                          "psw: an update with 0 keeps 0");
                    all[1].Value = 1;
                    CHECK(!inst_psw_propose(1, 0, 1, all, 2),
                          "psw: an update with 1 keeps 1 (nothing written)");
                    all[1].Value = 2;
                    CHECK(!inst_psw_propose(1, 0, 1, all, 2),
                          "psw: an update with 2 keeps 2");
                    all[1].State = -1;
                    CHECK(!inst_psw_propose(1, 0, 1, all, 2),
                          "psw: a key that cannot be read: no proposal");
                    all[1].State = 0;
                    all[1].Value = 0;
                    /* The proposal is written only after a listed
                     * controller was installed (task 49.47). */
                    CHECK(inst_psw_counts_nt(1, 1, 1, 0),
                          "psw nt: a listed controller started counts");
                    CHECK(inst_psw_counts_nt(1, 1, 0, 1),
                          "psw nt: installed, restart pending counts");
                    CHECK(!inst_psw_counts_nt(1, 1, 0, 0),
                          "psw nt: a failed install (a failed takeover) does not");
                    CHECK(!inst_psw_counts_nt(1, 0, 0, 1),
                          "psw nt: another driver still on it does not");
                    CHECK(!inst_psw_counts_nt(0, 1, 1, 0),
                          "psw nt: an unlisted controller does not");
                    CHECK(inst_psw_counts_9x(1, 1, 0, 0) &&
                          inst_psw_counts_9x(1, 0, 1, 0) &&
                          inst_psw_counts_9x(1, 0, 0, 1),
                          "psw 9x: started, swapped or restart pending counts");
                    CHECK(!inst_psw_counts_9x(1, 0, 0, 0),
                          "psw 9x: a failed or refused helper install does not");
                    CHECK(!inst_psw_counts_9x(0, 1, 0, 0),
                          "psw 9x: an unlisted controller does not");
                    CHECK(inst_psw_write_now(1, 1, 0, 0, 0, 0),
                          "psw: proposed and a listed controller installed: written");
                    CHECK(!inst_psw_write_now(1, 0, 0, 0, 0, 0),
                          "psw: no listed controller installed: nothing written");
                    CHECK(!inst_psw_write_now(0, 1, 0, 0, 0, 0),
                          "psw: not proposed: nothing written");
                    CHECK(!inst_psw_write_now(1, 1, 1, 0, 0, 0),
                          "psw: an answer is written as answers are, not here");
                    CHECK(!inst_psw_write_now(1, 1, 0, 1, 0, 0),
                          "psw: /list writes nothing");
                    CHECK(!inst_psw_write_now(1, 1, 0, 0, 1, 0),
                          "psw: a cancelled run writes nothing");
                    CHECK(!inst_psw_write_now(1, 1, 0, 0, 0, 1),
                          "psw: a listed controller installed, then a takeover "
                          "stopped at its failure: nothing written");
                }
                e = env(1, 5, 1, INST_ARCH_X86);
                CHECK(inst_value_default_env(inst_value_find("XhciResumeKeep"),
                                             x86, &e, &d) && d == 1,
                      "every other value: inst_value_default's");
                continue;
            }
            rowsSeen++;
            CHECK(inst_inf_addreg_default(x86, instValues[i].Name, &d86),
                  instValues[i].Name);
            if (instValues[i].Apply == INST_APPLY_NT5X86_XHCI) {
                /* Mode 4's input pool: the x86 image's alone. */
                CHECK(!inst_inf_addreg_default(a64, instValues[i].Name,
                                               &d64),
                      "not in the amd64 INF");
            } else {
                CHECK(inst_inf_addreg_default(a64, instValues[i].Name,
                                              &d64),
                      instValues[i].Name);
                CHECK_EQ(d86, d64, instValues[i].Name);
            }
            for (w = 0; w < (int)(sizeof(want) / sizeof(want[0])); w++) {
                if (inst_eq(want[w].Name, instValues[i].Name)) {
                    break;
                }
            }
            CHECK(w < (int)(sizeof(want) / sizeof(want[0])) &&
                  want[w].Def == d86, instValues[i].Name);
            if (instValues[i].Hi != 0) {
                CHECK(instValues[i].Asked == 0 && instValues[i].Settable == 0 &&
                      inst_value_in_range(i, d86),
                      "a range row's default is in its range");
                continue;
            }
            CHECK(d86 < 32 && (instValues[i].Asked == 0 ||
                               (instValues[i].Asked & (1UL << d86)) != 0),
                  "the default is an offered answer");
            CHECK(d86 < 32 && (instValues[i].Settable & (1UL << d86)) != 0,
                  "the default is a value /set takes");
        }
        CHECK_EQ(rowsSeen, (int)(sizeof(want) / sizeof(want[0])),
                 "every xhci98 row has its expected default");
    }

    free(x86);
    free(a64);
    free(u86);
    free(u64);
}

/* Stor98's two INFs (roadmap-usb4 43.59, owner 2026-10-23): on 2000 to 8
 * and 98 SE and ME the models match every NVMe controller with
 * PCI\CC_010802 as the INF's hardware id; on 8.1 and 10 (NT 6.3 and later)
 * the same id as its compatible id, the hardware id empty, which Windows'
 * own stornvme (the same id as its hardware id) outranks, so Stor98 goes
 * only where the installer puts it - and the installer, reading the same
 * models, still finds an NVMe controller (and no other 01:08 device). */
static int s98_model_fields(const char *inf, const char *section, char *hw,
                            char *compat)
{
    char line[512];
    char key[512];
    char value[512];
    const char *p = inst_inf_section(inf, section);

    if (p == NULL || (p = inst_inf_line(p, line, sizeof(line))) == NULL ||
        !inst_inf_kv(line, key, sizeof(key), value, sizeof(value)) ||
        !inst_inf_field(value, 1, hw, 128)) {
        return 0;
    }
    compat[0] = '\0';
    (void)inst_inf_field(value, 2, compat, 128);
    return 1;
}

static void test_stor98_infs(void)
{
    static const char nvmeHw[] =
        "PCI\\VEN_1B36&DEV_0010&SUBSYS_11001AF4&REV_02\0"
        "PCI\\VEN_1B36&DEV_0010&SUBSYS_11001AF4\0\0";
    static const char nvmeCompat[] =
        "PCI\\VEN_1B36&DEV_0010&REV_02\0PCI\\VEN_1B36&DEV_0010\0"
        "PCI\\VEN_1B36&CC_010802\0PCI\\VEN_1B36&CC_0108\0PCI\\VEN_1B36\0"
        "PCI\\CC_010802\0PCI\\CC_0108\0\0";
    static const char tunCompat[] =
        "PCI\\XHCI98_TUNNEL&CC_010802\0"
        "PCI\\VEN_1B36&DEV_0010&REV_02\0PCI\\VEN_1B36&DEV_0010\0"
        "PCI\\VEN_1B36&CC_010802\0PCI\\VEN_1B36&CC_0108\0PCI\\VEN_1B36\0"
        "PCI\\CC_010802\0PCI\\CC_0108\0\0";
    char *x86 = load("..\\src\\stor98\\inf\\stor98-x86.inf");
    char *a64 = load("..\\src\\stor98\\inf\\stor98-x64.inf");
    char *w9x = load("..\\src\\stor98\\inf\\stor98-9x.inf");
    INST_ENV e;
    INST_IDLIST ids;
    int sys;
    static const struct {
        int Nt;
        INST_U32 Major;
        INST_U32 Minor;
        int Arch;
        const char *What;
    } systems[] = {
        { 0, 4, 10, INST_ARCH_X86, "98 SE" },
        { 1, 5, 0, INST_ARCH_X86, "2000" },
        { 1, 6, 2, INST_ARCH_X86, "8 x86" },
        { 1, 6, 2, INST_ARCH_AMD64, "8 x64" },
        { 1, 6, 3, INST_ARCH_X86, "8.1 x86" },
        { 1, 6, 3, INST_ARCH_AMD64, "8.1 x64" },
        { 1, 10, 0, INST_ARCH_X86, "10 x86" },
        { 1, 10, 0, INST_ARCH_AMD64, "10 x64" }
    };

    CHECK(x86 != NULL && a64 != NULL && w9x != NULL, "Stor98's three INFs read");
    if (x86 == NULL || a64 == NULL || w9x == NULL) {
        free(x86);
        free(a64);
        free(w9x);
        return;
    }
    CHECK(inst_inf_arch_ok(x86, INST_ARCH_X86), "stor98-x86.inf is x86");
    CHECK(inst_inf_arch_ok(a64, INST_ARCH_AMD64), "stor98-x64.inf is amd64");
    /* Record 31 sections 4.7, 4.8 and 5.2 (owner, 2026-10-25): stor98.inf
     * the tunnel id alone on 2000 to 8 (Windows 2000 reads the undecorated
     * section) and since section 4.10 (task 48.29) on 8.1 too, the class as
     * the compatible id on 10, as 3.2.0.0;
     * stor98w9.inf, which 98 SE and ME install from, the id and the class.
     * The installer finds its candidates by class (section 7.2), a tagged
     * drive by the INF's id too. */
    for (sys = 0; sys < (int)(sizeof(systems) / sizeof(systems[0])); sys++) {
        char what[96];
        int undecorated = !systems[sys].Nt;
        /* The class model: 10 alone (section 4.10). */
        int nt63 = systems[sys].Nt && systems[sys].Major >= 10;
        e = env(systems[sys].Nt, systems[sys].Major, systems[sys].Minor,
                systems[sys].Arch);
        sprintf(what, "%s: the models' ids", systems[sys].What);
        CHECK(inst_inf_models(systems[sys].Arch != INST_ARCH_X86 ? a64 :
                              undecorated ? w9x : x86, &e, &ids) &&
              ids.Count == (undecorated ? 2 : 1) &&
              has_id(&ids, INST_S98_TUNNEL_ID) == !nt63 &&
              has_id(&ids, INST_S98_CLASS_ID) == (undecorated || nt63), what);
        sprintf(what, "%s: a plain NVMe drive matches only a class model",
                systems[sys].What);
        CHECK((inst_ids_match(nvmeHw, sizeof(nvmeHw), nvmeCompat,
                              sizeof(nvmeCompat), &ids) >= 0) ==
              (undecorated || nt63), what);
        sprintf(what, "%s: a tagged drive matches the model", systems[sys].What);
        CHECK(inst_ids_match(nvmeHw, sizeof(nvmeHw), tunCompat,
                             sizeof(tunCompat), &ids) >= 0, what);
        sprintf(what, "%s: a plain drive is still a candidate", systems[sys].What);
        CHECK(inst_s98_candidate(nvmeHw, sizeof(nvmeHw), nvmeCompat,
                                 sizeof(nvmeCompat), &ids), what);
    }
    {
        char hw[128];
        char cp[128];
        CHECK(s98_model_fields(w9x, "Stor98_Models", hw, cp) &&
              inst_eq(hw, INST_S98_TUNNEL_ID) && inst_eq(cp, INST_S98_CLASS_ID),
              "98 SE and ME models: the id, and the class as its compatible id");
        CHECK(s98_model_fields(x86, "Stor98_Models", hw, cp) &&
              inst_eq(hw, INST_S98_TUNNEL_ID) && cp[0] == '\0',
              "x86 models before 6.3 (2000 too): the id as the hardware id");
        CHECK(s98_model_fields(x86, "Stor98_Models.NTx86.6.3", hw, cp) &&
              inst_eq(hw, INST_S98_TUNNEL_ID) && cp[0] == '\0',
              "x86 6.3 models: the id as the hardware id (the kept child)");
        CHECK(s98_model_fields(x86, "Stor98_Models.NTx86.10.0", hw, cp) &&
              hw[0] == '\0' && inst_eq(cp, INST_S98_CLASS_ID),
              "x86 10.0 models: the class as the compatible id, as 3.2.0.0");
        CHECK(s98_model_fields(a64, "Stor98_Models.NTamd64", hw, cp) &&
              inst_eq(hw, INST_S98_TUNNEL_ID) && cp[0] == '\0',
              "x64 models before 6.3: the id as the hardware id");
        CHECK(s98_model_fields(a64, "Stor98_Models.NTamd64.6.3", hw, cp) &&
              inst_eq(hw, INST_S98_TUNNEL_ID) && cp[0] == '\0',
              "x64 6.3 models: the id as the hardware id (the kept child)");
        CHECK(s98_model_fields(a64, "Stor98_Models.NTamd64.10.0", hw, cp) &&
              hw[0] == '\0' && inst_eq(cp, INST_S98_CLASS_ID),
              "x64 10.0 models: the class as the compatible id, as 3.2.0.0");
    }
    /* Record 31 section 7.1: the bundled INFs are this project's Stor98
     * INF by the identity test the store removal and the 98 SE sweep
     * make. */
    CHECK(inst_s98_inf_identity(x86), "stor98-x86.inf: the identity test");
    CHECK(inst_s98_inf_identity(a64), "stor98-x64.inf: the identity test");
    CHECK(inst_s98_inf_identity(w9x), "stor98-9x.inf: the identity test");
    free(x86);
    free(a64);
    free(w9x);
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
        if (instValues[i].Kind == INST_VK_TEXT) {
            /* Task 49.38: XhciCmTunnelBridge, a string both INFs write
             * as auto. */
            char t[INST_TEXT_CHARS];
            CHECK(inst_inf_addreg_text(cm, instValues[i].Name, t,
                                       sizeof(t)) &&
                      strcmp(t, "auto") == 0 &&
                      inst_inf_addreg_text(c64, instValues[i].Name, t,
                                           sizeof(t)) &&
                      strcmp(t, "auto") == 0,
                  "the text row: both INFs write auto");
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
        } else if (instValues[i].Apply == INST_APPLY_9X_CM) {
            /* Task 44.46: written by the x86 INF's 98 SE and ME install;
             * the amd64 INF, with no such install, does not write it and
             * the table's default, the x86 INF's, stands for it. */
            CHECK(inst_inf_addreg_default(cm, instValues[i].Name, &d) &&
                      d == instValues[i].Def,
                  "a 98 SE and ME cm value: the x86 INF writes the "
                  "table's default");
            CHECK(!inst_inf_addreg_default(c64, instValues[i].Name, &d64),
                  "a 98 SE and ME cm value: the amd64 INF does not write it");
            CHECK(inst_value_default(i, c64, &d64) && d64 == d,
                  "a 98 SE and ME cm value: the same default for the amd64 "
                  "INF");
        } else {
            CHECK(inst_inf_addreg_default(cm, instValues[i].Name, &d),
                  instValues[i].Name);
            CHECK(inst_inf_addreg_default(c64, instValues[i].Name, &d64) &&
                      d64 == d, "the amd64 INF writes the same default");
            CHECK(!inst_value_default(i, NULL, &d64),
                  "an INF-written value has no default without its INF");
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

/* Record 23 section 5.3.1 (roadmap-usb4 task 48.19): very long hardware
 * and compatible id lists and Driver values. */
static void test_long_ids(void)
{
    static const char arr[] = "PCI\\CC_0C0330\0";
    static const char cut[] = "USB\\VID_1234&PID_5678&REV_0001&MI_00";
    char *big;
    char *path;
    char drv[INST_DRIVER_CHARS + 2];
    size_t n;
    size_t len;
    size_t nb;
    const char *p;
    int i;

    /* The accessor: the array when there is no copy, the copy when there
     * is one, each with its own byte count. */
    p = inst_ids_pick(arr, sizeof(arr), NULL, 0, &n);
    CHECK(p == arr && n == sizeof(arr), "no copy: the array");
    p = inst_ids_pick("", 0, arr, sizeof(arr), &n);
    CHECK(p == arr && n == sizeof(arr), "a copy: the copy, whatever the array");

    /* 98 SE's comma string, longer than the record's 2,048 bytes, split
     * once at its final length in a buffer of that length and two bytes. */
    big = (char *)malloc(4096 + 64);
    CHECK(big != NULL, "memory for the long list");
    if (big != NULL) {
        big[0] = '\0';
        for (i = 0; strlen(big) + sizeof(cut) + 8 < 4096; i++) {
            if (i > 0) {
                strcat(big, ",");
            }
            sprintf(big + strlen(big), "%s%03d", cut, i);
        }
        strcat(big, ",PCI\\CC_0C0330");
        len = strlen(big);
        CHECK(len > 2048, "the list is longer than the record's array");
        nb = inst_ids_split(big, len + 2);
        CHECK_EQ(nb, len + 2, "split at its final length");
        CHECK(inst_msz_has(big, nb, "PCI\\CC_0C0330"), "its last id read");
        CHECK(inst_msz_has(big, nb, "USB\\VID_1234&PID_5678&REV_0001&MI_00000"),
              "its first id read");
        free(big);
    }

    /* The configuration manager's answer, longer than the array, held
     * whole: several strings, a comma list among them, every id read. */
    big = (char *)malloc(4096 + 64);
    CHECK(big != NULL, "memory for the CM's list");
    if (big != NULL) {
        memset(big, 0, 4096 + 64);
        n = 0;
        for (i = 0; n + sizeof(cut) + 8 < 4000; i++) {
            sprintf(big + n, "%s%03d", cut, i);
            n += strlen(big + n) + 1;
        }
        strcpy(big + n, "PCI\\VEN_1B36&DEV_000D,PCI\\CC_0C0330");
        n += strlen(big + n) + 2;
        CHECK(n > 2048, "the CM's list is longer than the record's array");
        CHECK_EQ(inst_ids_commas(big, n), n, "its length kept");
        CHECK(inst_msz_has(big, n, "PCI\\CC_0C0330"),
              "an id after the first string, in a comma list");
        CHECK(inst_msz_has(big, n, "PCI\\VEN_1B36&DEV_000D"),
              "the comma list's first id");
        CHECK(inst_msz_has(big, n, "USB\\VID_1234&PID_5678&REV_0001&MI_00000"),
              "the first string");
        free(big);
    }

    /* The type of a list read again at its size: NT a multi-string, 98
     * SE and ME a string too; anything else is unreadable. */
    CHECK(inst_cm_ids_type_ok(1, 7UL), "NT: REG_MULTI_SZ");
    CHECK(!inst_cm_ids_type_ok(1, 1UL), "NT: REG_SZ refused");
    CHECK(!inst_cm_ids_type_ok(1, 3UL), "NT: REG_BINARY refused");
    CHECK(inst_cm_ids_type_ok(0, 1UL) && inst_cm_ids_type_ok(0, 7UL),
          "98: REG_SZ or REG_MULTI_SZ");
    CHECK(!inst_cm_ids_type_ok(0, 3UL) && !inst_cm_ids_type_ok(0, 4UL),
          "98: REG_BINARY, REG_DWORD refused");

    /* 98 SE and ME: the registry decides; absent from it, a list too big
     * whose ask again failed is unreadable, any other answer kept as
     * before (read, or no list: the root devnode's, read 2026-11-09). */
    CHECK(inst_ids9x_ok(1, 1, INST_CR_BUFFER_SMALL), "registry read: read");
    CHECK(!inst_ids9x_ok(-1, 1, INST_CR_SUCCESS), "registry unreadable");
    CHECK(inst_ids9x_ok(0, 0, 0xFFFFFFFFUL), "absent, no devnode: absent");
    CHECK(inst_ids9x_ok(0, 1, INST_CR_SUCCESS), "absent: the CM's list");
    CHECK(inst_ids9x_ok(0, 1, INST_CR_NO_SUCH_VALUE), "absent both");
    CHECK(!inst_ids9x_ok(0, 1, INST_CR_BUFFER_SMALL),
          "absent, the CM's list too big to read again: unreadable");
    CHECK(inst_ids9x_ok(0, 1, 0x13UL), "absent, another CM answer: no list");

    /* The driver key path: 511 characters fit a buffer of
     * INST_DRIVER_PATH_CHARS on both families; 512 do not, nor an empty
     * value, nor a buffer one character short (out is then ""). */
    path = (char *)malloc(INST_DRIVER_PATH_CHARS);
    CHECK(path != NULL, "memory for the path");
    if (path != NULL) {
        memset(drv, 'A', INST_DRIVER_CHARS);
        drv[38] = '\\';
        drv[INST_DRIVER_CHARS] = '\0';
        CHECK(inst_driver_key_path(path, INST_DRIVER_PATH_CHARS, 0, drv) &&
              strlen(path) == strlen(INST_CLASS_ROOT_9X) + 1 + INST_DRIVER_CHARS &&
              strncmp(path, INST_CLASS_ROOT_9X "\\", strlen(INST_CLASS_ROOT_9X) + 1) == 0,
              "98: 511 characters fit");
        CHECK(inst_driver_key_path(path, INST_DRIVER_PATH_CHARS, 1, drv) &&
              strlen(path) == strlen(INST_CLASS_ROOT_NT) + 1 + INST_DRIVER_CHARS,
              "NT: 511 characters fit");
        CHECK(!inst_driver_key_path(path, INST_DRIVER_PATH_CHARS - 1, 0, drv) &&
              path[0] == '\0', "a buffer one short: refused, empty");
        drv[INST_DRIVER_CHARS] = 'A';
        drv[INST_DRIVER_CHARS + 1] = '\0';
        CHECK(!inst_driver_key_path(path, INST_DRIVER_PATH_CHARS, 1, drv) &&
              path[0] == '\0', "512 characters: refused");
        CHECK(!inst_driver_key_path(path, INST_DRIVER_PATH_CHARS, 1, ""),
              "no Driver: no path");
        CHECK(inst_driver_key_path(path, INST_DRIVER_PATH_CHARS, 1,
                                   "{36FC9E60-C465-11CF-8056-444553540000}\\0001") &&
              strcmp(path, "SYSTEM\\CurrentControlSet\\Control\\Class\\"
                     "{36FC9E60-C465-11CF-8056-444553540000}\\0001") == 0,
              "an ordinary driver key");
        free(path);
    }
    CHECK(INST_DRIVER_BYTES >= INST_DRIVER_CHARS + 1 + 3,
          "the record's Driver holds 511 characters and the readers' spare");
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

    /* Stor98 took the package's version at 3.1.0.0 (task p42-s98ver): the
     * bundled stor98.inf says 3.1.0.0, and a Stor98 0.4.0.0 that a 3.0.0.0
     * package installed is older, so it is updated, never refused. */
    {
        static const char s98New[] =
            "[Version]\r\nSignature=\"$CHICAGO$\"\r\n"
            "DriverVer=10/19/2026,3.1.0.0\r\n";
        static const char s98Old[] =
            "[Version]\r\nSignature=\"$CHICAGO$\"\r\n"
            "DriverVer=10/16/2026,0.4.0.0\r\n";
        INST_U32 bundled[4];
        INST_U32 old[4];

        CHECK(inst_inf_driverver(s98New, bundled) &&
              bundled[0] == 3 && bundled[1] == 1 && bundled[2] == 0 &&
              bundled[3] == 0, "the bundled stor98.inf reads 3.1.0.0");
        CHECK(inst_inf_driverver(s98Old, old) && inst_ver_cmp(old, bundled) < 0,
              "Stor98 0.4.0.0 is older than the bundled 3.1.0.0");
        memset(&d, 0, sizeof(d));
        d.HasService = 1;
        d.OwnService = 1;
        d.VersionState = INST_VS_HAVE;
        memcpy(d.Version, old, sizeof(old));
        CHECK_EQ(inst_action(&d, bundled), INST_ACT_UPDATE,
                 "a drive on Stor98 0.4.0.0: updated to 3.1.0.0");
        CHECK(inst_action_runs(INST_ACT_UPDATE, 0), "that update runs");
        /* The group: no newer devnode, the installed stor98.sys or
         * stor98.mpd (file version 0.4.0.0) older than 3.1.0.0. */
        CHECK_EQ(inst_group_verdict(0, 0, inst_ver_cmp(old, bundled), 0),
                 INST_GROUP_OK, "Stor98 0.4.0.0 installed: not refused");
        inst_ver_parse("0.3.0.1", d.Version);
        CHECK_EQ(inst_action(&d, bundled), INST_ACT_UPDATE,
                 "Stor98 0.3.0.1: updated to 3.1.0.0");
        memcpy(d.Version, bundled, sizeof(bundled));
        CHECK_EQ(inst_action(&d, bundled), INST_ACT_CURRENT,
                 "Stor98 3.1.0.0 installed: current");
        CHECK_EQ(inst_group_verdict(0, 0, 0, 0), INST_GROUP_OK,
                 "Stor98 3.1.0.0 installed: the group runs");
        inst_ver_parse("3.2.0.0", d.Version);
        CHECK_EQ(inst_action(&d, bundled), INST_ACT_NEWER,
                 "Stor98 3.2.0.0 installed: newer, left");
        CHECK_EQ(inst_group_verdict(1, 0, 1, 0), INST_GROUP_REFUSED_NEWER,
                 "Stor98 3.2.0.0 installed: refused without /force");
        /* Ownership does not depend on a version: a Stor98 there before
         * xhci98's installer (PreExisting 1) stays at uninstall whatever
         * its version; one a 3.0.0.0 package installed (PreExisting 0)
         * goes once nothing is bound to it. */
        CHECK(!inst_s98_service_removable(0, 1) &&
              inst_s98_service_removable(0, 0),
              "PreExisting decides, not 0.4.0.0 against 3.1.0.0");
    }
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
    CHECK(inst_same_ver_stale(1, 0, 0),
          "5.4.1: the package's version with other bytes is stale");
    CHECK(!inst_same_ver_stale(1, 0, 1) && !inst_same_ver_stale(1, 0, -1) &&
          !inst_same_ver_stale(1, 1, 0) && !inst_same_ver_stale(1, -1, 0) &&
          !inst_same_ver_stale(0, 0, 0) && !inst_same_ver_stale(-1, 0, 0),
          "5.4.1: never for the same bytes, bytes not read, a newer or older "
          "version, an absent or unreadable file");
    CHECK(strcmp(inst_action_text(INST_ACT_CURRENT),
                 "Keep (same version)") == 0 &&
          strcmp(inst_action_row_text(INST_ACT_CURRENT, 0, 0, 0),
                 "Keep (same version)") == 0,
          "5.4.1: a current device left says so");
    CHECK(strcmp(inst_action_row_text(INST_ACT_CURRENT, 1, 0, 0),
                 "Replace files (same version)") == 0 &&
          strcmp(inst_action_row_text(INST_ACT_CURRENT, 0, 0, 1),
                 "Replace files (same version)") == 0,
          "5.4.1: a repair, by reinstall or by file, says so");
    CHECK(strcmp(inst_action_row_text(INST_ACT_CURRENT, 1, 1, 0),
                 "Reinstall (/force)") == 0 &&
          strcmp(inst_action_row_text(INST_ACT_NEWER, 1, 1, 0),
                 "Reinstall (/force)") == 0,
          "5.4.1: /force reinstalls");
    CHECK(strcmp(inst_action_row_text(INST_ACT_UPDATE, 1, 0, 1),
                 "Update") == 0 &&
          strcmp(inst_action_row_text(INST_ACT_NEWER, 0, 0, 1),
                 inst_action_text(INST_ACT_NEWER)) == 0,
          "5.4.1: other actions keep their text");
    CHECK(inst_x98_place9x(0, 1, 1, 0),
          "5.4.1: 9x, a stale xhci98.sys placed by the tool");
    CHECK(!inst_x98_place9x(1, 1, 1, 0) && !inst_x98_place9x(0, 0, 1, 0) &&
          !inst_x98_place9x(0, 1, 0, 0) && !inst_x98_place9x(0, 1, 1, 1),
          "5.4.1: never on NT, out of the run, not stale, or beside a "
          "controller the helper installs");
    {
        int trap[INST_INF_COUNT];
        int failed[INST_INF_COUNT];
        memset(trap, 0, sizeof(trap));
        memset(failed, 0, sizeof(failed));
        CHECK(!inst_stale_blocked(inst_stale_infs(2, 0), trap, failed),
              "5.4.1: xhcicm.sys free with no trap and no failure");
        failed[INST_INF_CMF] = 1;
        CHECK(inst_stale_blocked(inst_stale_infs(2, 0), trap, failed),
              "5.4.1: a cancelled filter install (no NHI present) keeps "
              "xhcicm.sys");
        failed[INST_INF_CMF] = 0;
        trap[INST_INF_CMF] = 1;
        CHECK(inst_stale_blocked(inst_stale_infs(2, 0), trap, failed),
              "5.4.1: a filter-only store trap (no NHI present) keeps "
              "xhcicm.sys");
        CHECK(!inst_stale_blocked(inst_stale_infs(0, 0), trap, failed) &&
              !inst_stale_blocked(inst_stale_infs(0, 1), trap, failed),
              "5.4.1: the filter's trap holds neither xhci98.sys nor "
              "xhcihlpr.sys");
        trap[INST_INF_CMF] = 0;
        failed[INST_INF_CM] = 1;
        CHECK(inst_stale_blocked(inst_stale_infs(0, 1), trap, failed) &&
              inst_stale_blocked(inst_stale_infs(2, 0), trap, failed) &&
              !inst_stale_blocked(inst_stale_infs(0, 0), trap, failed),
              "5.4.1: a failed NHI install keeps xhcihlpr.sys and "
              "xhcicm.sys, not xhci98.sys");
        failed[INST_INF_CM] = 0;
        trap[INST_INF_XHCI] = 1;
        CHECK(inst_stale_blocked(inst_stale_infs(0, 1), trap, failed) &&
              !inst_stale_blocked(inst_stale_infs(2, 0), trap, failed),
              "5.4.1: the controller's trap keeps xhcihlpr.sys, not "
              "xhcicm.sys");
        trap[INST_INF_XHCI] = 0;
        failed[INST_INF_S98] = 1;
        CHECK(inst_stale_blocked(inst_stale_infs(3, 0), trap, failed) &&
              !inst_stale_blocked(inst_stale_infs(1, 0), trap, failed) &&
              !inst_stale_blocked(inst_stale_infs(4, 0), trap, failed),
              "5.4.1: each single-INF file held by its own INF alone");
        failed[INST_INF_S98] = 0;
        failed[INST_INF_UAS] = 1;
        CHECK(inst_stale_blocked(inst_stale_infs(1, 0), trap, failed),
              "5.4.1: a failed absent UAS install keeps xhciuas.sys");
    }
    CHECK(inst_hlpr_stale_ok(1, 1, 1, 1) && inst_hlpr_stale_ok(1, 1, 1, 0) &&
          inst_hlpr_stale_ok(1, 1, 0, 1),
          "5.4.1: xhcihlpr.sys with either group in the run, none refused");
    CHECK(!inst_hlpr_stale_ok(1, 0, 1, 1) && !inst_hlpr_stale_ok(0, 1, 1, 1) &&
          !inst_hlpr_stale_ok(1, 0, 1, 0) && !inst_hlpr_stale_ok(0, 1, 0, 1),
          "5.4.1: a refused connection manager group keeps xhcihlpr.sys "
          "from the USB branch, and a refused USB group from the "
          "connection manager's");
    CHECK(!inst_hlpr_stale_ok(1, 1, 0, 0),
          "5.4.1: neither group in the run");
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
    /* NT: a root hub after its controller (run-43-24, the root hub that
     * came back on 8.1 and 10). */
    CHECK_EQ(inst_remove_rank_nt("USB\\XHCI98_ROOT_HUB\\4&1", "xhci98"),
             INST_REMOVE_RANK_NT_MAX, "NT: the root hub after the controllers");
    CHECK_EQ(inst_remove_rank_nt("PCI\\VEN_1B36&DEV_000D\\3", "xhci98"), 5,
             "NT: the controller before its root hub");
    CHECK(inst_remove_rank_nt("PCI\\VEN_1B36&DEV_000D\\3", "xhci98") <
          inst_remove_rank_nt("USB\\XHCI98_ROOT_HUB\\4&1", "xhci98"),
          "NT: controller, then root hub");
    CHECK_EQ(inst_remove_rank_nt("USB\\XHCI98_HUB&VID_1&PID_2\\x", "xhci98"), 2,
             "NT: a hub as before");
    CHECK_EQ(inst_remove_rank_nt("USB\\VID_1&PID_2\\x", "xhciuas"), 0,
             "NT: UAS first as before");
    CHECK_EQ(inst_remove_rank_nt("PCI\\VEN_8086&DEV_15BF\\3", "xhcicm"), 4,
             "NT: the NHI as before");
    CHECK_EQ(inst_remove_rank_nt("USB\\VID_046D&PID_C077\\5", "HidUsb"), -1,
             "NT: another driver's device left");
    CHECK_EQ(inst_remove_rank("USB\\XHCI98_ROOT_HUB\\4&1", "xhci98", 0), 3,
             "98 SE and ME keep the root hub before the controller");

    /* The driver-store check (run-43-24, the stor98.mpd check on NT). */
    CHECK_EQ(inst_store_verdict("stor98.sys", 1, 1, 1), INST_STORE_EQUAL,
             "store: equal");
    CHECK_EQ(inst_store_verdict("stor98.sys", 0, 1, 1), INST_STORE_DIFFERS,
             "store: differs");
    CHECK_EQ(inst_store_verdict("stor98.mpd", -1, 1, 0), INST_STORE_SKIP,
             "store: stor98.mpd the stored package lacks (NT)");
    CHECK_EQ(inst_store_verdict("S98VSD.VXD", -1, 1, 0), INST_STORE_SKIP,
             "store: a VxD the stored package lacks (NT)");
    CHECK_EQ(inst_store_verdict("stor98.sys", -1, 1, 0), INST_STORE_UNREAD,
             "store: an NT file the stored package lacks blocks");
    CHECK_EQ(inst_store_verdict("xhci98.cat", -1, 1, 0), INST_STORE_UNREAD,
             "store: a catalog the stored package lacks blocks");
    CHECK_EQ(inst_store_verdict("stor98.mpd", -1, 1, -1), INST_STORE_UNREAD,
             "store: a stored file not readable");
    CHECK_EQ(inst_store_verdict("stor98.mpd", -1, 0, 1), INST_STORE_UNREAD,
             "store: the package's own file missing");
    CHECK_EQ(inst_store_verdict("stor98.mpd", -1, -1, 0), INST_STORE_UNREAD,
             "store: the package's file not readable");
    CHECK_EQ(inst_store_verdict(NULL, -1, 1, 0), INST_STORE_UNREAD,
             "store: no name");
    /* A tunnel port at the uninstall (run-43-24, the uninstall waiting for
     * every filtered port). */
    CHECK_EQ(inst_port_step(1, 0), INST_PORT_GIVE_BACK, "port present: given back");
    CHECK_EQ(inst_port_step(1, 1), INST_PORT_GIVE_BACK,
             "port present, other filters: given back");
    CHECK_EQ(inst_port_step(0, 0), INST_PORT_REMOVE, "port absent: its entry removed");
    CHECK_EQ(inst_port_step(0, 1), INST_PORT_WAIT,
             "port absent with another filter: the run waits for it");
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
    CHECK(inst_value_find("XhciTolerance") >= 0 &&
              instValues[inst_value_find("XhciTolerance")].Asked == 0,
          "task 49.38: a quirk value is a row, never asked");
    /* Task 45.54 (the owner, 2026-10-31): the log is an Advanced row now;
     * these stay out of the table. */
    {
        /* Task 49.38: every value is a row (the owner, 2026-11-12) but
         * these, which no page sets. */
        static const char *const out[] = {
            "XhciCmNet", "XhciNetMtu", "XhciEventRing"
        };
        int k;
        for (k = 0; k < (int)(sizeof(out) / sizeof(out[0])); k++) {
            CHECK_EQ(inst_value_find(out[k]), -1, out[k]);
        }
        CHECK(instValueCount <= INST_MAX_VALUES, "the table fits its arrays");
    }

    /* 98 SE with a QEMU controller: no switchover, no fine timer, no NHI. */
    e = env(0, 4, 10, INST_ARCH_X86);
    e.AnyXhci = 1;
    CHECK(!inst_value_applies(psw, &e), "98, no Intel 7-9: no port switch");
    /* Record 26 section 5.0 (owner, 2026-10-22): Advanced, not asked;
     * /set takes it as before. */
    CHECK(!inst_value_applies(im, &e), "98: interrupt mode not asked");
    {
        int fb = inst_value_find("XhciForceBulkOnly");
        int bo = inst_value_find("XhciBulkOnlyOwnId");
        int ni = inst_value_find("XhciCmNoIrqStart");
        INST_ENV e2 = e;
        e2.CmNhi = 1;
        CHECK(fb >= 0 && !inst_value_applies(fb, &e) &&
              inst_value_set_mask(fb, &e) == 0x3,
              "ForceBulkOnly: Advanced, /set 0 and 1");
        CHECK(bo == fb + 1 && !inst_value_applies(bo, &e) &&
              inst_value_set_mask(bo, &e) == 0x3 &&
              instValues[bo].Inf == INST_INF_XHCI && !instValues[bo].NoInf,
              "BulkOnlyOwnId: after it, Advanced, /set 0 and 1");
        CHECK(ni >= 0 && instValues[ni].Inf == INST_INF_CM &&
              !instValues[ni].NoInf && instValues[ni].Def == 1 &&
              !inst_value_applies(ni, &e2) &&
              inst_value_set_mask(ni, &e2) == 0x3,
              "XhciCmNoIrqStart: 98 SE, default 1, written by the INF "
              "(task 44.46), Advanced");
        e2 = env(1, 6, 1, INST_ARCH_X86);
        e2.AnyXhci = 1;
        e2.CmNhi = 1;
        CHECK(ni >= 0 && inst_value_set_mask(ni, &e2) == 0,
              "XhciCmNoIrqStart: refused on NT");
        /* Record 33 section 9.1, record 26 section 5.5 draft 49 (task
         * 45.12): 2000 and XP (and XP x64) only, never asked, not written
         * by the INFs, default 1, 0 to 2 by /set. */
        {
            int br = inst_value_find("XhciCmBridgeReq");
            INST_ENV n5 = env(1, 5, 1, INST_ARCH_X86);
            INST_ENV n2 = env(1, 5, 0, INST_ARCH_X86);
            INST_ENV n6 = env(1, 6, 1, INST_ARCH_X86);
            INST_ENV n9 = env(0, 4, 10, INST_ARCH_X86);
            n5.AnyXhci = n2.AnyXhci = n6.AnyXhci = n9.AnyXhci = 1;
            n5.CmNhi = n2.CmNhi = n6.CmNhi = n9.CmNhi = 1;
            CHECK(br >= 0 && instValues[br].Inf == INST_INF_CM &&
                  instValues[br].Apply == INST_APPLY_NT5_CM &&
                  instValues[br].Asked == 0 && !instValues[br].NoInf &&
                  instValues[br].Def == 1 && instValues[br].Loc == 0,
                  "XhciCmBridgeReq: never asked, written by the INFs "
                  "(task 45.50), default 1");
            CHECK(br >= 0 && !inst_value_applies(br, &n5) &&
                  inst_value_relevant(br, &n5) &&
                  inst_value_relevant(br, &n2) &&
                  !inst_value_greyed(br, &n5) &&
                  inst_value_set_mask(br, &n5) == 0x7 &&
                  inst_value_set_mask(br, &n2) == 0x7,
                  "XhciCmBridgeReq: 2000 and XP, /set 0 to 2");
            CHECK(br >= 0 && inst_value_greyed(br, &n6) &&
                  inst_value_greyed(br, &n9) &&
                  !inst_value_relevant(br, &n6) &&
                  !inst_value_relevant(br, &n9) &&
                  inst_value_set_mask(br, &n6) == 0 &&
                  inst_value_set_mask(br, &n9) == 0 &&
                  inst_value_only(br) != NULL &&
                  strcmp(inst_value_only(br), "Windows 2000 and XP only") == 0,
                  "XhciCmBridgeReq: greyed and refused on 7 and 98 SE");
            n5.CmNhi = 0;
            CHECK(br >= 0 && !inst_value_relevant(br, &n5) &&
                  inst_value_set_mask(br, &n5) == 0x7,
                  "XhciCmBridgeReq: XP with no host interface not relevant");
        }
        {
            int rs = inst_value_find("Restart");
            INST_ENV e9 = env(0, 4, 10, INST_ARCH_X86);
            CHECK(rs >= 0 && instValues[rs].Loc == INST_LOC_MARK &&
                  instValues[rs].Inf == INST_INF_CM &&
                  instValues[rs].Asked == 0 && instValues[rs].NoInf &&
                  instValues[rs].Def == 1,
                  "Restart: the mark service's key, never asked, default 1");
            CHECK(rs >= 0 && inst_value_set_mask(rs, &e2) == 0x3 &&
                  inst_value_relevant(rs, &e2) &&
                  !inst_value_greyed(rs, &e2),
                  "Restart on 7: /set 0 and 1, shown");
            CHECK(rs >= 0 && inst_value_set_mask(rs, &e9) == 0 &&
                  inst_value_greyed(rs, &e9),
                  "Restart on 98 SE: greyed, /set refuses it");
            for (rs = 0; rs < instValueCount; rs++) {
                if (instValues[rs].Loc != INST_LOC_MW &&
                    !inst_eq(instValues[rs].Name, "Restart") &&
                    !inst_eq(instValues[rs].Name, "LogBytes") &&
                    instValues[rs].Loc != INST_LOC_DRIVE &&
                    !inst_eq(instValues[rs].Name, "XhciPfwNhiLine")) {
                    break;
                }
            }
            CHECK(rs == instValueCount,
                  "Restart and XhciPfwNhiLine the values outside "
                  "Services\\USB");
            /* Record 32 section 12.10 (task 46.11): XHCIPFW.VXD's switch
             * in its own key, written at 1 where missing (no INF), never
             * asked, /set 0 and 1 on 98 SE and ME alone. */
            rs = inst_value_find("XhciPfwNhiLine");
            CHECK(rs >= 0 && instValues[rs].Loc == INST_LOC_PFW &&
                  instValues[rs].NoInf && instValues[rs].Def == 1 &&
                  instValues[rs].Asked == 0 &&
                  inst_value_set_mask(rs, &e9) == 0x3 &&
                  inst_value_set_mask(rs, &e2) == 0,
                  "XhciPfwNhiLine: the VxD's key, default 1, /set on 98 SE "
                  "and ME");
        }
    }
    CHECK(!inst_value_applies(ft, &e), "98: no fine timer");
    /* Owner, 2026-10-15: the fine timer and selective suspend are never
     * asked; /set Name=Value still sets them. */
    CHECK(!inst_value_applies(ss, &e), "98: selective suspend not asked");
    CHECK(inst_value_set("XhciSelectiveSuspend=1", &idx, &v) == 1 &&
          idx == ss && v == 1, "/set still sets selective suspend");
    CHECK(inst_value_set("XhciPollFineTimer=1", &idx, &v) == 1 &&
          idx == ft && v == 1, "/set still sets the fine timer");
    CHECK(!inst_value_applies(cm, &e), "no NHI: no cm values");
    CHECK_EQ(inst_value_set_mask(im, &e), 0x7,
             "98: interrupt mode 0-2, no 4 (it runs as 0 there)");
    {
        INST_ENV eme = e;
        eme.Minor = 90;
        CHECK_EQ(inst_value_set_mask(im, &eme), 0x7, "ME: no 4 either");
    }
    CHECK(strstr(instValues[im].Choice[4], "98") == NULL &&
          strstr(instValues[im].Choice[4], "msi4") == NULL &&
          strstr(instValues[im].CautionAdv, "98") == NULL &&
          strstr(instValues[im].CautionAdv, "msi4") == NULL,
          "mode 4's choice and caution: 2000 and XP, no component named");
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
        /* Roadmap 43.6 (owner, 2026-10-19): the note says only to
         * restart; the user knows what is being plugged in. */
        CHECK(strstr(instValues[si].Caution, "Restart") != NULL &&
              strstr(instValues[si].Caution, "at least once") == NULL &&
              strstr(instValues[si].Caution, "CD") == NULL,
              "the silent install's note: the restart alone");
    }
    CHECK_EQ(inst_value_set_mask(im, &e), 0x17, "2000: no MSI but mode 4");
    e = env(1, 6, 1, INST_ARCH_AMD64);
    e.AnyXhci = 1;
    CHECK_EQ(inst_value_set_mask(im, &e), 0xF, "7: MSI offered");
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
    CHECK_EQ(inst_value_set_mask(im, &e), 0x17, "XP: /set refuses mode 3");
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

    /* Owner, 2026-10-19: XhciFastPollFsLs, never asked, settable for 0, 2,
     * 3 and 4 (1 is not a mode), each choice named, 4 included. */
    {
        int fp = inst_value_find("XhciFastPollFsLs");
        INST_ENV en = env(0, 4, 10, INST_ARCH_X86);
        INST_ENV nt = env(1, 6, 1, INST_ARCH_X86);
        INST_VNOW fnow[1];
        INST_U32 fenter;
        char q[4096];
        en.AnyXhci = 1;
        nt.AnyXhci = 1;
        CHECK(fp >= 0, "the fast-poll row");
        if (fp >= 0) {
            CHECK_EQ(instValues[fp].Inf, INST_INF_XHCI, "an xhci98 value");
            CHECK_EQ(instValues[fp].Apply, INST_APPLY_ANY_XHCI, "any system");
            CHECK_EQ(instValues[fp].Asked, 0, "fast poll never asked");
            CHECK(!inst_value_applies(fp, &en), "98 SE: not asked");
            CHECK(inst_value_relevant(fp, &en), "98 SE: an Advanced row");
            CHECK(inst_value_relevant(fp, &nt), "7: an Advanced row");
            CHECK_EQ(inst_value_set_mask(fp, &en), 0x1D, "98 SE: 0, 2, 3, 4");
            CHECK_EQ(inst_value_set_mask(fp, &nt), 0x1D, "7: 0, 2, 3, 4");
            CHECK(inst_value_set("XhciFastPollFsLs=0", &idx, &v) == 1 &&
                  idx == fp && v == 0, "/set takes fast poll 0");
            CHECK_EQ(inst_value_set("XhciFastPollFsLs=1", &idx, &v), -1,
                     "/set refuses fast poll 1");
            CHECK(inst_value_set("XhciFastPollFsLs=2", &idx, &v) == 1 &&
                  v == 2, "/set takes fast poll 2");
            CHECK(inst_value_set("XhciFastPollFsLs=3", &idx, &v) == 1 &&
                  v == 3, "/set takes fast poll 3");
            CHECK(inst_value_set("XhciFastPollFsLs=4", &idx, &v) == 1 &&
                  idx == fp && v == 4, "/set takes fast poll 4");
            CHECK_EQ(inst_value_set("XhciFastPollFsLs=5", &idx, &v), -1,
                     "/set refuses fast poll 5");
            CHECK(!inst_value_allowed(fp, &nt, 1), "Advanced: 1 refused");
            CHECK(inst_value_allowed(fp, &nt, 4), "Advanced: 4 taken");
            CHECK(!inst_value_allowed(fp, &nt, 5), "Advanced: 5 refused");
            CHECK(instValues[fp].Choice[1] == NULL, "1 has no meaning");
            CHECK(instValues[fp].Choice[4] != NULL &&
                  inst_eq(instValues[fp].Choice[4],
                          "rate chosen by hidusbf, codes 128 to 143"),
                  "4 named");
            CHECK(instValues[fp].Caution != NULL &&
                  instValues[fp].CautionMask == 0 &&
                  strstr(instValues[fp].Caution, "restart") != NULL &&
                  strstr(instValues[fp].Caution, "3.1.0.0") == NULL,
                  "its caution every time, no release history");
            memset(fnow, 0, sizeof(fnow));
            fnow[0].State = 1;
            fnow[0].Value = 0;
            CHECK(inst_value_question(q, sizeof(q), fp, 0x1D, 0, fnow, 1, 0,
                                      0, &fenter) == 1, "the row renders");
            CHECK(strstr(q, "    4  rate chosen by hidusbf, codes 128 "
                            "to 143\n") != NULL, "choice 4 listed");
            CHECK(strstr(q, "    2  31/62 Hz in hidusbf gives 2000/4000 Hz "
                            "(root-port mouse)\n") != NULL,
                  "choice 2 listed");
            CHECK(strstr(q, "\n    1") == NULL, "1 not listed");
        }
    }

    /* Task 45.54 (record 26 section 5.4.1; the owner, 2026-10-31): the
     * Advanced rows added, each never asked, on its tab, with its range or
     * list, its systems and its /set bounds. */
    {
        static const struct {
            const char *Name;
            int Inf;
            int Apply;
            INST_U32 Settable;  /* a list's /set mask, 0 for a range */
            INST_U32 Lo;
            INST_U32 Hi;
            INST_U32 Step;
        } add[] = {
            { "XhciLinkKeep", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x3, 0, 0, 0 },
            { "XhciSsFlapFallback", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x3,
              0, 0, 0 },
            { "XhciResumeKeep", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x3, 0, 0,
              0 },
            { "XhciUsb2Bos", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x3, 0, 0, 0 },
            { "XhciImodInterval250ns", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0,
              10, 4000, 0 },
            { "XhciLogVerbosity", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x1F, 0,
              0, 0 },
            { "XhciLogDebugView", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0x3, 0,
              0, 0 },
            { "XhciPollIdleMs", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0, 1, 5000,
              0 },
            { "XhciPollActiveMs", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0, 1,
              5000, 0 },
            { "XhciMaxTransferKB", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0, 64,
              2048, 4 },
            { "XhciWideMap", INST_INF_XHCI, INST_APPLY_NT_XHCI, 0x3, 0, 0, 0 },
            { "XhciFirstEnumWaitMs", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0, 0,
              30000, 0 },
            { "XhciFirstEnumPortMs", INST_INF_XHCI, INST_APPLY_ANY_XHCI, 0, 0,
              30000, 0 },
            { "XhciCmRingInterrupt", INST_INF_CM, INST_APPLY_CM, 0x7, 0, 0,
              0 },
            { "XhciCmUsb4V2", INST_INF_CM, INST_APPLY_CM, 0x3, 0, 0, 0 }
        };
        INST_ENV e98 = env(0, 4, 10, INST_ARCH_X86);
        INST_ENV exp = env(1, 5, 1, INST_ARCH_X86);
        char arg[64];
        char range[64];
        int k;
        e98.AnyXhci = 1;
        e98.CmNhi = 1;
        exp.AnyXhci = 1;
        exp.CmNhi = 1;
        for (k = 0; k < (int)(sizeof(add) / sizeof(add[0])); k++) {
            int ix = inst_value_find(add[k].Name);
            const INST_VALUE *x;
            CHECK(ix >= 0, add[k].Name);
            if (ix < 0) {
                continue;
            }
            x = &instValues[ix];
            CHECK(x->Inf == add[k].Inf && x->Apply == add[k].Apply &&
                  x->Asked == 0 && x->Loc == INST_LOC_MW && !x->NoInf,
                  "a 45.54 row: its tab and system, never asked");
            CHECK(!inst_value_applies(ix, &exp) && inst_value_relevant(ix, &exp),
                  "a 45.54 row: an Advanced row on XP");
            if (add[k].Hi != 0) {
                CHECK(x->Settable == 0 && x->Lo == add[k].Lo &&
                      x->Hi == add[k].Hi && x->Step == add[k].Step,
                      "a 45.54 range row's bounds and step");
                sprintf(arg, "%s=%lu", add[k].Name, (unsigned long)add[k].Lo);
                CHECK(inst_value_set(arg, &idx, &v) == 1 && idx == ix &&
                      v == add[k].Lo, "/set takes the range's lowest");
                sprintf(arg, "%s=%lu", add[k].Name, (unsigned long)add[k].Hi);
                CHECK(inst_value_set(arg, &idx, &v) == 1 && v == add[k].Hi,
                      "/set takes the range's highest");
                sprintf(arg, "%s=%lu", add[k].Name,
                        (unsigned long)(add[k].Hi + 1));
                CHECK_EQ(inst_value_set(arg, &idx, &v), -1,
                         "/set refuses one past the highest");
                if (add[k].Lo > 0) {
                    sprintf(arg, "%s=%lu", add[k].Name,
                            (unsigned long)(add[k].Lo - 1));
                    CHECK_EQ(inst_value_set(arg, &idx, &v), -1,
                             "/set refuses one under the lowest");
                }
                CHECK(inst_value_allowed(ix, &e98, add[k].Hi) &&
                      inst_value_allowed(ix, &exp, add[k].Lo) &&
                      !inst_value_allowed(ix, &exp, add[k].Hi + 1),
                      "a range row allowed on every system");
                inst_value_range_text(ix, range, sizeof(range));
                CHECK(strstr(range, " to ") != NULL &&
                      (add[k].Step == 0) == (strstr(range, "multiple") == NULL),
                      "a range row's text");
            } else {
                CHECK(x->Settable == add[k].Settable && x->Hi == 0,
                      "a 45.54 list row's choices");
                inst_value_range_text(ix, range, sizeof(range));
                CHECK(range[0] == '\0', "a list row has no range text");
            }
        }
        {
            int mt = inst_value_find("XhciMaxTransferKB");
            int ri = inst_value_find("XhciCmRingInterrupt");
            int wm = inst_value_find("XhciWideMap");
            int lv = inst_value_find("XhciLogVerbosity");
            INST_ENV ex = env(1, 5, 1, INST_ARCH_X86);
            CHECK_EQ(inst_value_set("XhciMaxTransferKB=1026", &idx, &v), -1,
                     "/set refuses a command size off its step of 4");
            CHECK(inst_value_set("XhciMaxTransferKB=1028", &idx, &v) == 1 &&
                  idx == mt && v == 1028, "/set takes one on its step");
            CHECK(!inst_value_in_range(mt, 1026) && inst_value_in_range(mt, 64),
                  "the step held by the range check");
            inst_value_range_text(mt, range, sizeof(range));
            CHECK(inst_eq(range, "64 to 2048, a multiple of 4"),
                  "the command size's range text");
            inst_value_range_text(inst_value_find("XhciPollIdleMs"), range,
                                  sizeof(range));
            CHECK(inst_eq(range, "1 to 5000"), "a range text with no step");
            CHECK(!inst_value_in_range(wm, 0), "a list row is no range");
            /* The ring interrupt: every system since task 48.26; 2 (task
             * 49.27) on 2000 and later alone. */
            CHECK(!inst_value_greyed(ri, &e98) && inst_value_set_mask(ri, &e98)
                  == 0x3 && inst_value_allowed(ri, &e98, 1) &&
                  !inst_value_allowed(ri, &e98, 2) &&
                  !inst_value_allowed(ri, &e98, 3) &&
                  inst_value_only(ri) == NULL,
                  "XhciCmRingInterrupt on 98 SE: 0 and 1 taken, not 2");
            CHECK(strstr(instValues[ri].Choice[2], "msi4") == NULL &&
                  strstr(instValues[ri].Caution, "msi4") == NULL &&
                  strstr(instValues[ri].Caution, "98") == NULL,
                  "XhciCmRingInterrupt: no component named, no 98 SE clause");
            CHECK(!inst_value_greyed(ri, &exp) && inst_value_set_mask(ri, &exp)
                  == 0x7 && inst_value_allowed(ri, &exp, 1) &&
                  inst_value_allowed(ri, &exp, 2),
                  "XhciCmRingInterrupt on XP: taken");
            CHECK(instValues[ri].Choice[2] != NULL &&
                  instValues[ri].Choice[3] == NULL &&
                  instValues[ri].CautionMask == 0x6,
                  "XhciCmRingInterrupt: 2's meaning, the caution on 1 and 2");
            ex.Major = 10;
            ex.Minor = 0;
            ex.CmNhi = 1;
            CHECK(!inst_value_greyed(ri, &ex) && inst_value_relevant(ri, &ex),
                  "XhciCmRingInterrupt on 10: shown");
            ex.CmNhi = 0;
            CHECK(!inst_value_relevant(ri, &ex) && !inst_value_greyed(ri, &ex),
                  "XhciCmRingInterrupt with no NHI: neither relevant nor "
                  "greyed (the engine's Thunderbolt rows show it)");
            /* The wide map: greyed on 98 SE, as the fine timer. */
            CHECK(inst_value_greyed(wm, &e98) && !inst_value_greyed(wm, &exp),
                  "XhciWideMap greyed on 98 SE, shown on XP");
            CHECK(instValues[lv].Choice[4] != NULL &&
                  instValues[lv].Caution != NULL &&
                  instValues[lv].CautionMask == 0x10,
                  "the log's level 4 named, with its caution");
        }
        /* Record 23 section 8.2, revision 51: the 98 SE target rule. */
        CHECK(inst_value_target_9x(0, INST_ACT_INSTALL),
              "98 SE: a controller with no driver the run installs");
        CHECK(inst_value_target_9x(1, INST_ACT_UPDATE) &&
              inst_value_target_9x(1, INST_ACT_CURRENT),
              "98 SE: a controller already bound");
        CHECK(!inst_value_target_9x(0, INST_ACT_FOREIGN),
              "98 SE: a controller another driver holds");
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
    const char *a1[] = { "x", "/dir", "nt-x86", "/list" };
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
    CHECK(o.List && inst_eq(o.Dir, "nt-x86"), "fields");
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
    /* Owner, 2026-10-15: /clean, the switch that once ran the cleanup
     * tool, is gone - an unknown argument. The tool itself, XHCICLN, was
     * dropped in 3.3.0.0. */
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
    {
        /* The NT device list grows past its first capacity, to a bound
         * (audit 2026-11-06: a long-lived system lists more devnodes,
         * present or not, than 1536). */
        int cap = INST_DEV_CAP_FIRST;
        int steps = 0;
        CHECK_EQ(inst_dev_cap_next(1536, 16384), 3072, "1536 doubles");
        CHECK_EQ(inst_dev_cap_next(12288, 16384), 16384, "the last step stops at the bound");
        CHECK_EQ(inst_dev_cap_next(16384, 16384), 16384, "full at the bound");
        CHECK_EQ(inst_dev_cap_next(20000, 16384), 20000, "beyond the bound: no growth");
        CHECK_EQ(inst_dev_cap_next(0, 16384), 0, "no list: no growth");
        CHECK_EQ(inst_dev_cap_next(-1, 16384), -1, "a negative count: no growth");
        CHECK_EQ(inst_dev_cap_next(1536, 1536), 1536, "98 SE and ME's bound");
        while (inst_dev_cap_next(cap, INST_DEV_CAP_NT) > cap && steps < 64) {
            int next = inst_dev_cap_next(cap, INST_DEV_CAP_NT);
            CHECK(next > cap && next <= INST_DEV_CAP_NT, "each step grows within the bound");
            cap = next;
            steps++;
        }
        CHECK_EQ(cap, INST_DEV_CAP_NT, "the NT list ends at its bound");
        CHECK_EQ(steps, 4, "1536 to 16384 in four steps");
        CHECK(INST_DEV_CAP_NT <= 0x7FFFFFFF / 8192, "the bound's bytes fit an int-sized product");
    }
    {
        /* A range setting's box, typed or pasted (audit 2026-11-06). */
        INST_U32 v = 7;
        CHECK(inst_parse_dec("12", &v) && v == 12UL, "12");
        CHECK(inst_parse_dec("0", &v) && v == 0UL, "0");
        CHECK(inst_parse_dec("4294967295", &v) && v == 0xFFFFFFFFUL, "the largest");
        v = 7;
        CHECK(!inst_parse_dec("4294967296", &v) && v == 7UL, "one past: refused, untouched");
        CHECK(!inst_parse_dec("99999999999", &v), "far past: refused");
        CHECK(!inst_parse_dec("12abc", &v), "trailing text: refused");
        CHECK(!inst_parse_dec("abc", &v), "no digits: refused");
        CHECK(!inst_parse_dec("", &v), "empty: refused");
        CHECK(!inst_parse_dec(" 12", &v), "a blank: refused");
        CHECK(!inst_parse_dec("-1", &v), "a sign: refused");
        CHECK(!inst_parse_dec(NULL, &v), "no text: refused");
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
                 "Install the drivers now") == 0, "runs");
    CHECK(strstr(inst_confirm_text(0, 1, 1, 0), "settings") != NULL, "a setting");
    CHECK(strstr(inst_confirm_text(0, 0, 1, 0), "All devices are up to date") !=
          NULL, "nothing");
    CHECK(strcmp(inst_confirm_text(1, 0, 0, 0),
                 "Copy the drivers and update DevicePath now") == 0, "9x runs");
    CHECK(strstr(inst_confirm_text(0, 1, 0, 0), "settings") != NULL, "9x setting");
    CHECK(strstr(inst_confirm_text(0, 0, 0, 0), "DevicePath now") != NULL,
          "9x nothing");
    /* An entry that may be a cancelled wizard's controller: no claim that
     * every device is current or needs no driver. */
    CHECK(strstr(inst_confirm_text(0, 0, 0, 1), "up to date") == NULL &&
          strstr(inst_confirm_text(0, 0, 0, 1), "separate entry") != NULL,
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

    /* Record 32 section 12.8.11: the ASM4242's tier in flags 18:16, known
     * only from a record of this boot of a started host. */
    put32(r, CM_ST_O_UPTIME_S, 600);
    put32(r, CM_ST_O_SYSTIME_LO, 0);
    put32(r, CM_ST_O_SYSTIME_HI, 0x01DD3D5FUL);
    put32(r, CM_ST_O_FLAGS, CM_ST_HO_KNOWN << CM_ST_F_HO_SHIFT);
    CHECK_EQ(CM_ST_F_HO_SHIFT, 16, "the tier's bits start at 16");
    CHECK(inst_asm_ho_known(r, n, 1, 0, 0x01DD3D5FUL, 600000UL),
          "tier 2, this boot, started: a known build");
    CHECK(!inst_asm_ho_known(r, n, 0, 0, 0x01DD3D5FUL, 600000UL),
          "the host not started: not known");
    CHECK(!inst_asm_ho_known(r, n, 1, 0x8F0D1800UL, 0x01DD3D60UL, 120000UL),
          "an earlier boot's record: not known");
    CHECK(!inst_asm_ho_known(r, n - 1, 1, 0, 0x01DD3D5FUL, 600000UL),
          "a record that does not check: not known");
    put32(r, CM_ST_O_FLAGS, (CM_ST_HO_SAME << CM_ST_F_HO_SHIFT) |
                                CM_ST_F_HO_SENT);
    CHECK(!inst_asm_ho_known(r, n, 1, 0, 0x01DD3D5FUL, 600000UL),
          "tier 3, sent: not a known build");
    put32(r, CM_ST_O_FLAGS, (CM_ST_HO_KNOWN << CM_ST_F_HO_SHIFT) |
                                CM_ST_F_HO_SENT | 0x0F00UL);
    CHECK(inst_asm_ho_known(r, n, 1, 0, 0x01DD3D5FUL, 600000UL),
          "the other flags do not matter");
    {
        int hs = inst_value_find("XhciCmAsm4242Structural");
        INST_ENV e9 = env(0, 4, 10, INST_ARCH_X86);
        INST_ENV nt = env(1, 5, 1, INST_ARCH_X86);
        e9.CmNhi = 1;
        nt.CmNhi = 1;
        CHECK(hs >= 0 && instValues[hs].Inf == INST_INF_CM &&
              instValues[hs].Apply == INST_APPLY_CM &&
              instValues[hs].Def == 0 && instValues[hs].Asked == 0x3UL,
              "XhciCmAsm4242Structural: every system's INF, default 0, asked");
        CHECK(hs >= 0 && !inst_value_applies(hs, &e9) &&
              inst_value_held(hs, &e9), "no ASM4242: held, not asked");
        e9.AsmHandover = 2;
        CHECK(hs >= 0 && !inst_value_applies(hs, &e9),
              "every ASM4242 a known build: not asked");
        e9.AsmHandover = 1;
        CHECK(hs >= 0 && inst_value_applies(hs, &e9) &&
              !inst_value_held(hs, &e9) &&
              inst_value_set_mask(hs, &e9) == 0x3UL,
              "one not known: asked, /set 0 and 1");
        nt.AsmHandover = 1;
        CHECK(hs >= 0 && !inst_value_applies(hs, &nt) &&
              inst_value_held(hs, &nt) && inst_value_set_mask(hs, &nt) == 0x3UL,
              "NT: never asked; /set 0 and 1 (record 32 section 12.8.15.12)");
        CHECK(!inst_value_held(inst_value_find("XhciCmApprove"), &e9),
              "no other value is held");
    }
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
/* Task 46.22 (record 26 section 5.3, draft 70; owner, 2026-11-03): the
 * Thunderbolt box locked by the USB drivers' box per system, the console's
 * question on 8, 8.1 and 10, the run of the tools alone, and Stor98's line
 * per system. */
static void test_tb_lock_4622(void)
{
    int ask;
    int def;
    int sys;
    int cm;

    /* The window's Thunderbolt box is the user's on every system (owner,
     * 2026-11-04); the window's self-test reads it. */
    /* The console: the connection manager alone's mode per system. */
    CHECK_EQ(inst_tb_alone(0, 1, 0), 0, "USB in: not alone");
    CHECK_EQ(inst_tb_alone(1, 1, 0), 0, "8 to 10, /usb: not alone");
    CHECK_EQ(inst_tb_alone(0, 0, 0), 1, "2000 to 7 /cmonly: alone, unasked");
    CHECK_EQ(inst_tb_alone(1, 0, 0), INST_TB_ALONE_ASK,
             "8 to 10 without /usb: asked");
    /* Owner, 2026-11-04 (record 26 section 5.3.9): on 8 to 10 /cmonly is
     * asked too, its default by what is found. */
    CHECK_EQ(inst_tb_alone(1, 0, 1), INST_TB_ALONE_ASK,
             "8 to 10 /cmonly: asked, as without it");
    CHECK_EQ(inst_tb_alone(0, 0, 1), 1, "2000 to 7 /cmonly: unasked");
    for (cm = 0; cm < 4; cm++) {
        /* cm: 0 nothing found, 1 an NHI, 2 98 SE's unlisted router's
         * port, 3 a known host's switch or tunnel port. */
        CHECK_EQ(inst_tb_decide(1, cm, 0, 0, INST_TB_ALONE_ASK, 0, 0, 0, &ask,
                                &def), 0, "8 to 10: asked");
        CHECK(ask && def == (cm != 0),
              "8 to 10: Enter for yes where a host is found, else no");
        CHECK_EQ(inst_tb_decide(1, cm, 1, 0, INST_TB_ALONE_ASK, 0, 0, 0, &ask,
                                &def), cm != 0,
                 "8 to 10, /yes: yes where a host is found, else no");
        CHECK(!ask, "8 to 10, /yes: not asked");
        CHECK_EQ(inst_tb_decide(1, cm, 0, 0, INST_TB_ALONE_ASK, 0, 1, 0, &ask,
                                &def), cm != 0,
                 "8 to 10, /list: as /yes");
        CHECK_EQ(inst_tb_decide(1, cm, 1, 0, INST_TB_ALONE_ASK, 0, 0, 1, &ask,
                                &def), 1,
                 "8 to 10, /yes over an installed part: yes, found or not");
        CHECK_EQ(inst_tb_decide(1, cm, 0, 1, INST_TB_ALONE_ASK, 0, 0, 0, &ask,
                                &def), 1, "8 to 10, /thunderbolt: yes");
        CHECK(!ask, "8 to 10, /thunderbolt: not asked");
        CHECK_EQ(inst_tb_decide(1, cm, 0, 0, INST_TB_ALONE_ASK, 1, 0, 0, &ask,
                                &def), 0, "8 to 10 over Intel's: asked");
        CHECK(ask && !def, "8 to 10 over Intel's: Enter for no");
        CHECK_EQ(inst_tb_decide(0, cm, 1, 1, INST_TB_ALONE_ASK, 0, 0, 0, &ask,
                                &def), 0, "8 to 10, no support: no");
        CHECK_EQ(inst_tb_decide(1, cm, 0, 0, 1, 0, 0, 0, &ask, &def), 1,
                 "2000 to 7 alone: in, unasked (unchanged)");
        CHECK(!ask, "2000 to 7 alone: not asked");
    }
    /* A known host's port, not QEMU's bridges (record 26 section 5.3.9). */
    {
        static const char asmPort[] = "PCI\\VEN_1B21&DEV_2423&SUBSYS_00000000"
                                      "&REV_01\0PCI\\VEN_1B21&DEV_2423&CC_0604"
                                      "\0";
        static const char qemuPort[] = "PCI\\VEN_1B36&DEV_000C&SUBSYS_00001B36"
                                       "&REV_00\0PCI\\VEN_1B36&DEV_000C&CC_0604"
                                       "\0";
        static const char other[] = "PCI\\VEN_8086&DEV_A0BC&CC_0604\0";
        CHECK(inst_tb_port_known(asmPort, sizeof(asmPort)) &&
              !inst_tb_port_known(qemuPort, sizeof(qemuPort)) &&
              !inst_tb_port_known(other, sizeof(other)) &&
              !inst_tb_port_known(asmPort, 0),
              "a known host's port found; QEMU's bridge and others not");
    }
    /* The tools alone, every system: no driver, no other set, not 98 SE's
     * storage drivers. */
    CHECK(inst_tools_only(0, 0, 0, 0), "both unticked: the tools alone");
    CHECK(!inst_tools_only(1, 0, 0, 0) && !inst_tools_only(0, 1, 0, 0) &&
          !inst_tools_only(0, 0, 1, 0) && !inst_tools_only(0, 0, 0, 1),
          "a driver, a set, or 98 SE's storage drivers: not the tools "
          "alone");
    /* 98 SE and ME: the USB drivers unticked warn with the connection
     * manager's last sentences, or with the tools alone's. */
    {
        char text[2048];
        CHECK(inst_comp_warnings(1, 1, 0, 1, 0, 0, 0, 1, 0) ==
              INST_CW_CMALONE &&
              inst_comp_warnings(1, 1, 0, 1, 0, 0, 0, 0, 0) ==
              INST_CW_TOOLS9X &&
              inst_comp_warnings(0, 1, 0, 1, 0, 0, 0, 0, 0) ==
              INST_CW_TOOLS9X &&
              inst_comp_warnings(0, 0, 0, 1, 0, 0, 0, 0, 0) == 0 &&
              inst_comp_warnings(0, 0, 1, 1, 0, 0, 0, 0, 0) == 0,
              "the tools alone: warned on 98 SE and ME only");
        inst_comp_warn_text(INST_CW_TOOLS9X, "C:\\XHCI98", text,
                            sizeof(text));
        CHECK(strstr(text, "Setup will install only the xHCI98 tools. "
                     "xHCI98 USB drivers already installed are kept.") !=
              NULL && strstr(text, "connection manager") == NULL,
              "the tools alone's warning names no connection manager");
    }
    CHECK(strcmp(inst_tools_only_text(), "Install/update only the xHCI98 "
                 "tools now (no driver changes)") == 0 &&
          strlen(inst_tools_only_text()) + 1 <= 78,
          "the tools alone's confirmation, a question within 78 columns");
    CHECK_EQ(inst_drivers_line(1, 0, 0), INST_PT_DRIVERS_T8,
             "8 to 10: the line under the USB box, both unticked");
    CHECK_EQ(inst_drivers_line(1, 1, 1), INST_PT_DRIVERS_T8,
             "8 to 10: the line under the USB box, ticked");
    CHECK_EQ(inst_drivers_line(0, 1, 0), INST_PT_DRIVERS_T,
             "2000 to 7: ticked");
    CHECK_EQ(inst_drivers_line(0, 0, 1), INST_PT_DRIVERS_OFF,
             "2000 to 7: the connection manager alone");
    CHECK_EQ(inst_drivers_line(0, 0, 0), INST_PT_DRIVERS_TOOLS,
             "both unticked: the tools alone, not the connection manager's");
    CHECK(strstr(inst_page_text(INST_PT_DRIVERS_TOOLS), "connection "
                 "manager") == NULL &&
          strstr(inst_page_text(INST_PT_DRIVERS_TOOLS), "tools") != NULL,
          "the tools alone's line names no connection manager");
    /* Stor98's line: Windows 10's own, every other system the same. */
    for (sys = 0; sys < INST_S98SYS_COUNT; sys++) {
        CHECK_EQ(inst_s98_line(sys), sys == INST_S98SYS_10 ?
                 INST_PT_S98_T10 : INST_PT_S98_T, "Stor98's line per system");
    }
    CHECK_EQ(inst_s98_line(inst_s98_sys_index(1, 10, 0)), INST_PT_S98_T10,
             "Stor98's line on 10.0");
    CHECK_EQ(inst_s98_line(inst_s98_sys_index(1, 6, 3)), INST_PT_S98_T,
             "Stor98's line on 8.1");
    CHECK_EQ(inst_s98_line(inst_s98_sys_index(1, 6, 2)), INST_PT_S98_T,
             "Stor98's line on 8");
    CHECK(strstr(inst_page_text(INST_PT_S98_T10), "Windows 10") != NULL &&
          inst_page_text(INST_PT_S98_T10)[strlen(inst_page_text(
              INST_PT_S98_T10)) - 1] == '.',
          "Stor98's line on 10: names 10, ends with a full stop");
    {
        const INST_PAGE_TEXT *t = NULL;
        int k;

        for (k = 0; k < instPageTextCount; k++) {
            if (instPageTexts[k].Id == INST_PT_S98_T10) {
                t = &instPageTexts[k];
            }
        }
        CHECK(t != NULL && t->Width == 272 && t->Height == 10 &&
              inst_text_lines(t->Text, inst_page_cols(t->Width)) == 1 &&
              strcmp(t->Text, "Windows 10 has its own NVMe driver.") == 0,
              "Stor98's line on 10: the owner's words, one line at the "
              "control's 272 units");
    }
    /* The tools alone's end: no restart unless a step flagged one. */
    {
        int line = -1;
        CHECK_EQ(inst_final_tools(1, 0, INST_EXIT_DONE, &line),
                 INST_EXIT_DONE, "the tools alone, changed: exit 0");
        CHECK_EQ(line, INST_FINAL_DONE, "the tools alone: Done, no restart");
        CHECK_EQ(inst_final_tools(1, 0, INST_EXIT_RESTART, &line),
                 INST_EXIT_RESTART, "the tools alone, a restart flagged: 3");
        CHECK_EQ(line, INST_FINAL_RESTART, "a restart flagged: asked");
        CHECK_EQ(inst_final_tools(0, 0, INST_EXIT_DONE, &line),
                 INST_EXIT_DONE, "the tools alone, nothing changed");
        CHECK_EQ(line, INST_FINAL_NO_CHANGES, "nothing changed: said so");
        CHECK_EQ(inst_final_tools(1, 1, INST_EXIT_DONE, &line),
                 INST_EXIT_DONE, "the tools alone under /list");
        CHECK_EQ(line, INST_FINAL_NO_CHANGES, "/list: no changes");
        CHECK_EQ(inst_final_tools(1, 0, INST_EXIT_STEPS, &line),
                 INST_EXIT_STEPS, "the tools alone, steps left");
        CHECK_EQ(line, INST_FINAL_UNFINISHED, "steps left: said so");
        CHECK(strcmp(inst_final_text(INST_FINAL_DONE),
                     "Done. No restart is needed.") == 0 &&
              strcmp(inst_final_text_shown(INST_FINAL_DONE, 0),
                     "Done. No restart is needed.") == 0,
              "the tools alone's last line");
        CHECK(strcmp(inst_final_text_shown(INST_FINAL_UNFINISHED, 0),
                     "Not everything is done: see the steps in the log, then "
                     "restart Windows.") == 0 &&
              strcmp(inst_final_text_shown(INST_FINAL_UNFINISHED, 1),
                     inst_final_text(INST_FINAL_UNFINISHED)) == 0 &&
              strcmp(inst_final_text_shown(INST_FINAL_RESTART, 0),
                     inst_final_text(INST_FINAL_RESTART)) == 0 &&
              strcmp(inst_final_text_shown(INST_FINAL_RESTART, 1),
                     "Done. Please restart Windows now.") == 0 &&
              strstr(inst_final_text(INST_FINAL_UNFINISHED),
                     "follow the steps above first") != NULL,
              "the window's Finish: the log where no Steps page was shown");
    }
    /* The defaults unchanged (task 43.59): unticked on 10, ticked on 8
     * and 8.1. */
    CHECK(inst_s98_box_default(INST_S98SYS_10) == 0 &&
          inst_s98_box_default(INST_S98SYS_8) == 1 &&
          inst_s98_box_default(INST_S98SYS_81) == 1,
          "Stor98's defaults on 8, 8.1 and 10 kept");
}

/* Record 32 section 12.8.15.12 (task 46.12 on NT): the ASM4242 xHCI's
 * devnodes for the xhcicm filter entry, and the read path's row. */
/* The emulator's identity (usb4-emu's ASM4242 model under q35, SeaBIOS,
 * -smbios type=2,manufacturer=usb4-emu,product=asm4242-model) as the
 * firmware table routine gives it: the header, type 0, type 2, type 127. */
static const unsigned char emuRsmb[] = {
    0, 3, 0, 0, 59, 0, 0, 0,
    0, 9, 0, 0, 1, 2, 0, 0, 3,
    'S', 'e', 'a', 'B', 'I', 'O', 'S', 0, '1', 0, '0', '1', 0, 0,
    2, 6, 1, 0, 1, 2,
    'u', 's', 'b', '4', '-', 'e', 'm', 'u', 0,
    'a', 's', 'm', '4', '2', '4', '2', '-', 'm', 'o', 'd', 'e', 'l', 0, 0,
    127, 4, 2, 0, 0, 0
};

static const char bRoot[] = "PCI\\VEN_1B36&DEV_000C&SUBSYS_11001AF4&REV_00\\3&1";
static const char bUsp[] = "PCI\\VEN_1B21&DEV_2421&SUBSYS_24211B21&REV_01\\4&1";
static const char bDsp[] = "PCI\\VEN_1B21&DEV_2423&SUBSYS_24211B21&REV_01\\5&1";
static const char bNhi[] = "PCI\\VEN_1B21&DEV_2425&SUBSYS_24211B21&REV_01\\6&1";

/* Record 32 section 12.8.15.12 (task 46.12 on NT): the board's class as
 * Setup reads it, by the driver's own cm_oscp.c (this host build holds
 * the emulator's profile, the installer's does not: test_cm_oscp_rel and
 * the tool's /selftest), the filter entry's step and the plan's texts. */
static void test_board_4612(void)
{
    unsigned char t[sizeof(emuRsmb)];
    unsigned long why;
    unsigned long prof;
    int c;
    int m;

    c = inst_board_class(bRoot, bUsp, bDsp, bNhi, INST_BOARD_R_READ, emuRsmb,
                         sizeof(emuRsmb), &why, &prof);
    CHECK(c == INST_BOARD_KNOWN && why == 0 && prof == 2UL,
          "the emulator's board known (the host build's profile 2)");
    c = inst_board_class("PCI\\VEN_1B36&DEV_000D&SUBSYS_11001AF4\\3&1", bUsp,
                         bDsp, bNhi, INST_BOARD_R_READ, emuRsmb,
                         sizeof(emuRsmb), &why, &prof);
    CHECK(c == INST_BOARD_UNKNOWN && why == INST_BOARD_W_MATCH && prof == 0,
          "another root port: not recognised");
    c = inst_board_class(bRoot, bUsp, bDsp,
                         "PCI\\VEN_1B21&DEV_2425&SUBSYS_00001B21&REV_01\\6&1",
                         INST_BOARD_R_READ, emuRsmb, sizeof(emuRsmb), &why,
                         &prof);
    CHECK(c == INST_BOARD_UNKNOWN && why == INST_BOARD_W_MATCH,
          "another NHI subsystem: not recognised");
    c = inst_board_class(bRoot, bUsp, bDsp, "PCI\\VEN_1B21&DEV_2425\\6&1",
                         INST_BOARD_R_READ, emuRsmb, sizeof(emuRsmb), &why,
                         &prof);
    CHECK(c == INST_BOARD_UNKNOWN && why == INST_BOARD_W_MATCH,
          "no SUBSYS in the id: subsystem 0, not recognised");
    c = inst_board_class(bRoot, bDsp, bUsp, bNhi, INST_BOARD_R_READ, emuRsmb,
                         sizeof(emuRsmb), &why, &prof);
    CHECK(c == INST_BOARD_UNKNOWN, "the ports the other way round");
    memcpy(t, emuRsmb, sizeof(t));
    t[8 + 23 + 6] = 'U';       /* "usb4-emu" -> "Usb4-emu": case kept */
    c = inst_board_class(bRoot, bUsp, bDsp, bNhi, INST_BOARD_R_READ, t,
                         sizeof(t), &why, &prof);
    CHECK(c == INST_BOARD_UNKNOWN && why == INST_BOARD_W_MATCH,
          "another board maker: not recognised");
    memcpy(t, emuRsmb, sizeof(t));
    t[4] = 60;
    c = inst_board_class(bRoot, bUsp, bDsp, bNhi, INST_BOARD_R_READ, t,
                         sizeof(t), &why, &prof);
    CHECK(c == INST_BOARD_UNKNOWN && why == INST_BOARD_W_LENGTH,
          "a length past the answer: not recognised");
    c = inst_board_class(bRoot, bUsp, bDsp, bNhi, INST_BOARD_R_READ, t, 7,
                         &why, &prof);
    CHECK(c == INST_BOARD_UNKNOWN && why == INST_BOARD_W_LENGTH,
          "an answer shorter than its header");
    memcpy(t, emuRsmb, sizeof(t));
    t[8 + 23 + 5] = 3;           /* type 2's product: no third string */
    c = inst_board_class(bRoot, bUsp, bDsp, bNhi, INST_BOARD_R_READ, t,
                         sizeof(t), &why, &prof);
    CHECK(c == INST_BOARD_UNKNOWN && why == 9UL,
          "a bad string index: the driver's own refusal (CM_OSCP_SM_INDEX)");
    c = inst_board_class(NULL, bUsp, bDsp, bNhi, INST_BOARD_R_READ, emuRsmb,
                         sizeof(emuRsmb), &why, &prof);
    CHECK(c == INST_BOARD_UNREAD && why == INST_BOARD_W_ID,
          "a parent not read: not checked");
    c = inst_board_class(bRoot, bUsp, bDsp, bNhi, INST_BOARD_R_FAILED, NULL,
                         0, &why, &prof);
    CHECK(c == INST_BOARD_UNREAD && why == INST_BOARD_W_READ,
          "the tables' read failed or did not answer: not checked");
    c = inst_board_class(bRoot, bUsp, bDsp, bNhi, INST_BOARD_R_NONE, NULL, 0,
                         &why, &prof);
    CHECK(c == INST_BOARD_UNREAD && why == INST_BOARD_W_ROUTE,
          "no route: not checked");
    CHECK(inst_board_class(bRoot, bUsp, bDsp, bNhi, INST_BOARD_R_PENDING,
                           NULL, 0, &why, &prof) == INST_BOARD_PENDING &&
          inst_board_class(bRoot, bUsp, bDsp, bNhi, INST_BOARD_R_LATER, NULL,
                           0, &why, &prof) == INST_BOARD_LATER,
          "WMI's answer awaited, or still to be asked at Next");
    CHECK(inst_board_worse(INST_BOARD_KNOWN, INST_BOARD_UNKNOWN) ==
              INST_BOARD_UNKNOWN &&
          inst_board_worse(INST_BOARD_UNKNOWN, INST_BOARD_PENDING) ==
              INST_BOARD_UNKNOWN &&
          inst_board_worse(INST_BOARD_UNREAD, INST_BOARD_PENDING) ==
              INST_BOARD_PENDING &&
          inst_board_worse(INST_BOARD_UNREAD, INST_BOARD_LATER) ==
              INST_BOARD_LATER &&
          inst_board_worse(INST_BOARD_KNOWN, INST_BOARD_UNREAD) ==
              INST_BOARD_UNREAD &&
          inst_board_worse(INST_BOARD_NONE, INST_BOARD_KNOWN) ==
              INST_BOARD_KNOWN, "two boards: the worse class");
    /* The owner's decision of 2026-11-04: the tables asked for only with
     * the box ticked and an ASM4242 host listed. */
    CHECK(!inst_board_query(1, 0, 1),
          "the box unticked at the start: nothing asked");
    CHECK(inst_board_query(1, 1, 1), "ticked later: asked then");
    CHECK(!inst_board_query(1, 0, 1) && !inst_board_query(1, 0, 0),
          "unticked throughout: never asked");
    CHECK(!inst_board_query(1, 1, 0) && !inst_board_query(0, 1, 1),
          "no ASM4242 host, or 98 SE and ME: never asked");
    CHECK(!inst_board_reads(1, 5, 0) && inst_board_reads(1, 5, 1) &&
          inst_board_reads(1, 5, 2) && inst_board_reads(1, 6, 1) &&
          inst_board_reads(1, 10, 0) && !inst_board_reads(0, 4, 10),
          "the identity read where the driver reads it: not on 2000, on "
          "32-bit XP and later");
    CHECK(inst_board_shown(1, 1) && !inst_board_shown(0, 1) &&
          !inst_board_shown(1, 0),
          "the line shown with the box ticked and an NHI found alone");
    CHECK(inst_board_nhi(bNhi) && inst_board_nhi("PCI\\VEN_1B21&DEV_2425") &&
          !inst_board_nhi(bUsp) && !inst_board_nhi(NULL) &&
          !inst_board_nhi("PCI\\VEN_1B21&DEV_24250"),
          "the ASM4242's NHI by its id");

    m = inst_cm_xhci_mode(1, 0, 0, 1, 1);
    CHECK(m == INST_CMX_ADD, "in place and admitted: added");
    CHECK(inst_cm_xhci_mode(1, 0, 0, 1, 0) == INST_CMX_LEAVE,
          "in place, not this run's: left");
    CHECK(inst_cm_xhci_mode(1, 0, 0, 0, 0) == INST_CMX_REMOVE &&
          inst_cm_xhci_mode(1, 0, 0, 0, 1) == INST_CMX_REMOVE,
          "not in place: taken out");
    CHECK(inst_cm_xhci_mode(0, 0, 0, 1, 1) == INST_CMX_NONE &&
          inst_cm_xhci_mode(1, 1, 0, 0, 0) == INST_CMX_NONE &&
          inst_cm_xhci_mode(1, 0, 1, 0, 0) == INST_CMX_NONE,
          "nothing on 98 SE and ME, on Windows 11, for the tools alone");

    /* The line follows what the driver will do (record 32 section
     * 12.8.15.15): on 32-bit NT a known board is sent only through the
     * helper the xHCI98 USB drivers install. */
    CHECK(inst_board_sendable(1, 0, 0, 0, -1),
          "64-bit Windows: the routines are the driver's own");
    CHECK(!inst_board_sendable(0, 0, 0, 0, -1),
          "32-bit, the helper absent and not installed by the run: not sent");
    CHECK(!inst_board_sendable(0, 0, 0, 1, -1),
          "32-bit, an older helper (no routine service): not sent");
    CHECK(inst_board_sendable(0, 0, 0, 1, 0) &&
          inst_board_sendable(0, 0, 0, 1, 1),
          "32-bit, the helper in place at this package's version or later");
    CHECK(inst_board_sendable(0, 1, 0, 0, -1) &&
          inst_board_sendable(0, 1, 0, 1, -1),
          "32-bit, the helper installed by this run with a controller");
    /* The helper in place is measured against the first with the routine
     * service, never the package's version: 3.4.0.0's carries the same
     * stamp as this tree until the bump and answers minor 0 alone. */
    CHECK(inst_board_hlpr_cmp(3UL, 4UL, 0UL, 0UL) < 0 &&
          !inst_board_sendable(0, 0, 0, 1,
                               inst_board_hlpr_cmp(3UL, 4UL, 0UL, 0UL)),
          "32-bit, the 3.4.0.0 helper in place: not sent");
    CHECK(inst_board_hlpr_cmp(3UL, 4UL, 1UL, 0UL) == 0 &&
          inst_board_hlpr_cmp(3UL, 5UL, 0UL, 0UL) == 0 &&
          inst_board_hlpr_cmp(3UL, 3UL, 9UL, 9UL) < 0,
          "the first capable helper is 3.4.1.0");
    /* An install the staging holds back (a stored build the run may not
     * replace, a store that could not be read) brings no helper. */
    CHECK(inst_board_run_installs(1, 0, 1, 0) &&
          !inst_board_run_installs(1, 0, 1, 1) &&
          !inst_board_run_installs(1, 1, 1, 0) &&
          !inst_board_run_installs(1, 0, 0, 0) &&
          !inst_board_run_installs(0, 0, 1, 0),
          "a run's install: present, not refused, to run, not held");
    CHECK(!inst_board_sendable(0, inst_board_run_installs(1, 0, 1, 1),
                               inst_board_run_installs(1, 0, 1, 1), 0, -1),
          "32-bit, both INFs held by the staging, no helper: not sent");
    /* Record 18 section 15.15: xhcicm.inf installs the helper too. */
    CHECK(inst_board_sendable(0, 0, 1, 0, -1) &&
          inst_board_sendable(0, 0, 1, 1, -1),
          "32-bit, the Thunderbolt part alone installed by this run: sent");
    CHECK(strstr(inst_board_line(INST_BOARD_HELPER), "xhcihlpr") != NULL &&
          strstr(inst_board_note(INST_BOARD_HELPER), "xhcihlpr") != NULL,
          "known with no helper: the helper named");
    CHECK(inst_board_worse(INST_BOARD_HELPER, INST_BOARD_UNKNOWN) ==
              INST_BOARD_UNKNOWN &&
          inst_board_worse(INST_BOARD_NONE, INST_BOARD_HELPER) ==
              INST_BOARD_HELPER, "known with no helper ranks as known");
    CHECK(strstr(inst_board_line(INST_BOARD_HELPER), "known") != NULL &&
          strstr(inst_board_line(INST_BOARD_HELPER), "is sent") == NULL &&
          strstr(inst_board_note(INST_BOARD_HELPER), "is sent") == NULL,
          "known with no helper: never said to be sent");
    for (c = INST_BOARD_KNOWN; c <= INST_BOARD_HELPER; c++) {
        CHECK(c == INST_BOARD_LATER ? inst_board_line(c)[0] == '\0' :
              strlen(inst_board_line(c)) > 0 &&
                  4 + strlen(inst_board_line(c)) <= INST_COLUMNS,
              "the plan's board line within 79 columns, indent included");
        CHECK(strlen(inst_board_note(c)) > 0 &&
              strchr(inst_board_note(c), '(') == NULL,
              "the row's board sentence");
    }
    CHECK(strlen(inst_page_text(INST_PT_BOARD_WAIT)) + 3 <= INST_COLUMNS,
          "the board's countdown text");
    CHECK(inst_board_line(INST_BOARD_NONE)[0] == '\0' &&
          inst_board_note(INST_BOARD_NONE)[0] == '\0', "no board: no text");
    for (m = INST_CMX_ADD; m <= INST_CMX_REMOVE; m++) {
        CHECK(strlen(inst_cm_xhci_head(m)) > 0 &&
              2 + strlen(inst_cm_xhci_head(m)) <= INST_COLUMNS,
              "the filter list's head within 79 columns, the Summary's "
              "\"- \" included");
    }
    CHECK(inst_cm_xhci_head(INST_CMX_LEAVE)[0] == '\0', "no head to leave");
}

static void test_cm_xhci_4612(void)
{
    static const char a[] = "PCI\\VEN_1B21&DEV_2426&SUBSYS_24211B21&REV_01\0"
                            "PCI\\VEN_1B21&DEV_2426&REV_01\0";
    static const char b[] = "PCI\\VEN_1B21&DEV_2426\0";
    static const char c[] = "PCI\\VEN_1B21&DEV_24260&REV_01\0"
                            "PCI\\VEN_1B21&DEV_2425&REV_01\0";
    static const char d[] = "PCI\\VEN_1B21&DEV_2421\0PCI\\CC_0C0330\0";
    int v;

    CHECK(inst_cm_xhci_target(a, sizeof(a)) &&
          inst_cm_xhci_target(b, sizeof(b)),
          "the ASM4242 xHCI: its id alone or with more after '&'");
    CHECK(!inst_cm_xhci_target(c, sizeof(c)) &&
          !inst_cm_xhci_target(d, sizeof(d)) &&
          !inst_cm_xhci_target(NULL, 0) &&
          !inst_cm_xhci_target(a, 10),
          "not another device id, a longer id, or a list cut short");
    v = inst_value_find("XhciCmAsm4242Reads");
    CHECK(v >= 0, "the read path's row");
    if (v >= 0) {
        CHECK(instValues[v].Apply == INST_APPLY_NT_CM &&
              instValues[v].Inf == INST_INF_CM &&
              instValues[v].Asked == 0 && instValues[v].Settable == 0x3UL &&
              !instValues[v].NoInf && instValues[v].Def == 0 &&
              strchr(instValues[v].Meaning, '(') == NULL &&
              instValues[v].Caution != NULL,
              "the read path: 2000 and later, never asked, /set 0 or 1, "
              "default 0");
    }
}

static void test_tb_question(void)
{
    int ask;
    int def;
    char *argv1[4];
    INST_OPTS o;
    char err[256];

    /* A controller present: asked, default no (owner, 2026-10-19: the
     * support is experimental and ticked only on purpose); /yes leaves it
     * out. */
    CHECK_EQ(inst_tb_decide(1, 1, 0, 0, 0, 0, 0, 0, &ask, &def), 0, "present: asked");
    CHECK(ask && !def, "present: default no");
    CHECK_EQ(inst_tb_decide(1, 1, 1, 0, 0, 0, 0, 0, &ask, &def), 0, "present, /yes: no");
    CHECK(!ask, "present, /yes: not asked");
    /* None found: still asked, default no; /yes skips it. */
    CHECK_EQ(inst_tb_decide(1, 0, 0, 0, 0, 0, 0, 0, &ask, &def), 0, "absent: asked");
    CHECK(ask && !def, "absent: default no");
    CHECK_EQ(inst_tb_decide(1, 0, 1, 0, 0, 0, 0, 0, &ask, &def), 0, "absent, /yes: no");
    CHECK(!ask, "absent, /yes: not asked");
    /* /thunderbolt: yes, unasked, present or not, /yes or not. */
    CHECK_EQ(inst_tb_decide(1, 0, 0, 1, 0, 0, 0, 0, &ask, &def), 1, "/thunderbolt, absent");
    CHECK(!ask, "/thunderbolt: not asked");
    CHECK_EQ(inst_tb_decide(1, 0, 1, 1, 0, 0, 0, 0, &ask, &def), 1, "/thunderbolt /yes");
    CHECK_EQ(inst_tb_decide(1, 1, 0, 1, 0, 0, 0, 0, &ask, &def), 1, "/thunderbolt, present");
    /* /list: not asked, the default shown. */
    CHECK_EQ(inst_tb_decide(1, 1, 0, 0, 0, 0, 1, 0, &ask, &def), 0, "/list, present");
    CHECK_EQ(inst_tb_decide(1, 0, 0, 0, 0, 0, 1, 0, &ask, &def), 0, "/list, absent");
    CHECK(!ask, "/list: not asked");
    /* A package without the Thunderbolt driver: nothing, whatever the
     * switches. */
    CHECK_EQ(inst_tb_decide(0, 1, 0, 1, 0, 0, 0, 0, &ask, &def), 0, "no driver: no");
    CHECK(!ask, "no driver: not asked");
    /* The connection manager alone (the USB drivers out of the run): in
     * unasked - but over Intel's Thunderbolt driver asked, the default no
     * (record 23 section 8.5.1; owner, 2026-10-25: ask, the default
     * leave). */
    CHECK_EQ(inst_tb_decide(1, 1, 0, 0, 1, 0, 0, 0, &ask, &def), 1,
             "the connection manager alone: in");
    CHECK(!ask, "the connection manager alone: not asked");
    CHECK_EQ(inst_tb_decide(1, 1, 1, 0, 1, 0, 0, 0, &ask, &def), 1,
             "the connection manager alone, /yes: in");
    CHECK_EQ(inst_tb_decide(1, 1, 0, 0, 1, 1, 0, 0, &ask, &def), 0,
             "alone over Intel's driver: asked");
    CHECK(ask && !def, "alone over Intel's driver: the default no");
    CHECK_EQ(inst_tb_decide(1, 1, 1, 0, 1, 1, 0, 0, &ask, &def), 0,
             "alone over Intel's driver, /yes: no");
    CHECK(!ask, "alone over Intel's driver, /yes: not asked");
    CHECK_EQ(inst_tb_decide(1, 1, 0, 0, 1, 1, 1, 0, &ask, &def), 0,
             "alone over Intel's driver, /list: no");
    CHECK_EQ(inst_tb_decide(1, 1, 1, 1, 1, 1, 0, 0, &ask, &def), 1,
             "over Intel's driver, /thunderbolt /yes: in");
    CHECK(!ask, "/thunderbolt over Intel's driver: not asked");
    CHECK_EQ(inst_tb_decide(1, 1, 0, 0, 0, 1, 0, 0, &ask, &def), 0,
             "with the USB drivers, over Intel's driver: asked");
    CHECK(ask && !def, "with the USB drivers, over Intel's: the default no");
    /* The window's Thunderbolt box's own state (task 44.79): the console's
     * answer with the USB drivers in the run, which the box opens with and
     * takes back when they are ticked again after the connection manager
     * alone - unticked, a controller found or not, but by /thunderbolt;
     * never the connection manager alone's unasked yes. */
    CHECK(inst_tb_box_default(1, 0, 0, 0, 0) == 0 &&
          inst_tb_box_default(1, 1, 0, 0, 0) == 0,
          "the box's own state: unticked, a controller found or not");
    CHECK(inst_tb_box_default(1, 0, 1, 0, 0) == 1 &&
          inst_tb_box_default(1, 1, 1, 1, 0) == 1,
          "the box's own state: /thunderbolt ticks it");
    CHECK(inst_tb_box_default(1, 1, 0, 1, 0) == 0 &&
          inst_tb_box_default(0, 1, 1, 0, 0) == 0,
          "the box's own state: over Intel's driver unticked, no driver none");
    CHECK(inst_tb_decide(1, 0, 1, 0, 1, 0, 0, 0, &ask, &def) == 1 &&
          inst_tb_box_default(1, 0, 0, 0, 0) == 0,
          "the connection manager alone's yes is not the box's own state");
    /* Record 26 section 5.3.8 (task 46.22; owner, 2026-11-04): over an
     * installed Thunderbolt part the default is yes - the box ticked, the
     * question's Enter, /yes and /list - but over Intel's driver; the
     * caller clears installed where Secure Boot refuses. */
    CHECK(inst_tb_box_default(1, 1, 0, 0, 1) == 1 &&
          inst_tb_box_default(1, 0, 0, 0, 1) == 1,
          "installed: the box starts ticked, a controller found or not");
    CHECK(inst_tb_box_default(1, 1, 0, 1, 1) == 0 &&
          inst_tb_box_default(0, 1, 0, 0, 1) == 0,
          "installed: over Intel's driver unticked, no driver none");
    CHECK_EQ(inst_tb_decide(1, 1, 0, 0, 0, 0, 0, 1, &ask, &def), 0,
             "installed: asked");
    CHECK(ask == 1 && def == 1, "installed: asked, Enter for yes");
    CHECK_EQ(inst_tb_decide(1, 1, 1, 0, 0, 0, 0, 1, &ask, &def), 1,
             "installed, /yes: yes (the part is updated)");
    CHECK_EQ(inst_tb_decide(1, 1, 0, 0, 0, 0, 1, 1, &ask, &def), 1,
             "installed, /list: yes");
    CHECK_EQ(inst_tb_decide(1, 1, 1, 0, 0, 1, 0, 1, &ask, &def), 0,
             "installed over Intel's driver, /yes: no");
    CHECK_EQ(inst_tb_decide(1, 1, 1, 0, 1, 1, 0, 1, &ask, &def), 0,
             "installed, the connection manager alone over Intel's: no");
    CHECK_EQ(inst_tb_decide(1, 1, 1, 0, 0, 0, 0, 0, &ask, &def), 0,
             "not installed, /yes: no, as before");
    CHECK(inst_hlpr_removable(1, 1) && !inst_hlpr_removable(1, 0) &&
          !inst_hlpr_removable(0, 1) && !inst_hlpr_removable(0, 0),
          "the helper goes only when neither xhci98 nor xhcicm is left");
    /* Record 26 section 5.3.6 (task 46.5): a USB4 host found by its switch
     * ports on 98 SE and ME, its router not listed: asked with yes the
     * default, /yes and /list give yes, the box opens ticked; /thunderbolt,
     * the connection manager alone and Intel's driver as before. */
    {
        const int u = INST_TB_ROUTER_UNLISTED;
        CHECK_EQ(inst_tb_decide(1, u, 0, 0, 0, 0, 0, 0, &ask, &def), 0,
                 "router not listed: asked");
        CHECK(ask && def, "router not listed: the default yes");
        CHECK_EQ(inst_tb_decide(1, u, 1, 0, 0, 0, 0, 0, &ask, &def), 1,
                 "router not listed, /yes: yes");
        CHECK(!ask, "router not listed, /yes: not asked");
        CHECK_EQ(inst_tb_decide(1, u, 0, 0, 0, 0, 1, 0, &ask, &def), 1,
                 "router not listed, /list: the default yes");
        CHECK_EQ(inst_tb_decide(1, u, 0, 1, 0, 0, 0, 0, &ask, &def), 1,
                 "router not listed, /thunderbolt: yes");
        CHECK_EQ(inst_tb_decide(1, u, 0, 0, 1, 0, 0, 0, &ask, &def), 1,
                 "router not listed, the connection manager alone: yes");
        CHECK_EQ(inst_tb_decide(1, u, 1, 0, 0, 1, 0, 0, &ask, &def), 0,
                 "router not listed over Intel's driver, /yes: no");
        CHECK_EQ(inst_tb_decide(0, u, 1, 1, 0, 0, 0, 0, &ask, &def), 0,
                 "router not listed, no driver: no");
        CHECK(inst_tb_box_default(1, u, 0, 0, 0) == 1 &&
              inst_tb_box_default(1, u, 1, 0, 0) == 1 &&
              inst_tb_box_default(1, u, 0, 1, 0) == 0 &&
              inst_tb_box_default(0, u, 0, 0, 0) == 0,
              "router not listed: the box opens ticked, not over Intel's "
              "driver, none without the driver");
        CHECK(inst_tb_box_default(1, 1, 0, 0, 0) == 0,
              "the router listed: unticked again (revision 23)");
    }
    /* The ids that tell such a host (record 26 section 5.3.6): the
     * ASM4242's own switch alone, its downstream port the one cm_ports.c's
     * ASMedia family names; every other family's id refused. */
    {
        INST_U32 vd;
        const CM_FAMILY *asmf = CmPortsFamily(CM_PORTS_ID(0x1B21, 0x2425));
        ULONG f;
        ULONG i;
        int others = 1;

        CHECK(inst_pci_vendev("PCI\\VEN_1B21&DEV_2421&SUBSYS_00000000&REV_01",
                              &vd) && inst_tb_switch_port(vd),
              "the ASM4242's upstream port");
        CHECK(inst_pci_vendev("PCI\\VEN_1B21&DEV_2423&SUBSYS_00000000&REV_01",
                              &vd) && inst_tb_switch_port(vd),
              "the ASM4242's downstream port");
        CHECK(asmf != NULL && asmf->SwitchPortIdCount == 1 &&
              inst_tb_switch_port(asmf->SwitchPortIds[0]),
              "cm_ports.c's ASMedia family's switch port is taken");
        CHECK(!inst_tb_switch_port(CM_PORTS_ID(0x1B21, 0x2425)) &&
              !inst_tb_switch_port(CM_PORTS_ID(0x1B21, 0x2426)) &&
              !inst_tb_switch_port(CM_PORTS_ID(0x1B36, 0x000C)) &&
              !inst_tb_switch_port(CM_PORTS_ID(0x1B36, 0x0001)) &&
              !inst_tb_switch_port(CM_PORTS_ID(0x8086, 0x244E)) &&
              !inst_tb_switch_port(CM_PORTS_ID(0x2423, 0x1B21)),
              "the ASM4242's NHI and xHCI, QEMU's bridges, an ordinary "
              "bridge and the ids swapped refused");
        for (f = 0; f < CmPortsFamilyCount(); f++) {
            const CM_FAMILY *fam = CmPortsFamilyAt(f);
            for (i = 0; i < fam->NhiIdCount; i++) {
                if (inst_tb_switch_port(fam->NhiIds[i])) {
                    others = 0;
                }
            }
            for (i = 0; i < fam->RootPortIdCount; i++) {
                if (inst_tb_switch_port(fam->RootPortIds[i])) {
                    others = 0;
                }
            }
            for (i = 0; fam != asmf && i < fam->SwitchPortIdCount; i++) {
                if (inst_tb_switch_port(fam->SwitchPortIds[i])) {
                    others = 0;
                }
            }
        }
        CHECK(others, "no other family's NHI, root port or switch port id");
    }
    CHECK(inst_tb_intel("nhi") && inst_tb_intel("NHI") &&
          !inst_tb_intel("xhcicm") && !inst_tb_intel("") &&
          !inst_tb_intel(NULL), "Intel's Thunderbolt driver by its service");
    CHECK(inst_cm_tb1_foreign("PCI\\VEN_8086&DEV_1513&SUBSYS_00001234&REV_01\\3&1&0") &&
          inst_cm_tb1_foreign("PCI\\VEN_8086&DEV_1547&SUBSYS_22B017AA&REV_00") &&
          inst_cm_tb1_foreign("PCI\\VEN_8086&DEV_1513") &&
          !inst_cm_tb1_foreign("PCI\\VEN_8086&DEV_1513&SUBSYS_11112222&REV_01\\3&1&0") &&
          !inst_cm_tb1_foreign("PCI\\VEN_8086&DEV_1547&SUBSYS_11112222&REV_00") &&
          !inst_cm_tb1_foreign("PCI\\VEN_8086&DEV_1577&SUBSYS_0000106B&REV_02") &&
          !inst_cm_tb1_foreign(NULL),
          "a PC's Light or Cactus Ridge: its driver never installed over (record 22 section 19.3)");
    {
        char cid[64];

        CHECK(inst_cm_tb1_classid("PCI\\VEN_8086&DEV_1513&SUBSYS_11112222&REV_01", 1, cid,
                                  sizeof(cid)) &&
              strcmp(cid, "PCI\\VEN_8086&DEV_1513&CC_0880") == 0 &&
              inst_cm_tb1_classid("PCI\\VEN_8086&DEV_1547&SUBSYS_11112222&REV_00", 0, cid,
                                  sizeof(cid)) &&
              strcmp(cid, "PCI\\VEN_8086&DEV_1547&CC_0604") == 0 &&
              !inst_cm_tb1_classid("PCI\\VEN_8086&DEV_1577&SUBSYS_0000106B&REV_02", 1, cid,
                                   sizeof(cid)),
              "a Thunderbolt 1 NHI's and bridge's install calls name their class forms");
    }

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
    CHECK(strstr(inst_unplug_text(INST_UNPLUG_RESTARTED, INST_TOOL_CONSOLE),
                 "not safe") != NULL,
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
    CHECK(inst_eq(inst_unplug_text(INST_UNPLUG_NOW, INST_TOOL_CONSOLE),
                  "It is now safe to disconnect the device."),
          "the owner's words (2026-11-04)");
    /* Roadmap 43.8 (owner, 2026-10-20): after the stop each kind says
     * only what to do now, in either tool; the restarted text names the
     * tool the user is in. */
    {
        int kind;
        int tool;
        for (tool = INST_TOOL_CONSOLE; tool <= INST_TOOL_TREE; tool++) {
            for (kind = INST_UNPLUG_NOW; kind <= INST_UNPLUG_RESTARTED;
                 kind++) {
                const char *s = inst_unplug_text(kind, tool);
                CHECK(strstr(s, "stop them in Windows") == NULL &&
                          strstr(s, "first") == NULL,
                      "no step that comes too late after the stop");
            }
            CHECK(inst_eq(inst_unplug_text(INST_UNPLUG_CHECK, tool),
                          "It is now safe to disconnect the device."),
                  "a dock-shaped unit: unplug it now");
            CHECK(strstr(inst_unplug_text(INST_UNPLUG_OTHERS, tool),
                         "eject them") != NULL, "other units: eject them");
            CHECK(strstr(inst_unplug_text(INST_UNPLUG_RESTARTED, tool),
                         "not safe") != NULL, "restarted: not safe");
        }
        CHECK(strstr(inst_unplug_text(INST_UNPLUG_RESTARTED,
                                      INST_TOOL_CONSOLE),
                     "INSTALL /console /eject") != NULL,
              "the console names its own switch");
        CHECK(strstr(inst_unplug_text(INST_UNPLUG_RESTARTED, INST_TOOL_TREE),
                     "xHCI98 USB Tree") != NULL &&
                  strstr(inst_unplug_text(INST_UNPLUG_RESTARTED,
                                          INST_TOOL_TREE), "INSTALL") == NULL,
              "the USB Tree names itself, not the console");
    }
    /* The reminder, asked before the eject where the unit as it is now
     * would not get "unplug now" after it. */
    CHECK(!inst_eject_remind(1, CM_ST_TUN_NONE),
          "an enclosure alone, no USB3 tunnel: no reminder");
    CHECK(inst_eject_remind(0, CM_ST_TUN_NONE), "a dock-shaped unit: reminded");
    CHECK(inst_eject_remind(1, CM_ST_TUN_UP), "USB3 up: reminded");
    CHECK(inst_eject_remind(1, CM_ST_TUN_UNKNOWN), "USB3 not known: reminded");
    CHECK(strstr(inst_eject_remind_text(), "Before you eject it") != NULL &&
              strstr(inst_eject_remind_text(), "stop them in Windows") != NULL,
          "the reminder's words, before the eject");
    {
        unsigned char r[CM_ST_MAX_BYTES];
        unsigned char *e1 = r + CM_ST_HDR_BYTES;
        unsigned char *e2 = e1 + CM_ST_ENTRY_BYTES;

        CHECK_EQ(CM_ST_O_PORT_COUNT, 48, "port count offset");
        CHECK_EQ(CM_ST_O_PORT_ACTIVE, 52, "active offset");
        CHECK_EQ(CM_ST_O_PORTS, 56, "ports offset");
        CHECK_EQ(CM_ST_MAX_PORTS, 4, "port limit");
        CHECK_EQ(CM_ST_E_TUNNELS, 36, "tunnels offset");
        CHECK_EQ(CM_ST_E_BRIDGE, 40, "bridge offset");
        CHECK_EQ(CM_ST_TUN_UNKNOWN, 5, "unknown tunnel");
        memset(r, 0, sizeof(r));
        put32(r, CM_ST_O_PORT_COUNT, 2);
        put32(r, CM_ST_O_PORT_ACTIVE, 0x2);
        put32(r, CM_ST_O_PORTS, 0x0018);
        put32(r, CM_ST_O_PORTS + 4, 0x0020);
        put32(r, CM_ST_O_ROUTER_COUNT, 2);
        put32(e1, CM_ST_E_BRIDGE, 0x0020);
        put32(e1, CM_ST_E_TUNNELS, (CM_ST_TUN_UP << 4) | CM_ST_TUN_UP);
        put32(e2, CM_ST_E_BRIDGE, 0x0018);
        put32(e2, CM_ST_E_TUNNELS, CM_ST_TUN_UP);
        CHECK_EQ(inst_port_usb3(r, 0x0020), CM_ST_TUN_UP,
                 "the one router behind an active port: its USB3 tunnel");
        put32(e1, CM_ST_E_TUNNELS, CM_ST_TUN_UP);
        CHECK_EQ(inst_port_usb3(r, 0x0020), CM_ST_TUN_NONE, "none");
        CHECK_EQ(inst_port_usb3(r, 0x0018), CM_ST_TUN_UNKNOWN,
                 "a port not active: not known");
        put32(e2, CM_ST_E_BRIDGE, 0x0020);
        CHECK_EQ(inst_port_usb3(r, 0x0020), CM_ST_TUN_UNKNOWN,
                 "two routers behind the port: not known");
        put32(r, CM_ST_O_ROUTER_COUNT, 0);
        CHECK_EQ(inst_port_usb3(r, 0x0020), CM_ST_TUN_UNKNOWN,
                 "no router: not known");
    }
    /* The PCIe tunnel watched after an NT eject (record 24 section 5.2):
     * closed, still named, or never named - a port whose bridge the
     * connection manager does not know (a tunnel found up at start and
     * never learned), which its teardown cannot close. */
    {
        unsigned char r[CM_ST_MAX_BYTES];
        unsigned char *e1 = r + CM_ST_HDR_BYTES;
        unsigned char *e2 = e1 + CM_ST_ENTRY_BYTES;

        CHECK_EQ(CM_ST_O_ROUTER_COUNT, 72, "router count offset");
        CHECK_EQ(CM_ST_TUN_EJECTED, 4, "ejected tunnel");
        CHECK_EQ(CM_ST_HDR_BYTES, 128, "record header");
        CHECK_EQ(CM_ST_ENTRY_BYTES, 128, "router entry");
        memset(r, 0, sizeof(r));
        put32(r, CM_ST_O_PORT_COUNT, 2);
        put32(r, CM_ST_O_PORT_ACTIVE, 0x3);
        put32(r, CM_ST_O_PORTS, 0x0200);
        put32(r, CM_ST_O_PORTS + 4, 0x0208);
        put32(r, CM_ST_O_ROUTER_COUNT, 2);
        put32(e1, CM_ST_E_BRIDGE, 0x0018);
        put32(e1, CM_ST_E_TUNNELS, CM_ST_TUN_UP);
        put32(e2, CM_ST_E_BRIDGE, 0x0200);
        put32(e2, CM_ST_E_TUNNELS, CM_ST_TUN_PRESERVED);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_NAMED,
                 "a router naming the port, its tunnel up: named");
        put32(e2, CM_ST_E_TUNNELS, (CM_ST_TUN_UP << 4) | CM_ST_TUN_EJECTED);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_CLOSED,
                 "torn down after an eject: closed, the USB3 nibble aside");
        CHECK_EQ(inst_port_pcie(r, 0x0208), INST_PCIE_UNNAMED,
                 "another port: not named");
        put32(e2, CM_ST_E_BRIDGE, CM_ST_UNKNOWN);
        put32(e2, CM_ST_E_TUNNELS, CM_ST_TUN_PRESERVED);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_UNNAMED,
                 "an entry of no kind, its port not known: not read");
        CHECK_EQ(CM_ST_KIND_USB4_PORT, 2, "a host port's router");
        CHECK_EQ(CM_ST_KIND_USB4_LINK, 3, "a deeper router");
        put32(e2, CM_ST_E_KIND, CM_ST_KIND_USB4_PORT);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_UNMAPPED,
                 "a host port's router, its tunnel kept from the start and "
                 "its port not known: unmapped");
        put32(e2, CM_ST_E_TUNNELS, (CM_ST_TUN_UP << 4) | CM_ST_TUN_UP);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_UNMAPPED,
                 "the same with its tunnel built: unmapped");
        put32(r, CM_ST_O_PORT_ACTIVE, 0x2);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_UNNAMED,
                 "the port not an active member: not unmapped");
        CHECK_EQ(inst_port_pcie(r, 0x0310), INST_PCIE_UNNAMED,
                 "a chain port, never a member: not unmapped");
        put32(r, CM_ST_O_PORT_ACTIVE, 0x3);
        put32(e2, CM_ST_E_TUNNELS, CM_ST_TUN_NONE);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_UNNAMED,
                 "no PCIe tunnel on it: not unmapped");
        put32(e2, CM_ST_E_TUNNELS, CM_ST_TUN_UP);
        put32(e2, CM_ST_E_KIND, CM_ST_KIND_USB4_LINK);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_UNNAMED,
                 "a deeper router's entry: not read for unmapped");
        put32(e2, CM_ST_E_KIND, CM_ST_KIND_USB4_PORT);
        put32(e1, CM_ST_E_BRIDGE, 0x0200);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_NAMED,
                 "named by one entry, another not known: named");
        put32(e1, CM_ST_E_BRIDGE, 0x0018);
        put32(e2, CM_ST_E_KIND, 0);
        /* The watch's end (inst_pcie_unbound): unmapped with the unit
         * present before the call, never named in the watch, and still
         * unmapped at its last, fresh read. */
        CHECK(inst_pcie_unbound(INST_PCIE_UNMAPPED, 0, 1, INST_PCIE_UNMAPPED),
              "unmapped before, never named, unmapped just now: the line for "
              "an unknown port");
        CHECK(!inst_pcie_unbound(INST_PCIE_NAMED, 0, 1, INST_PCIE_UNMAPPED),
              "named before the call (the unit's router gone since, another "
              "port's unbound): the plain line");
        CHECK(!inst_pcie_unbound(INST_PCIE_UNNAMED, 0, 1, INST_PCIE_UNMAPPED),
              "nothing known before the call: the plain line");
        CHECK(!inst_pcie_unbound(INST_PCIE_UNMAPPED, 1, 1, INST_PCIE_UNMAPPED),
              "named earlier in the watch: the plain line");
        CHECK(!inst_pcie_unbound(INST_PCIE_UNMAPPED, 0, 0, INST_PCIE_UNMAPPED),
              "an unmapped read, then reads that failed: the plain line");
        CHECK(!inst_pcie_unbound(INST_PCIE_UNMAPPED, 0, 1, INST_PCIE_UNNAMED),
              "no host port's router unmapped just now: the plain line");
        CHECK(!inst_pcie_unbound(INST_PCIE_UNMAPPED, 0, 1, INST_PCIE_NAMED),
              "named just now: the plain line");
        /* Whether the last record stands as read just now
         * (inst_watch_current): it must echo the ask made late in the
         * watch; one still echoing the ask made before the call is a
         * value written then, read again, however unmapped it reads. */
        CHECK_EQ(CM_ST_O_ECHO, 124, "echo offset");
        CHECK_EQ(CM_ST_O_STAGE, 32, "stage offset");
        put32(r, CM_ST_O_STAGE, CM_ST_STAGE_ACTIVE);
        put32(e2, CM_ST_E_KIND, CM_ST_KIND_USB4_PORT);
        put32(e2, CM_ST_E_BRIDGE, CM_ST_UNKNOWN);
        put32(e2, CM_ST_E_TUNNELS, CM_ST_TUN_PRESERVED);
        put32(r, CM_ST_O_ECHO, 0x1111UL);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_UNMAPPED,
                 "a frozen record, read whole: unmapped");
        CHECK(!inst_watch_current(r, 0x2223UL, 500UL),
              "it echoes the ask made before the call, not the late one: "
              "not current");
        CHECK(!inst_pcie_unbound(INST_PCIE_UNMAPPED, 0,
                                 inst_watch_current(r, 0x2223UL, 500UL),
                                 inst_port_pcie(r, 0x0200)),
              "so the plain line");
        CHECK(!inst_watch_current(r, 0UL, 500UL),
              "no late ask written (the key refused): not current");
        put32(r, CM_ST_O_ECHO, 0x2223UL);
        CHECK(inst_watch_current(r, 0x2223UL, 3000UL),
              "the late ask echoed, read at the bound: current");
        CHECK(inst_pcie_unbound(INST_PCIE_UNMAPPED, 0,
                                inst_watch_current(r, 0x2223UL, 3000UL),
                                inst_port_pcie(r, 0x0200)),
              "so the line for an unknown port");
        CHECK(inst_watch_current(r, 0x2223UL, INST_WATCH_ASK_MAX_MS),
              "read at the limit after the late ask: current");
        CHECK(!inst_watch_current(r, 0x2223UL, INST_WATCH_ASK_MAX_MS + 1UL),
              "the late echo published, the record frozen since and the "
              "last read made past the limit (the tool descheduled): not "
              "current");
        CHECK(!inst_pcie_unbound(INST_PCIE_UNMAPPED, 0,
                                 inst_watch_current(r, 0x2223UL, 60000UL),
                                 inst_port_pcie(r, 0x0200)),
              "so the plain line");
        /* At the bound (inst_watch_last): the record's age is taken at
         * the decision. Read 3 s after the late ask, in time, then the
         * tool descheduled and the decision made past the limit. */
        CHECK(inst_watch_last(INST_PCIE_UNMAPPED, 0, r,
                              inst_port_pcie(r, 0x0200), 0x2223UL, 1000UL,
                              4000UL),
              "the late echo read 3 s after the ask, decided at once: the "
              "line for an unknown port");
        CHECK(inst_watch_current(r, 0x2223UL, 3000UL) &&
              !inst_watch_last(INST_PCIE_UNMAPPED, 0, r,
                               inst_port_pcie(r, 0x0200), 0x2223UL, 1000UL,
                               1000UL + INST_WATCH_ASK_MAX_MS + 1UL),
              "current when read, the decision past the limit (the tool "
              "descheduled after the read): the plain line");
        CHECK(inst_watch_last(INST_PCIE_UNMAPPED, 0, r,
                              inst_port_pcie(r, 0x0200), 0x2223UL, 1000UL,
                              1000UL + INST_WATCH_ASK_MAX_MS),
              "decided at the limit: the line for an unknown port");
        CHECK(inst_watch_last(INST_PCIE_UNMAPPED, 0, r,
                              inst_port_pcie(r, 0x0200), 0x2223UL,
                              0xFFFFFC18UL, 2000UL),
              "the tick count wrapped between the ask and the decision, "
              "3 s apart: the line for an unknown port");
        CHECK(!inst_watch_last(INST_PCIE_UNMAPPED, 0, r,
                               inst_port_pcie(r, 0x0200), 0x2223UL,
                               0xFFFFFC18UL, 6000UL),
              "wrapped, 7 s apart: the plain line");
        CHECK(!inst_watch_last(INST_PCIE_UNMAPPED, 0, 0, INST_PCIE_UNMAPPED,
                               0x2223UL, 1000UL, 4000UL),
              "the last read failed: the plain line");
        CHECK(!inst_watch_last(INST_PCIE_UNMAPPED, 0, r,
                               inst_port_pcie(r, 0x0200), 0UL, 1000UL,
                               4000UL),
              "no late ask written by this watch: the plain line");
        put32(r, CM_ST_O_STAGE, CM_ST_STAGE_STOPPED);
        CHECK(!inst_watch_current(r, 0x2223UL, 500UL),
              "echoed by a stopping driver: not current");
        put32(r, CM_ST_O_STAGE, 0);
        put32(r, CM_ST_O_ECHO, 0);
        put32(e2, CM_ST_E_KIND, 0);
        put32(e2, CM_ST_E_BRIDGE, 0x0200);
        put32(e2, CM_ST_E_TUNNELS, CM_ST_TUN_EJECTED);
        put32(r, CM_ST_O_ROUTER_COUNT, 1);
        CHECK_EQ(inst_port_pcie(r, 0x0200), INST_PCIE_UNNAMED,
                 "an entry past the router count is not read");
        put32(r, CM_ST_O_ROUTER_COUNT, 0);
        CHECK_EQ(inst_port_pcie(r, 0x0018), INST_PCIE_UNNAMED,
                 "no router: not named");
        CHECK(strstr(inst_pcie_unbound_text(), "stays up until the device "
                                               "is unplugged") != NULL &&
                  strstr(inst_pcie_unbound_text(), "which of its ports") !=
                      NULL,
              "the line for a port the driver does not know");
        CHECK(longest_line(inst_pcie_unbound_text()) <= 79,
              "that line fits the console");
    }

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
    argv1[3] = "98-me";
    CHECK(inst_parse_args(4, argv1, &o, err, sizeof(err)) == 0,
          "INSTALL.BAT's /dir is accepted");
    argv1[1] = "/eject";
    argv1[2] = "/uninstall";
    CHECK_EQ(inst_parse_args(3, argv1, &o, err, sizeof(err)), 1,
             "/eject /uninstall refused");
}

/* The filter INF's place per architecture (record 23 section 4.1): nt\ in
 * the x86 package, kept as it was; the flavour directory itself in the x64
 * package. Its .PNF beside it, and the place the Have Disk text names,
 * short enough that the longest line naming it stays within 79 columns. */
static void test_cmf_place(void)
{
    int a;
    char pnf[64];
    size_t n;

    CHECK(strcmp(inst_cmf_rel(INST_ARCH_X86), "nt\\xhcicmf.inf") == 0,
          "x86: nt\\xhcicmf.inf, as before");
    CHECK(strcmp(inst_cmf_rel(INST_ARCH_AMD64), "xhcicmf.inf") == 0,
          "x64: xhcicmf.inf at the flavour directory's root");
    CHECK(strchr(inst_cmf_rel(INST_ARCH_AMD64), '\\') == NULL,
          "x64: no subdirectory");
    CHECK(strcmp(inst_cmf_pnf(INST_ARCH_X86), "nt\\xhcicmf.PNF") == 0,
          "x86: its .PNF in nt\\");
    CHECK(strcmp(inst_cmf_pnf(INST_ARCH_AMD64), "xhcicmf.PNF") == 0,
          "x64: its .PNF at the root");
    CHECK(strstr(inst_cmf_where(INST_ARCH_X86), "nt\\") != NULL,
          "x86: the text names nt\\");
    CHECK(strstr(inst_cmf_where(INST_ARCH_AMD64), "nt\\") == NULL &&
          strstr(inst_cmf_where(INST_ARCH_AMD64), "x64") != NULL,
          "x64: the text names the x64 copy, not nt\\");
    CHECK(strstr(inst_cmf_where(INST_ARCH_X86), "copy") != NULL &&
          strstr(inst_cmf_where(INST_ARCH_AMD64), "copy") != NULL,
          "Have Disk names the local copy: no wizard takes cm\\xhcicmf.inx");
    for (a = INST_ARCH_X86; a <= INST_ARCH_AMD64; a++) {
        strcpy(pnf, inst_cmf_rel(a));
        n = strlen(pnf);
        strcpy(pnf + n - 4, ".PNF");
        CHECK(strcmp(pnf, inst_cmf_pnf(a)) == 0, "the .PNF is the INF's");
        CHECK(strlen("  Driver, Have Disk, ") + strlen(inst_cmf_where(a)) +
              strlen("), then run INSTALL.BAT again.") <= 79,
              "the Have Disk line within 79 columns");
    }
}

/* The download by Windows family (record 36 sections 6.2 to 6.4). */
static void test_family(void)
{
    int i;
    int f;
    int any;

    CHECK_EQ(inst_family(0, INST_ARCH_X86), INST_FAM_9X, "98 SE and ME: 98-me");
    CHECK_EQ(inst_family(1, INST_ARCH_X86), INST_FAM_NT86, "NT x86: nt-x86");
    CHECK_EQ(inst_family(1, INST_ARCH_AMD64), INST_FAM_NT64, "x64: nt-x64");
    CHECK(strcmp(inst_family_dir(INST_FAM_9X), "98-me") == 0 &&
          strcmp(inst_family_dir(INST_FAM_NT86), "nt-x86") == 0 &&
          strcmp(inst_family_dir(INST_FAM_NT64), "nt-x64") == 0 &&
          inst_family_dir(3) == NULL, "the three folders' names");
    CHECK_EQ(inst_family_of_dir("NT-X86"), INST_FAM_NT86, "any case");
    CHECK_EQ(inst_family_of_dir("release-x86"), -1, "the flavour directory");
    CHECK_EQ(inst_family_of_dir(NULL), -1, "NULL");
    /* Every INF and image at its place. */
    CHECK(strcmp(inst_pkg_rel(INST_FAM_9X, "xhci98.inf"), "xhci98.inf") == 0,
          "the controller's INF at the root");
    CHECK(strcmp(inst_pkg_rel(INST_FAM_NT86, "xhcicm.inf"), "cm\\xhcicm.inf")
          == 0, "the connection manager in cm\\");
    CHECK(strcmp(inst_pkg_rel(INST_FAM_NT86, inst_cmf_rel(INST_ARCH_X86)),
                 "cm\\xhcicmf.inx") == 0, "x86 filter: cm\\xhcicmf.inx");
    CHECK(strcmp(inst_pkg_rel(INST_FAM_NT64, inst_cmf_rel(INST_ARCH_AMD64)),
                 "cm\\xhcicmf.inx") == 0, "x64 filter: cm\\xhcicmf.inx");
    CHECK(strcmp(inst_pkg_rel(INST_FAM_NT86, "nt\\xhcicm.sys"),
                 inst_pkg_rel(INST_FAM_NT86, "xhcicm.sys")) == 0,
          "the filter's own xhcicm.sys is the one in cm\\");
    CHECK(inst_pkg_rel(INST_FAM_9X, inst_cmf_rel(INST_ARCH_X86)) == NULL &&
          inst_pkg_rel(INST_FAM_9X, "nt\\xhcicm.sys") == NULL,
          "98-me: no filter");
    CHECK(inst_pkg_rel(INST_FAM_9X, "xhcihlpr.sys") == NULL &&
          strcmp(inst_pkg_rel(INST_FAM_NT86, "xhcihlpr.sys"), "xhcihlpr.sys")
          == 0 && inst_pkg_rel(INST_FAM_NT64, "xhcihlpr.sys") == NULL,
          "the helper in nt-x86 alone (record 36 section 3.1)");
    CHECK(strcmp(inst_pkg_rel(INST_FAM_9X, "stor98.inf"),
                 "stor98\\stor98.inf") == 0, "98-me: the 9x INF as stor98.inf");
    CHECK(strcmp(inst_pkg_rel(INST_FAM_9X, "stor98.mpd"),
                 "stor98\\stor98.mpd") == 0 &&
          inst_pkg_rel(INST_FAM_NT86, "stor98.mpd") == NULL &&
          inst_pkg_rel(INST_FAM_NT64, "stor98.mpd") == NULL,
          "stor98.mpd in 98-me alone (record 36 section 3.3)");
    CHECK(strcmp(inst_pkg_rel(INST_FAM_9X, INST_VSD_FILE),
                 "stor98\\S98VSD.VXD") == 0 &&
          strcmp(inst_pkg_rel(INST_FAM_9X, INST_PFW_FILE),
                 "cm\\XHCIPFW.VXD") == 0 &&
          strcmp(inst_pkg_rel(INST_FAM_NT86, INST_PFWSYS_FILE),
                 "cm\\XHCIPFW.SYS") == 0 &&
          inst_pkg_rel(INST_FAM_NT86, INST_VSD_FILE) == NULL &&
          inst_pkg_rel(INST_FAM_NT64, INST_PFWSYS_FILE) == NULL,
          "the images no INF names, each in its family");
    CHECK(inst_pkg_rel(INST_FAM_NT86, "xhcicm.cat") == NULL &&
          strcmp(inst_pkg_rel(INST_FAM_NT64, "xhcicm.cat"), "cm\\xhcicm.cat")
          == 0 && strcmp(inst_pkg_rel(INST_FAM_NT64, "xhcicmf.cat"),
                         "cm\\xhcicmf.cat") == 0,
          "x64: each catalog beside its INF");
    CHECK(inst_pkg_rel(INST_FAM_NT86, "unknown.inf") == NULL &&
          inst_pkg_rel(-1, "xhci98.inf") == NULL &&
          inst_pkg_rel(INST_FAM_9X, NULL) == NULL, "no such name or family");
    /* Every engine INF slot has a place in each family that carries it,
     * and no family path is in nt\ or names a filter as an INF. */
    for (i = 0; i < instPkgFileCount; i++) {
        any = 0;
        for (f = 0; f < INST_FAM_COUNT; f++) {
            const char *p = instPkgFiles[i].Fam[f];
            if (p == NULL) {
                continue;
            }
            any = 1;
            CHECK(!inst_pkg_foreign(p), "no family path is 3.3.0.0's layout");
            CHECK(strlen(p) < 40, "within pkgRel's room");
        }
        CHECK(any, "every local name is in some family");
        CHECK(instPkgFileCount <= 48, "within the engine's INST_PKG_MAX");
        CHECK(strlen(instPkgFiles[i].Local) < 32, "within infRel's room");
    }
    CHECK(inst_pkg_foreign("stor98w9.inf") &&
          inst_pkg_foreign("stor98\\stor98w9.inf") &&
          inst_pkg_foreign("nt\\xhcicmf.inf") &&
          inst_pkg_foreign("cm\\xhcicmf.inf") &&
          !inst_pkg_foreign("cm\\xhcicmf.inx") &&
          !inst_pkg_foreign(NULL), "3.3.0.0's names refused");
}

/* XHCITR's "can run faster" notice (record 25 section 7.7 item 7; record 23
 * section 9.5 item 7): the default per system, the setting in force, the
 * switches and /list's line. */
static void test_fast(void)
{
    INST_OPTS o;
    char err[256];
    const char *a1[] = { "x", "/faster" };
    const char *a2[] = { "x", "/nofaster", "/yes" };
    const char *a3[] = { "x", "/faster", "/nofaster" };
    const char *a4[] = { "x", "/faster", "/uninstall" };
    const char *a5[] = { "x", "/nofaster", "/devices" };
    const char *a6[] = { "x", "/eject", "/faster" };

    CHECK(strcmp(INST_FAST_VALUE, "TrayFasterNotice") == 0, "the value's name");
    CHECK_EQ(inst_fast_default(0, 4, 10), 1, "98 SE: on");
    CHECK_EQ(inst_fast_default(0, 4, 90), 1, "ME: on");
    CHECK_EQ(inst_fast_default(1, 5, 0), 1, "2000: on");
    CHECK_EQ(inst_fast_default(1, 5, 1), 1, "XP: on");
    CHECK_EQ(inst_fast_default(1, 5, 2), 1, "XP x64: on");
    CHECK_EQ(inst_fast_default(1, 6, 0), 1, "Vista: on");
    CHECK_EQ(inst_fast_default(1, 6, 1), 1, "7: on");
    CHECK_EQ(inst_fast_default(1, 6, 2), 1, "8: on");
    CHECK_EQ(inst_fast_default(1, 6, 3), 1, "8.1: on");
    CHECK_EQ(inst_fast_default(1, 6, 4), 1, "6.4: on");
    CHECK_EQ(inst_fast_default(1, 10, 0), 1, "10: on (the owner, 2026-10-22)");
    CHECK_EQ(inst_fast_default(1, 11, 0), 1, "later: on");

    CHECK_EQ(inst_fast_on(1, 0, 1, 1, 1), 0, "the user's 0 over the computer's 1");
    CHECK_EQ(inst_fast_on(1, 1, 1, 0, 0), 1, "the user's 1 over the computer's 0");
    CHECK_EQ(inst_fast_on(0, 0, 1, 0, 1), 0, "no user value: the computer's 0");
    CHECK_EQ(inst_fast_on(0, 0, 1, 1, 0), 1, "no user value: the computer's 1");
    CHECK_EQ(inst_fast_on(0, 0, 0, 0, 1), 1, "neither: the default on");
    CHECK_EQ(inst_fast_on(0, 0, 0, 0, 0), 0, "neither: the default off");
    CHECK_EQ(inst_fast_on(1, 2, 1, 0, 1), 0, "the user's 2 is absent: the computer's");
    CHECK_EQ(inst_fast_on(1, 7, 1, 9, 1), 1, "both out of range: the default");
    CHECK_EQ(inst_fast_on(1, 0xFFFFFFFFUL, 0, 0, 0), 0, "-1 is absent");

    CHECK(inst_parse_args(2, (char **)a1, &o, err, sizeof(err)) == 0 && o.Faster &&
              !o.NoFaster, "/faster");
    CHECK(inst_parse_args(3, (char **)a2, &o, err, sizeof(err)) == 0 && o.NoFaster &&
              !o.Faster, "/nofaster");
    CHECK(inst_parse_args(3, (char **)a3, &o, err, sizeof(err)) != 0 &&
              strstr(err, "contradict") != NULL, "both: refused");
    CHECK(inst_parse_args(3, (char **)a4, &o, err, sizeof(err)) != 0 &&
              strstr(err, "/faster belongs to an install") != NULL,
          "with /uninstall: refused");
    CHECK(inst_parse_args(3, (char **)a5, &o, err, sizeof(err)) != 0 &&
              strstr(err, "/nofaster belongs to an install") != NULL,
          "with /devices: refused");
    CHECK(inst_parse_args(3, (char **)a6, &o, err, sizeof(err)) != 0,
          "with /eject: refused");

    CHECK(strstr(inst_fast_list_text(1, 0, 0), "would be turned on") != NULL,
          "/list /faster, none: turned on");
    CHECK(strstr(inst_fast_list_text(1, 1, 0), "would be turned on") != NULL,
          "/list /faster, 0: turned on");
    CHECK(strstr(inst_fast_list_text(1, 1, 1), "nothing to change") != NULL,
          "/list /faster, 1: nothing");
    CHECK(strstr(inst_fast_list_text(0, 1, 0), "nothing to change") != NULL,
          "/list /nofaster, 0: nothing");
    CHECK(strstr(inst_fast_list_text(0, 1, 5), "would be turned off") != NULL,
          "/list /nofaster, 5: turned off");
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

    CHECK_EQ(inst_tray_copy_needed(0, NULL, 1, v300, 1), 1, "absent: copied");
    CHECK_EQ(inst_tray_copy_needed(0, NULL, -1, NULL, 1), 1,
             "absent, package unread: copied");
    CHECK_EQ(inst_tray_copy_needed(1, v300, 1, v301, 1), 1, "older: copied");
    CHECK_EQ(inst_tray_copy_needed(1, v300, 1, v300, 1), 0, "equal: left");
    CHECK_EQ(inst_tray_copy_needed(1, v301, 1, v300, 1), 0, "newer: left");
    CHECK_EQ(inst_tray_copy_needed(-1, NULL, 1, v300, 1), 1,
             "installed unreadable: copied");
    CHECK_EQ(inst_tray_copy_needed(1, v300, -1, NULL, 1), 0,
             "package unreadable: left");
    CHECK_EQ(inst_tray_copy_needed(-1, NULL, -1, NULL, 1), 0,
             "both unreadable: left");
    CHECK_EQ(inst_tray_copy_needed(1, v300, 1, v300, 0), 1,
             "equal, other bytes: copied (record 23 section 5.4.1)");
    CHECK_EQ(inst_tray_copy_needed(1, v300, 1, v300, -1), 0,
             "equal, bytes not read: left");
    CHECK_EQ(inst_tray_copy_needed(1, v301, 1, v300, 0), 0,
             "newer, other bytes: left");

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
    /* /list says what the Run value is now (98 SE leg, 2026-10-19). */
    CHECK(strcmp(inst_tray_list_text(0, INST_TRAY_RUN_KEEP),
                 "; it does not start at login.") == 0,
          "/list: no Run value, /notray: does not start");
    CHECK(strstr(inst_tray_list_text(0, INST_TRAY_RUN_SET),
                 "does not start at login now") != NULL,
          "/list: no Run value after an unticked update: not started now");
    CHECK(strcmp(inst_tray_list_text(1, INST_TRAY_RUN_KEEP),
                 "; it starts at login.") == 0, "/list: the value: starts");
    CHECK(strstr(inst_tray_list_text(1, INST_TRAY_RUN_DELETE),
                 "would stop doing so") != NULL, "/list /notray over the value");
    CHECK(strstr(inst_tray_list_text(2, INST_TRAY_RUN_SET),
                 "other data") != NULL, "/list: other data");
    CHECK(strstr(inst_tray_list_text(2, INST_TRAY_RUN_KEEP),
                 "other data") != NULL, "/list: other data, kept");
    CHECK(strstr(inst_tray_list_text(2, INST_TRAY_RUN_DELETE),
                 "removed") != NULL, "/list: other data, /notray");
    /* SETUPX's OEM Name record of a deleted INF\OTHER copy. */
    CHECK_EQ(inst_oem_name_ours("C:\\WINDOWS\\INF\\OTHER\\Yeo Kheng Mengxhci98.inf",
                                "C:\\WINDOWS\\INF\\OTHER\\Yeo Kheng Mengxhci98.inf",
                                "C:\\WINDOWS\\INF\\OTHER\\YEOKHE~1.INF"), 1,
             "OEM Name: our deleted copy");
    CHECK_EQ(inst_oem_name_ours("c:\\windows\\inf\\other\\YEO KHENG MENGXHCI98.INF",
                                "C:\\WINDOWS\\INF\\OTHER\\Yeo Kheng Mengxhci98.inf",
                                NULL), 1, "OEM Name: case aside");
    CHECK_EQ(inst_oem_name_ours("C:\\WINDOWS\\INF\\OTHER\\YEOKHE~1.INF",
                                "C:\\WINDOWS\\INF\\OTHER\\Yeo Kheng Mengxhci98.inf",
                                "C:\\WINDOWS\\INF\\OTHER\\YEOKHE~1.INF"), 1,
             "OEM Name: the short alias");
    CHECK_EQ(inst_oem_name_ours("C:\\WINDOWS\\INF\\OTHER\\Yeo Kheng Mengxhci98uas.inf",
                                "C:\\WINDOWS\\INF\\OTHER\\Yeo Kheng Mengxhci98.inf",
                                ""), 0, "OEM Name: a longer name is not ours");
    CHECK_EQ(inst_oem_name_ours("C:\\WINDOWS\\INF\\OTHER\\Other Vendorfoo.inf",
                                "C:\\WINDOWS\\INF\\OTHER\\Yeo Kheng Mengxhci98.inf",
                                NULL), 0, "OEM Name: another vendor's stays");
    CHECK_EQ(inst_oem_name_ours("", "", NULL), 0,
             "OEM Name: nothing deleted matches nothing");

    /* Task 47.7: a unit's disk under Enum\SCSI, as the 3.2.0.0 matrix read
     * them on 98 SE. */
    CHECK_EQ(inst_scsi_disk_of(
                 "USBSTOR&DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+&N1STICK01&000",
                 "USBSTOR\\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\\N1STICK01&0",
                 "0", "0"), 1, "SCSI disk: a Bulk-Only unit's");
    CHECK_EQ(inst_scsi_disk_of(
                 "XHCIUAS&DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+&27842-0000:00:03.0-1&000",
                 "XHCIUAS\\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\\27842-0000:00:03.0-1&0",
                 "0", "0"), 1, "SCSI disk: a UAS unit's");
    CHECK_EQ(inst_scsi_disk_of(
                 "usbstor&disk&ven_qemu&prod_qemu_harddisk&rev_2.5+&n1stick01&000",
                 "USBSTOR\\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\\N1STICK01&0",
                 "0", "0"), 1, "SCSI disk: case aside");
    CHECK_EQ(inst_scsi_disk_of(
                 "USBSTOR&DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+&N1STICK01&101",
                 "USBSTOR\\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\\N1STICK01&1",
                 "0", "1"), 1, "SCSI disk: LUN 1's");
    CHECK_EQ(inst_scsi_disk_of(
                 "USBSTOR&DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+&N1STICK03&000",
                 "USBSTOR\\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\\N1STICK01&0",
                 "0", "0"), 0, "SCSI disk: another unit's");
    CHECK_EQ(inst_scsi_disk_of(
                 "USBSTOR&DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+&N1STICK01&001",
                 "USBSTOR\\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\\N1STICK01&0",
                 "0", "0"), 0, "SCSI disk: another LUN's");
    CHECK_EQ(inst_scsi_disk_of(
                 "USBSTOR&DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+&N1STICK01&0",
                 "USBSTOR\\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\\N1STICK01&0",
                 "", ""), 0, "SCSI disk: no target or LUN matches nothing");
    CHECK_EQ(inst_scsi_disk_of(
                 "USBSTOR&DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+&N1STICK01&0x0",
                 "USBSTOR\\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\\N1STICK01&0",
                 "x", "0"), 0, "SCSI disk: a target not of digits");
    CHECK_EQ(inst_scsi_disk_of("PCI&VEN_8086&DEV_7010&000", "", "0", "0"), 0,
             "SCSI disk: no unit matches nothing");
    CHECK_EQ(inst_scsi_disk_of(
                 "MF&CHILD0001&PCI&VEN_8086&DEV_7010&SUBSYS_11001AF4&REV_00&BUS_00&DEV_01&FUNC_0100",
                 "USBSTOR\\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\\N1STICK01&0",
                 "0", "0"), 0, "SCSI disk: the IDE CD-ROM's is not a unit's");
    CHECK_EQ(inst_uas_unit_id("XHCIUAS\\DISK&VEN_QEMU&PROD_QEMU_HARDDISK&REV_2.5+\\1&0"),
             1, "UAS unit: the XHCIUAS enumerator");
    CHECK_EQ(inst_uas_unit_id("USBSTOR\\DISK&VEN_QEMU\\1&0"), 0,
             "UAS unit: not a Bulk-Only unit");
    CHECK_EQ(inst_uas_unit_id("USB\\VID_46F4&PID_0003\\1"), 0,
             "UAS unit: not the UAS device");

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
        for (b = 0; b < INST_CHOICES; b++) {
            named += instValues[i].Choice[b] != NULL;
            if ((instValues[i].Settable & (1UL << b)) == 0 &&
                b >= instValues[i].PickCount) {
                /* a row of picks: Choice[k] is its k-th pick's */
                CHECK(instValues[i].Choice[b] == NULL,
                      "a choice has a meaning only where the value is allowed");
            }
        }
        CHECK(named > 0 || instValues[i].Hi != 0 ||
                  instValues[i].Kind == INST_VK_TEXT,
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
    CHECK(strstr(out, "XhciCmApprove - connect Thunderbolt / USB4 PCIe "
                      "devices automatically\n") == out,
          "the name and meaning first");
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
    CHECK(strstr(out, "NOTE: change only takes effect after restart") != NULL,
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
    /* Task 49.38: the interrupt mode's caution is one text, with mode
     * 4's way back (it is never asked; Advanced and /set show it). */
    inst_value_question(out, sizeof(out), mode, 0x17, 0, now, 1, 0, 0, &enter);
    CHECK(strstr(out, "  NOTE: with 1 or 3, the controller") != NULL &&
              strstr(out, "Last Known Good") != NULL &&
              strstr(out, "msi4") == NULL,
          "the interrupt mode's caution, with mode 4's way back, no "
          "component named");
    inst_caution_text(mode, 1UL << 3, longName, sizeof(longName));
    CHECK(strncmp(longName, "with 1 or 3, ", 13) == 0, "/set 3: the caution");
    inst_caution_text(inst_value_find("XhciForceBulkOnly"), 0x3, longName,
                      sizeof(longName));
    CHECK(strncmp(longName, "applies when", 12) == 0, "a plain caution as is");
    CHECK(strstr(out, "    2  always poll\n") != NULL &&
          strstr(out, "    3  ") == NULL, "3 not offered: not listed");
    inst_value_question(out, sizeof(out), clean, 0x7, 0, now, 1, 0, 1, &enter);
    CHECK(strstr(out, "    2  remove all\n") != NULL, "a three-way choice");
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
    CHECK(strstr(out, "Install folder for the xHCI98 tools\n") == out &&
          strstr(out, "  XHCITR.EXE  xHCI98 USB Tree") != NULL &&
          strstr(out, "8 characters") == NULL &&
          strcmp(out + strlen(out) - 25, "  Enter for [C:\\XHCI98]: ") == 0,
          "NT: the tool, then Enter for [C:\\XHCI98]");
    inst_dir_question(out, sizeof(out), "XHCITR.EXE", "XHCIWIZ.EXE",
                      "*.INF, *.SYS",
                      "C:\\WINDOWS\\XHCI98", "C:\\WINDOWS\\XHCI98", 0, 0);
    CHECK(strstr(out, "  *.INF, *.SYS  this package's drivers, for Windows' "
                      "hardware wizard\n") != NULL &&
          strstr(out, "  now: C:\\WINDOWS\\XHCI98\n") != NULL &&
          strstr(out, "  XHCIWIZ.EXE   Setup, which Add/Remove "
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

/* The gate before a pass on 98 SE and ME (record 27 section 6,
 * W9XGATE.1): asked once, then only when a wizard is seen open or the
 * look fails. */
static void test_gate(void)
{
    CHECK_EQ(inst_gate_ask(0, 1, 0), 1, "gate: the first call asks");
    CHECK_EQ(inst_gate_ask(0, 0, 0), 1, "gate: the first call asks, none seen");
    CHECK_EQ(inst_gate_ask(0, -1, 0), 1, "gate: the first call asks, no look");
    CHECK_EQ(inst_gate_ask(0, 0, -1), 1, "gate: first, language unread: asks");
    CHECK_EQ(inst_gate_ask(0, 0, 1), 0, "gate: first, English, none seen: not asked");
    CHECK_EQ(inst_gate_ask(0, 1, 1), 1, "gate: first, English, a wizard open: asked");
    CHECK_EQ(inst_gate_ask(0, -1, 1), 1, "gate: first, English, no look: asked");
    CHECK_EQ(inst_gate_ask(1, 0, 0), 0, "gate: later, no wizard: not asked (0)");
    CHECK_EQ(inst_gate_ask(1, 1, 0), 1, "gate: later, a wizard open: asked (0)");
    CHECK_EQ(inst_gate_ask(1, -1, 0), 1, "gate: later, no look: asked (0)");
    CHECK_EQ(inst_gate_ask(1, 0, 1), 0, "gate: later, no wizard: not asked (1)");
    CHECK_EQ(inst_gate_ask(1, 1, 1), 1, "gate: later, a wizard open: asked (1)");
    CHECK_EQ(inst_gate_ask(1, -1, 1), 1, "gate: later, no look: asked (1)");
    CHECK_EQ(inst_gate_ask(1, 0, -1), 0, "gate: later, no wizard: not asked (-1)");
    CHECK_EQ(inst_gate_ask(1, 1, -1), 1, "gate: later, a wizard open: asked (-1)");
    CHECK_EQ(inst_gate_ask(1, -1, -1), 1, "gate: later, no look: asked (-1)");
    CHECK_EQ(inst_gate_english(0x0409, 0x0409), 1, "gate: US English");
    CHECK_EQ(inst_gate_english(0x0809, 0x0409), 1, "gate: UK default, US build");
    CHECK_EQ(inst_gate_english(0x0407, 0x0409), 0, "gate: German default, US build");
    CHECK_EQ(inst_gate_english(0x0409, 0x0407), 0, "gate: US default, German build");
    CHECK_EQ(inst_gate_english(0x040C, 0x040C), 0, "gate: French");
    CHECK_EQ(inst_gate_english(-1, 0x0409), -1, "gate: default unread");
    CHECK_EQ(inst_gate_english(0x0409, -1), -1, "gate: resource unread");
    CHECK_EQ(inst_gate_english(0x0009, 0x0009), 1, "gate: neutral English");

    CHECK(inst_wizard_title("Found New Hardware"), "wizard: Found New Hardware");
    CHECK(inst_wizard_title("New Hardware Found"), "wizard: New Hardware Found");
    CHECK(inst_wizard_title("Add New Hardware Wizard"),
          "wizard: Add New Hardware Wizard");
    CHECK(inst_wizard_title("update device driver wizard"),
          "wizard: Update Device Driver Wizard, any case");
    CHECK(!inst_wizard_title("xHCI98 Setup"), "wizard: not the installer");
    CHECK(!inst_wizard_title("Add New Hardware"), "wizard: a part is not one");
    CHECK(!inst_wizard_title("Found New Hardware Wizard Help"),
          "wizard: a longer title is not one");
    CHECK(!inst_wizard_title(""), "wizard: an empty title");
    CHECK(!inst_wizard_title(NULL), "wizard: no title");
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
    /* Record 27 section 4.1 (task 45.38): nocopy after force and quiet. */
    CHECK(inst_di_jobfile_ex(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL,
                             id, inf, 1, 1, 1) &&
          strstr(tail, "\r\nFLAGS force quiet nocopy\r\nEND\r\n") != NULL,
          "force, quiet and nocopy");
    CHECK(inst_di_jobfile_ex(tail, sizeof(tail), INST_DI_INSTALL, res, 0UL,
                             id, inf, 0, 0, 1) &&
          strstr(tail, "\r\nFLAGS nocopy\r\nEND\r\n") != NULL,
          "nocopy alone");
    CHECK(inst_di_jobfile_ex(tail, sizeof(tail), INST_DI_REMOVE, res, 0UL,
                             id, NULL, 0, 0, 1) &&
          strstr(tail, "FLAGS") == NULL,
          "a removal takes no nocopy");
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
          strstr(out, "now: 1 (all controllers)") != NULL &&
          strstr(out, "own value: 2, ignored") != NULL &&
          strstr(out, "own value: 0, ignored") != NULL &&
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
    /* The ranges (record 26 section 5.5) and a 98 SE and ME value. */
    CHECK(poll >= 0 && inst_value_allowed(poll, &e, 50) &&
          inst_value_allowed(poll, &e, 5000) &&
          !inst_value_allowed(poll, &e, 49) &&
          !inst_value_allowed(poll, &e, 5001), "a range's bounds");
    CHECK(inst_value_set("XhciCmCmdTimeoutMs=60000", &mode, &d) == 1 && d == 60000UL &&
          inst_value_set("XhciCmCmdTimeoutMs=60001", &mode, &d) == -1,
          "/set takes a range's values");
    CHECK(br >= 0 && !instValues[br].NoInf && instValues[br].Def == 1UL &&
          !inst_value_default(br, NULL, &d),
          "XhciCmBridgeNumbering: written by the INF (task 44.46), so "
          "its default needs it");
    CHECK(inst_value_set("XhciCmBridgeNumbering=0", &mode, &d) == 1 &&
          mode == br && d == 0UL &&
          inst_value_set("XhciCmBridgeNumbering=2", &mode, &d) == -1,
          "/set takes XhciCmBridgeNumbering 0 and 1");
    mode = inst_value_find("XhciInterruptMode");
    CHECK(inst_value_set_mask(br, &e) == 0, "a 98 SE and ME value on NT");
    e = env(0, 4, 10, INST_ARCH_X86);
    e.CmNhi = 1;
    CHECK(inst_value_relevant(br, &e) && !inst_value_applies(br, &e),
          "98 SE with a host interface: an Advanced row, never asked");
    CHECK(inst_value_allowed(mode, &e, 2) && !inst_value_allowed(mode, &e, 3),
          "a list's values by the system");
}

/* ---- record 26 section 5.4: a row of another system, greyed ----------- */

static void test_greyed(void)
{
    INST_ENV e;
    int sil = inst_value_find("XhciSilentInstall");
    int fine = inst_value_find("XhciPollFineTimer");
    int psw = inst_value_find("XhciIntelPortSwitch");
    int mode = inst_value_find("XhciInterruptMode");
    int br = inst_value_find("XhciCmBridgeNumbering");
    int cm = inst_value_find("XhciCmMode");

    CHECK(sil >= 0 && fine >= 0 && psw >= 0 && mode >= 0 && br >= 0 &&
          cm >= 0, "the values are in the table");
    e = env(1, 6, 1, INST_ARCH_X86);
    e.AnyXhci = 1;
    e.PswController = 1;
    CHECK(inst_value_greyed(sil, &e) && !inst_value_applies(sil, &e) &&
          inst_value_set_mask(sil, &e) == 0,
          "7: the 98 SE and ME switch greyed, never asked or set");
    CHECK(strcmp(inst_value_only(sil), "Windows 98 SE and ME only") == 0,
          "and says which systems");
    CHECK(inst_value_greyed(br, &e) && !inst_value_greyed(cm, &e) &&
          !inst_value_greyed(fine, &e) && !inst_value_greyed(mode, &e) &&
          !inst_value_greyed(psw, &e),
          "7: the 98 SE and ME Thunderbolt value greyed, the rest not");
    e.PswController = 0;
    CHECK(inst_value_greyed(psw, &e) && !inst_value_applies(psw, &e) &&
          strcmp(inst_value_only(psw), "Intel 7-9 series, C610 only") == 0,
          "no port-switch controller: the switch greyed");
    e = env(0, 4, 10, INST_ARCH_X86);
    e.AnyXhci = 1;
    e.PswController = 1;
    CHECK(inst_value_greyed(fine, &e) && !inst_value_relevant(fine, &e) &&
          strcmp(inst_value_only(fine), "Windows 2000 to 7 only") == 0,
          "98 SE: the 2000-to-7 value greyed");
    CHECK(!inst_value_greyed(sil, &e) && !inst_value_greyed(br, &e) &&
          !inst_value_greyed(mode, &e) && !inst_value_greyed(psw, &e),
          "98 SE: its own values not greyed");
    CHECK(inst_value_only(mode) == NULL && inst_value_only(cm) == NULL &&
          inst_value_only(-1) == NULL && !inst_value_greyed(-1, &e),
          "a value of every system has no words");
    e.AnyXhci = 0;
    CHECK(!inst_value_greyed(fine, &e) && !inst_value_greyed(psw, &e),
          "no controller: nothing greyed for it");
    CHECK(!inst_s98_greyed(INST_S98_GONE, 0) &&
          !inst_s98_greyed(INST_S98_IDLE, 0) &&
          !inst_s98_greyed(INST_S98_GONE, 1) &&
          !inst_s98_greyed(INST_S98_WRITETHROUGH, 0) &&
          !inst_s98_greyed(INST_S98_WRITETHROUGH, 1) &&
          !inst_s98_greyed(-1, 0),
          "Stor98: gone, idle and the rest settable everywhere (3.3.0.0)");
    CHECK(!inst_s98_greyed(INST_S98_HOLD9X, 0) &&
          inst_s98_greyed(INST_S98_HOLD9X, 1) &&
          strcmp(inst_s98_only(INST_S98_HOLD9X),
                 "Windows 98 SE and ME only") == 0,
          "Stor98: StorHold9xMs greyed on 2000 to 10, 98 SE and ME only");
    CHECK(inst_s98_only(INST_S98_GONE) == NULL &&
          inst_s98_only(INST_S98_IDLE) == NULL &&
          inst_s98_only(INST_S98_WRITETHROUGH) == NULL &&
          inst_s98_only(INST_S98_COUNT) == NULL,
          "Stor98: no other row says it is for one family");
}

/* ---- record 26 section 5.3: the UAS driver's box (owner, 2026-10-19) --- */

static void test_uas(void)
{
    INST_OPTS o;
    char err[128];
    char *argv[4];

    CHECK(inst_uas_decide(1, 0) && !inst_uas_decide(1, 1) &&
          !inst_uas_decide(0, 0) && !inst_uas_decide(0, 1),
          "UAS in the run only with the USB drivers and without /nouas");
    /* Windows 8 and 8.1 (task 43.29, owner 2026-10-22): xhciuas.inf
     * staged, never forced; Windows' own UAS driver stays the default. */
    CHECK(inst_uas_nt6_action(INST_OS_CMONLY, INST_ACT_INSTALL) ==
              INST_ACT_WINDOWS &&
          inst_uas_nt6_action(INST_OS_CMONLY, INST_ACT_FOREIGN) ==
              INST_ACT_FOREIGN &&
          inst_uas_nt6_action(INST_OS_CMONLY, INST_ACT_UPDATE) ==
              INST_ACT_UPDATE &&
          inst_uas_nt6_action(INST_OS_CMONLY, INST_ACT_CURRENT) ==
              INST_ACT_CURRENT &&
          inst_uas_nt6_action(INST_OS_CMONLY, INST_ACT_NEWER) ==
              INST_ACT_NEWER,
          "8 and 8.1: a UAS disk with no driver left for Windows' own, xhciuas "
          "kept current where it runs");
    CHECK(inst_uas_nt6_action(INST_OS_FULL, INST_ACT_INSTALL) ==
              INST_ACT_INSTALL &&
          inst_uas_nt6_action(INST_OS_REFUSED, INST_ACT_INSTALL) ==
              INST_ACT_INSTALL,
          "2000 to 7, 98 SE and ME: a UAS disk with no driver installed");
    CHECK(!inst_action_runs(INST_ACT_WINDOWS, 0) &&
          !inst_action_runs(INST_ACT_WINDOWS, 1),
          "left for Windows: never run, /force neither");
    CHECK(strstr(inst_action_text(INST_ACT_WINDOWS),
                 "use Windows UAS driver") != NULL,
          "left for Windows: said so");
    argv[0] = "x";
    argv[1] = "/nouas";
    CHECK(inst_parse_args(2, argv, &o, err, sizeof(err)) == 0 && o.NoUas,
          "/nouas");
    argv[2] = "/uninstall";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) == 1,
          "/nouas belongs to an install");
    CHECK(inst_parse_args(1, argv, &o, err, sizeof(err)) == 0 && !o.NoUas,
          "no switch: UAS in the run");
}

/* ---- the takeover (record 23 section 5.4.2; task 49.44) ----------------- */

/* 1 when every line of text is within cols columns. */
static int take_cols(const char *text, size_t cols)
{
    size_t col = 0;

    for (; *text != '\0'; text++) {
        col = *text == '\n' ? 0 : col + 1;
        if (col > cols) {
            return 0;
        }
    }
    return 1;
}

static void test_takeover(void)
{
    INST_OPTS o;
    char err[256];
    char *argv[4];
    char t[1024];
    char w[2048];
    char s[16];
    char v[64];
    char f[64];
    int n;

    /* The candidates. */
    CHECK(inst_take_candidate(1, INST_OS_FULL, "nusb3xhc") &&
          inst_take_candidate(1, INST_OS_FULL, "iusb3xhc") &&
          inst_take_candidate(1, INST_OS_CMONLY, "asmtxhci"),
          "takeover: a manufacturer's driver on 2000 to 10 is a candidate");
    CHECK(!inst_take_candidate(1, INST_OS_FULL, "xhci98") &&
          !inst_take_candidate(1, INST_OS_FULL, "XHCI98") &&
          !inst_take_candidate(1, INST_OS_FULL, "") &&
          !inst_take_candidate(1, INST_OS_FULL, NULL),
          "takeover: this package's own, or no driver, is none");
    CHECK(!inst_take_candidate(1, INST_OS_CMONLY, "USBXHCI") &&
          !inst_take_candidate(1, INST_OS_CMONLY, "usbxhci") &&
          inst_take_candidate(1, INST_OS_FULL, "USBXHCI"),
          "takeover: Windows' own USBXHCI on 8 to 10 stays case 3a's");
    CHECK(!inst_take_candidate(0, INST_OS_FULL, "nusb3xhc"),
          "takeover: none on 98 SE and ME");

    /* The reasons and the rows. */
    CHECK(inst_take_reason(0, 0, 0) == INST_TAKE_OFF &&
          inst_take_reason(0, 1, 1) == INST_TAKE_OFF &&
          inst_take_reason(1, 0, 0) == INST_TAKE_YES &&
          inst_take_reason(1, 1, 0) == INST_TAKE_BOOTDISK &&
          inst_take_reason(1, 1, 1) == INST_TAKE_BOOTDISK &&
          inst_take_reason(1, -1, 0) == INST_TAKE_BOOTUNK &&
          inst_take_reason(1, 0, 1) == INST_TAKE_STOPPED,
          "takeover: the reasons");
    CHECK(strcmp(inst_take_row_text(INST_TAKE_OFF, 0),
                 "Skip (other driver)") == 0 &&
          strcmp(inst_take_row_text(INST_TAKE_OFF, 1),
                 "Skip (other driver)") == 0 &&
          strcmp(inst_take_row_text(INST_TAKE_YES, 0),
                 "Replace other driver") == 0 &&
          strcmp(inst_take_row_text(INST_TAKE_YES, 1),
                 "Replace other driver (/takeover)") == 0 &&
          strcmp(inst_take_row_text(INST_TAKE_BOOTDISK, 1),
                 "Skip (other driver; the Windows disk is behind it)") == 0 &&
          strcmp(inst_take_row_text(INST_TAKE_BOOTUNK, 0),
                 "Replace other driver") == 0 &&
          strcmp(inst_take_row_text(INST_TAKE_STOPPED, 0),
                 "Skip (other driver; an earlier replacement failed)") == 0,
          "takeover: the rows");
    CHECK(strcmp(inst_take_row_text(INST_TAKE_OFF, 0),
                 inst_action_text(INST_ACT_FOREIGN)) == 0,
          "takeover: the choice off reads as before");
    CHECK(inst_take_runs(INST_TAKE_YES) && inst_take_runs(INST_TAKE_BOOTUNK) &&
          !inst_take_runs(INST_TAKE_OFF) &&
          !inst_take_runs(INST_TAKE_BOOTDISK) &&
          !inst_take_runs(INST_TAKE_STOPPED),
          "takeover: an unread Windows disk is warned and replaced (owner, "
          "2026-11-13)");
    CHECK(inst_take_able(0, 0) && inst_take_able(-1, 0) &&
          !inst_take_able(1, 0) && !inst_take_able(1, 1) &&
          !inst_take_able(0, 1),
          "takeover: a candidate is replaceable as its reason with the "
          "choice on");
    {
        /* The texts that announce a replacement (the warning's paragraph,
         * the hint, the box) are due only where one can happen (owner,
         * 2026-11-14): the plans by inst_boot_path_has's answers. */
        static const int allRefused[2] = { 1, 1 };
        static const int oneEach[2] = { 0, 1 };
        static const int unreadAndRefused[2] = { -1, 1 };
        int able;
        int k;

        able = 0;
        for (k = 0; k < 2; k++) {
            able += inst_take_able(allRefused[k], 0);
        }
        CHECK(able == 0 && inst_take_said(2, able) == 0,
              "takeover: every candidate refused - no paragraph, hint or box");
        CHECK(inst_take_said(1, inst_take_able(1, 0)) == 0,
              "takeover: the one candidate refused - nothing announced");
        able = 0;
        for (k = 0; k < 2; k++) {
            able += inst_take_able(oneEach[k], 0);
        }
        CHECK(able == 1 && inst_take_said(2, able) == 2,
              "takeover: one eligible, one refused - shown as before, both "
              "counted");
        able = 0;
        for (k = 0; k < 2; k++) {
            able += inst_take_able(unreadAndRefused[k], 0);
        }
        CHECK(able == 1 && inst_take_said(2, able) == 2,
              "takeover: an unread disk is replaced, so it is announced");
        CHECK(inst_take_said(0, 0) == 0,
              "takeover: no candidate - nothing announced, as before");
        CHECK(inst_take_said(3, 3) == 3 && inst_take_said(1, 1) == 1,
              "takeover: every candidate eligible - as before");
    }
    {
        /* The engine's sequence (inst_eng.c plan, take_lock): the plan
         * shown, the warning's set taken at the run's start, then a
         * barrier's reading that finds a candidate not seen before. */
        int warnShown;
        int r;

        /* The plan shown, /takeover: the one candidate refused. */
        r = inst_take_reason(inst_take_offered(1, 0, 0), 1, 0);
        warnShown = inst_take_said(1, inst_take_able(1, 0)) > 0;
        CHECK(r == INST_TAKE_BOOTDISK && !warnShown,
              "takeover sequence: all refused - no warning, the refusal row");
        /* The barrier finds an eligible one; the warning covered none. */
        r = inst_take_reason(inst_take_offered(1, 1, 0), 0, 0);
        CHECK(r == INST_TAKE_OFF && !inst_take_runs(r) &&
              strcmp(inst_take_row_text(r, 1),
                     inst_action_text(INST_ACT_FOREIGN)) == 0,
              "takeover sequence: a controller found after a suppressed "
              "warning is not replaced, \"Skip (other driver)\"");
        /* No candidate in the plan shown: the same (before 2026-11-14 a
         * controller found at a barrier was replaced unwarned). */
        warnShown = inst_take_said(0, 0) > 0;
        r = inst_take_reason(inst_take_offered(1, 1, 0), 0, 0);
        CHECK(!warnShown && !inst_take_runs(r),
              "takeover sequence: no candidate shown - one found later is "
              "left");
        /* Warned for A; B appears later: A replaced, B left. */
        CHECK(inst_take_runs(inst_take_reason(inst_take_offered(1, 1, 1), 0,
                                              0)) &&
              !inst_take_runs(inst_take_reason(inst_take_offered(1, 1, 0), 0,
                                               0)),
              "takeover sequence: only the controllers the warning covered");
        CHECK(inst_take_offered(1, 0, 0) && !inst_take_offered(0, 0, 1) &&
              !inst_take_offered(0, 1, 1),
              "takeover sequence: before the run, the choice alone; off, "
              "never");
    }
    inst_take_bootunk_text("Renesas USB 3.0 eXtensible Host Controller - 0100 "
                           "(Microsoft)", 0, t, sizeof(t));
    CHECK(strcmp(t, "Setup cannot tell whether the Windows disk is behind "
                    "Renesas USB 3.0 eXtensible Host Controller - 0100 "
                    "(Microsoft). If it is, do not go on: Windows may not "
                    "start once its driver is replaced.") == 0,
          "takeover: the unread Windows disk's warning");
    inst_wrap_lines("  ", t, INST_WARN_COLUMNS, w, sizeof(w));
    CHECK(take_cols(w, 78), "takeover: that warning within 78 columns");
    {
        static const char *const nm[3] = { "A", "B", "C" };
        inst_take_bootunk_many(nm, 1, t, sizeof(t));
        CHECK(strcmp(t, "Setup cannot tell whether the Windows disk is behind "
                        "A. If it is, do not go on: Windows may not start once "
                        "its driver is replaced.") == 0,
              "takeover: one unplaced disk, the one paragraph");
        inst_take_bootunk_many(nm, 2, t, sizeof(t));
        CHECK(strstr(t, "behind A or B. If it is,") != NULL,
              "takeover: two unplaced disks in one paragraph");
        inst_take_bootunk_many(nm, 3, t, sizeof(t));
        CHECK(strstr(t, "behind A, B or C. If it is,") != NULL,
              "takeover: three");
        inst_take_bootunk_many(nm, 4, t, sizeof(t));
        CHECK(strstr(t, "behind A, B, C or one more controller. If") != NULL,
              "takeover: four, the fourth counted");
        inst_take_bootunk_many(nm, 20, t, sizeof(t));
        CHECK(strstr(t, "behind A, B, C or 17 more controllers. If") != NULL &&
              strlen(t) < 900, "takeover: twenty, bounded");
    }
    inst_take_bootunk_text("X", 1, t, sizeof(t));
    CHECK(strcmp(t, "  Setup could not tell whether the Windows disk is "
                    "behind it.\n") == 0 && take_cols(t, 78),
          "takeover: the unread Windows disk's result line");

    /* The switch. */
    CHECK(inst_take_switch(0, INST_OS_FULL, 0) != NULL &&
          strstr(inst_take_switch(0, INST_OS_FULL, 1), "98 SE and ME") != NULL,
          "takeover: refused on 98 SE and ME");
    CHECK(inst_take_switch(1, INST_OS_CMONLY, 0) != NULL &&
          strstr(inst_take_switch(1, INST_OS_CMONLY, 0), "/usb") != NULL &&
          inst_take_switch(1, INST_OS_CMONLY, 1) == NULL &&
          inst_take_switch(1, INST_OS_FULL, 0) == NULL,
          "takeover: on 8 to 10 only with /usb; on 2000 to 7 alone");
    argv[0] = "x";
    argv[1] = "/takeover";
    CHECK(inst_parse_args(2, argv, &o, err, sizeof(err)) == 0 && o.Takeover,
          "/takeover");
    CHECK(inst_parse_args(1, argv, &o, err, sizeof(err)) == 0 && !o.Takeover,
          "no /takeover: off");
    argv[2] = "/cmonly";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) == 1 &&
          strcmp(err, "/takeover needs the USB drivers: not with /cmonly") ==
              0, "/takeover with /cmonly refused");
    argv[2] = "/uninstall";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) == 1,
          "/takeover belongs to an install (/uninstall)");
    argv[2] = "/devices";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) == 1,
          "/takeover belongs to an install (/devices)");
    argv[2] = "/list";
    CHECK(inst_parse_args(3, argv, &o, err, sizeof(err)) == 0 &&
          o.Takeover && o.List, "/takeover with /list");

    /* The console's hint, the window's box and sentence. */
    inst_take_hint(1, t, sizeof(t));
    CHECK(strcmp(t, "1 USB 3 controller runs another manufacturer's driver: "
                    "/takeover replaces it.\n") == 0, "takeover: hint, one");
    inst_take_hint(12, t, sizeof(t));
    CHECK(strcmp(t, "12 USB 3 controllers run another manufacturer's driver: "
                    "/takeover replaces it.\n") == 0 && take_cols(t, 78),
          "takeover: hint, several, within 78 columns");
    inst_take_box_text(1, t, sizeof(t));
    CHECK(strcmp(t, "Replace another manufacturer's driver on 1 USB 3 controller")
              == 0, "takeover: box, one");
    inst_take_box_text(99, t, sizeof(t));
    CHECK(strcmp(t, "Replace another manufacturer's driver on 99 USB 3 controllers")
              == 0 &&
          inst_text_lines(t, inst_page_cols(272)) <= inst_page_rows(12),
          "takeover: box, several, one line in its control");
    inst_take_line_text(1, "nusb3xhc", t, sizeof(t));
    CHECK(strcmp(t, "It runs nusb3xhc now. If unticked, Setup leaves it as "
                    "it is.") == 0, "takeover: sentence, one");
    inst_take_line_text(2, "nusb3xhc", t, sizeof(t));
    CHECK(strcmp(t, "They run nusb3xhc now. If unticked, Setup leaves them "
                    "as they are.") == 0, "takeover: sentence, several");
    inst_take_line_text(2, NULL, t, sizeof(t));
    CHECK(strcmp(t, "They run other drivers now. If unticked, Setup leaves "
                    "them as they are.") == 0,
          "takeover: sentence, several drivers");
    inst_take_line_text(1, "abcdefghijklmnopqrstuvwxyz", t, sizeof(t));
    CHECK(strcmp(t, "It runs another driver now. If unticked, Setup leaves "
                    "it as it is.") == 0,
          "takeover: sentence, a name too long to say");
    inst_take_line_text(3, "abcdefghijklmnopqrstuvwx", t, sizeof(t));
    CHECK(strstr(t, "abcdefghijklmnopqrstuvwx") != NULL &&
          inst_text_lines(t, inst_page_cols(260)) <= inst_page_rows(18),
          "takeover: sentence at its longest name fits two lines");

    /* The warning. */
    inst_comp_warn_text(INST_CW_TAKEOVER, "C:\\XHCI98", t, sizeof(t));
    CHECK(strstr(t, "Another manufacturer's driver runs one or more USB 3 "
                    "controllers here. If ticked,") == t &&
          strstr(t, "filter driver") != NULL &&
          strstr(t, "Windows Update") != NULL,
          "takeover: the warning's words");
    inst_wrap_lines("  ", t, INST_WARN_COLUMNS, w, sizeof(w));
    CHECK(take_cols(w, 78), "takeover: the warning within 78 columns");
    inst_comp_warn_text(INST_CW_TAKEOVER | INST_CW_USB8 | INST_CW_INTELTB,
                        "C:\\XHCI98", w, sizeof(w));
    CHECK(strstr(w, "On Windows 8, 8.1 and 10") < strstr(w, "Another manufacturer's") &&
          strstr(w, "Another manufacturer's") < strstr(w, "Intel's Thunderbolt"),
          "takeover: the warning after the USB drivers', before Intel's");
    CHECK(inst_comp_warnings(0, 0, 0, 1, 1, 0, 0, 1, 1) & INST_CW_TAKEOVER ?
          0 : 1, "takeover: never set by inst_comp_warnings");

    /* The results and the uninstall's lines. */
    inst_take_result_text(1, "nusb3xhc", "Renesas USB 3.0 eXtensible Host "
                          "Controller - 0100 (Microsoft)", "nusb3flt",
                          t, sizeof(t));
    CHECK(strstr(t, "Replaced nusb3xhc with xHCI98 on Renesas") == t &&
          strstr(t, "\n  Its filter driver stays on it: nusb3flt.\n") != NULL,
          "takeover: result, replaced with a filter");
    inst_wrap_lines("", t, INST_WARN_COLUMNS, w, sizeof(w));
    CHECK(take_cols(w, 78), "takeover: result within 78 columns wrapped");
    inst_take_result_text(1, "nusb3xhc", "X", "", t, sizeof(t));
    CHECK(strcmp(t, "Replaced nusb3xhc with xHCI98 on X.\n") == 0,
          "takeover: result, no filter");
    inst_take_result_text(0, "nusb3xhc", "X", "f", t, sizeof(t));
    CHECK(strcmp(t, "Could not replace nusb3xhc on X. The other controllers "
                    "on another driver are left as they are.\n") == 0,
          "takeover: result, failed");
    inst_take_back_text(INST_TAKE_BACK_GIVEN, "X", "nusb3xhc", t, sizeof(t));
    CHECK(strcmp(t, "X: given back to nusb3xhc.\n") == 0,
          "takeover: uninstall, given back");
    inst_take_back_text(INST_TAKE_BACK_NONE, "X", "", t, sizeof(t));
    CHECK(strcmp(t, "X: has no driver now. In Device Manager, Update Driver "
                    "on it and let Windows search.\n") == 0,
          "takeover: uninstall, no driver");
    inst_take_back_text(INST_TAKE_BACK_LATER, "X", "", t, sizeof(t));
    CHECK(strcmp(t, "X: not found yet; Windows sets it up again at the next "
                    "restart.\n") == 0, "takeover: uninstall, later");
    inst_take_back_text(INST_TAKE_BACK_SETUP, "X", "", t, sizeof(t));
    CHECK(strcmp(t, "X: Windows is still setting it up. Answer its Found New "
                    "Hardware window if one is open.\n") == 0,
          "takeover: uninstall, still setting up");
    inst_take_back_text(INST_TAKE_BACK_SETUP, "xHCI98 USB 3 Controller (Red "
                        "Hat QEMU xHCI)", "", t, sizeof(t));
    inst_wrap_lines("", t, INST_WARN_COLUMNS, w, sizeof(w));
    CHECK(take_cols(w, 78), "takeover: still setting up within 78 columns");

    /* Which line: each controller's own reading; the settle only splits
     * no driver into SETUP (unsettled) and NONE (settled). */
    CHECK(inst_take_back_kind(0, 1, 0, "", 28UL) == INST_TAKE_BACK_SETUP,
          "takeover: kind, no driver, not settled");
    CHECK(inst_take_back_kind(0, 1, 1, "", 0UL) == INST_TAKE_BACK_SETUP,
          "takeover: kind, empty Service, not settled");
    CHECK(inst_take_back_kind(0, 1, 1, "tstxhc", 0UL) ==
          INST_TAKE_BACK_GIVEN,
          "takeover: kind, back on its driver, not settled");
    CHECK(inst_take_back_kind(0, 1, 1, "tstxhc", 31UL) ==
          INST_TAKE_BACK_GIVEN,
          "takeover: kind, back on its driver at another code, not settled");
    CHECK(inst_take_back_kind(0, 1, 1, "tstxhc", INST_PROB_NOT_CONFIGURED) ==
          INST_TAKE_BACK_SETUP, "takeover: kind, a service at Code 1");
    CHECK(inst_take_back_kind(1, 1, 1, "tstxhc", INST_PROB_REINSTALL) ==
          INST_TAKE_BACK_SETUP, "takeover: kind, a service at Code 18");
    CHECK(inst_take_back_kind(0, 1, 1, "tstxhc", INST_TAKE_PROB_UNREAD) ==
          INST_TAKE_BACK_LATER, "takeover: kind, status unread, not settled");
    CHECK(inst_take_back_kind(1, 1, 1, "tstxhc", INST_TAKE_PROB_UNREAD) ==
          INST_TAKE_BACK_GIVEN, "takeover: kind, status unread, settled");
    CHECK(inst_take_back_kind(0, 1, -1, "", 0UL) == INST_TAKE_BACK_LATER,
          "takeover: kind, Service unreadable, not settled");
    CHECK(inst_take_back_kind(0, 0, -1, "", INST_TAKE_PROB_UNREAD) ==
          INST_TAKE_BACK_LATER, "takeover: kind, not present, not settled");
    CHECK(inst_take_back_kind(1, 0, -1, "", INST_TAKE_PROB_UNREAD) ==
          INST_TAKE_BACK_LATER, "takeover: kind, not present, settled");
    CHECK(inst_take_back_kind(1, 1, 1, "tstxhc", 0UL) == INST_TAKE_BACK_GIVEN,
          "takeover: kind, given back");
    CHECK(inst_take_back_kind(1, 1, 1, "USBXHCI", 0UL) ==
          INST_TAKE_BACK_GIVEN, "takeover: kind, given back to Windows' own");
    CHECK(inst_take_back_kind(1, 1, 0, "", 28UL) == INST_TAKE_BACK_NONE,
          "takeover: kind, no Service value");
    CHECK(inst_take_back_kind(1, 1, 1, "", 0UL) == INST_TAKE_BACK_NONE,
          "takeover: kind, empty Service value");
    CHECK(inst_take_back_kind(1, 1, -1, "", 0UL) == INST_TAKE_BACK_LATER,
          "takeover: kind, Service unreadable");
    CHECK(inst_take_back_kind(1, 1, 1, "xhci98", 0UL) == INST_TAKE_BACK_LATER,
          "takeover: kind, still this package's");
    {
        /* One uninstall, the settle run out: one controller back on its
         * driver, one Windows is still setting up, one not found. */
        int k1 = inst_take_back_kind(0, 1, 1, "tstxhc", 0UL);
        int k2 = inst_take_back_kind(0, 1, 0, "", 28UL);
        int k3 = inst_take_back_kind(0, 0, -1, "", INST_TAKE_PROB_UNREAD);
        CHECK(k1 == INST_TAKE_BACK_GIVEN && k2 == INST_TAKE_BACK_SETUP &&
              k3 == INST_TAKE_BACK_LATER,
              "takeover: kind, mixed: restored, pending, absent");
    }

    /* The record. */
    CHECK(inst_take_rec_text("attempt", "nusb3xhc", "oem12.inf", t,
                             sizeof(t)) &&
          strcmp(t, "attempt;nusb3xhc;oem12.inf") == 0,
          "takeover: record composed");
    CHECK(inst_take_rec_text("done", "nusb3xhc", NULL, t, sizeof(t)) &&
          strcmp(t, "done;nusb3xhc;") == 0, "takeover: record, no InfPath");
    CHECK(!inst_take_rec_text("done", "a;b", "x", t, sizeof(t)) &&
          !inst_take_rec_text("done", "", "x", t, sizeof(t)) &&
          !inst_take_rec_text("done", "abc", "x", t, 6),
          "takeover: record refused");
    n = inst_take_rec_parse("done;nusb3xhc;oem12.inf", s, sizeof(s), v,
                            sizeof(v), f, sizeof(f));
    CHECK(n == 1 && strcmp(s, "done") == 0 && strcmp(v, "nusb3xhc") == 0 &&
          strcmp(f, "oem12.inf") == 0, "takeover: record parsed");
    CHECK(inst_take_rec_parse("attempt;x;", s, sizeof(s), v, sizeof(v), f,
                              sizeof(f)) == 1 && f[0] == '\0',
          "takeover: record parsed, no InfPath");
    CHECK(!inst_take_rec_parse("maybe;x;y", s, sizeof(s), v, sizeof(v), f,
                               sizeof(f)) &&
          !inst_take_rec_parse("done;;y", s, sizeof(s), v, sizeof(v), f,
                               sizeof(f)) &&
          !inst_take_rec_parse("done;x;y;z", s, sizeof(s), v, sizeof(v), f,
                               sizeof(f)) &&
          !inst_take_rec_parse("done", s, sizeof(s), v, sizeof(v), f,
                               sizeof(f)) &&
          !inst_take_rec_parse("done;x;y", s, 3, v, sizeof(v), f, sizeof(f)) &&
          s[0] == '\0',
          "takeover: record refused when parsed");
}

/* ---- owner, 2026-11-04: no xhciuas on 8 to 10; absent devnodes updated -- */

static void test_uas_absent(void)
{
    static const INST_U32 pkg[4] = { 3, 4, 1, 0 };
    static const INST_U32 old[4] = { 3, 4, 0, 0 };
    static const INST_U32 same[4] = { 3, 4, 1, 0 };
    static const INST_U32 newer[4] = { 3, 5, 0, 0 };

    CHECK(inst_uas_here(INST_OS_FULL) && !inst_uas_here(INST_OS_CMONLY) &&
          !inst_uas_here(INST_OS_REFUSED),
          "xhciuas on 98 SE, ME and 2000 to 7 alone, never on 8, 8.1, 10");
    CHECK(inst_os_kind(1, 6, 2, 9200) == INST_OS_CMONLY &&
          inst_os_kind(1, 6, 3, 9600) == INST_OS_CMONLY &&
          inst_os_kind(1, 10, 0, 19045) == INST_OS_CMONLY &&
          !inst_uas_here(inst_os_kind(1, 6, 2, 9200)) &&
          !inst_uas_here(inst_os_kind(1, 6, 3, 9600)) &&
          !inst_uas_here(inst_os_kind(1, 10, 0, 19045)),
          "8, 8.1 and 10: the UAS driver out of the run");
    CHECK(inst_uas_here(inst_os_kind(1, 5, 0, 2195)) &&
          inst_uas_here(inst_os_kind(1, 5, 1, 2600)) &&
          inst_uas_here(inst_os_kind(1, 5, 2, 3790)) &&
          inst_uas_here(inst_os_kind(1, 6, 0, 6002)) &&
          inst_uas_here(inst_os_kind(1, 6, 1, 7601)) &&
          inst_uas_here(inst_os_kind(0, 4, 10, 2222)) &&
          inst_uas_here(inst_os_kind(0, 4, 90, 3000)),
          "2000, XP, XP x64, Vista, 7, 98 SE, ME: the UAS driver offered");

    CHECK(inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "xhciuas",
                               INST_VS_HAVE, old, pkg, 0),
          "2000 to 7: an absent xhciuas devnode at an older version updated");
    CHECK(inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "XHCIUAS",
                               INST_VS_HAVE, old, pkg, 0),
          "the service matched without case");
    CHECK(!inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 1, "xhciuas",
                                INST_VS_HAVE, old, pkg, 0),
          "a present one is the update by hardware id's, not this");
    CHECK(!inst_uas_absent_runs(1, INST_OS_CMONLY, 1, 1, 0, "xhciuas",
                                INST_VS_HAVE, old, pkg, 0) &&
          !inst_uas_absent_runs(1, INST_OS_CMONLY, 1, 1, 0, "xhciuas",
                                INST_VS_HAVE, old, pkg, 1),
          "8 to 10: an xhciuas devnode an earlier release bound is left, "
          "with /force too");
    CHECK(!inst_uas_absent_runs(0, INST_OS_FULL, 1, 1, 0, "xhciuas",
                                INST_VS_HAVE, old, pkg, 0),
          "98 SE and ME: not this route");
    CHECK(!inst_uas_absent_runs(1, INST_OS_FULL, 0, 1, 0, "xhciuas",
                                INST_VS_HAVE, old, pkg, 0),
          "the UAS driver out of the run (/nouas, unticked): left");
    CHECK(!inst_uas_absent_runs(1, INST_OS_FULL, 1, 0, 0, "xhciuas",
                                INST_VS_HAVE, old, pkg, 1),
          "the group refused (newer or unknown): left");
    CHECK(!inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "USBSTOR",
                                INST_VS_HAVE, old, pkg, 0) &&
          !inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "UASPStor",
                                INST_VS_HAVE, old, pkg, 0) &&
          !inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "",
                                INST_VS_HAVE, old, pkg, 0) &&
          !inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, NULL,
                                INST_VS_HAVE, old, pkg, 0) &&
          !inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "xhciuas2",
                                INST_VS_HAVE, old, pkg, 0),
          "another driver's devnode, or none: never");
    CHECK(!inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "xhciuas",
                                INST_VS_ABSENT, old, pkg, 1) &&
          !inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "xhciuas",
                                INST_VS_UNREADABLE, old, pkg, 1),
          "no DriverVersion read: left");
    CHECK(!inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "xhciuas",
                                INST_VS_HAVE, same, pkg, 0) &&
          !inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "xhciuas",
                                INST_VS_HAVE, newer, pkg, 0),
          "current or newer: left without /force");
    CHECK(inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "xhciuas",
                               INST_VS_HAVE, same, pkg, 1) &&
          inst_uas_absent_runs(1, INST_OS_FULL, 1, 1, 0, "xhciuas",
                               INST_VS_HAVE, newer, pkg, 1),
          "/force: as a present device's (inst_action_runs)");

    /* 98 SE and ME (task 46.24): a job whose driver file the placement put
     * in SYSTEM32\DRIVERS, or queued, copies nothing. */
    CHECK(inst_di_nocopy(0, 0, 1) && inst_di_nocopy(1, 0, 1),
          "the file placed: the job copies nothing (a UAS disk, an NHI)");
    CHECK(inst_di_nocopy(1, 1, 0),
          "a later controller of an INF a controller's job copied (45.38)");
    CHECK(!inst_di_nocopy(0, 1, 0) && !inst_di_nocopy(1, 0, 0) &&
          !inst_di_nocopy(0, 0, 0),
          "otherwise the job copies its files");
}

/* ---- record 26 section 5.7: the Summary's headline (matrix N1) --------- */

static void test_summary_head(void)
{
    CHECK(strstr(inst_summary_head(0, 0, 0, 1), "All devices are up to date") !=
          NULL && strstr(inst_summary_head(0, 0, 0, 0),
                         "All devices are up to date") != NULL,
          "nothing changes: every device is current");
    CHECK(strstr(inst_summary_head(0, 1, 0, 0), "All devices") == NULL &&
          strstr(inst_summary_head(0, 1, 0, 0), "settings") != NULL,
          "a setting written: said, never \"every device is current\"");
    CHECK(strstr(inst_summary_head(0, 0, 1, 1), "All devices") == NULL &&
          strstr(inst_summary_head(0, 0, 1, 1), "changes below") != NULL,
          "a component added: said");
    CHECK(strstr(inst_summary_head(0, 1, 1, 1), "settings") != NULL &&
          strstr(inst_summary_head(0, 1, 1, 1), "make the changes") != NULL,
          "both: both said");
    CHECK(strcmp(inst_summary_head(1, 1, 1, 1), inst_confirm_text(1, 0, 1, 0))
          == 0 && strcmp(inst_summary_head(1, 0, 0, 0),
                         inst_confirm_text(1, 0, 0, 0)) == 0,
          "a device run: the install's own line");
}

/* ---- record 26 sections 9.1 and 9.2: the entry, the relaunch ------------ */

static void test_uninstall_entry(void)
{
    char out[512];
    INST_OPTS o;
    char err[128];
    char *argv[5];

    inst_arp_name(out, sizeof(out), "3.1.0.0", 0, 0);
    CHECK(strcmp(out, "xHCI98 USB 3 drivers 3.1.0.0") == 0, "the name");
    inst_arp_name(out, sizeof(out), "3.1.0.0", 1, 0);
    CHECK(strcmp(out, "xHCI98 USB 3 drivers 3.1.0.0 (incomplete: run "
                      "again)") == 0, "an incomplete uninstall's name");
    /* Record 26 section 9.1, draft 8: named by what is registered. */
    inst_arp_name(out, sizeof(out), "3.1.0.0", 0, 1);
    CHECK(strcmp(out, "xHCI98 Thunderbolt 3 / USB4 connection manager "
                      "3.1.0.0") == 0, "the connection manager alone's name");
    inst_arp_name(out, sizeof(out), "3.1.0.0", 1, 1);
    CHECK(strcmp(out, "xHCI98 Thunderbolt 3 / USB4 connection manager "
                      "3.1.0.0 (incomplete: run again)") == 0,
          "the connection manager alone, incomplete");
    CHECK(inst_arp_cm_only(0, 1, 0, 0) && !inst_arp_cm_only(1, 1, 0, 0) &&
          !inst_arp_cm_only(-1, 1, 0, 0) && !inst_arp_cm_only(0, 0, 0, 0) &&
          !inst_arp_cm_only(0, -1, 0, 0) && !inst_arp_cm_only(1, 0, 0, 0),
          "the connection manager alone: xhci98 absent, xhcicm registered");
    /* A run without the USB drivers that only staged (no controller yet,
     * no service registered) is named for the connection manager; an
     * xhci98 still registered keeps the USB drivers' name. */
    CHECK(inst_arp_cm_only(0, 0, 1, 0) && inst_arp_cm_only(0, -1, 1, 0) &&
          !inst_arp_cm_only(1, 0, 1, 0) && !inst_arp_cm_only(-1, 0, 1, 0),
          "the connection manager alone: a staging-only run");
    /* Task 44.60: a run that installs the USB drivers is named for them
     * though their service is not registered yet (a staging-only run, or
     * 98 SE and ME before Update Driver), and xhcicm's is. */
    CHECK(!inst_arp_cm_only(0, 1, 0, 1) && !inst_arp_cm_only(0, 0, 0, 1) &&
          !inst_arp_cm_only(0, -1, 0, 1),
          "a run with the USB drivers: their name");
    /* Task 44.60: /cmonly on 98 SE and ME, no xhci98.sys, xhcicm.sys
     * copied by the run (or not yet): the connection manager's name;
     * /cmonly with the connection manager declined (runCmOnly 0) and
     * nothing of it there keeps the package's name. */
    CHECK(inst_arp_cm_only(0, 1, 1, 0) && inst_arp_cm_only(0, 0, 1, 0) &&
          !inst_arp_cm_only(0, 0, 0, 0),
          "/cmonly: the connection manager's name");
    inst_arp_name(out, sizeof(out), "3.3.0.0", 0,
                  inst_arp_cm_only(0, 1, 1, 0));
    CHECK(strcmp(out, "xHCI98 Thunderbolt 3 / USB4 connection manager "
                      "3.3.0.0") == 0, "/cmonly: the entry's name");
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
    /* Task 45.45: /debug went with the debug- directory. */
    argv[1] = "/debug";
    CHECK(inst_parse_args(2, argv, &o, err, sizeof(err)) != 0,
          "/debug is refused: the debug flavour was retired");
    argv[1] = "/thunderbolt";
    argv[2] = "/to";
    argv[3] = "C:\\XHCI98";
    CHECK(inst_parse_args(4, argv, &o, err, sizeof(err)) == 0 &&
          inst_window_refusal(&o, 4, argv) == NULL,
          "/thunderbolt and /to preselect the window's pages");
}

/* ---- record 26 section 5.3.3: Secure Boot and memory integrity (44.25) -- */

/* Every line of text at most cols characters. */
static int lines_within(const char *text, size_t cols)
{
    const char *p = text;

    while (*p != '\0') {
        const char *e = strchr(p, '\n');
        size_t n = e != NULL ? (size_t)(e - p) : strlen(p);
        if (n > cols) {
            printf("too long (%u): %.*s\n", (unsigned)n, (int)n, p);
            return 0;
        }
        p += n;
        if (*p == '\n') {
            p++;
        }
    }
    return 1;
}

static void test_secure_boot_refusals(void)
{
    int kind[4];
    int a;
    int k;
    int cm;
    int st;
    int bad = 0;
    INST_U32 set;
    char text[4096];
    char block[8192];

    kind[0] = INST_OS_FULL;
    kind[1] = INST_OS_CMONLY;
    kind[2] = INST_OS_REFUSED;
    kind[3] = INST_OS_CMONLY;
    /* The x86 connection manager: refused only on the CM-alone kind, in
     * the run, Secure Boot read as on; x64 left to inst_cm_boot_gate. */
    for (a = 0; a < 2; a++) {
        for (k = 0; k < 3; k++) {
            for (cm = 0; cm < 2; cm++) {
                for (st = INST_SB_UNKNOWN; st <= INST_SB_ON; st++) {
                    int arch = a ? INST_ARCH_AMD64 : INST_ARCH_X86;
                    int want = arch == INST_ARCH_X86 &&
                               kind[k] == INST_OS_CMONLY && cm &&
                               st == INST_SB_ON;
                    if (inst_cm_sb_refused(arch, kind[k], cm, st) != want) {
                        printf("cm sb: arch %d kind %d cm %d state %d\n", a,
                               kind[k], cm, st);
                        bad = 1;
                    }
                    want = kind[k] == INST_OS_CMONLY && cm &&
                           st == INST_SB_ON;
                    if (inst_usb_sb_refused(1, kind[k], cm, st) != want ||
                        inst_usb_sb_refused(0, kind[k], cm, st) != 0) {
                        printf("usb sb: kind %d usb %d state %d\n", kind[k],
                               cm, st);
                        bad = 1;
                    }
                }
            }
        }
    }
    CHECK(!bad, "Secure Boot on: the x86 connection manager and the USB "
          "drivers refused on 8 to 10 alone");
    /* The x64 gate is unchanged: Secure Boot on never confirms
     * test-signing mode, so the gate refuses there. */
    CHECK(inst_cm_boot_gate(INST_ARCH_AMD64, 1, 10UL, INST_OS_CMONLY, 1,
                            inst_ts_confirmed(1, INST_SB_ON)) ==
          INST_CMBOOT_REFUSE &&
          inst_cm_boot_gate(INST_ARCH_AMD64, 1, 10UL, INST_OS_CMONLY, 1,
                            inst_ts_confirmed(1, INST_SB_OFF)) ==
          INST_CMBOOT_WARN, "x64: the gate as before");
    /* Memory integrity: inst_sb_state's rule; read on x64 10 alone. */
    CHECK(inst_hvci_state(1, INST_REG_DWORD_TYPE, 1UL) == INST_SB_ON &&
          inst_hvci_state(1, INST_REG_DWORD_TYPE, 0UL) == INST_SB_OFF &&
          inst_hvci_state(1, INST_REG_DWORD_TYPE, 2UL) == INST_SB_UNKNOWN &&
          inst_hvci_state(1, 1UL, 1UL) == INST_SB_UNKNOWN &&
          inst_hvci_state(0, INST_REG_DWORD_TYPE, 1UL) == INST_SB_UNKNOWN,
          "memory integrity: on, off, not known");
    CHECK(inst_hvci_applies(INST_ARCH_AMD64, INST_OS_CMONLY, 10UL) &&
          !inst_hvci_applies(INST_ARCH_AMD64, INST_OS_CMONLY, 6UL) &&
          !inst_hvci_applies(INST_ARCH_X86, INST_OS_CMONLY, 10UL) &&
          !inst_hvci_applies(INST_ARCH_AMD64, INST_OS_FULL, 6UL),
          "memory integrity read on 64-bit 10 alone");
    CHECK(inst_hvci_text(INST_SB_ON, 0) != NULL &&
          strstr(inst_hvci_text(INST_SB_ON, 0), "Code 39") != NULL &&
          inst_hvci_text(INST_SB_ON, 1) != NULL &&
          strstr(inst_hvci_text(INST_SB_ON, 1), "Automatic") != NULL &&
          inst_hvci_text(INST_SB_OFF, 1) == NULL &&
          inst_hvci_text(INST_SB_UNKNOWN, 0) == NULL,
          "memory integrity's sentence: the boot-driver form with the "
          "connection manager");
    CHECK(lines_within(inst_cm_sb_refuse_text(), 78) &&
          lines_within(inst_usb_sb_refuse_text(), 78) &&
          lines_within(inst_cm_x86_boot_text(), 78) &&
          strstr(inst_cm_x86_boot_text(), "sets it back") != NULL &&
          strstr(inst_cm_x86_boot_text(), "boot driver here") == NULL &&
          lines_within(inst_hvci_text(INST_SB_ON, 0), 78) &&
          lines_within(inst_hvci_text(INST_SB_ON, 1), 78) &&
          strstr(inst_cm_sb_refuse_text(), "Nothing was changed") != NULL &&
          strstr(inst_usb_sb_refuse_text(), "Nothing was changed") != NULL,
          "the refusals' and the notes' lines within 78 columns");
    /* Every set of the Components warnings, rendered as the console renders
     * it ("  " before each line), within 78 columns (record 26 section
     * 5.3.2); a long tools folder included. */
    bad = 0;
    for (set = 0; set < 64; set++) {
        char *p;
        char *q;
        inst_comp_warn_text(set, "C:\\Program Files\\xHCI98 tools folder",
                            text, sizeof(text));
        for (p = text; *p != '\0' && !bad; p = q) {
            q = strstr(p, "\n\n");
            if (q != NULL) {
                *q = '\0';
                q += 2;
            } else {
                q = p + strlen(p);
            }
            inst_wrap_lines("  ", p, INST_WARN_COLUMNS, block, sizeof(block));
            if (!lines_within(block, 78) || block[0] != ' ') {
                printf("warning set %lu\n", (unsigned long)set);
                bad = 1;
            }
        }
    }
    CHECK(!bad, "every Components warning set within 78 columns");
    inst_comp_warn_text(INST_CW_USB8, "C:\\XHCI98", text, sizeof(text));
    CHECK(strstr(text, "no longer apply") != NULL &&
          strstr(text, "USB power settings") != NULL &&
          strstr(text, "USB Tree notifies you") != NULL,
          "the USB drivers' warning names what is lost (44.24)");
    inst_comp_warn_text(INST_CW_INTELTB, "C:\\XHCI98", text, sizeof(text));
    CHECK(strstr(text, "Intel's Thunderbolt driver runs") != NULL &&
          strstr(text, "Windows Update") != NULL,
          "Intel's driver: the warning");
    inst_wrap_lines("", "aaaa bbbb cccc", 9, block, sizeof(block));
    CHECK(strcmp(block, "aaaa bbbb\ncccc\n") == 0, "wrap: at the width");
    inst_wrap_lines("", "one\ntwo", 78, block, sizeof(block));
    CHECK(strcmp(block, "one\ntwo\n") == 0, "wrap: a newline kept");
    (void)kind[3];
}

/* ---- record 26 section 11: the window's texts ------------------------- */

static void test_page_texts(void)
{
    int i;

    {
        /* Task 44.4: INSTALL.BAT /toomany's text, printed by the console
         * tool, each line within 79 columns, ending with its line. */
        const char *t = INST_TOOMANY_TEXT;
        size_t col = 0;
        CHECK(t[strlen(t) - 1] == '\n' && strchr(t, '\r') == NULL,
              "toomany: whole lines");
        for (i = 0; t[i] != '\0'; i++) {
            col = t[i] == '\n' ? 0 : col + 1;
            CHECK(col <= 79, "toomany: within 79 columns");
        }
    }

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
        /* Task 44.61: the package's own things say "USB 3". */
        CHECK(strstr(t->Text, "xHCI98 USB 3.x") == NULL,
              "a page text: xHCI98 USB 3, not 3.x");
    }
    CHECK(strcmp(inst_page_text(INST_PT_UN_TITLE),
                 "Remove xHCI98 USB 3 drivers") == 0 &&
          strcmp(inst_page_text(INST_PT_DRIVERS),
                 "xHCI98 USB 3 driver (xhci98.sys)") == 0,
          "the Remove page's title and the Components row (task 44.61)");
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

/* One column of a block of the readme's table, its lines' parts from
 * column `from` to before `to` joined by single spaces: the table is laid
 * out in fixed columns (unit 26, default 32, range 51). */
static void s98_col(const char *block, size_t from, size_t to, char *out,
                    size_t cap)
{
    const char *l;
    size_t o;

    o = 0;
    out[0] = '\0';
    for (l = block; *l != '\0';) {
        const char *nl = strchr(l, '\n');
        size_t len = nl != NULL ? (size_t)(nl - l) : strlen(l);
        size_t a;
        size_t b;

        if (len > 0 && l[len - 1] == '\r') {
            len--;
        }
        a = from < len ? from : len;
        b = to < len ? to : len;
        while (b > a && l[b - 1] == ' ') {
            b--;
        }
        while (a < b && l[a] == ' ') {
            a++;
        }
        if (b > a && o + (b - a) + 2 < cap) {
            if (o > 0) {
                out[o++] = ' ';
            }
            memcpy(out + o, l + a, b - a);
            o += b - a;
            out[o] = '\0';
        }
        if (nl == NULL) {
            break;
        }
        l = nl + 1;
    }
}

/* `text` in `s` as a whole: at the start or after a space or '(', and
 * followed by the end, a space, ',' ';' or ')'. */
static int has_whole(const char *s, const char *text)
{
    const char *p;
    size_t n = strlen(text);

    for (p = strstr(s, text); p != NULL; p = strstr(p + 1, text)) {
        char after = p[n];
        if ((p == s || p[-1] == ' ' || p[-1] == '(') &&
            (after == '\0' || after == ' ' || after == ',' ||
             after == ';' || after == ')')) {
            return 1;
        }
    }
    return 0;
}

/* The range column against a row's bounds, by the forms the table uses:
 * "lo-hi", "lo to ..." and "lo and up" name the bottom first (and the
 * first form the top); a choice row ("1 = ..., 0 = ...") names each value
 * as "n =", so both bounds must be among them. */
static int s98_range_ok(const char *range, unsigned long lo,
                        unsigned long hi, int checkTop)
{
    const char *p;
    char *e;
    unsigned long a;
    int haveLo;
    int haveHi;

    if (range[0] < '0' || range[0] > '9') {
        return 0;
    }
    a = strtoul(range, &e, 10);
    if (e[0] == '-' && e[1] >= '0' && e[1] <= '9') {
        return a == lo && (!checkTop || strtoul(e + 1, NULL, 10) == hi);
    }
    if (strncmp(e, " to ", 4) == 0 || strncmp(e, " and up", 7) == 0) {
        return a == lo && !checkTop;
    }
    if (strncmp(e, " = ", 3) != 0) {
        return 0;
    }
    haveLo = 0;
    haveHi = 0;
    for (p = range; *p != '\0'; p++) {
        if (*p >= '0' && *p <= '9' && (p == range || p[-1] == ' ')) {
            a = strtoul(p, &e, 10);
            if (strncmp(e, " = ", 3) == 0) {
                haveLo |= (a == lo);
                haveHi |= (a == hi);
            }
            p = e - 1;
        }
    }
    return haveLo && (!checkTop || haveHi);
}

/* The default column against a row's two defaults: "a on 98 SE / ME" and
 * "b on 2000 to 7" where they differ by system, else the column's first
 * word is each. An empty default (no such system) is not asked for. */
static int s98_default_ok(const char *col, const char *d9x, const char *dnt)
{
    char want[64];
    size_t n = strcspn(col, " (");

    if (strncmp(col + n, " on ", 4) == 0) {
        if (d9x[0] != '\0') {
            sprintf(want, "%.40s on 98 SE / ME", d9x);
            if (!has_whole(col, want)) {
                return 0;
            }
        }
        if (dnt[0] != '\0') {
            sprintf(want, "%.40s on 2000 to 7", dnt);
            if (!has_whole(col, want)) {
                return 0;
            }
        }
        return 1;
    }
    return (d9x[0] == '\0' || (strlen(d9x) == n && strncmp(col, d9x, n) == 0)) &&
           (dnt[0] == '\0' || (strlen(dnt) == n && strncmp(col, dnt, n) == 0));
}

/* Task 45.50: Stor98's INFs write each setting at the default the
 * installer's table gives (record 21 section 8.1), and
 * inst_s98_is_default_9x knows a default as read. */
static void test_s98_inf_defaults(void)
{
    char *nine = load("..\\src\\stor98\\inf\\stor98-9x.inf");
    char *x86 = load("..\\src\\stor98\\inf\\stor98-x86.inf");
    char *x64 = load("..\\src\\stor98\\inf\\stor98-x64.inf");
    char *infs[3];
    char line[256];
    char what[256];
    char got[64];
    const char *p;
    const char *q;
    int i;
    int f;
    int keys;
    int pairs;

    infs[0] = nine;
    infs[1] = x86;
    infs[2] = x64;
    CHECK(nine != NULL && x86 != NULL && x64 != NULL, "Stor98's three INFs read");
    /* The x86 INF is NT-only (record 36 section 3.3): no 98 SE and ME
     * setting, no stor98.mpd. */
    CHECK(x86 != NULL && !has_text(x86, "System\\CurrentControlSet\\Services\\stor98\",") &&
          !has_text(x86, "stor98.mpd=") && !has_text(x86, "PortDriver"),
          "stor98-x86.inf writes no 98 SE and ME setting and names no stor98.mpd");
    for (f = 0; f < 1; f++) {
        for (i = 0; infs[f] != NULL && i < INST_S98_COUNT; i++) {
            const INST_S98_VALUE *v = &instS98Values[i];
            if (v->Name9x == NULL || !inst_s98_applies(i, 0)) {
                continue;
            }
            sprintf(line, "HKLM,\"System\\CurrentControlSet\\Services\\stor98\",%s,%s,%s%s%s",
                    v->Name9x,
                    v->Kind == INST_S98_WORDS ? "0x00000002" : "0x00010003",
                    v->Kind == INST_S98_WORDS ? "\"" : "", v->Def9x,
                    v->Kind == INST_S98_WORDS ? "\"" : "");
            sprintf(what, "%s writes %s at the table's 98 SE and ME default",
                    "stor98-9x.inf", v->Name9x);
            CHECK(has_text(infs[f], line), what);
        }
    }
    for (f = 1; f < 3; f++) {
        p = infs[f] != NULL ? strstr(infs[f], "\"DriverParameter\",0x00000002,\"") : NULL;
        CHECK(p != NULL, "an NT INF writes DriverParameter, only where missing");
        if (p == NULL) {
            continue;
        }
        p += strlen("\"DriverParameter\",0x00000002,\"");
        q = strchr(p, '"');
        CHECK(q != NULL && (size_t)(q - p) < sizeof(line), "the string ends");
        if (q == NULL || (size_t)(q - p) >= sizeof(line)) {
            continue;
        }
        memcpy(line, p, (size_t)(q - p));
        line[q - p] = '\0';
        keys = 0;
        for (i = 0; i < INST_S98_COUNT; i++) {
            const INST_S98_VALUE *v = &instS98Values[i];
            if (v->KeyNt == NULL || !inst_s98_applies(i, 1)) {
                continue;
            }
            keys++;
            sprintf(what, "DriverParameter holds %s= at the table's NT default", v->KeyNt);
            CHECK(inst_s98_param_get(line, v->KeyNt, got, sizeof(got)) &&
                  strcmp(got, v->DefNt) == 0, what);
        }
        pairs = 1;
        for (p = line; *p != '\0'; p++) {
            if (*p == ';') {
                pairs++;
            }
        }
        CHECK_EQ(pairs, keys, "DriverParameter holds every key and nothing else");
    }
    free(nine);
    free(x86);
    free(x64);

    CHECK(inst_s98_is_default_9x(INST_S98_MAXTRANSFERKB, 1, 2048UL, NULL) &&
          !inst_s98_is_default_9x(INST_S98_MAXTRANSFERKB, 1, 1024UL, NULL) &&
          inst_s98_is_default_9x(INST_S98_MAXTRANSFERKB, 0, 0, "2048") &&
          !inst_s98_is_default_9x(INST_S98_MAXTRANSFERKB, 0, 0, "2048k") &&
          !inst_s98_is_default_9x(INST_S98_MAXTRANSFERKB, 0, 0, ""),
          "a number's default, as a DWORD or decimal text");
    {
        char t[32];
        const char *w;
        CHECK(inst_s98_hi(INST_S98_MAXTRANSFERKB, 0) == 2048UL &&
              inst_s98_hi(INST_S98_MAXTRANSFERKB, 1) == 256UL &&
              inst_s98_check(INST_S98_MAXTRANSFERKB, 0, "2048", t, sizeof(t),
                             &w) == 1 &&
              inst_s98_check(INST_S98_MAXTRANSFERKB, 0, "2052", t, sizeof(t),
                             &w) == 0 &&
              inst_s98_check(INST_S98_MAXTRANSFERKB, 1, "260", t, sizeof(t),
                             &w) == 0,
              "the transfer size's top, shown and checked, is its default: "
              "2048 KiB on 98 SE and ME, 256 KiB on 2000 to 10");
    }
    {
        /* The Advanced dialog's range: no space where there is no unit. */
        char t[96];

        inst_s98_range(INST_S98_INTWATCH, inst_s98_hi(INST_S98_INTWATCH, 0),
                       t, sizeof(t));
        CHECK(strcmp(t, "0 to 1") == 0, "Stor98 range: no unit, no space");
        inst_s98_range(INST_S98_MAXTRANSFERKB,
                       inst_s98_hi(INST_S98_MAXTRANSFERKB, 0), t, sizeof(t));
        CHECK(strcmp(t, "64 to 2048 KiB, in steps of 4") == 0,
              "Stor98 range: a unit and a step");
        inst_s98_range(INST_S98_POLLIDLEMS, inst_s98_hi(INST_S98_POLLIDLEMS, 1),
                       t, sizeof(t));
        CHECK(strcmp(t, "100 to 60000 ms, or 0: no polling when idle") == 0,
              "Stor98 range: a unit and its 0");
        inst_s98_range(INST_S98_GONE, inst_s98_hi(INST_S98_GONE, 1), t,
                       sizeof(t));
        CHECK(strcmp(t, "1 to 600000 ms") == 0, "Stor98 range: a unit alone");
    }
    CHECK(inst_s98_is_default_9x(INST_S98_WRITETHROUGH, 0, 0, "auto") &&
          inst_s98_is_default_9x(INST_S98_WRITETHROUGH, 0, 0, "AUTO") &&
          !inst_s98_is_default_9x(INST_S98_WRITETHROUGH, 0, 0, "off") &&
          !inst_s98_is_default_9x(INST_S98_WRITETHROUGH, 1, 0, NULL),
          "write-through's default is the word auto, as text");
    CHECK(inst_s98_is_default_9x(INST_S98_POLLSPINUS, 1, 0, NULL) &&
          !inst_s98_is_default_9x(INST_S98_POLLSPINUS, 1, 500UL, NULL) &&
          inst_s98_is_default_9x(INST_S98_HOLD9X, 1, 4000UL, NULL) &&
          !inst_s98_is_default_9x(-1, 1, 0, NULL) &&
          !inst_s98_is_default_9x(INST_S98_COUNT, 1, 0, NULL),
          "the 98 SE and ME defaults, and no row out of range");
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
        CHECK(s98_block(table, i, block, sizeof(block)),
              "each Stor98 row names a row of its readme's table");
        if (v->KeyNt != NULL) {
            strcpy(key, v->KeyNt);
            strcat(key, "=");
            CHECK(has_text(block, key), "the row's DriverParameter key");
        } else {
            CHECK(has_text(block, inst_s98_me_refused(i) ?
                                  "98 SE only, not ME" : "98 SE / ME only"),
                  "a row with no DriverParameter key says so");
        }
        /* Each field in its own column, as a whole value (a bottom of 0
         * or 1 no longer matches any digit of the block). */
        s98_col(block, 26, 32, t, sizeof(t));
        CHECK(v->Unit[0] ? has_whole(t, v->Unit) && strcspn(t, " ") == strlen(v->Unit)
                         : (t[0] == '-' || t[0] == '\0'),
              "the row's unit, in its column");
        s98_col(block, 32, 51, t, sizeof(t));
        CHECK(s98_default_ok(t, v->Def9x, v->DefNt),
              "the row's defaults, each for its systems");
        if (v->Kind == INST_S98_NUMBER && i != INST_S98_POLLMODE) {
            s98_col(block, 51, 200, t, sizeof(t));
            CHECK(s98_range_ok(t, (unsigned long)v->Lo, (unsigned long)v->Hi,
                               i != INST_S98_MAXTRANSFERKB && i != INST_S98_IDLE),
                  "the row's range: its bottom, and its top where it has one");
        }
        if (v->Kind == INST_S98_WORDS) {
            CHECK(has_text(block, "auto, off, cacheoff, fua"),
                  "the write-through words");
        }
    }
    free(table);
    CHECK(strcmp(INST_S98_TABLE_VERSION, "3.4.3.0") == 0,
          "the table is the bundled Stor98's");

    /* Which system each row is for. */
    CHECK(inst_s98_applies(INST_S98_GONE, 1) &&
          inst_s98_applies(INST_S98_GONE, 0) &&
          inst_s98_applies(INST_S98_IDLE, 0) &&
          inst_s98_applies(INST_S98_WRITETHROUGH, 0) &&
          inst_s98_only(INST_S98_GONE) == NULL &&
          strcmp(instS98Values[INST_S98_GONE].Name9x, "StorGoneMs") == 0 &&
          strcmp(instS98Values[INST_S98_IDLE].KeyNt, "storwriteidlems") == 0,
          "every setting on every system, its names starting Stor (3.3.0.0)");
    CHECK(strcmp(inst_s98_default(INST_S98_MAXTRANSFERKB, 0), "2048") == 0 &&
          strcmp(inst_s98_default(INST_S98_MAXTRANSFERKB, 1), "256") == 0 &&
          strcmp(inst_s98_default(INST_S98_POLLSPINUS, 0), "0") == 0 &&
          strcmp(inst_s98_default(INST_S98_POLLSPINUS, 1), "500") == 0,
          "per-system defaults");
    CHECK(inst_s98_applies(INST_S98_INTWATCH, 0) && inst_s98_applies(INST_S98_INTWATCH, 1) &&
          strcmp(instS98Values[INST_S98_INTWATCH].Name9x, "StorIntWatchdog") == 0 &&
          strcmp(inst_s98_default(INST_S98_INTWATCH, 0), "1") == 0 &&
          strcmp(inst_s98_default(INST_S98_INTWATCH, 1), "1") == 0 &&
          inst_s98_check(INST_S98_INTWATCH, 0, "0", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_INTWATCH, 1, "2", w, sizeof(w), &why),
          "the missed-interrupt watchdog: on by default, 0 or 1 (3.4.1.0)");
    CHECK(inst_s98_applies(INST_S98_TAKEFW, 0) && !inst_s98_applies(INST_S98_TAKEFW, 1) &&
          inst_s98_greyed(INST_S98_TAKEFW, 1) &&
          strcmp(instS98Values[INST_S98_TAKEFW].Name9x, "StorTakeFirmware") == 0 &&
          strcmp(inst_s98_default(INST_S98_TAKEFW, 0), "0") == 0 &&
          strcmp(inst_s98_only(INST_S98_TAKEFW), "Windows 98 SE and ME only") == 0 &&
          inst_s98_check(INST_S98_TAKEFW, 0, "1", w, sizeof(w), &why) &&
          inst_s98_check(INST_S98_TAKEFW, 0, "0", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_TAKEFW, 0, "2", w, sizeof(w), &why),
          "taking a controller the BIOS set up: 98 SE and ME only, off by default, 0 or 1 (3.4.3.0)");
    CHECK(inst_s98_me_refused(INST_S98_TAKEFW) &&
          !inst_s98_me_refused(INST_S98_HOLD9X) &&
          !inst_s98_me_refused(INST_S98_WRITETHROUGH) &&
          inst_s98_applies_os(INST_S98_TAKEFW, 0, 0) &&
          !inst_s98_applies_os(INST_S98_TAKEFW, 0, 1) &&
          !inst_s98_applies_os(INST_S98_TAKEFW, 1, 0) &&
          !inst_s98_greyed_os(INST_S98_TAKEFW, 0, 0) &&
          inst_s98_greyed_os(INST_S98_TAKEFW, 0, 1) &&
          inst_s98_greyed_os(INST_S98_TAKEFW, 1, 0) &&
          inst_s98_applies_os(INST_S98_HOLD9X, 0, 1) &&
          !inst_s98_greyed_os(INST_S98_HOLD9X, 0, 1) &&
          inst_s98_applies_os(INST_S98_GONE, 0, 1) &&
          strcmp(inst_s98_only_os(INST_S98_TAKEFW, 1), INST_S98_ME_WHY) == 0 &&
          strcmp(inst_s98_only_os(INST_S98_TAKEFW, 0), "Windows 98 SE and ME only") == 0 &&
          strcmp(inst_s98_only_os(INST_S98_HOLD9X, 1), "Windows 98 SE and ME only") == 0 &&
          inst_s98_only_os(INST_S98_GONE, 1) == NULL &&
          !inst_s98_applies_os(-1, 0, 1) && !inst_s98_greyed_os(-1, 0, 1),
          "StorTakeFirmware: not settable on ME, greyed there with why (48.28)");
    inst_s98_meanings(INST_S98_TAKEFW, t, sizeof(t));
    CHECK(strncmp(t, "0: leave it to the BIOS. 1: take it; ", 37) == 0 &&
          strstr(t, "can hang Windows' start or corrupt data") != NULL,
          "the Advanced dialog's text for StorTakeFirmware carries its warning");
    inst_s98_meanings(INST_S98_INTWATCH, t, sizeof(t));
    CHECK(strcmp(t, "0: keep waiting for interrupts. 1: poll once interrupts stop.") == 0,
          "a 0-or-1 row's meanings");
    inst_s98_meanings(INST_S98_GONE, t, sizeof(t));
    CHECK(t[0] == '\0', "a row without meanings: none");
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
          inst_s98_check(INST_S98_MAXTRANSFERKB, 0, "2048", w, sizeof(w),
                         &why) &&
          !inst_s98_check(INST_S98_MAXTRANSFERKB, 0, "2052", w, sizeof(w),
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
    CHECK(inst_s98_check(INST_S98_GONE, 0, "5000", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_GONE, 0, "600001", w, sizeof(w), &why) &&
          inst_s98_check(INST_S98_IDLE, 0, "10", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_IDLE, 0, "9", w, sizeof(w), &why),
          "gone and idle settable on 98 SE and ME too, the same ranges");
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
    CHECK(inst_s98_check(INST_S98_HOLD9X, 0, "1000", w, sizeof(w), &why) &&
          strcmp(w, "1000") == 0 &&
          inst_s98_check(INST_S98_HOLD9X, 0, "10000", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_HOLD9X, 0, "999", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_HOLD9X, 0, "10001", w, sizeof(w), &why) &&
          !inst_s98_check(INST_S98_HOLD9X, 0, "0", w, sizeof(w), &why),
          "hold9x: 1000 to 10000 on 98 SE and ME");
    CHECK(!inst_s98_check(INST_S98_HOLD9X, 1, "4000", w, sizeof(w), &why) &&
          why != NULL && !inst_s98_applies(INST_S98_HOLD9X, 1) &&
          strcmp(inst_s98_default(INST_S98_HOLD9X, 0), "4000") == 0 &&
          strcmp(instS98Values[INST_S98_HOLD9X].Name9x, "StorHold9xMs") == 0,
          "hold9x: no setting of 2000 to 10, its default 4000 on 9x");

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
    CHECK(strcmp(t, "not set (default 0)") == 0,
          "not set, with the default");
    inst_s98_now_text(INST_S98_POLLMODE, 0, 0, 1, "1", t, sizeof(t));
    CHECK(strcmp(t, "1") == 0, "set");
    inst_s98_now_text(INST_S98_POLLMODE, 0, 0, -1, NULL, t, sizeof(t));
    CHECK(strcmp(t, "unreadable") == 0, "unreadable");

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
        CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 1 &&
              strstr(err, "/thunderbolt") != NULL,
              "/stor98 alone: refused, it needs /thunderbolt");
        argv1[2] = "/thunderbolt";
        CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 0 &&
              o.Stor98 && o.Thunderbolt, "/stor98 /thunderbolt");
        argv1[2] = "/cmonly";
        CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 0 &&
              o.Stor98 && o.CmOnly, "/stor98 /cmonly: the support is in the "
              "run");
        argv1[2] = "/uninstall";
        CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1,
              "/stor98 belongs to an install");
        argv1[1] = "/nostor98";
        argv1[2] = "/thunderbolt";
        CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 0 &&
              o.NoStor98 && !o.Stor98, "/nostor98 /thunderbolt");
        argv1[2] = "/stor98";
        CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1,
              "/nostor98 /stor98: a contradiction");
        argv1[2] = "/uninstall";
        CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1,
              "/nostor98 belongs to an install");
        /* Chosen with the Thunderbolt support (owner, 2026-10-19,
         * replacing "never unasked"). */
        CHECK(inst_s98_decide(1, 1, 0, 0, 0, 0, 1, &asked, &def) && asked && def,
              "asked, Enter gives Y");
        CHECK(inst_s98_decide(1, 1, 1, 0, 0, 0, 1, &asked, &def) && !asked,
              "/thunderbolt /yes: Y, not asked");
        CHECK(!inst_s98_decide(1, 1, 1, 0, 1, 0, 1, &asked, &def) && !asked,
              "/nostor98: left out unasked");
        CHECK(inst_s98_decide(1, 1, 1, 1, 0, 0, 1, &asked, &def) && !asked,
              "/stor98: installed unasked");
        CHECK(!inst_s98_decide(0, 1, 0, 1, 0, 0, 1, &asked, &def) && !asked,
              "no Stor98 in the package: nothing");
        CHECK(inst_s98_decide(1, 1, 0, 0, 0, 1, 1, &asked, &def) && !asked,
              "/list asks nothing, Y shown");
        CHECK(!inst_s98_decide(1, 0, 0, 0, 0, 0, 1, &asked, &def) && !asked &&
              !inst_s98_decide(1, 0, 1, 1, 0, 0, 1, &asked, &def) && !asked,
              "no Thunderbolt support in the run: Stor98 neither asked nor "
              "installed");
        CHECK(inst_s98_box_default(INST_S98SYS_7) &&
              inst_s98_box_default(INST_S98SYS_8) &&
              inst_s98_box_default(INST_S98SYS_81) &&
              !inst_s98_box_default(INST_S98SYS_10),
              "43.59: Stor98's table ticks 7, 8 and 8.1, not 10");
        CHECK(!inst_s98_decide(1, 1, 0, 0, 0, 0, 0, &asked, &def) && asked &&
              !def, "a system whose table line is 0: asked, Enter gives N");
        CHECK(!inst_s98_decide(1, 1, 1, 0, 0, 0, 0, &asked, &def) && !asked,
              "table line 0, /yes: N");
        CHECK(inst_s98_decide(1, 1, 1, 1, 0, 0, 0, &asked, &def) && !asked,
              "table line 0, /stor98: installed unasked");
        /* Record 31 section 6.2: the value on 2000 to 8.1, for the mark
         * service's fallback; deleted on 10. */
        CHECK(inst_s98_auto_value(1, 1, 1, INST_MK_INSTALL),
              "2000 to 8.1 with Stor98 staged and the service: the value written");
        CHECK(!inst_s98_auto_value(0, 1, 1, INST_MK_INSTALL),
              "10: no Stor98Inf");
        CHECK(!inst_s98_auto_value(1, 0, 1, INST_MK_INSTALL) &&
              !inst_s98_auto_value(1, 1, 0, INST_MK_INSTALL) &&
              !inst_s98_auto_value(1, 1, 1, INST_MK_LEAVE) &&
              !inst_s98_auto_value(1, 1, 1, INST_MK_REMOVE),
              "Stor98 not in the run or not staged, or no service: not written");
        CHECK(inst_s98_switch_os(1, 6, 3, 9600), "8.1 (6.3.9600)");
        CHECK(inst_s98_switch_os(1, 6, 3, 9601), "an 8.1 build past 9600");
        CHECK(!inst_s98_switch_os(1, 6, 2, 9200), "8 (6.2.9200)");
        CHECK(!inst_s98_switch_os(1, 10, 0, 10240), "10 (10.0.10240)");
        CHECK(!inst_s98_switch_os(1, 10, 0, 19045), "10 22H2");
        CHECK(!inst_s98_switch_os(1, 10, 0, 22000), "11");
        CHECK(!inst_s98_switch_os(1, 6, 3, 10240),
              "6.3 with a build of 10 (an unmanifested answer on 10)");
        CHECK(!inst_s98_switch_os(1, 6, 3, 19045),
              "6.3 with a build of 10 22H2: a stale 8.1 value refused");
        CHECK(!inst_s98_switch_os(1, 6, 1, 7601) &&
              !inst_s98_switch_os(1, 6, 0, 6002) &&
              !inst_s98_switch_os(1, 5, 1, 2600) &&
              !inst_s98_switch_os(1, 5, 0, 2195), "7, Vista, XP, 2000");
        CHECK(!inst_s98_switch_os(0, 4, 10, 2222) &&
              !inst_s98_switch_os(0, 4, 90, 3000) &&
              !inst_s98_switch_os(0, 6, 3, 9600), "98 SE, ME, not NT");
        /* Task 43.23 (record 26 section 5.3.1 case 3b): 8.1's and 10's own
         * stornvme on a tunnel drive is installed over, forced; nothing
         * else is. */
        CHECK(inst_s98_over_inbox(1, "stornvme") &&
              inst_s98_over_inbox(1, "StorNVMe"),
              "8 and 8.1: a drive under stornvme is installed over");
        CHECK(!inst_s98_over_inbox(0, "stornvme"),
              "2000 to 7: stornvme is another driver, left");
        CHECK(!inst_s98_over_inbox(1, "stor98") &&
              !inst_s98_over_inbox(1, "secnvme") &&
              !inst_s98_over_inbox(1, "iaStorAC") &&
              !inst_s98_over_inbox(1, "stornvme2") &&
              !inst_s98_over_inbox(1, "") &&
              !inst_s98_over_inbox(1, NULL),
              "8 and 8.1: Stor98 itself, a vendor's NVMe driver, no service: "
              "not installed over");
        /* Windows 10 below build 22000 (task 43.34) takes Stor98's row as 8
         * and 8.1 do: the engine passes osKind == INST_OS_CMONLY to both
         * calls; 11 (build 22000 and later) never reaches them. */
        {
            int k10 = inst_os_kind(1, 10, 0, 19045) == INST_OS_CMONLY;
            int k1507 = inst_os_kind(1, 10, 0, 10240) == INST_OS_CMONLY;
            int k11 = inst_os_kind(1, 10, 0, 22000) == INST_OS_CMONLY;

            CHECK(k10 && k1507 && !k11 &&
                  !inst_s98_decide(1, 1, 0, 0, 0, 0, !k10, &asked, &def) &&
                  asked && !def,
                  "a 0 line (as 10 would take): Stor98 asked, Enter gives N");
            CHECK(!inst_s98_decide(1, 1, 1, 0, 0, 0, !k10, &asked, &def) &&
                  !asked &&
                  inst_s98_decide(1, 1, 1, 1, 0, 0, !k10, &asked, &def) &&
                  !asked,
                  "a 0 line: /yes gives N, /stor98 installs unasked");
            CHECK(inst_s98_over_inbox(k10, "stornvme") &&
                  !inst_s98_over_inbox(k10, "secnvme") &&
                  !inst_s98_over_inbox(k10, "stor98"),
                  "10: a drive under 10's own stornvme is installed over, "
                  "forced; a vendor's driver is left");
        }
        {
            int t;
            int all = 1;
            for (t = 0; t < INST_S98SYS_COUNT; t++) {
                all = all && inst_s98_box_default(t) ==
                      (t == INST_S98SYS_10 ? 0 : 1);
            }
            CHECK(all && inst_s98_box_default(-1) == 1 &&
                  inst_s98_box_default(INST_S98SYS_COUNT) == 1,
                  "Stor98's table: ticked on every system but 10 (owner, "
                  "2026-10-22; task 43.59, 2026-10-23)");
            CHECK(inst_s98_sys_index(0, 4, 10) == INST_S98SYS_98SE &&
                  inst_s98_sys_index(0, 4, 90) == INST_S98SYS_ME &&
                  inst_s98_sys_index(1, 5, 0) == INST_S98SYS_2000 &&
                  inst_s98_sys_index(1, 5, 1) == INST_S98SYS_XP &&
                  inst_s98_sys_index(1, 5, 2) == INST_S98SYS_XP &&
                  inst_s98_sys_index(1, 6, 0) == INST_S98SYS_VISTA &&
                  inst_s98_sys_index(1, 6, 1) == INST_S98SYS_7 &&
                  inst_s98_sys_index(1, 6, 2) == INST_S98SYS_8 &&
                  inst_s98_sys_index(1, 6, 3) == INST_S98SYS_81 &&
                  inst_s98_sys_index(1, 10, 0) == INST_S98SYS_10 &&
                  inst_s98_sys_index(0, 4, 0) == -1,
                  "the table's systems by version");
            CHECK(inst_cm_cat_refusal(INST_ARCH_AMD64, 1, 0) != NULL &&
                  strstr(inst_cm_cat_refusal(INST_ARCH_AMD64, 1, 0),
                         "xhcicm.cat") != NULL &&
                  inst_cm_cat_refusal(INST_ARCH_AMD64, 1, 1) == NULL &&
                  inst_cm_cat_refusal(INST_ARCH_AMD64, 0, 0) == NULL &&
                  inst_cm_cat_refusal(INST_ARCH_X86, 1, 0) == NULL,
                  "x64: the connection manager refused without xhcicm.cat; "
                  "no CM, or x86, not");
        }
        CHECK(inst_s98_box_open(1, 1, 1) && !inst_s98_box_open(1, 1, 0) &&
              !inst_s98_box_open(1, 0, 1) && !inst_s98_box_open(0, 1, 1),
              "the window's box: open only under a ticked Thunderbolt box");
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
    /* The record's Version (record 26 draft 43, task 44.30). */
    CHECK(inst_s98_version_owed(1, 1, 0, 0, 0, 1, 0),
          "98 SE and ME: a new copy carrying Stor98, no drive: written");
    CHECK(!inst_s98_version_owed(1, 1, 0, 0, 1, 1, 0),
          "NT: the copy alone writes nothing");
    CHECK(inst_s98_version_owed(1, 1, 0, 1, 1, 0, 0) &&
          inst_s98_version_owed(1, 1, 0, 1, 0, 0, 0),
          "a device installed or updated: written on both");
    CHECK(!inst_s98_version_owed(1, 1, 0, 0, 0, 0, 0),
          "98 SE and ME: no new copy and no device: nothing");
    CHECK(!inst_s98_version_owed(1, 1, 0, 0, 0, 1, 1),
          "a new copy, the owed file update not made: nothing");
    CHECK(inst_s98_version_owed(1, 1, 0, 1, 0, 1, 1),
          "a new copy and the file update made: written");
    CHECK(!inst_s98_version_owed(0, 1, 0, 1, 0, 1, 0) &&
          !inst_s98_version_owed(1, 0, 0, 1, 0, 1, 0) &&
          !inst_s98_version_owed(1, 1, 1, 1, 0, 1, 0),
          "not included, no baseline, or a listing: nothing");
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
    /* Record 23 section 4.4, revision 23: a known tunnel drive. */
    CHECK(inst_s98_known_tunnel(1, 1, INST_S98_AT_TUNNEL, 0) &&
          inst_s98_known_tunnel(0, 1, INST_S98_AT_TUNNEL, 0) &&
          !inst_s98_known_tunnel(0, 1, INST_S98_AT_INTERNAL, 1) &&
          !inst_s98_known_tunnel(1, 1, INST_S98_AT_UNKNOWN, 1),
          "present: only behind a tunnel");
    CHECK(inst_s98_known_tunnel(1, 0, INST_S98_AT_UNKNOWN, 1) &&
          !inst_s98_known_tunnel(1, 0, INST_S98_AT_UNKNOWN, 0),
          "NT, not present: only one the record names");
    CHECK(inst_s98_known_tunnel(0, 0, INST_S98_AT_UNKNOWN, 0),
          "98 SE and ME, not present: gone with its tunnel");
    /* Record 26 section 6.3, draft 16: 98 SE and ME's file update. */
    CHECK(inst_s98_file_update(0, 1, INST_S98_SHARE_FREE, 1, -1, 1),
          "98: an older stor98.mpd, free: replaced as a file");
    CHECK(!inst_s98_file_update(1, 1, INST_S98_SHARE_FREE, 1, -1, 1),
          "NT: never (SetupAPI updates Stor98 there)");
    CHECK(!inst_s98_file_update(0, 0, INST_S98_SHARE_FREE, 1, -1, 1) &&
          !inst_s98_file_update(0, 1, INST_S98_SHARE_HOLD, 1, -1, 1) &&
          !inst_s98_file_update(0, 1, INST_S98_SHARE_SAME, 1, -1, 1),
          "98: not without Stor98 in the run, nor under the shared-file "
          "rule");
    CHECK(!inst_s98_file_update(0, 1, INST_S98_SHARE_FREE, 0, -1, 1) &&
          !inst_s98_file_update(0, 1, INST_S98_SHARE_FREE, -1, -1, 1) &&
          !inst_s98_file_update(0, 1, INST_S98_SHARE_FREE, 1, 0, 1) &&
          !inst_s98_file_update(0, 1, INST_S98_SHARE_FREE, 1, 1, 1),
          "98: not with no file, an unreadable one, the same release or a "
          "newer one");
    CHECK(inst_s98_file_update(0, 1, INST_S98_SHARE_FREE, 1, 0, 0),
          "98: the same release with other bytes: replaced (record 23 "
          "section 5.4.1)");
    CHECK(!inst_s98_file_update(0, 1, INST_S98_SHARE_FREE, 1, 0, -1) &&
          !inst_s98_file_update(0, 1, INST_S98_SHARE_FREE, 1, 1, 0) &&
          !inst_s98_file_update(1, 1, INST_S98_SHARE_FREE, 1, 0, 0) &&
          !inst_s98_file_update(0, 1, INST_S98_SHARE_HOLD, 1, 0, 0) &&
          !inst_s98_file_update(0, 0, INST_S98_SHARE_FREE, 1, 0, 0),
          "98: not for unread bytes, a newer release, NT, a held file or "
          "Stor98 out of the run");
    CHECK(strcmp(inst_page_text(INST_PT_S98_UNUSED),
                 "Disconnect all external NVMe drives before Setup "
                 "continues.") == 0 &&
          strcmp(instS98DisconnectText, inst_page_text(INST_PT_S98_UNUSED))
          == 0, "the one box's text, the owner's words");
    /* The helper that did not finish (98 SE update, 2026-10-19). */
    CHECK(inst_di_unfinished(INST_DI_V_NONE) &&
          !inst_di_unfinished(INST_DI_V_INSTALLED) &&
          !inst_di_unfinished(INST_DI_V_FAILED) &&
          !inst_di_unfinished(INST_DI_V_REFUSED) &&
          !inst_di_unfinished(INST_DI_V_CANCELLED),
          "an unfinished helper is told from a failed or refused install");
    /* XhciCmMode 0 said (owner, 2026-10-19). */
    CHECK(inst_cm_mode_off("XhciCmMode", 0) &&
          !inst_cm_mode_off("XhciCmMode", 1) &&
          !inst_cm_mode_off("XhciCmApprove", 0) &&
          !inst_cm_mode_off(NULL, 0) &&
          strcmp(instCmOffText, "Thunderbolt / USB4 will be turned off "
                 "(XhciCmMode 0)") == 0, "the XhciCmMode 0 warning");
    /* The Thunderbolt row's note (owner, 2026-10-19): since record 26
     * draft 54 (owner, 2026-10-31) that one was found, with no name; what
     * a device can reach is the approval row's (section 5.3.5). */
    {
        CHECK(strcmp(inst_page_text(INST_PT_TB_T), "Thunderbolt / USB4 "
                     "controller found.") == 0,
              "found: the short sentence, no controller's name");
        CHECK(strstr(inst_page_text(INST_PT_TB), "Experimental") != NULL &&
              strstr(inst_page_text(INST_PT_TB_ABSENT), "No Thunderbolt / "
                     "USB4 controller found") != NULL,
              "the row says experimental, and whether one was found");
    }
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
    CHECK(inst_s98_run_report_9x("StorInterrupt 5.0.0") &&
          inst_s98_run_report_9x("storinterrupt 255.31.7") &&
          !inst_s98_run_report_9x("StorInterrupt") &&
          !inst_s98_run_report_9x("StorInterruptX 1.0.0") &&
          !inst_s98_run_report_9x("StorPollMode") &&
          !inst_s98_run_report_9x("LinkState"),
          "98 SE and ME: the driver's interrupt report is named "
          "StorInterrupt and a space, case aside");
    CHECK(inst_s98_stray_inf_ours(0, 1) && !inst_s98_stray_inf_ours(1, 1) &&
          !inst_s98_stray_inf_ours(0, 0),
          "a stray INF copy: the package's when Stor98's and none before");
    /* The owner, 2026-10-27: "the installer selects tunnel drives only"
     * dropped from it. */
    CHECK(strcmp(inst_page_text(INST_PT_S98_T),
                 "For NVMe drives in Thunderbolt / USB4 enclosures.") == 0,
          "the Components page's sentence");
    /* The owner, 2026-10-23 (replacing roadmap 43.7's "the bundled"). */
    CHECK(strcmp(inst_page_text(INST_PT_S98),
                 "Stor98 (hot-pluggable NVMe driver)") == 0,
          "the Components row: the owner's words of 2026-10-23");
}

/* ---- task 42-A.17's reading rule; task 42-A.5's installer hooks --------- */

/* Design record 31 (Stor98 for tunnelled drives only), the installer's
 * pure half: the candidates by class, the systems of the mark service's
 * fallback, the per-device route's force, the earlier INF's identity and
 * kind, and the LEFT sentence of an unknown place. */
static const char s98TestInf[] =
    "[Version]\r\n"
    "Signature=\"$CHICAGO$\"\r\n"
    "Class=SCSIAdapter\r\n"
    "ClassGUID={4D36E97B-E325-11CE-BFC1-08002BE10318}\r\n"
    "Provider=%Provider%\r\n"
    "DriverVer=10/24/2026,3.2.0.0\r\n"
    "[DestinationDirs]\r\nDefaultDestDir=12\r\n"
    "[SourceDisksNames]\r\n1=%DiskName%,,\r\n"
    "[SourceDisksFiles]\r\nstor98.mpd=1\r\nstor98.sys=1\r\n"
    "[Manufacturer]\r\n%Mfg%=Stor98_Models,NTx86.6.3\r\n"
    "[Stor98_Models]\r\n%Stor98.DeviceDesc%=Stor98_Inst,PCI\\CC_010802\r\n"
    "[Stor98_Models.NTx86.6.3]\r\n"
    "%Stor98.DeviceDesc%=Stor98_Inst,,PCI\\CC_010802\r\n"
    "[Stor98_Inst]\r\nCopyFiles=Stor98_Files_9x\r\n"
    "AddReg=Stor98_IOS,Stor98_AddReg_9x\r\n"
    "[Stor98_Files_9x]\r\nstor98.mpd\r\n"
    "[Stor98_IOS]\r\nHKR,,DevLoader,,*IOS\r\n"
    "[Stor98_AddReg_9x]\r\nHKR,,PortDriver,,stor98.mpd\r\n"
    "[Stor98_Inst.NTx86]\r\nCopyFiles=Stor98_Files_NT\r\n"
    "[Stor98_Inst.NTx86.Services]\r\n"
    "AddService=stor98,%SPSVCINST_ASSOCSERVICE%,Stor98_Service\r\n"
    "[Stor98_Files_NT]\r\nstor98.sys ; the NT miniport\r\n"
    "[Stor98_Service]\r\nServiceBinary=%12%\\stor98.sys\r\n"
    "[Strings]\r\nProvider=\"Yeo Kheng Meng\"\r\nMfg=\"Stor98 Project\"\r\n"
    "Stor98.DeviceDesc=\"Stor98 Storage Controller for NVMe SSDs\"\r\n"
    "DiskName=\"Stor98 Storage Controller Disk\"\r\n"
    "SPSVCINST_ASSOCSERVICE=0x00000002\r\n";

/* A copy of s98TestInf with one string replaced (the first occurrence). */
static char *s98_inf_with(const char *from, const char *to)
{
    const char *at = strstr(s98TestInf, from);
    size_t n = strlen(s98TestInf) - strlen(from) + strlen(to) + 1;
    char *out = (char *)malloc(n);

    if (out == NULL || at == NULL) {
        free(out);
        return NULL;
    }
    memcpy(out, s98TestInf, (size_t)(at - s98TestInf));
    strcpy(out + (at - s98TestInf), to);
    strcat(out, at + strlen(from));
    return out;
}

static void test_s98_tunnel(void)
{
    static const char nvme[] =
        "PCI\\VEN_1B36&DEV_0010&REV_02\0PCI\\VEN_1B36&DEV_0010\0"
        "PCI\\VEN_1B36&CC_010802\0PCI\\VEN_1B36&CC_0108\0PCI\\VEN_1B36\0"
        "PCI\\CC_010802\0PCI\\CC_0108\0\0";
    static const char nvmeLower[] = "pci\\cc_010802\0\0";
    static const char tagged[] =
        "PCI\\XHCI98_TUNNEL&CC_010802\0PCI\\CC_010802\0\0";
    static const char tagOnly[] = "PCI\\XHCI98_TUNNEL&CC_010802\0\0";
    static const char bridge[] =
        "PCI\\VEN_1B21&DEV_2421&REV_01\0PCI\\VEN_1B21&CC_060400\0"
        "PCI\\VEN_1B21&CC_0604\0PCI\\CC_060400\0PCI\\CC_0604\0\0";
    static const char usb[] =
        "PCI\\VEN_1B36&CC_0C0330\0PCI\\CC_0C0330\0PCI\\CC_0C03\0\0";
    static const char vendorOnly[] = "PCI\\VEN_1B36&CC_010802\0\0";
    static const char vmd[] = "PCI\\CC_010803\0PCI\\CC_0108\0\0";
    static const char hw[] = "PCI\\VEN_1B36&DEV_0010\0\0";
    static const INST_U32 pkg[4] = { 3, 3, 0, 0 };
    static const INST_U32 same[4] = { 3, 2, 0, 0 };
    static const INST_U32 older[4] = { 3, 1, 0, 0 };
    INST_IDLIST ids;
    char *t;
    const char *s;

    /* Section 7.2: the candidates by class. */
    CHECK(inst_s98_by_class(nvme, sizeof(nvme)),
          "an NVMe function: PCI\\CC_010802 among its compatible ids");
    CHECK(inst_s98_by_class(nvmeLower, sizeof(nvmeLower)),
          "the class compared case aside");
    CHECK(inst_s98_by_class(tagged, sizeof(tagged)),
          "a tagged drive: a candidate by class too");
    CHECK(!inst_s98_by_class(bridge, sizeof(bridge)),
          "a PCI-to-PCI bridge: refused");
    CHECK(!inst_s98_by_class(usb, sizeof(usb)),
          "a USB controller: refused");
    CHECK(!inst_s98_by_class(vendorOnly, sizeof(vendorOnly)),
          "the class inside a vendor's id is not the class id");
    CHECK(!inst_s98_by_class(vmd, sizeof(vmd)),
          "another 01:08 interface: refused");
    CHECK(!inst_s98_by_class(NULL, 0) && !inst_s98_by_class("\0", 2),
          "no list, an empty list: refused");
    memset(&ids, 0, sizeof(ids));
    ids.Count = 1;
    inst_copy(ids.Id[0], INST_S98_TUNNEL_ID, sizeof(ids.Id[0]));
    CHECK(inst_s98_candidate(hw, sizeof(hw), nvme, sizeof(nvme), &ids),
          "a plain drive, the INF naming the tunnel id alone: a candidate");
    CHECK(inst_s98_candidate(hw, sizeof(hw), tagOnly, sizeof(tagOnly), &ids),
          "the INF's id alone: a candidate");
    CHECK(!inst_s98_candidate(hw, sizeof(hw), bridge, sizeof(bridge), &ids) &&
          !inst_s98_candidate(hw, sizeof(hw), bridge, sizeof(bridge), NULL),
          "a bridge: no candidate");

    /* Section 6.2: the fallback's systems, 2000 to 8.1. */
    CHECK(inst_s98_fallback_os(1, 5, 0, 2195) &&
          inst_s98_fallback_os(1, 5, 1, 2600) &&
          inst_s98_fallback_os(1, 5, 2, 3790) &&
          inst_s98_fallback_os(1, 6, 0, 6002) &&
          inst_s98_fallback_os(1, 6, 1, 7601) &&
          inst_s98_fallback_os(1, 6, 2, 9200) &&
          inst_s98_fallback_os(1, 6, 3, 9600),
          "2000, XP, 2003, Vista, 7, 8 and 8.1: the fallback");
    CHECK(!inst_s98_fallback_os(1, 10, 0, 10240) &&
          !inst_s98_fallback_os(1, 10, 0, 19045) &&
          !inst_s98_fallback_os(1, 6, 3, 10240) &&
          !inst_s98_fallback_os(1, 6, 4, 9841),
          "10, and 6.3 or 6.4 at a 10 build: none");
    CHECK(!inst_s98_fallback_os(0, 4, 10, 2222) &&
          !inst_s98_fallback_os(0, 4, 90, 3000) &&
          !inst_s98_fallback_os(1, 4, 0, 1381),
          "98 SE, ME and NT 4: none");

    /* Section 7.2: the per-device route's force. */
    CHECK(!inst_s98_dev_forced(INST_ACT_INSTALL, 0),
          "a first install on a drive with no driver: unforced");
    CHECK(inst_s98_dev_forced(INST_ACT_INSTALL, 1),
          "case 3b, over stornvme: forced");
    CHECK(inst_s98_dev_forced(INST_ACT_UPDATE, 0) &&
          inst_s98_dev_forced(INST_ACT_CURRENT, 0) &&
          inst_s98_dev_forced(INST_ACT_NEWER, 0),
          "an update, a /force reinstall: forced");

    /* Section 7.1: the identity test and the kind. */
    CHECK(inst_s98_inf_identity(s98TestInf),
          "a Stor98 INF of this project's: taken");
    CHECK(inst_s98_inf_kind(s98TestInf, pkg) == INST_S98_INF_OLDER,
          "3.2.0.0's INF under a 3.3.0.0 package: older, removed");
    CHECK(inst_s98_inf_kind(s98TestInf, same) == INST_S98_INF_CURRENT &&
          inst_s98_inf_kind(s98TestInf, older) == INST_S98_INF_CURRENT,
          "the package's version or later: left");
    t = s98_inf_with("[SourceDisksFiles]\r\nstor98.mpd=1\r\nstor98.sys=1\r\n",
                     "[SourceDisksFiles]\r\nxhci98.sys=1\r\n");
    CHECK(t != NULL && !inst_s98_inf_identity(t) &&
          inst_s98_inf_kind(t, pkg) == INST_S98_INF_OTHER,
          "an xhci98.inf (this project's Provider, another file): refused");
    free(t);
    t = s98_inf_with("AddService=stor98,", "AddService=xhciuas,");
    CHECK(t != NULL && !inst_s98_inf_identity(t),
          "an xhciuas.inf (another service): refused");
    free(t);
    t = s98_inf_with("AddService=stor98,", "; AddService=stor98,");
    CHECK(t != NULL && !inst_s98_inf_identity(t),
          "stor98.sys with no service: refused (only a stor98.mpd INF has none)");
    free(t);
    {
        char *w9 = load("..\\src\\stor98\\inf\\stor98-9x.inf");
        char *m9 = NULL;
        CHECK(w9 != NULL && inst_s98_inf_identity(w9),
              "the 98 SE and ME INF (stor98.mpd alone, no service): taken");
        if (w9 != NULL) {
            const char *at = strstr(w9, "CopyFiles=Stor98_Files_9x");
            if (at != NULL) {
                size_t pre = (size_t)(at - w9);
                m9 = (char *)malloc(strlen(w9) + 32);
                if (m9 != NULL) {
                    memcpy(m9, w9, pre);
                    strcpy(m9 + pre, "CopyFiles=Stor98_Files_9x,@stor98.sys");
                    strcat(m9, at + strlen("CopyFiles=Stor98_Files_9x"));
                }
            }
        }
        CHECK(m9 != NULL && !inst_s98_inf_identity(m9),
              "a no-service INF copying stor98.sys by name: refused");
        free(m9);
        free(w9);
    }
    t = s98_inf_with("stor98.sys=1\r\n", "stor98.sys=1\r\nS98VSD.VXD=1\r\n");
    CHECK(t != NULL && !inst_s98_inf_identity(t),
          "a Stor98 INF with an added source file: refused");
    free(t);
    t = s98_inf_with("[Stor98_Files_9x]\r\nstor98.mpd\r\n",
                     "[Stor98_Files_9x]\r\nstor98.mpd\r\nother.vxd\r\n");
    CHECK(t != NULL && !inst_s98_inf_identity(t),
          "a Stor98 INF copying another file: refused");
    free(t);
    t = s98_inf_with("CopyFiles=Stor98_Files_NT", "CopyFiles=@other.sys");
    CHECK(t != NULL && !inst_s98_inf_identity(t),
          "a Stor98 INF copying another file by name: refused");
    free(t);
    t = s98_inf_with("CopyFiles=Stor98_Files_NT", "CopyFiles=@stor98.sys");
    CHECK(t != NULL && inst_s98_inf_identity(t),
          "stor98.sys copied by name: taken");
    free(t);
    t = s98_inf_with("[Stor98_Service]",
                     "AddService=other,0,Stor98_Service\r\n[Stor98_Service]");
    CHECK(t != NULL && !inst_s98_inf_identity(t),
          "a Stor98 INF with an added service: refused");
    free(t);
    t = s98_inf_with("[Stor98_Service]",
                     "DelService=other\r\n[Stor98_Service]");
    CHECK(t != NULL && !inst_s98_inf_identity(t),
          "a Stor98 INF that deletes a service: refused");
    free(t);
    t = s98_inf_with("CopyFiles=Stor98_Files_NT", "CopyFiles=Stor98_None");
    CHECK(t != NULL && !inst_s98_inf_identity(t),
          "a CopyFiles section that is not there: refused");
    free(t);
    t = s98_inf_with("Provider=\"Yeo Kheng Meng\"", "Provider=\"Someone Else\"");
    CHECK(t != NULL && !inst_s98_inf_identity(t) &&
          inst_s98_inf_kind(t, pkg) == INST_S98_INF_OTHER,
          "another Provider's Stor98: refused, left");
    free(t);
    t = s98_inf_with("[Stor98_Inst.NTx86.Services]\r\n"
                     "AddService=stor98,%SPSVCINST_ASSOCSERVICE%,"
                     "Stor98_Service\r\n", "");
    CHECK(t != NULL && !inst_s98_inf_identity(t),
          "no stor98 service: refused");
    free(t);
    t = s98_inf_with("DriverVer=10/24/2026,3.2.0.0\r\n", "");
    CHECK(t != NULL && inst_s98_inf_identity(t) &&
          inst_s98_inf_kind(t, pkg) == INST_S98_INF_OTHER,
          "no DriverVer: left");
    free(t);
    CHECK(!inst_s98_inf_identity(NULL) && !inst_s98_inf_identity(""),
          "no text: refused");

    /* Section 7.2's last paragraph: the LEFT sentence names the service. */
    s = inst_s98_left_text(INST_S98_SKIP_UNKNOWN, 1, 0, 0);
    CHECK(s != NULL && strstr(s, "mark service") != NULL &&
          strstr(s, "run the installer again") == NULL,
          "2000 to 8.1, place unknown: the mark service takes it");
    s = inst_s98_left_text(INST_S98_SKIP_UNKNOWN, 1, 0, 1);
    CHECK(s != NULL && strstr(s, "mark service") != NULL,
          "the window's short form says it too");
    CHECK(inst_s98_left_text(INST_S98_SKIP_UNKNOWN, 0, 0, 0) ==
              inst_s98_skip_text(INST_S98_SKIP_UNKNOWN, 0) &&
          inst_s98_left_text(INST_S98_SKIP_INTERNAL, 1, 0, 0) ==
              inst_s98_skip_text(INST_S98_SKIP_INTERNAL, 0) &&
          inst_s98_left_text(INST_S98_ELIGIBLE, 1, 0, 0) == NULL,
          "10 and 98 SE: the sentence as before; another reason unchanged");

    /* Record 31 section 9.3 item 10 (c), task 44.77: an update over a
     * connection manager that lists none of the host's tunnel ports
     * (3.2.0.0's on an ASMedia ASM4242) leaves Stor98 for the run after
     * the restart, and says so rather than "by hand". */
    CHECK(inst_tb_cm_changes(1, 1, 1, INST_ACT_UPDATE) &&
          inst_tb_cm_changes(1, 1, 1, INST_ACT_INSTALL) &&
          !inst_tb_cm_changes(1, 1, 1, INST_ACT_CURRENT) &&
          !inst_tb_cm_changes(1, 1, 1, INST_ACT_NEWER) &&
          !inst_tb_cm_changes(1, 1, 0, INST_ACT_UPDATE) &&
          !inst_tb_cm_changes(0, 1, 1, INST_ACT_UPDATE) &&
          !inst_tb_cm_changes(1, 0, 1, INST_ACT_UPDATE),
          "the connection manager changes: present, its INF, run, no "
          "driver or older");
    CHECK(inst_s98_read_later(1, 1, INST_S98_AT_UNKNOWN, 1) &&
          !inst_s98_read_later(1, 1, INST_S98_AT_UNKNOWN, 0) &&
          !inst_s98_read_later(1, 1, INST_S98_AT_INTERNAL, 1) &&
          !inst_s98_read_later(1, 1, INST_S98_AT_TUNNEL, 1) &&
          !inst_s98_read_later(1, 0, INST_S98_AT_UNKNOWN, 1) &&
          !inst_s98_read_later(0, 1, INST_S98_AT_UNKNOWN, 1),
          "read after the restart: present, an older Stor98, unknown, the "
          "connection manager changed; an internal drive never");
    s = inst_s98_left_text(INST_S98_SKIP_UNKNOWN, 1, 1, 0);
    CHECK(s != NULL && strstr(s, "Run Setup again after the "
                                 "restart") != NULL &&
          strstr(s, "mark service") == NULL &&
          strstr(s, "by hand") == NULL &&
          strstr(s, "internal drive is left unchanged") != NULL,
          "unknown, read later: run again after the restart, on 2000 to "
          "8.1 too");
    CHECK(inst_s98_left_text(INST_S98_SKIP_UNKNOWN, 0, 1, 0) == s,
          "unknown, read later: the same on 10 and 98 SE");
    s = inst_s98_left_text(INST_S98_SKIP_UNKNOWN, 1, 1, 1);
    CHECK(s != NULL && strstr(s, "after the restart") != NULL,
          "unknown, read later: the window's short form");
    s = inst_s98_left_text(INST_S98_SKIP_SHARED, 1, 1, 0);
    CHECK(s != NULL && strstr(s, "after the restart") != NULL &&
          strstr(s, "by hand") == NULL &&
          inst_s98_left_text(INST_S98_SKIP_SHARED, 1, 0, 0) ==
              inst_s98_skip_text(INST_S98_SKIP_SHARED, 0) &&
          inst_s98_left_text(INST_S98_SKIP_SHARED, 0, 1, 1) != NULL,
          "shared, held for a later reading: after the restart; otherwise "
          "as before");
    CHECK(inst_s98_later_said(INST_S98_SKIP_UNKNOWN, 1, 0, 0) &&
          inst_s98_later_said(INST_S98_SKIP_UNKNOWN, 1, 1, 1) &&
          !inst_s98_later_said(INST_S98_SKIP_UNKNOWN, 1, 1, 0) &&
          !inst_s98_later_said(INST_S98_SKIP_UNKNOWN, 0, 1, 1),
          "unknown: after the restart unless another drive keeps the hold "
          "(an internal drive on an older Stor98 beside it)");
    CHECK(inst_s98_later_said(INST_S98_SKIP_SHARED, 0, 1, 1) &&
          !inst_s98_later_said(INST_S98_SKIP_SHARED, 0, 1, 0) &&
          !inst_s98_later_said(INST_S98_SKIP_SHARED, 0, 0, 1) &&
          !inst_s98_later_said(INST_S98_SKIP_INTERNAL, 1, 1, 1) &&
          !inst_s98_later_said(INST_S98_NEEDS_TB, 1, 0, 0),
          "shared: after the restart only when every holder is read then; "
          "no other reason");
    /* Record 31 section 4.10 (task 48.29): on 8.1 a drive without the
     * tunnel id is left; after the restart when the run changes the
     * connection manager, else with no route. */
    CHECK(inst_s98_noid_why(0, 0, 1, 1) == INST_S98_ELIGIBLE &&
          inst_s98_noid_why(0, 0, 0, 0) == INST_S98_ELIGIBLE,
          "noid: not 8.1, never");
    CHECK(inst_s98_noid_why(1, 1, 1, 1) == INST_S98_ELIGIBLE &&
          inst_s98_noid_why(1, 1, 0, 0) == INST_S98_ELIGIBLE,
          "noid: 8.1 with the id: eligible");
    CHECK(inst_s98_noid_why(1, 0, 1, 1) == INST_S98_SKIP_NOID_RUN,
          "noid: 8.1, on Stor98, the connection manager changing: run again");
    CHECK(inst_s98_noid_why(1, 0, 0, 1) == INST_S98_SKIP_NOID_MARK,
          "noid: 8.1, on stornvme or none, changing: the mark service");
    CHECK(inst_s98_noid_why(1, 0, 1, 0) == INST_S98_SKIP_NOID &&
          inst_s98_noid_why(1, 0, 0, 0) == INST_S98_SKIP_NOID,
          "noid: 8.1, the connection manager unchanged: no route");
    CHECK(inst_s98_later_said(INST_S98_SKIP_NOID_RUN, 0, 0, 0) &&
          inst_s98_later_said(INST_S98_SKIP_NOID_RUN, 0, 1, 0) &&
          !inst_s98_later_said(INST_S98_SKIP_NOID_MARK, 1, 0, 0) &&
          !inst_s98_later_said(INST_S98_SKIP_NOID, 1, 0, 0),
          "noid: only the drive on Stor98 sends the user to a run after "
          "the restart");
    s = inst_s98_left_text(INST_S98_SKIP_NOID_RUN, 1, 1, 0);
    CHECK(s != NULL && strstr(s, "after the restart") != NULL &&
          strstr(s, "8.1") != NULL,
          "noid run: its sentence");
    s = inst_s98_left_text(INST_S98_SKIP_NOID_MARK, 1, 0, 0);
    CHECK(s != NULL && strstr(s, "mark service") != NULL,
          "noid mark: its sentence");
    s = inst_s98_left_text(INST_S98_SKIP_NOID, 1, 0, 0);
    CHECK(s != NULL && strstr(s, "no tunnel id") != NULL &&
          strstr(s, "after the restart") == NULL,
          "noid: no route, no promise");
    CHECK(inst_s98_left_text(INST_S98_SKIP_NOID_RUN, 0, 0, 1) != NULL &&
          inst_s98_left_text(INST_S98_SKIP_NOID_MARK, 0, 0, 1) != NULL &&
          inst_s98_left_text(INST_S98_SKIP_NOID, 0, 0, 1) != NULL,
          "noid: the window's short forms");
    /* 98 SE and ME: stor98.mpd put in IOSUBSYS before the first plug. */
    CHECK(inst_s98_mpd_place(0, 1, INST_S98_SHARE_FREE, 0, 0) &&
          !inst_s98_mpd_place(1, 1, INST_S98_SHARE_FREE, 0, 0) &&
          !inst_s98_mpd_place(0, 0, INST_S98_SHARE_FREE, 0, 0) &&
          !inst_s98_mpd_place(0, 1, INST_S98_SHARE_SAME, 0, 0) &&
          !inst_s98_mpd_place(0, 1, INST_S98_SHARE_HOLD, 0, 0) &&
          !inst_s98_mpd_place(0, 1, INST_S98_SHARE_FREE, 1, 0) &&
          !inst_s98_mpd_place(0, 1, INST_S98_SHARE_FREE, -1, 0) &&
          !inst_s98_mpd_place(0, 1, INST_S98_SHARE_FREE, 0, 1),
          "stor98.mpd placed: 9x, carried, free, absent, not a listing");
    CHECK(inst_s98_left_text(INST_S98_SKIP_INTERNAL, 1, 1, 0) ==
              inst_s98_skip_text(INST_S98_SKIP_INTERNAL, 0) &&
          inst_s98_left_text(INST_S98_NEEDS_TB, 0, 1, 0) ==
              inst_s98_skip_text(INST_S98_NEEDS_TB, 0),
          "later changes no other reason's sentence");
}

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
        INST_ENV w10x86 = env(1, 10, 0, INST_ARCH_X86);
        INST_ENV w101 = env(1, 10, 1, INST_ARCH_AMD64);
        INST_ENV w64 = env(1, 6, 4, INST_ARCH_AMD64);
        INST_ENV w98 = env(0, 4, 10, INST_ARCH_X86);
        CHECK(inst_mk_system(&w2k) && inst_mk_system(&w7) &&
              inst_mk_system(&w8) && inst_mk_system(&w81) &&
              inst_mk_system(&w10) && inst_mk_system(&w10x86) &&
              !inst_mk_system(&w101) && !inst_mk_system(&w64) &&
              !inst_mk_system(&w98),
              "the mark service: Windows 2000 to 10 (revision 28)");
    }
    CHECK(inst_mk_end_action(1, 1, 1, 1) == INST_MK_INSTALL &&
          inst_mk_end_action(1, 0, 1, 1) == INST_MK_LEAVE &&
          inst_mk_end_action(1, 1, 0, 1) == INST_MK_LEAVE &&
          inst_mk_end_action(0, 1, 1, 1) == INST_MK_REMOVE &&
          inst_mk_end_action(0, 0, 0, 1) == INST_MK_REMOVE &&
          inst_mk_end_action(-1, 1, 1, 1) == INST_MK_LEAVE,
          "a run's end: the service with xhcicm, every mark gone without it");
    CHECK(inst_mk_end_action(1, 1, 1, 0) == INST_MK_REMOVE &&
          inst_mk_end_action(1, 0, 1, 0) == INST_MK_REMOVE &&
          inst_mk_end_action(1, 1, 0, 0) == INST_MK_REMOVE &&
          inst_mk_end_action(0, 1, 1, 0) == INST_MK_REMOVE &&
          inst_mk_end_action(-1, 1, 1, 0) == INST_MK_LEAVE,
          "a run's end with XhciCmSafeEject 0: no service, every mark gone "
          "(a run that set it without the Thunderbolt component too)");
    CHECK(inst_mk_hold_how(1, 1, 1, 1) == INST_MK_HOLD_HELD &&
          inst_mk_hold_how(1, 1, 1, -1) == INST_MK_HOLD_HELD,
          "the hold: a service that knows it, the barrier passed: held");
    CHECK(inst_mk_hold_how(1, 0, 0, 1) == INST_MK_HOLD_STOP &&
          inst_mk_hold_how(1, 0, 0, -1) == INST_MK_HOLD_STOP,
          "the hold: an earlier release's service running (or unread): "
          "stopped for the run");
    CHECK(inst_mk_hold_how(1, 1, 0, 1) == INST_MK_HOLD_STOP &&
          inst_mk_hold_how(1, 1, 0, -1) == INST_MK_HOLD_STOP,
          "the hold: the idle event not seen set within the wait: the "
          "service stopped for the run");
    CHECK(inst_mk_owner_ok(INST_OWNER_SYSTEM, 1) &&
          !inst_mk_owner_ok(INST_OWNER_ADMINS, 1) &&
          !inst_mk_owner_ok(INST_OWNER_OTHER, 1) &&
          inst_mk_owner_ok(INST_OWNER_ADMINS, 0) &&
          !inst_mk_owner_ok(INST_OWNER_OTHER, 0),
          "named objects: an idle event not SYSTEM's is no service's; a hold "
          "event of another owner is no installer's");
    CHECK(inst_mk_restore_how(0, 1, 0) == INST_MK_RESTORE_NONE &&
          inst_mk_restore_how(1, 1, 1) == INST_MK_RESTORE_CLEAR &&
          inst_mk_restore_how(1, 0, 0) == INST_MK_RESTORE_CLEAR &&
          inst_mk_restore_how(1, 0, -1) == INST_MK_RESTORE_CLEAR &&
          inst_mk_restore_how(1, 1, 0) == INST_MK_RESTORE_START &&
          inst_mk_restore_how(1, 1, 2) == INST_MK_RESTORE_FAIL &&
          inst_mk_restore_how(1, 1, 3) == INST_MK_RESTORE_FAIL &&
          inst_mk_restore_how(1, 1, -1) == INST_MK_RESTORE_FAIL &&
          inst_mk_restore_how(1, -1, 0) == INST_MK_RESTORE_FAIL,
          "the owed restart: cleared only once read running or removed, started "
          "when stopped, a stop still pending or an unread state reported "
          "and kept");
    CHECK(inst_mk_hold_how(0, 1, 1, 1) == INST_MK_HOLD_STOP &&
          inst_mk_hold_how(0, 0, 0, 1) == INST_MK_HOLD_STOP &&
          inst_mk_hold_how(0, 0, 0, 0) == INST_MK_HOLD_STOP,
          "the hold not made (no event): the service stopped instead");
    CHECK(inst_mk_hold_gen_next(0UL) == 1UL &&
          inst_mk_hold_gen_next(1UL) == 2UL &&
          inst_mk_hold_gen_next(41UL) == 42UL &&
          inst_mk_hold_gen_next(0xFFFFFFFFUL) == 1UL,
          "the hold's run number: the last one's plus one, never 0, and "
          "never the last one's");
    CHECK(inst_mk_hold_gen_pick(-1, 0UL, 1, 1UL) == 0UL &&
          inst_mk_hold_gen_pick(1, 1UL, -1, 0UL) == 0UL &&
          inst_mk_hold_gen_pick(0, 0UL, 0, 0UL) == 1UL &&
          inst_mk_hold_gen_pick(1, 4UL, 1, 4UL) == 5UL &&
          inst_mk_hold_gen_pick(0, 0UL, 1, 1UL) == 2UL &&
          inst_mk_hold_gen_pick(1, 0xFFFFFFFFUL, 1, 1UL) == 2UL,
          "the hold's run number: none when HoldRun or HoldAck cannot be "
          "read (an earlier run's hold, expired, with HoldAck 1 and HoldRun "
          "unread: no reuse of 1); 1 when both are absent; never the "
          "number already acknowledged");
    CHECK(inst_run_guard_how(1, INST_OWNER_ADMINS) == INST_GUARD_TAKEN &&
          inst_run_guard_how(1, INST_OWNER_OTHER) == INST_GUARD_TAKEN &&
          inst_run_guard_how(0, INST_OWNER_ADMINS) == INST_GUARD_BUSY &&
          inst_run_guard_how(0, INST_OWNER_SYSTEM) == INST_GUARD_BUSY &&
          inst_run_guard_how(0, INST_OWNER_OTHER) == INST_GUARD_FOREIGN &&
          inst_run_guard_how(-1, INST_OWNER_ADMINS) == INST_GUARD_ERROR &&
          inst_run_guard_how(-1, INST_OWNER_OTHER) == INST_GUARD_ERROR,
          "one run at a time: the guard made by this run runs; found and an "
          "installer's (another run), found and no installer's, or not "
          "made: refused");
    {
        /* The hold's end's lifecycle: run A holds the guard and has stopped
         * the service (its marker set); a run of the tools alone ending
         * meanwhile, and A's own second end after the guard was given back
         * (another run may hold it by then), touch nothing. */
        int ownsA = 1;
        int ownsTools = 0;

        CHECK(inst_mk_end_may(ownsA) &&
              !inst_mk_end_may(ownsTools),
              "the hold's end: only the guard's holder reads the marker and "
              "restarts; a run that took none does nothing");
        ownsA = 0;  /* given back at A's first end */
        CHECK(!inst_mk_end_may(ownsA),
              "the hold's end called again after the guard was given back: "
              "nothing (another run may have stopped the service)");
    }
    CHECK(inst_run_guard_needed(1, 0) && !inst_run_guard_needed(1, 1) &&
          !inst_run_guard_needed(0, 0) && !inst_run_guard_needed(0, 1),
          "the run's guard: every NT run that can change anything (the "
          "tools alone and a driver set alone included); not /list, not 98 "
          "SE and ME");
    /* Every exit path of both front ends after the guard is taken: kept
     * only while the run can still go on. */
    CHECK(!inst_run_guard_keep(INST_EXIT_REFUSED, -1, 0),
          "the guard: the reading refused (the device scan, a driver set "
          "asked for and not there, no rights): given back");
    CHECK(!inst_run_guard_keep(-1, INST_EXIT_REFUSED, 0),
          "the guard: the first plan refused (the wizard's Welcome greyed, "
          "the console's end): given back");
    CHECK(inst_run_guard_keep(-1, -1, 0),
          "the guard: read and planned, the questions or the pages still "
          "open (the wizard's Components page refusing a choice included): "
          "kept");
    CHECK(!inst_run_guard_keep(-1, -1, 1),
          "the guard: inst_eng_end reached (a run's end, the questions "
          "declined or refused, a refusal at the run's start, the window "
          "closed before Install): given back");
    CHECK(!inst_run_guard_keep(INST_EXIT_DONE, -1, 1) &&
          !inst_run_guard_keep(-1, INST_EXIT_DONE, 1),
          "the guard: a reading or plan that ended the run, then the end: "
          "given back (once: the give is idempotent)");
    {
        /* Two runs started together, the guard deciding which picks a
         * number: run A takes it and picks; run B finds it held and picks
         * nothing (it is refused before any read); after A's end B's next
         * attempt takes it and picks the number after A's, never A's. */
        INST_U32 a;
        INST_U32 b = 0;
        int guardA = inst_run_guard_how(1, INST_OWNER_ADMINS);
        int guardB = inst_run_guard_how(0, INST_OWNER_ADMINS);

        a = guardA == INST_GUARD_TAKEN ?
            inst_mk_hold_gen_pick(1, 6UL, 1, 6UL) : 0UL;
        if (guardB == INST_GUARD_TAKEN) {
            b = inst_mk_hold_gen_pick(1, 6UL, 1, 6UL);
        }
        CHECK(a == 7UL && b == 0UL,
              "two runs together: the guard's holder picks 7, the other "
              "picks nothing");
        b = inst_run_guard_how(1, INST_OWNER_ADMINS) == INST_GUARD_TAKEN ?
            inst_mk_hold_gen_pick(1, a, 1, a) : 0UL;
        CHECK(b == 8UL && b != a,
              "two runs: the later one, once the first has ended, picks 8");
    }
    CHECK(inst_mk_hold_how(1, 0, 0, 0) == INST_MK_HOLD_NONE,
          "the hold: no service running: the event alone, for a service the "
          "run starts");
    CHECK(inst_mk_put_next(1, 5UL, 1) == INST_MK_PUT_RETRY &&
          inst_mk_put_next(1, 32UL, 1) == INST_MK_PUT_RETRY &&
          inst_mk_put_next(0, 5UL, 1) == INST_MK_PUT_RETRY &&
          inst_mk_put_next(1, 5UL, 0) == INST_MK_PUT_QUEUE &&
          inst_mk_put_next(1, 32UL, 0) == INST_MK_PUT_QUEUE &&
          inst_mk_put_next(0, 5UL, 0) == INST_MK_PUT_FAIL &&
          inst_mk_put_next(0, 32UL, 0) == INST_MK_PUT_FAIL,
          "the mark service's copy in use: tried again, then replaced at the "
          "restart when the service was registered (task 44.76)");
    CHECK(inst_mk_put_next(1, 2UL, 1) == INST_MK_PUT_FAIL &&
          inst_mk_put_next(1, 112UL, 0) == INST_MK_PUT_FAIL &&
          inst_mk_put_next(1, 0UL, 1) == INST_MK_PUT_FAIL,
          "any other error fails the mark service's copy at once");
    {
        static const char *const gone[] = { "xhci98", "xhcicm" };

        CHECK(inst_cddb_service_gone("xhcicm", gone, 2) &&
              inst_cddb_service_gone("XHCI98", gone, 2) &&
              !inst_cddb_service_gone("xhciuas", gone, 2) &&
              !inst_cddb_service_gone("pci", gone, 2) &&
              !inst_cddb_service_gone("usbxhci", gone, 2) &&
              !inst_cddb_service_gone("xhcicmx", gone, 2) &&
              !inst_cddb_service_gone("", gone, 2) &&
              !inst_cddb_service_gone(NULL, gone, 2) &&
              !inst_cddb_service_gone("xhcicm", gone, 0) &&
              !inst_cddb_service_gone("xhcicm", NULL, 2),
              "a CriticalDeviceDatabase entry goes only when its Service is a "
              "service of this package the run removed (case ignored), never "
              "another driver's");
    }
    {
        INST_VNOW n0[2];
        INST_VNOW n1[2];
        INST_VNOW nn[1];

        n0[0].State = 1;
        n0[0].Value = 0;
        n0[0].Desc = "a";
        n0[1] = n0[0];
        n1[0] = n0[0];
        n1[1].State = 1;
        n1[1].Value = 1;
        n1[1].Desc = "b";
        nn[0].State = 0;
        nn[0].Value = 0;
        nn[0].Desc = "c";
        CHECK(inst_mk_safe_eject(1, 0, n1, 2) == 0 &&
              inst_mk_safe_eject(1, 1, n0, 2) == 1 &&
              inst_mk_safe_eject(1, 2, n0, 2) == 1,
              "safe eject in force: the machine-wide value wins over every "
              "device's own");
        CHECK(inst_mk_safe_eject(-1, 0, n0, 2) == 1,
              "safe eject in force: a machine-wide value not a DWORD is the "
              "driver's default 1");
        CHECK(inst_mk_safe_eject(0, 0, n0, 2) == 0 &&
              inst_mk_safe_eject(0, 0, n1, 2) == 1 &&
              inst_mk_safe_eject(0, 0, nn, 1) == 1,
              "safe eject in force with no machine-wide value: 0 only when "
              "every device holds 0; one not set is the default 1");
        CHECK(inst_mk_safe_eject(0, 0, NULL, 0) == 1 &&
              inst_mk_safe_eject(0, 0, n0, 0) == 1,
              "safe eject in force with no device and no machine-wide value: "
              "the default 1");
    }

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

/* ---- record 26 section 6.5: the USB storage drivers on Windows 98 SE ---- */

/* Every name unreadable. */
static int silent_taken_err(const char *name, void *ctx)
{
    (void)name;
    (void)ctx;
    return -1;
}

/* A name taken: XHCI98S2 alone. */
static int silent_taken_s2(const char *name, void *ctx)
{
    (void)ctx;
    return strcmp(name, "XHCI98S2") == 0;
}

static void stor_fill(int *st, int v)
{
    int i;

    for (i = 0; i < INST_STOR_COUNT; i++) {
        st[i] = v;
    }
}

static int stor_inf_is(const char *t, int i)
{
    return inst_stor_inf_is(t, strlen(t), i);
}

static void test_storage(void)
{
    int st[INST_STOR_COUNT];
    int cand[3];
    char text[512];
    char *lp;
    int i;
    char ids[64];
    size_t n;
    const char *own = "AAAA";
    const char *pack = instStorFile[0].Sha256;

    /* The seven against legal-provenance.md section 5's table. */
    lp = load("..\\docs\\contributing\\legal-provenance.md");
    CHECK(lp != NULL, "legal-provenance.md read");
    for (i = 0; lp != NULL && i < INST_STOR_COUNT; i++) {
        char row[200];
        char hash[65];
        size_t k;
        for (k = 0; k < 64; k++) {
            char c = instStorFile[i].Sha256[k];
            hash[k] = (char)((c >= 'A' && c <= 'F') ? c - 'A' + 'a' : c);
        }
        hash[64] = '\0';
        sprintf(row, "| `%s` |", instStorFile[i].Name);
        CHECK(strstr(lp, row) != NULL &&
              strstr(strstr(lp, row), hash) != NULL &&
              strstr(strstr(lp, row), "\n") > strstr(strstr(lp, row), hash),
              "the seven's hash is legal-provenance.md's");
        CHECK(inst_stor_index(instStorFile[i].Name) == i, "the seven by name");
    }
    free(lp);
    CHECK(inst_stor_index("xhci98.sys") < 0, "not one of the seven");
    CHECK(instStorFile[4].Dir == INST_STOR_DIR_IOS &&
          instStorFile[0].Dir == INST_STOR_DIR_DRIVERS &&
          instStorFile[INST_STOR_IDX_USBSTOR_INF].Dir == INST_STOR_DIR_INF,
          "where their INFs send them");

    /* The states. */
    CHECK(inst_stor_state(0, 0, NULL, pack, NULL) == INST_STOR_ABSENT,
          "absent");
    CHECK(inst_stor_state(1, 0, NULL, pack, NULL) == INST_STOR_OTHER,
          "unreadable is other");
    CHECK(inst_stor_state(1, 1, NULL, pack, pack) == INST_STOR_SAME,
          "the download's bytes");
    CHECK(inst_stor_state(1, 1, pack, pack, pack) == INST_STOR_OWN,
          "recorded with its bytes");
    CHECK(inst_stor_state(1, 1, own, pack, pack) == INST_STOR_SAME,
          "recorded with other bytes than now, the download's: the same");
    CHECK(inst_stor_state(1, 1, NULL, pack, own) == INST_STOR_OTHER,
          "other bytes");
    cand[0] = INST_STOR_OWN;
    cand[1] = INST_STOR_SAME;
    cand[2] = INST_STOR_OTHER;
    CHECK(inst_stor_inf_state(cand, 0) == INST_STOR_ABSENT &&
          inst_stor_inf_state(cand, 1) == INST_STOR_OWN &&
          inst_stor_inf_state(cand, 2) == INST_STOR_SAME &&
          inst_stor_inf_state(cand, 3) == INST_STOR_OTHER,
          "an INF over its candidates");
    CHECK(stor_inf_is("[Manufacturer]\r\nx=y\r\n[a]\r\nHKR,,NTMPDriver,,"
                           "USBSTOR.SYS\r\n", INST_STOR_IDX_USBSTOR_INF),
          "a renamed USBSTOR.INF by its text");
    CHECK(!stor_inf_is("[Version]\r\n[W98Un.Sys32]\r\nUSBSTOR.SYS\r\n", INST_STOR_IDX_USBSTOR_INF),
          "no [Manufacturer]: not a device INF");
    CHECK(!stor_inf_is("[Manufacturer]\r\n; USBMPHLP.PDR\r\n", INST_STOR_IDX_USBNTMAP_INF),
          "a comment does not name it");
    CHECK(!stor_inf_is("[Manufacturer]\r\nUSBSTOR.SYS\r\n", 0),
          "a binary has no key file");

    /* The cases, the default, the copies, the text. */
    stor_fill(st, INST_STOR_ABSENT);
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_NONE &&
          inst_stor_route(INST_STOR_CASE_NONE), "none: the route");
    CHECK(inst_stor_default(0, 0, 0, st), "none found: ticked");
    CHECK(!inst_stor_default(0, 1, 0, st), "a recorded No is kept");
    /* NUSB installed (its Uninstall\USB20U entry; owner, 2026-10-22):
     * unticked with no file found, and over a recorded Yes. */
    CHECK(!inst_stor_default(1, 0, 0, st), "NUSB installed, none found: unticked");
    CHECK(!inst_stor_default(1, 1, 1, st),
          "NUSB installed: a recorded Yes does not tick it");
    CHECK(!inst_stor_default(1, 1, 0, st), "NUSB installed, a recorded No");
    CHECK(inst_stor_copy_set(1, st, 0) == 0x1FUL,
          "none: the five binaries copied, never an INF");
    CHECK(inst_stor_copy_set(0, st, 0) == 0, "unticked: nothing copied");
    inst_stor_found_text(text, sizeof(text), st);
    CHECK(strstr(text, "Not installed") != NULL, "none: the text");
    /* The row's note: the same without NUSB; with it, the sentence that
     * NUSB is installed and the row can be left unticked. */
    inst_stor_note_text(text, sizeof(text), st, 0);
    CHECK(strcmp(text, "Not installed, USB disks will not work on Windows 98 "
                 "SE without them.") == 0, "none, no NUSB: the note");
    inst_stor_note_text(text, sizeof(text), st, 1);
    CHECK(strcmp(text, "Not installed, USB disks will not work on Windows 98 "
                 "SE without them. NUSB is installed on this computer "
                 "(found its Uninstall\\USB20U entry).") == 0,
          "none, NUSB installed: the note");

    stor_fill(st, INST_STOR_SAME);
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_COMPLETE &&
          !inst_stor_route(INST_STOR_CASE_COMPLETE), "complete: nothing");
    CHECK(!inst_stor_default(0, 0, 0, st), "found: unticked");
    CHECK(inst_stor_default(0, 1, 1, st), "a recorded Yes is kept");
    CHECK(!inst_stor_default(1, 0, 0, st), "found, NUSB installed: unticked");
    CHECK(inst_stor_copy_set(1, st, 0) == 0, "complete: nothing copied");
    inst_stor_found_text(text, sizeof(text), st);
    CHECK(strstr(text, "Already installed: USBSTOR.SYS,") != NULL &&
          strstr(text, "Left as-is") != NULL &&
          strstr(text, "not known") == NULL, "complete: the text");
    inst_stor_note_text(text, sizeof(text), st, 1);
    CHECK(strstr(text, "Left as-is. " INST_STOR_NUSB_TEXT) != NULL &&
          strlen(text) < 300, "complete, NUSB installed: the note");
    st[1] = INST_STOR_OTHER;
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_COMPLETE,
          "complete whatever the bytes");
    stor_fill(st, INST_STOR_OWN);
    st[2] = INST_STOR_SAME;
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_COMPLETE,
          "all present, one not this installer's: complete");

    stor_fill(st, INST_STOR_ABSENT);
    st[0] = INST_STOR_SAME;
    st[INST_STOR_IDX_USBSTOR_INF] = INST_STOR_SAME;
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_SAME &&
          inst_stor_route(INST_STOR_CASE_SAME), "partial, the same: the route");
    CHECK(!inst_stor_default(0, 0, 0, st), "partial: unticked");
    CHECK(inst_stor_copy_set(1, st, 0) == 0x1EUL,
          "partial, the same: only the absent binaries copied");
    inst_stor_found_text(text, sizeof(text), st);
    CHECK(strcmp(text, "Found: USBSTOR.SYS, USBSTOR.INF. Only some of the "
                 "seven, so USB storage may not work.") == 0,
          "partial: the text says it may not work");
    CHECK(strstr(text, "is ready") == NULL, "partial: never ready");

    st[1] = INST_STOR_OTHER;
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_OTHER &&
          !inst_stor_route(INST_STOR_CASE_OTHER),
          "partial, other: no route");
    CHECK(inst_stor_copy_set(1, st, 0) == 0, "partial, other: nothing copied");
    inst_stor_found_text(text, sizeof(text), st);
    CHECK(strstr(text, "USBNTMAP.SYS") != NULL &&
          strstr(text, "may not work") != NULL,
          "partial, other: the text");

    stor_fill(st, INST_STOR_ABSENT);
    for (i = 0; i < INST_STOR_BINARIES; i++) {
        st[i] = INST_STOR_SAME;
    }
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_SAME &&
          inst_stor_copy_set(1, st, 0) == 0,
          "the binaries without their INFs, the same: the route, no copy");

    stor_fill(st, INST_STOR_OWN);
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_OWN &&
          inst_stor_route(INST_STOR_CASE_OWN), "this installer's: the route");
    CHECK(inst_stor_default(0, 0, 0, st), "this installer's are not found");
    CHECK(inst_stor_copy_set(1, st, 0) == 0,
          "this installer's: nothing present replaced");
    inst_stor_found_text(text, sizeof(text), st);
    CHECK(strcmp(text, "Installed earlier by this installer.") == 0,
          "this installer's: the text");
    st[3] = INST_STOR_ABSENT;
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_OWN &&
          inst_stor_copy_set(1, st, 0) == 0x08UL,
          "this installer's, one deleted since: that one copied again");
    st[0] = INST_STOR_OTHER;
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_OTHER &&
          inst_stor_copy_set(1, st, 0) == 0,
          "a recorded file changed: other, nothing copied");

    /* Every combination of the seven's states, with and without Source,
     * ticked and not: the case, the route and the copies held to the
     * design's rules stated as properties (record 26 section 6.5.4). */
    {
        long combo;
        int source;
        int bad = 0;
        for (combo = 0; combo < 16384L && !bad; combo++) {
            int absent = 0;
            int found = 0;
            int other = 0;
            long c = combo;
            for (i = 0; i < INST_STOR_COUNT; i++) {
                st[i] = (int)(c % 4);
                c /= 4;
                absent += st[i] == INST_STOR_ABSENT;
                found += st[i] == INST_STOR_SAME || st[i] == INST_STOR_OTHER;
                other += st[i] == INST_STOR_OTHER;
            }
            for (source = 0; source < 2 && !bad; source++) {
                int cs = inst_stor_case(st, source);
                int route = inst_stor_route(cs);
                INST_U32 set1 = inst_stor_copy_set(1, st, source);
                INST_U32 set0 = inst_stor_copy_set(0, st, source);
                int want = other == 0 &&
                           !(absent == 0 && found > 0 && !source);
                /* The route exactly when nothing of another's bytes is
                 * there and it is not a complete installation that is not
                 * this installer's deployment. */
                if (route != want) {
                    bad = 1;
                }
                /* Complete only with every file present. */
                if (cs == INST_STOR_CASE_COMPLETE && absent != 0) {
                    bad = 1;
                }
                /* Never a copy unticked, off the route, over a present
                 * file, or of an INF; every absent binary on the route. */
                if (set0 != 0 || (!route && set1 != 0)) {
                    bad = 1;
                }
                for (i = 0; i < INST_STOR_COUNT; i++) {
                    int copied = i < 32 && (set1 & (1UL << i)) != 0;
                    if (copied && (st[i] != INST_STOR_ABSENT ||
                                   i >= INST_STOR_BINARIES)) {
                        bad = 1;
                    }
                    if (route && i < INST_STOR_BINARIES &&
                        st[i] == INST_STOR_ABSENT && !copied) {
                        bad = 1;
                    }
                }
                /* The default: ticked exactly when nothing is found, and
                 * never while NUSB is installed. */
                if (inst_stor_default(0, 0, 0, st) != (found == 0) ||
                    !inst_stor_default(0, 1, 1, st) ||
                    inst_stor_default(0, 1, 0, st) ||
                    inst_stor_default(1, 0, 0, st) ||
                    inst_stor_default(1, 1, 1, st)) {
                    bad = 1;
                }
                if (bad) {
                    printf("storage combination %ld, Source %d: case %d\n",
                           combo, source, cs);
                }
            }
        }
        CHECK(!bad, "every combination of the seven's states: the case, the "
              "route, the copies and the default");
    }

    /* The devices. */
    memset(ids, 0, sizeof(ids));
    strcpy(ids, "USB\\Class_08&SubClass_06&Prot_50");
    n = strlen(ids) + 2;
    CHECK(inst_stor_kind(NULL, 0, ids, n) == INST_STOR_KIND_BULK,
          "Bulk-Only by its compatible id");
    strcpy(ids, "usb\\class_08&subclass_02&prot_50");
    CHECK(inst_stor_kind(ids, strlen(ids) + 2, NULL, 0) ==
          INST_STOR_KIND_BULK, "case aside");
    memset(ids, 0, sizeof(ids));
    strcpy(ids, "USB\\Class_08&SubClass_06&Prot_62");
    CHECK(inst_stor_kind(NULL, 0, ids, strlen(ids) + 2) ==
          INST_STOR_KIND_NONE, "UAS is xhciuas.inf's");
    memset(ids, 0, sizeof(ids));
    strcpy(ids, "USB\\Class_08&SubClass_04&Prot_00");
    CHECK(inst_stor_kind(NULL, 0, ids, strlen(ids) + 2) ==
          INST_STOR_KIND_NONE, "CBI is not the helper's");
    memset(ids, 0, sizeof(ids));
    strcpy(ids, "USBSTOR\\GenDisk");
    CHECK(inst_stor_kind(ids, strlen(ids) + 2, NULL, 0) ==
          INST_STOR_KIND_UNIT, "a unit");
    CHECK(inst_stor_unit_takes(0), "a unit with no driver");
    CHECK(!inst_stor_unit_takes(1),
          "a unit bound any way (DISKDRV.INF included): left");

    /* The pair all or nothing (record 26 section 6.5.5, task 43.5): the
     * order, the late removal, the pair and the Bulk-Only gate. */
    {
        int k;
        int ntmap = -1;
        int usbstor = -1;
        for (k = 0; k < INST_STOR_COUNT; k++) {
            if (instStorCopyOrder[k] == INST_STOR_IDX_USBNTMAP_INF) {
                ntmap = k;
            } else if (instStorCopyOrder[k] == INST_STOR_IDX_USBSTOR_INF) {
                usbstor = k;
            } else {
                CHECK(instStorCopyOrder[k] == k && k < INST_STOR_BINARIES,
                      "order: the binaries first, in their order");
            }
        }
        CHECK(ntmap == INST_STOR_BINARIES && usbstor == INST_STOR_COUNT - 1,
              "order: USBNTMAP.INF before USBSTOR.INF, USBSTOR.INF last");
    }
    CHECK(inst_stor_rm_late("USBNTMAP.INF", NULL) &&
          inst_stor_rm_late("usbntmap.inf", NULL),
          "late: USBNTMAP.INF by name, any case");
    CHECK(!inst_stor_rm_late("USBSTOR.INF", NULL) &&
          !inst_stor_rm_late("USBMPHLP.PDR", NULL),
          "late: USBSTOR.INF and the binaries first");
    CHECK(inst_stor_rm_late(NULL,
              instStorFile[INST_STOR_IDX_USBNTMAP_INF].Sha256),
          "late: a copy of USBNTMAP.INF's bytes");
    CHECK(!inst_stor_rm_late(NULL,
              instStorFile[INST_STOR_IDX_USBSTOR_INF].Sha256) &&
          !inst_stor_rm_late(NULL, NULL) && !inst_stor_rm_late(NULL, ""),
          "late: USBSTOR.INF's bytes, or nothing recorded: first");
    /* roadmap-usb4 43.5 (2026-10-23): an uninstall whose own USBSTOR.INF could not be
     * deleted keeps USBNTMAP.INF's record for the run after the restart. */
    CHECK(inst_stor_late_action(0, 0) == INST_STOR_LATE_GO &&
          inst_stor_late_action(0, 1) == INST_STOR_LATE_GO,
          "late: no USBSTOR.INF left, USBNTMAP.INF goes");
    CHECK(inst_stor_late_action(-1, 0) == INST_STOR_LATE_UNREAD &&
          inst_stor_late_action(-1, 1) == INST_STOR_LATE_UNREAD,
          "late: USBSTOR.INF unread, USBNTMAP.INF and its record stay");
    CHECK(inst_stor_late_action(1, 1) == INST_STOR_LATE_HELD,
          "late: this installer's own USBSTOR.INF held, the record stays");
    CHECK(inst_stor_late_action(1, 0) == INST_STOR_LATE_FOREIGN,
          "late: another's USBSTOR.INF, the record goes, the file stays");
    CHECK(inst_stor_pair(0, 0) == INST_STOR_PAIR_NONE &&
          inst_stor_pair(1, 1) == INST_STOR_PAIR_BOTH &&
          inst_stor_pair(1, 0) == INST_STOR_PAIR_STOR &&
          inst_stor_pair(0, 1) == INST_STOR_PAIR_NTMAP,
          "pair: none, both, USBSTOR.INF alone, USBNTMAP.INF alone");
    CHECK(inst_stor_pair(-1, 1) == -1 && inst_stor_pair(1, -1) == -1,
          "pair: a side unread is unread");
    CHECK(inst_stor_bulk_admit(1, 1, INST_STOR_PAIR_BOTH) &&
          inst_stor_bulk_admit(1, 1, INST_STOR_PAIR_NONE) &&
          inst_stor_bulk_admit(1, 1, INST_STOR_PAIR_NTMAP),
          "bulk: the package's USBNTMAP.INF, the INF directory no hazard");
    CHECK(!inst_stor_bulk_admit(1, 1, INST_STOR_PAIR_STOR),
          "bulk: USBSTOR.INF alone in the INF directory refuses");
    CHECK(!inst_stor_bulk_admit(1, 0, INST_STOR_PAIR_BOTH) &&
          !inst_stor_bulk_admit(1, -1, INST_STOR_PAIR_BOTH),
          "bulk: the package's USBNTMAP.INF missing or unread refuses");
    CHECK(!inst_stor_bulk_admit(0, 1, INST_STOR_PAIR_BOTH),
          "bulk: no package copy of the seven refuses");
    CHECK(!inst_stor_bulk_admit(1, 1, -1),
          "bulk: the INF directory unread refuses");
    CHECK(inst_stor_half(INST_STOR_PAIR_BOTH, 1) == INST_STOR_HALF_NONE &&
          inst_stor_half(INST_STOR_PAIR_NONE, 1) == INST_STOR_HALF_NONE &&
          inst_stor_half(INST_STOR_PAIR_NONE, 0) == INST_STOR_HALF_NONE,
          "half: a whole pair or neither, nothing to do");
    CHECK(inst_stor_half(INST_STOR_PAIR_STOR, 1) == INST_STOR_HALF_WITHDRAW &&
          inst_stor_half(INST_STOR_PAIR_NTMAP, 1) == INST_STOR_HALF_WITHDRAW,
          "half: a half with a placed copy recorded (any run) is withdrawn");
    CHECK(inst_stor_half(INST_STOR_PAIR_STOR, 0) == INST_STOR_HALF_STEP &&
          inst_stor_half(INST_STOR_PAIR_NTMAP, 0) == INST_STOR_HALF_STEP,
          "half: another's half is never deleted, a step");
    CHECK(inst_stor_half(INST_STOR_PAIR_STOR, -1) == INST_STOR_HALF_STEP &&
          inst_stor_half(-1, 1) == INST_STOR_HALF_STEP,
          "half: the record or the pair unread, a step");
    CHECK(inst_stor_scan(1, 1, 1) == 1 && inst_stor_scan(1, 0, 0) == 1,
          "scan: a copy found is found");
    CHECK(inst_stor_scan(0, 0, 0) == 0, "scan: none, every INF read");
    CHECK(inst_stor_scan(0, 1, 0) == -1 && inst_stor_scan(0, 0, 1) == -1,
          "scan: an unread INF or a listing cut short is unknown");
    CHECK(inst_stor_pair_warn(INST_STOR_PAIR_STOR) && inst_stor_pair_warn(-1),
          "pair left: USBSTOR.INF alone, or unread, is a step");
    CHECK(!inst_stor_pair_warn(INST_STOR_PAIR_BOTH) &&
          !inst_stor_pair_warn(INST_STOR_PAIR_NONE) &&
          !inst_stor_pair_warn(INST_STOR_PAIR_NTMAP),
          "pair left: both, neither or USBNTMAP.INF alone is not");
    /* The row's default with a lone USBSTOR.INF: NUSB installed wins
     * (unticked), then the lone half ticks it when a ticked run completes
     * the pair, over a recorded "no" and over found files. */
    CHECK(inst_stor_half_default(0, 0, INST_STOR_PAIR_STOR, 1) == 1 &&
          inst_stor_half_default(1, 0, INST_STOR_PAIR_STOR, 1) == 1,
          "lone USBSTOR.INF: the row ticked when the run completes the pair");
    CHECK(inst_stor_half_default(1, 1, INST_STOR_PAIR_STOR, 1) == 0 &&
          inst_stor_half_default(0, 1, INST_STOR_PAIR_STOR, 1) == 0 &&
          inst_stor_half_default(1, 1, INST_STOR_PAIR_NONE, 1) == 0,
          "lone USBSTOR.INF: NUSB installed wins, unticked");
    CHECK(inst_stor_half_default(0, 0, INST_STOR_PAIR_STOR, 0) == 0 &&
          inst_stor_half_default(1, 0, INST_STOR_PAIR_STOR, 0) == 1,
          "lone USBSTOR.INF: no route, the default as it was");
    CHECK(inst_stor_half_default(0, 0, INST_STOR_PAIR_BOTH, 1) == 0 &&
          inst_stor_half_default(1, 0, INST_STOR_PAIR_NONE, 1) == 1 &&
          inst_stor_half_default(0, 0, INST_STOR_PAIR_NTMAP, 1) == 0 &&
          inst_stor_half_default(0, 0, -1, 1) == 0 &&
          inst_stor_half_default(2, 0, INST_STOR_PAIR_NONE, 1) == 1,
          "no lone USBSTOR.INF: the default as it was");
    {
        const char *t1 = inst_stor_half_text(1);
        const char *t0 = inst_stor_half_text(0);
        CHECK(strstr(t1, "USBSTOR.INF is in the INF folder without "
                     "USBNTMAP.INF") == t1 && strstr(t1, "stop Windows") &&
              strstr(t1, "until these drivers complete the pair") != NULL,
              "lone USBSTOR.INF: the fixable sentence");
        CHECK(strstr(t0, "stop Windows") &&
              strstr(t0, "remove that copy of USBSTOR.INF") != NULL &&
              strstr(t0, "complete the pair") == NULL,
              "lone USBSTOR.INF: the sentence when the run cannot fix it");
        /* Task 44.36: short enough to show whole under the row. */
        CHECK(strlen(t1) <= 140 && strlen(t0) <= 140,
              "lone USBSTOR.INF: the sentences fit the row's note");
    }

    /* An update with Source recorded: the engine's later copies, and a
     * file of the download's bytes the first run found and kept, are this
     * installer's deployment (a partial-same install, then every file
     * present, then an update: the route, not complete). */
    stor_fill(st, INST_STOR_OWN);
    st[0] = INST_STOR_SAME;
    st[INST_STOR_IDX_USBSTOR_INF] = INST_STOR_SAME;
    st[INST_STOR_IDX_USBNTMAP_INF] = INST_STOR_SAME;
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_COMPLETE,
          "without Source: complete");
    inst_stor_norm(st, 1);
    CHECK(st[0] == INST_STOR_OWN && inst_stor_case(st, 0) == INST_STOR_CASE_OWN &&
          inst_stor_route(inst_stor_case(st, 1)) && inst_stor_default(0, 0, 0, st),
          "with Source: this installer's, the route");
    stor_fill(st, INST_STOR_SAME);
    st[1] = INST_STOR_OTHER;
    inst_stor_norm(st, 1);
    CHECK(st[1] == INST_STOR_OTHER &&
          inst_stor_case(st, 1) == INST_STOR_CASE_OTHER &&
          !inst_stor_route(INST_STOR_CASE_OTHER) &&
          inst_stor_copy_set(1, st, 1) == 0,
          "with Source, all seven present, one changed: partial-other, "
          "withdrawn, never complete");
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_COMPLETE,
          "the same files without Source: another installation, complete");
    stor_fill(st, INST_STOR_ABSENT);
    st[1] = INST_STOR_OTHER;
    st[0] = INST_STOR_SAME;
    inst_stor_norm(st, 1);
    CHECK(inst_stor_case(st, 1) == INST_STOR_CASE_OTHER &&
          !inst_stor_route(inst_stor_case(st, 1)),
          "with Source, another file present: partial-other, withdrawn");
    stor_fill(st, INST_STOR_SAME);
    inst_stor_norm(st, 0);
    CHECK(inst_stor_case(st, 0) == INST_STOR_CASE_COMPLETE,
          "another installation, no Source: complete");
    /* A stick first plugged after the install, bound by Windows from the
     * installer's copy on DevicePath (owner, 2026-10-20): adopted when its
     * InfPath names one copy of the pack's bytes, then removed with its
     * files; one bound from a hand install of other bytes is not. */
    CHECK(inst_stor_adoptable(1, 1, 0, 0, 1, 0),
          "a later stick, a new copy of the pack's INF bytes: adopted");
    CHECK(inst_stor_adoptable(1, 1, 0, 0, 1, 1) &&
          inst_stor_adoptable(1, 0, 0, 0, 1, 1),
          "a later stick through a recorded copy: adopted");
    /* A partial install (one identical binary there before), then the
     * first stick, then the uninstall: the stick's copy is new, so it is
     * adopted though the install found a file of the seven. */
    CHECK(inst_stor_adoptable(1, 1, 0, 0, 1, 0),
          "partial install, first stick, uninstall: adopted");
    CHECK(!inst_stor_adoptable(1, 1, 1, 0, 1, 0),
          "a copy of the pack's bytes there before the deployment: not "
          "adopted");
    CHECK(!inst_stor_adoptable(1, 0, 0, 0, 1, 0),
          "the before-list not whole, the copy not recorded: not adopted");
    CHECK(!inst_stor_adoptable(1, 1, 0, 0, 0, 0),
          "a copy of other bytes (a hand install): not adopted");
    CHECK(!inst_stor_adoptable(1, 1, 0, 0, -1, 0),
          "an InfPath that names more than one copy, or unreadable: not "
          "adopted");
    CHECK(!inst_stor_adoptable(1, 1, 0, 1, 1, 1), "USB20U present: not adopted");
    /* The INFs placed in %windir%\INF (task 43.42, record 26 section
     * 6.5.11): only on the route with every binary in place, only where
     * no file of the name is there, never over one. */
    {
        int named[2];
        named[0] = 0;
        named[1] = 0;
        CHECK(inst_stor_inf_set(1, named) ==
              ((1UL << INST_STOR_IDX_USBSTOR_INF) |
               (1UL << INST_STOR_IDX_USBNTMAP_INF)),
              "placed: both INFs, neither name there");
        CHECK(inst_stor_inf_set(0, named) == 0,
              "placed: nothing off the route or with a binary not in place");
        named[0] = 1;
        CHECK(inst_stor_inf_set(1, named) ==
              (1UL << INST_STOR_IDX_USBNTMAP_INF),
              "placed: never over a USBSTOR.INF already there");
        named[0] = 0;
        named[1] = -1;
        CHECK(inst_stor_inf_set(1, named) ==
              (1UL << INST_STOR_IDX_USBSTOR_INF),
              "placed: a name that cannot be read is kept out");
        named[0] = 1;
        named[1] = 1;
        CHECK(inst_stor_inf_set(1, named) == 0,
              "placed: both there (NUSB's own, or an earlier run's): none");
        named[0] = 0;
        named[1] = 0;
        CHECK((inst_stor_inf_set(1, named) & 0x1FUL) == 0,
              "placed: never a binary");
    }
    /* A stick bound with no wizard from the placed USBSTOR.INF: its copy
     * is held by Placed, so recorded, and adopted. */
    CHECK(inst_stor_adoptable(1, 1, 0, 0, 1, 1),
          "a stick bound from the placed INF: adopted");
    /* The SilentClasses entries with the storage row (task 43.42 option A,
     * record 26 section 6.5.12). */
    {
        unsigned char have[4][4];
        int valid[4];
        char nm[32];
        CHECK(instStorSilent[2][0] == 0xC3 && instStorSilent[2][1] == 0x08 &&
              instStorSilent[2][2] == 0x06 && instStorSilent[2][3] == 0x50 &&
              instStorSilent[0][2] == 0x02 && instStorSilent[1][2] == 0x05,
              "silent: the three Bulk-Only entries, silent.reg's among them");
        CHECK(inst_stor_silent_add((const unsigned char (*)[4])have, valid,
                                   0) == 0x7UL,
              "silent: an empty list takes all three");
        memcpy(have[0], instStorSilent[2], 4);
        valid[0] = 1;
        memcpy(have[1], instStorSilent[0], 4);
        valid[1] = 0;
        have[2][0] = 0x83;
        have[2][1] = 0x03;
        have[2][2] = 0x01;
        have[2][3] = 0x01;
        valid[2] = 1;
        CHECK(inst_stor_silent_add((const unsigned char (*)[4])have, valid,
                                   3) == 0x3UL,
              "silent: an entry already there is not added again; a value of "
              "another type or length does not count");
        memcpy(have[1], instStorSilent[0], 4);
        valid[1] = 1;
        memcpy(have[3], instStorSilent[1], 4);
        valid[3] = 1;
        CHECK(inst_stor_silent_add((const unsigned char (*)[4])have, valid,
                                   4) == 0,
              "silent: all three there (silent-on.bat and more): none added");
        CHECK(inst_stor_silent_name(0, NULL, NULL, nm, sizeof(nm)) &&
              strcmp(nm, "XHCI98S1") == 0, "silent: the first name");
        CHECK(inst_stor_silent_name(1, silent_taken_s2, NULL, nm, sizeof(nm)) &&
              strcmp(nm, "XHCI98S5") == 0,
              "silent: a name in use is never written over");
        CHECK(!inst_stor_silent_name(0, silent_taken_err, NULL, nm,
                                     sizeof(nm)),
              "silent: a name that cannot be read stops the merge");
    }
    /* A run whose package copy failed after its storage copies: nothing
     * published, so a hand install of the same INFs later is never
     * taken by the uninstall. */
    CHECK(!inst_stor_adoptable(0, 1, 0, 0, 1, 0) &&
          !inst_stor_adoptable(0, 1, 0, 0, 1, 1),
          "failed staging, a hand install after it, the uninstall: not "
          "adopted");
    /* The usual flow after the adoption: the device recorded, no user
     * left, the hash unchanged, no USB20U - every file deleted. */
    CHECK(inst_stor_remove(1, 1, 0, 0) == INST_STOR_RM_DELETE,
          "the usual flow: install, first stick, uninstall: deleted");
    CHECK(inst_stor_remove(1, 1, 0, 0) == INST_STOR_RM_DELETE,
          "removed: recorded, its hash, no user, no USB20U");
    CHECK(inst_stor_remove(1, 0, 0, 0) == INST_STOR_RM_CHANGED,
          "kept: its hash changed");
    CHECK(inst_stor_remove(1, 1, 1, 0) == INST_STOR_RM_NUSB,
          "kept: USB20U present");
    CHECK(inst_stor_remove(1, 1, 0, 1) == INST_STOR_RM_USED,
          "kept: a devnode uses it");
    CHECK(inst_stor_remove(0, 1, 1, 1) == INST_STOR_RM_GONE,
          "already gone");
    for (i = INST_STOR_RM_DELETE; i <= INST_STOR_RM_GONE; i++) {
        CHECK(inst_stor_remove_text(i)[0] != '\0', "each reason said");
    }
}

/* S98VSD.VXD (design record 30 section 7): the box, the switches, the
 * mode, and the recovery and placement table step by step. */
static INST_VSDFACTS vf(int pend, int fileVal, int dest, int tmp, int entry,
                        int mode, int pkgPend, int pkgFile, int filePend)
{
    INST_VSDFACTS f;
    f.Pending = pend;
    f.FileVal = fileVal;
    f.Dest = dest;
    f.Tmp = tmp;
    f.Entry = entry;
    f.Mode = mode;
    f.PkgIsPending = pkgPend;
    f.PkgIsFile = pkgFile;
    f.FileIsPending = filePend;
    return f;
}

/* Runs the table against a model of the disk, the registry and
 * WININIT.INI as the engine would, from a state, until it stops; returns
 * the final op and the model's state. Hashes: 1 the package's (H), 2 the
 * installed file's (an older release), 9 foreign. */
typedef struct VMODEL {
    int Pending, File;      /* 0 unset, else the hash             */
    int Dest, Tmp;          /* 0 absent, else the hash            */
    int Entry;              /* INST_VE_*                          */
    int Cut;                /* the op number at which to stop     */
    int Steps;
} VMODEL;

static int vclass(int bytes, const VMODEL *m)
{
    if (bytes == 0) {
        return INST_VF_ABSENT;
    }
    if (m->Pending != 0 && bytes == m->Pending) {
        return INST_VF_PENDING;
    }
    if (m->File != 0 && bytes == m->File) {
        return INST_VF_FILE;
    }
    return INST_VF_OTHER;
}

static int vrun(VMODEL *m, int mode, int pkg)
{
    int i;
    for (i = 0; i < 20; i++) {
        INST_VSDFACTS f;
        int op;
        f.Pending = m->Pending != 0;
        f.FileVal = m->File != 0;
        f.Dest = vclass(m->Dest, m);
        f.Tmp = vclass(m->Tmp, m);
        f.Entry = m->Entry;
        f.Mode = mode;
        f.PkgIsPending = m->Pending != 0 && m->Pending == pkg;
        f.PkgIsFile = m->File != 0 && m->File == pkg;
        f.FileIsPending = m->Pending != 0 && m->File == m->Pending;
        op = inst_vsd_next(&f);
        if (op < INST_VOP_STEPS) {
            return op;
        }
        if (m->Cut != 0 && m->Steps + 1 == m->Cut) {
            return -1;      /* the run is cut short before this step */
        }
        m->Steps++;
        switch (op) {
        case INST_VOP_SET_FILE: m->File = m->Pending; break;
        case INST_VOP_DEL_ENTRY: m->Entry = INST_VE_ABSENT; break;
        case INST_VOP_DEL_TMP: m->Tmp = 0; break;
        case INST_VOP_DEL_PENDING: m->Pending = 0; break;
        case INST_VOP_DEL_FILEVAL: m->File = 0; break;
        case INST_VOP_DEL_DEST: m->Dest = 0; break;
        case INST_VOP_START: m->Pending = pkg; m->Tmp = pkg; break;
        case INST_VOP_MOVE: m->Dest = m->Tmp; m->Tmp = 0; break;
        case INST_VOP_WRITE_ENTRY: m->Entry = INST_VE_EXACT; break;
        default: return -2;
        }
    }
    return -3;
}

/* The start's rename, as Windows makes it. */
static void vboot(VMODEL *m)
{
    if (m->Entry == INST_VE_EXACT && m->Tmp != 0) {
        m->Dest = m->Tmp;
        m->Tmp = 0;
        m->Entry = INST_VE_ABSENT;
    }
}

/* The layer's file name against Stor98's in IOSUBSYS (record 30 section
 * 3.1: Windows loads an IOSUBSYS driver by its base name; STOR98.VXD
 * beside stor98.mpd stopped Stor98's start), and the old name's file
 * recognised as this project's. */
/* A minimal LE image: MZ at 0, LE at 0x40, one object of one page, the
 * entry table's first bundle naming the DDB at offset ddbOff of that
 * object, the page at file offset 0x200 (page size 0x100). */
static unsigned char leImg[0x400];

static void le_put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static size_t le_make(const char *ddbName, unsigned long ddbOff,
                      const char *text)
{
    unsigned char *le = leImg + 0x40;
    unsigned char *ot = le + 0x100;
    unsigned char *pm = le + 0x120;
    unsigned char *et = le + 0x130;

    memset(leImg, 0, sizeof(leImg));
    leImg[0] = 'M';
    leImg[1] = 'Z';
    le_put32(leImg + 0x3C, 0x40);
    le[0] = 'L';
    le[1] = 'E';
    le_put32(le + 0x14, 1);          /* pages */
    le_put32(le + 0x28, 0x100);      /* page size */
    le_put32(le + 0x2C, 0x100);      /* bytes on the last page */
    le_put32(le + 0x40, 0x100);      /* object table */
    le_put32(le + 0x44, 1);          /* objects */
    le_put32(le + 0x48, 0x120);      /* object page table */
    le_put32(le + 0x5C, 0x130);      /* entry table */
    le_put32(le + 0x80, 0x200);      /* data pages (file offset) */
    le_put32(ot, 0x100);             /* virtual size */
    le_put32(ot + 12, 1);            /* first page */
    le_put32(ot + 16, 1);            /* page count */
    pm[2] = 1;                       /* page 1 */
    et[0] = 1;
    et[1] = 3;
    et[2] = 1;                       /* object 1 */
    le_put32(et + 5, ddbOff);
    if (ddbOff + 0x14 <= 0x100) {
        memcpy(leImg + 0x200 + ddbOff + 0x0C, ddbName, 8);
    }
    memcpy(leImg + 0x300, text, strlen(text));
    return sizeof(leImg);
}

static int count_rename(void *ctx, const char *key, const char *value)
{
    int *n = (int *)ctx;

    (void)key;
    (void)value;
    (*n)++;
    return 1;
}

/* WININIT.INI as 98 SE leaves it after this installer's write, and the
 * variants a run meets (task 43.80). */
/* Task 44.11 (record 26 section 6.7.12): the two free packages staged on
 * 98 SE and ME, and the 280-character projection of DevicePath. */
/* 98 SE's DevicePath limit, for the projection's vectors. */
#define DP98 INST_9X_DEVICEPATH_MAX

static void test_9x_stage(void)
{
    static const char base[] = "C:\\WINDOWS\\INF;C:\\XHCI98";
    static const char *const two[] = {
        "C:\\XHCI98\\USBDRV\\FREE\\audio\\uac2\\w98se-me",
        "C:\\XHCI98\\USBDRV\\FREE\\audio\\usbmidi\\w98se"
    };
    const char *f[3];
    char b[400];
    char fold[300];
    int fits[3];
    int r;

    /* Record 26 section 6.7.13 (task 45.7): every package of either set,
     * the projection holding DevicePath to the limit. */
    CHECK(inst_other_9x_stageable(INST_SET_FREE, "audio/uac2") &&
          inst_other_9x_stageable(INST_SET_FREE, "AUDIO/USBMIDI") &&
          inst_other_9x_stageable(INST_SET_FREE, "video/uvc98") &&
          inst_other_9x_stageable(INST_SET_NONFREE, "serial/ftdi"),
          "9x stage: every package of either set");
    CHECK(!inst_other_9x_stageable(INST_SET_FREE, NULL) &&
          !inst_other_9x_stageable(INST_SET_NONFREE, "") &&
          !inst_other_9x_stageable(INST_SET_COUNT, "serial/ftdi") &&
          !inst_other_9x_stageable(-1, "serial/ftdi"),
          "9x stage: no id, or no such set");
    CHECK(inst_other_9x_keeps(1, 1) && !inst_other_9x_keeps(1, 0) &&
          inst_other_9x_keeps(0, 0) && inst_other_9x_keeps(0, 1),
          "9x keeps: carried with a STAGE line, or not carried");
    {
        static const char *const five[] = {
            "C:\\XHCI98\\USBDRV\\NONFREE\\serial\\ftdi\\w98se-me\\aaaaaa",
            "C:\\XHCI98\\USBDRV\\NONFREE\\serial\\ftdi\\w98se-me\\bbbbbb",
            "C:\\XHCI98\\USBDRV\\NONFREE\\serial\\ftdi\\w98se-me\\cccccc",
            "C:\\XHCI98\\USBDRV\\NONFREE\\serial\\ftdi\\w98se-me\\dddddd",
            "C:\\XHCI98\\USBDRV\\NONFREE\\serial\\ftdi\\w98se-me\\eeeeee",
            "C:\\XHCI98\\USBDRV\\NONFREE\\serial\\ftdi\\w98se-me\\ffffff"
        };
        int six[6];
        CHECK(strlen(five[0]) == 52, "9x project: the stand-in folder");
        r = inst_9x_devicepath_project(base, five, 6, six, DP98);
        CHECK(six[0] && six[1] && six[2] && six[3] && !six[4] && !six[5] &&
              r == (int)strlen(base) + 4 * 53,
              "9x project: four 52-character folders fit, the fifth not");
    }
    {
        char line[400];
        char rest[600];
        const char *fp[3];
        fp[0] = "USBDRV\\FREE\\net\\rtl815x\\w98se-me";
        fp[1] = "";
        fp[2] = "USBDRV\\FREE\\net\\rtl815x\\w2k";
        inst_other_copy_only_line(line, sizeof(line), "USB Ethernet", fp, 1);
        CHECK(strcmp(line, "USB Ethernet: USBDRV\\FREE\\net\\rtl815x\\"
                     "w98se-me") == 0, "9x copied only: one folder");
        inst_other_copy_only_line(line, sizeof(line), "USB Ethernet", fp, 3);
        CHECK(strcmp(line, "USB Ethernet: USBDRV\\FREE\\net\\rtl815x\\"
                     "w98se-me; USBDRV\\FREE\\net\\rtl815x\\w2k") == 0,
              "9x copied only: two folders, an empty one skipped");
        inst_other_rest_text(rest, sizeof(rest), 0, INST_SET_NONFREE,
                             "C:\\XHCI98", 1, 0, 0);
        CHECK(strcmp(rest, "Ticked: staged, so Windows finds the driver "
                     "itself.") == 0, "9x rest: what a tick does");
        inst_other_rest_text(rest, sizeof(rest), 1, INST_SET_NONFREE,
                             "C:\\XHCI98", 1, 0, 0);
        CHECK(strstr(rest, "Not ticked: copied only") == rest &&
              strstr(rest, "under C:\\XHCI98\\USBDRV\\NONFREE.") != NULL,
              "9x rest: what an unticked package is, the set's folder named");
        inst_other_rest_text(rest, sizeof(rest), 0, INST_SET_FREE, "C:\\X", 1,
                             0, 1);
        CHECK(strstr(rest, "itself. Earlier staged drivers are listed "
                     "above.") != NULL, "9x rest: copied, earlier staging kept");
        inst_other_rest_text(rest, sizeof(rest), 0, INST_SET_FREE, "C:\\X", 1,
                             1, 1);
        CHECK(strstr(rest, "replaces the earlier copy and its staging.") !=
              NULL &&
              strstr(rest, "listed above") == NULL,
              "9x rest: copied over another release, earlier staging out");
        inst_other_rest_text(rest, sizeof(rest), 0, INST_SET_FREE, "", 0, 0, 0);
        CHECK(strstr(rest, "Not copied") == rest &&
              strstr(rest, "arlier") == NULL,
              "9x rest: not copied, no earlier staging");
        inst_other_rest_text(rest, sizeof(rest), 1, INST_SET_FREE, "", 0, 0, 1);
        CHECK(rest[0] == '\0', "9x rest: not copied, no second part");
        inst_other_rest_text(rest, sizeof(rest), 0, INST_SET_FREE, "", 0, 0, 1);
        CHECK(strstr(rest, "Earlier staged drivers are listed above.") != NULL,
              "9x rest: not copied, with earlier staging");
        inst_other_rest_text(rest, 20, 1, INST_SET_FREE, "C:\\XHCI98", 1, 0,
                             1);
        CHECK(strlen(rest) == 19, "9x rest: held to its buffer");
    }
    /* Record 26 section 6.7.14 (task 45.6): a lifted pair bars only an
     * earlier staging of another release; EXCLUDES bars always, COEXISTS
     * never. */
    CHECK(inst_other_rel_bars(INST_REL_EXCLUDES, 0, 1) &&
          inst_other_rel_bars(INST_REL_EXCLUDES, 1, 1) &&
          !inst_other_rel_bars(INST_REL_COEXISTS, 1, 0) &&
          !inst_other_rel_bars(INST_REL_LIFTED, 0, 0) &&
          !inst_other_rel_bars(INST_REL_LIFTED, 1, 1) &&
          inst_other_rel_bars(INST_REL_LIFTED, 1, 0),
          "lifted: bars an earlier staging of another release alone");
    /* usb-wdm-free 0.1.5: lifted on XP to 7 x64, EXCLUDES on the rest. */
    {
        static const unsigned long v[13][5] = {
            { 0, 4, 10, 2222, 0 }, { 0, 4, 90, 3000, 0 },
            { 1, 5, 0, 2195, 0 }, { 1, 5, 1, 2600, 0 }, { 1, 5, 2, 3790, 1 },
            { 1, 6, 0, 6002, 0 }, { 1, 6, 0, 6002, 1 }, { 1, 6, 1, 7601, 0 },
            { 1, 6, 1, 7601, 1 }, { 1, 6, 2, 9200, 0 }, { 1, 6, 2, 9200, 1 },
            { 1, 10, 0, 19045, 0 }, { 1, 10, 0, 19045, 1 }
        };
        int k;
        int ok = 1;
        for (k = 0; k < 13; k++) {
            int tok = inst_sys_token((int)v[k][0], v[k][1], v[k][2],
                                     v[k][3], (int)v[k][4]);
            int lift = tok >= 3 && tok <= 8;
            if (tok != k ||
                inst_other_excludes(INST_SET_FREE, "net/rtl815x",
                                    INST_SET_NONFREE, "ethernet/rtl815x",
                                    tok) == lift ||
                inst_other_lifted(INST_SET_NONFREE, "ethernet/rtl815x",
                                  INST_SET_FREE, "net/rtl815x", tok) != lift) {
                ok = 0;
            }
        }
        CHECK(ok, "lifted: rtl815x lifted on XP to 7 x64, excluded on the "
                  "rest");
    }

    r = inst_9x_devicepath_project(base, two, 2, fits, DP98);
    CHECK(r == (int)(strlen(base) + 2 + strlen(two[0]) + strlen(two[1])) &&
          fits[0] && fits[1], "9x project: the default folders, 84 more");
    CHECK(r - (int)strlen(base) == 84, "9x project: about 90 characters");

    /* A value of 269, 270 and 271 characters with an entry of 10 (11 with
     * its separator): 280 fits, 281 does not, 279 fits. */
    f[0] = "C:\\X\\12345";
    memset(b, 'A', sizeof(b));
    memcpy(b, "C:\\", 3);
    b[269] = '\0';
    r = inst_9x_devicepath_project(b, f, 1, fits, DP98);
    CHECK(r == INST_9X_DEVICEPATH_MAX && fits[0], "9x project: 280 fits");
    b[269] = 'A';
    b[270] = '\0';
    r = inst_9x_devicepath_project(b, f, 1, fits, DP98);
    CHECK(r == 270 && !fits[0], "9x project: 281 does not");
    b[268] = '\0';
    r = inst_9x_devicepath_project(b, f, 1, fits, DP98);
    CHECK(r == INST_9X_DEVICEPATH_MAX - 1 && fits[0], "9x project: 279 fits");

    /* The install directory's own entry, by the same limit: a value of
     * 280 characters with it keeps it, 281 leaves it off. */
    memset(b, 'A', sizeof(b));
    memcpy(b, "C:\\", 3);
    b[INST_9X_DEVICEPATH_MAX] = '\0';
    CHECK(inst_9x_devicepath_room(b, DP98), "9x own entry: 280 kept");
    b[INST_9X_DEVICEPATH_MAX] = 'A';
    b[INST_9X_DEVICEPATH_MAX + 1] = '\0';
    CHECK(!inst_9x_devicepath_room(b, DP98), "9x own entry: 281 left off");
    CHECK(inst_9x_devicepath_room(base, DP98), "9x own entry: an ordinary value kept");
    CHECK(!inst_9x_devicepath_room(NULL, DP98), "9x own entry: nothing composed");

    /* ME's limit (record 26 section 6.7.13, task 45.7's ME leg): 259
     * fits, 260 does not; 98 SE's stays 280. */
    CHECK(inst_9x_devicepath_max(0) == 280 && inst_9x_devicepath_max(1) == 259,
          "9x limit: 280 on 98 SE, 259 on ME");
    CHECK(strstr(inst_9x_devicepath_harm(1), "ME") != NULL &&
          strstr(inst_9x_devicepath_harm(0), "98 SE") != NULL,
          "9x limit: the words name the system");
    memset(b, 'A', sizeof(b));
    memcpy(b, "C:\\", 3);
    b[248] = '\0';
    r = inst_9x_devicepath_project(b, f, 1, fits, inst_9x_devicepath_max(1));
    CHECK(r == 259 && fits[0], "ME project: 259 fits");
    b[248] = 'A';
    b[249] = '\0';
    r = inst_9x_devicepath_project(b, f, 1, fits, inst_9x_devicepath_max(1));
    CHECK(r == 249 && !fits[0], "ME project: 260 does not");
    r = inst_9x_devicepath_project(b, f, 1, fits, DP98);
    CHECK(r == 260 && fits[0], "98 SE project: 260 fits");
    b[249] = 'A';
    b[259] = '\0';
    CHECK(inst_9x_devicepath_room(b, inst_9x_devicepath_max(1)),
          "ME own entry: 259 kept");
    b[259] = 'A';
    b[260] = '\0';
    CHECK(!inst_9x_devicepath_room(b, inst_9x_devicepath_max(1)),
          "ME own entry: 260 left off");
    memset(b, 'A', sizeof(b));
    memcpy(b, "C:\\", 3);
    b[269] = '\0';
    r = inst_9x_devicepath_project(b, f, 1, fits, 0);
    CHECK(r == DP98 && fits[0], "9x project: no limit given reads as 280");

    /* An oversized value as read is the caller's to shrink first; once
     * this run's withdrawals bring it under the limit, the folders count. */
    b[INST_9X_DEVICEPATH_MAX + 1] = '\0';
    memset(b, 'A', INST_9X_DEVICEPATH_MAX + 1);
    r = inst_9x_devicepath_project(b, two, 2, fits, DP98);
    CHECK(r == -1 && !fits[0] && !fits[1], "9x project: over the limit as read");
    r = inst_9x_devicepath_project(base, two, 2, fits, DP98);
    CHECK(r > 0 && fits[0] && fits[1], "9x project: after the withdrawals");

    /* Two folders of which only the first fits; a folder already there
     * counted once. */
    memset(b, 'A', sizeof(b));
    memcpy(b, "C:\\", 3);
    b[INST_9X_DEVICEPATH_MAX - 1 - (int)strlen(two[0]) - 10] = '\0';
    r = inst_9x_devicepath_project(b, two, 2, fits, DP98);
    CHECK(fits[0] && !fits[1] && r <= INST_9X_DEVICEPATH_MAX,
          "9x project: the first fits, the second not");
    _snprintf(fold, sizeof(fold) - 1, "%s;%s", base, two[0]);
    fold[sizeof(fold) - 1] = '\0';
    f[0] = two[0];
    f[1] = two[0];
    f[2] = two[1];
    r = inst_9x_devicepath_project(fold, f, 3, fits, DP98);
    CHECK(fits[0] && fits[1] && fits[2] &&
          r == (int)(strlen(fold) + 1 + strlen(two[1])),
          "9x project: an entry already there counted once");
    f[0] = "";
    CHECK(inst_9x_devicepath_project(base, f, 1, fits, DP98) == -1 && !fits[0],
          "9x project: an empty folder is no entry");
}

/* Task 44.36: the Components page's one line for the storage row, each
 * state's within 62 characters, the most pressing first. */
static void test_stor_short(void)
{
    int st[INST_STOR_COUNT];
    char t[200];
    int i;
    int pack;
    int nusb;
    int half;
    int fix;
    int kind;

    for (kind = 0; kind < 5; kind++) {
        for (i = 0; i < INST_STOR_COUNT; i++) {
            st[i] = kind == 0 ? INST_STOR_ABSENT : kind == 1 ? INST_STOR_SAME :
                    kind == 2 ? (i < 3 ? INST_STOR_OTHER : INST_STOR_ABSENT) :
                    kind == 3 ? INST_STOR_OWN :
                    (i < 3 ? INST_STOR_OWN : INST_STOR_ABSENT);
        }
        for (pack = -1; pack <= 1; pack++) {
            for (nusb = 0; nusb <= 1; nusb++) {
                for (half = 0; half <= 1; half++) {
                    for (fix = 0; fix <= 1; fix++) {
                        inst_stor_short_text(t, sizeof(t), pack, st, nusb,
                                             half, fix);
                        CHECK(t[0] != '\0' && strlen(t) <= 62,
                              "storage short: one line within 62");
                    }
                }
            }
        }
    }
    for (i = 0; i < INST_STOR_COUNT; i++) {
        st[i] = INST_STOR_ABSENT;
    }
    inst_stor_short_text(t, sizeof(t), 1, st, 0, 0, 0);
    CHECK(strstr(t, "Not installed") == t, "storage short: none found");
    inst_stor_short_text(t, sizeof(t), 1, st, 1, 0, 0);
    CHECK(strstr(t, "NUSB is installed") == t &&
          strstr(t, "leave this unticked") != NULL,
          "storage short: NUSB over what was found");
    inst_stor_short_text(t, sizeof(t), 0, st, 1, 0, 0);
    CHECK(strstr(t, "Not in this download") == t,
          "storage short: the download's pack first");
    inst_stor_short_text(t, sizeof(t), -1, st, 1, 0, 0);
    CHECK(strstr(t, "do not match this release") != NULL,
          "storage short: the pack not this release's");
    inst_stor_short_text(t, sizeof(t), 0, st, 1, 1, 0);
    CHECK(strstr(t, "without USBNTMAP.INF") != NULL &&
          strstr(t, "Summary") != NULL, "storage short: the lone half first");
    inst_stor_short_text(t, sizeof(t), 1, st, 0, 1, 1);
    CHECK(strstr(t, "tick to fix") != NULL, "storage short: the half fixable");
    for (i = 0; i < INST_STOR_COUNT; i++) {
        st[i] = INST_STOR_SAME;
    }
    inst_stor_short_text(t, sizeof(t), 1, st, 0, 0, 0);
    CHECK(strcmp(t, "Already installed. Left as-is.") == 0,
          "storage short: all found");
    st[0] = INST_STOR_ABSENT;
    inst_stor_short_text(t, sizeof(t), 1, st, 0, 0, 0);
    CHECK(strstr(t, "Partly installed") == t, "storage short: some found");
    for (i = 0; i < INST_STOR_COUNT; i++) {
        st[i] = INST_STOR_OWN;
    }
    inst_stor_short_text(t, sizeof(t), 1, st, 0, 0, 0);
    CHECK(strcmp(t, "Installed earlier by this installer.") == 0,
          "storage short: this installer's");
}

/* Task 44.13: an xhci98.tmp no [rename] line names is an orphan. */
static void test_tmp_named(void)
{
    static const char T[] = "C:\\WINDOWS\\System32\\Drivers\\xhci98.tmp";
    static const char TS[] = "C:\\WINDOWS\\SYSTEM32\\DRIVERS\\XHCI98.TMP";
    char big[INST_RENAME_LINE + 64];

    CHECK(inst_tmp_named("[rename]\r\nC:\\WINDOWS\\System32\\Drivers\\"
                         "xhci98.sys=C:\\WINDOWS\\System32\\Drivers\\"
                         "xhci98.tmp\r\n", T, TS) == 1,
          "tmp: the setup engine's line, as read on 98 SE: named");
    CHECK(inst_tmp_named("[Rename]\nc:\\windows\\system32\\drivers\\"
                         "xhci98.sys = c:\\windows\\system32\\drivers\\"
                         "xhci98.tmp", T, TS) == 1,
          "tmp: any case, spaces round '=': named");
    CHECK(inst_tmp_named("[rename]\r\nNUL=C:\\WINDOWS\\SYSTEM32\\DRIVERS\\"
                         "XHCI98.TMP\r\n", T, TS) == 1,
          "tmp: a deletion of it by its 8.3 path: named");
    CHECK(inst_tmp_named(NULL, T, TS) == 0, "tmp: no WININIT.INI: an orphan");
    CHECK(inst_tmp_named("", T, TS) == 0 &&
          inst_tmp_named("[rename]\r\n", T, TS) == 0,
          "tmp: an empty section: an orphan");
    CHECK(inst_tmp_named("[rename]\r\nC:\\WINDOWS\\System32\\Drivers\\"
                         "xhci98.tmp=C:\\WINDOWS\\X.TMP\r\n", T, TS) == 0,
          "tmp: named as a target only: an orphan");
    CHECK(inst_tmp_named("[other]\r\nC:\\WINDOWS\\System32\\Drivers\\"
                         "xhci98.sys=C:\\WINDOWS\\System32\\Drivers\\"
                         "xhci98.tmp\r\n", T, TS) == 0,
          "tmp: a line of another section: an orphan");
    CHECK(inst_tmp_named("[rename]\r\n;C:\\WINDOWS\\System32\\Drivers\\"
                         "xhci98.sys=C:\\WINDOWS\\System32\\Drivers\\"
                         "xhci98.tmp\r\n", T, TS) == 0,
          "tmp: a comment: an orphan");
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    memcpy(big, "[rename]\r\nX=", 12);
    CHECK(inst_tmp_named(big, T, TS) == -1,
          "tmp: a line too long to read: nothing decided");
}

static void test_wininit_text(void)
{
    static const char D[] = "C:\\WINDOWS\\SYSTEM\\IOSUBSYS\\S98VSD.VXD";
    static const char T[] = "C:\\WINDOWS\\SYSTEM\\IOSUBSYS\\S98VSD.TMP";
    static const char DS[] = "C:\\WINDOWS\\SYSTEM\\IOSUBSYS\\S98VSD.VXD";
    static const char TS[] = "C:\\WINDOWS\\SYSTEM\\IOSUBSYS\\S98VSD.TMP";
    static const char DL[] = "C:\\Program Files\\W98\\SYSTEM\\IOSUBSYS\\S98VSD.VXD";
    static const char TL[] = "C:\\Program Files\\W98\\SYSTEM\\IOSUBSYS\\S98VSD.TMP";
    static const char DL8[] = "C:\\PROGRA~1\\W98\\SYSTEM\\IOSUBSYS\\S98VSD.VXD";
    static const char TL8[] = "C:\\PROGRA~1\\W98\\SYSTEM\\IOSUBSYS\\S98VSD.TMP";
    char big[INST_RENAME_LINE + 32];
    int n = 0;

    CHECK(inst_vsd_entry_text("[rename]\r\n"
                              "C:\\WINDOWS\\SYSTEM\\IOSUBSYS\\S98VSD.VXD=C:\\"
                              "WINDOWS\\SYSTEM\\IOSUBSYS\\S98VSD.TMP\r\n",
                              D, DS, T, TS) == INST_VE_EXACT,
          "the one line WritePrivateProfileString leaves: exact");
    CHECK(inst_vsd_entry_text("[Rename]\r\nc:\\windows\\system\\iosubsys\\"
                              "s98vsd.vxd = c:\\windows\\system\\iosubsys\\"
                              "s98vsd.tmp", D, DS, T, TS) == INST_VE_EXACT,
          "any case, spaces round '=', no final newline: exact");
    CHECK(inst_vsd_entry_text("[rename]\nNUL=C:\\WINDOWS\\X.TMP\n"
                              "C:\\WINDOWS\\SYSTEM\\IOSUBSYS\\S98VSD.VXD=C:\\"
                              "WINDOWS\\SYSTEM\\IOSUBSYS\\S98VSD.TMP\n"
                              "C:\\WINDOWS\\SYSTEM\\STOR98.MPD=C:\\WINDOWS\\"
                              "SYSTEM\\STOR98.TMP\n", D, DS, T, TS) ==
          INST_VE_EXACT, "other files' lines beside it: exact");
    CHECK(inst_vsd_entry_text("", D, DS, T, TS) == INST_VE_ABSENT &&
          inst_vsd_entry_text("[rename]\r\n", D, DS, T, TS) ==
              INST_VE_ABSENT &&
          inst_vsd_entry_text("[other]\r\nC:\\WINDOWS\\SYSTEM\\IOSUBSYS\\"
                              "S98VSD.VXD=C:\\WINDOWS\\SYSTEM\\IOSUBSYS\\"
                              "S98VSD.TMP\r\n[rename]\r\n", D, DS, T, TS) ==
              INST_VE_ABSENT &&
          inst_vsd_entry_text("[rename]\r\n;C:\\WINDOWS\\SYSTEM\\IOSUBSYS\\"
                              "S98VSD.VXD=C:\\WINDOWS\\SYSTEM\\IOSUBSYS\\"
                              "S98VSD.TMP\r\n", D, DS, T, TS) ==
              INST_VE_ABSENT,
          "no line, another section's, a comment: absent");
    CHECK(inst_vsd_entry_text("[rename]\r\nNUL=C:\\WINDOWS\\SYSTEM\\"
                              "IOSUBSYS\\S98VSD.TMP\r\n", D, DS, T, TS) ==
              INST_VE_CONFLICT &&
          inst_vsd_entry_text("[rename]\r\nC:\\WINDOWS\\SYSTEM\\IOSUBSYS\\"
                              "S98VSD.VXD=C:\\WINDOWS\\OTHER.VXD\r\n", D, DS, T,
                              TS) == INST_VE_CONFLICT &&
          inst_vsd_entry_text("[rename]\r\nC:\\WINDOWS\\SYSTEM\\IOSUBSYS\\"
                              "S98VSD.VXD=C:\\WINDOWS\\SYSTEM\\IOSUBSYS\\"
                              "S98VSD.TMP\r\nNUL=C:\\WINDOWS\\SYSTEM\\"
                              "IOSUBSYS\\S98VSD.VXD\r\n", D, DS, T, TS) ==
              INST_VE_CONFLICT,
          "another line naming either file: conflict");
    CHECK(inst_vsd_entry_text("[rename]\r\nC:\\PROGRA~1\\W98\\SYSTEM\\"
                              "IOSUBSYS\\S98VSD.VXD=C:\\PROGRA~1\\W98\\SYSTEM\\"
                              "IOSUBSYS\\S98VSD.TMP\r\n", DL, DL8, TL, TL8) ==
              INST_VE_EXACT &&
          inst_vsd_entry_text("[rename]\r\nC:\\Program Files\\W98\\SYSTEM\\"
                              "IOSUBSYS\\S98VSD.VXD=C:\\PROGRA~1\\W98\\SYSTEM\\"
                              "IOSUBSYS\\S98VSD.TMP\r\n", DL, DL8, TL, TL8) ==
              INST_VE_EXACT,
          "8.3 names, and a long key with an 8.3 value: exact");
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    memcpy(big, "[rename]\r\n", 10);
    CHECK(inst_vsd_entry_text(big, D, DS, T, TS) == INST_VE_CONFLICT &&
          inst_rename_scan(big, count_rename, &n) == 0,
          "a line too long to read: nothing decided");
    n = 0;
    CHECK(inst_rename_scan("[boot]\r\na=b\r\n[rename]\r\nx=y\r\n\r\n"
                           "  z = w  \r\n[other]\r\nq=r\r\n", count_rename,
                           &n) == 1 && n == 2,
          "only [rename]'s lines are passed on");
}

static void test_vsd_names(void)
{
    char name[9];
    size_t n;
    int i;
    int ios = 0;

    CHECK(!inst_base_differs("stor98.mpd", "STOR98.VXD"),
          "stor98.mpd and STOR98.VXD share a base name");
    CHECK(inst_base_differs("stor98.mpd", "S98VSD.VXD"),
          "stor98.mpd and S98VSD.VXD differ");
    CHECK(inst_base_differs("STOR98", "STOR98X.VXD"), "lengths differ");
    CHECK(!inst_base_differs("A.B.VXD", "a.b.mpd"), "the last dot");
    for (i = 0; i < INST_STOR_COUNT; i++) {
        if (instStorFile[i].Dir != INST_STOR_DIR_IOS) {
            continue;
        }
        ios++;
        CHECK(inst_base_differs(instStorFile[i].Name, INST_VSD_FILE),
              "an IOSUBSYS file of Stor98's and the layer differ in base name");
        CHECK(inst_base_differs(instStorFile[i].Name, INST_VSD_TMPNAME),
              "an IOSUBSYS file of Stor98's and the layer's .TMP differ");
    }
    CHECK(ios != 0, "Stor98 has an IOSUBSYS file");
    CHECK(!inst_base_differs(INST_VSD_FILE, INST_VSD_TMPNAME),
          "the .TMP is the layer's own name");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    CHECK(inst_le_ddb_name(leImg, n, name) && strcmp(name, "S98VSD  ") == 0,
          "the DDB name read through the entry and page tables");
    CHECK(inst_vsd_old_is_own(leImg, n, "AA", "", ""),
          "an old-name image: DDB S98VSD and the old stamp: own");
    n = le_make("S98VSD  ", 0x10, "S98VSD.VXD 3.2.0.0");
    CHECK(!inst_vsd_old_is_own(leImg, n, "AA", "", ""),
          "DDB S98VSD without the old stamp: not own");
    n = le_make("OTHERVXD", 0x10, "S98VSD   STOR98.VXD 3.2.0.0");
    CHECK(!inst_vsd_old_is_own(leImg, n, "AA", "", ""),
          "both strings in the text, another DDB name: not own");
    n = le_make("S98VSD  ", 0x1000, "STOR98.VXD 3.2.0.0");
    CHECK(!inst_le_ddb_name(leImg, n, name) &&
          !inst_vsd_old_is_own(leImg, n, "AA", "", ""),
          "the DDB outside its object: not read, not own");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    leImg[0x40 + 0x130 + 1] = 2;
    CHECK(!inst_vsd_old_is_own(leImg, n, "AA", "", ""),
          "an entry bundle not 32-bit: not own");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    leImg[0x41] = 'X';
    CHECK(!inst_vsd_old_is_own(leImg, n, "AA", "", ""),
          "no LE header: not own");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    CHECK(!inst_vsd_old_is_own(leImg, 0x250, "AA", "", ""),
          "the image cut short before the stamp: not own");
    CHECK(!inst_le_ddb_name(leImg, 0x80, name), "cut short in the header");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    leImg[0x40 + 0x120 + 3] = 3;
    CHECK(!inst_vsd_old_is_own(leImg, n, "AA", "", ""),
          "the DDB's page zero-filled (type 3): not own");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    le_put32(leImg + 0x40 + 0x2C, 0x10);
    CHECK(!inst_vsd_old_is_own(leImg, n, "AA", "", ""),
          "the DDB past the last page's declared bytes: not own");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    le_put32(leImg + 0x40 + 0x5C, 0xFFFFFFBCUL);
    CHECK(!inst_le_ddb_name(leImg, n, name),
          "an entry table offset that wraps: not read");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    le_put32(leImg + 0x40 + 0x40, 0xFFFFFFF0UL);
    CHECK(!inst_le_ddb_name(leImg, n, name),
          "an object table offset that wraps: not read");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    le_put32(leImg + 0x40 + 0x48, 0xFFFFFFF0UL);
    CHECK(!inst_le_ddb_name(leImg, n, name),
          "a page table offset that wraps: not read");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    le_put32(leImg + 0x40 + 0x80, 0xFFFFFF00UL);
    CHECK(!inst_le_ddb_name(leImg, n, name),
          "a data page offset that wraps: not read");
    n = le_make("S98VSD  ", 0x10, "STOR98.VXD 3.2.0.0");
    le_put32(leImg + 0x3C, 0xFFFFFFF0UL);
    CHECK(!inst_le_ddb_name(leImg, n, name), "an LE offset that wraps");
    n = le_make("OTHERVXD", 0x10, "");
    CHECK(inst_vsd_old_is_own(leImg, n, "AB", "AB", ""),
          "the record's File bytes: own");
    CHECK(inst_vsd_old_is_own(NULL, 0, "CD", "", "cd"),
          "the record's Pending bytes, case ignored: own");
    CHECK(!inst_vsd_old_is_own(NULL, 0, "", "", ""), "unreadable: not own");
}

/* XHCIPFW's box, switches and mode (design record 32 sections 8 and
 * 11.3; the owner, 2026-10-27: a box of its own, the Thunderbolt box's
 * child, ticked whenever it is, on every system offered it, Q9); the
 * VxD's placement is S98VSD.VXD's table (test_vsd). */
static void test_pfw_inst(void)
{
    char *argv1[4];
    char err[256];
    INST_OPTS o;
    int shown;
    int open;

    argv1[0] = "x";
    argv1[1] = "/xhcipfw";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 1 &&
          strstr(err, "/thunderbolt") != NULL,
          "/xhcipfw alone: refused, the Thunderbolt part not in the run");
    argv1[2] = "/thunderbolt";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 0 && o.Pfw &&
          !o.NoPfw, "/xhcipfw /thunderbolt");
    argv1[2] = "/cmonly";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 0 && o.Pfw,
          "/xhcipfw /cmonly");
    argv1[1] = "/noxhcipfw";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 && o.NoPfw &&
          !o.Pfw, "/noxhcipfw alone");
    argv1[2] = "/xhcipfw";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1 &&
          strstr(err, "contradict") != NULL,
          "/noxhcipfw /xhcipfw: a contradiction");
    argv1[2] = "/uninstall";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1 &&
          strstr(err, "belongs to an install") != NULL,
          "/noxhcipfw /uninstall: refused");
    argv1[1] = "/xhcipfw";
    argv1[2] = "/devices";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1,
          "/xhcipfw /devices: refused");
    argv1[2] = "/thunderbolt";
    argv1[3] = "/nostor98";
    CHECK(inst_parse_args(4, argv1, &o, err, sizeof(err)) == 0 && o.Pfw,
          "/xhcipfw /thunderbolt /nostor98: not Stor98's child");

    /* Which systems (record 32 sections 7 and 11; the owner's Q6). */
    CHECK(inst_pfw_sys(0, 4, 10, 0, 1) == INST_PFW_SYS_9X,
          "98 SE, ME: the VxD");
    CHECK(inst_pfw_sys(1, 5, 0, 0, 1) == INST_PFW_SYS_ALWAYS,
          "2000: offered");
    CHECK(inst_pfw_sys(1, 5, 1, 0, 1) == INST_PFW_SYS_SEEN,
          "XP: offered, the problem read for the note");
    CHECK(inst_pfw_sys(1, 5, 2, 0, 1) == INST_PFW_SYS_NONE,
          "Server 2003: not offered");
    CHECK(inst_pfw_sys(1, 6, 0, 0, 1) == INST_PFW_SYS_SEEN &&
          inst_pfw_sys(1, 6, 1, 0, 1) == INST_PFW_SYS_SEEN,
          "Vista, 7 x86: offered");
    /* Task 44.72 (the owner, 2026-10-28): from 8 on the boot driver's
     * start races the PCI driver's; task 45.9 (the owner, 2026-10-29): a
     * root bus filter on 8 and 8.1. */
    CHECK(inst_pfw_sys(1, 6, 2, 0, 1) == INST_PFW_SYS_FILTER &&
          inst_pfw_sys(1, 6, 3, 0, 1) == INST_PFW_SYS_FILTER,
          "8, 8.1 x86: offered, the filter role");
    /* Task 48.27 (the owner, 2026-11-09, Q29 (b)): the filter role on 10
     * from version 2004 (19041), the builds read; earlier builds as
     * before. */
    CHECK(inst_pfw_sys(1, 10, 0, 19040, 1) == INST_PFW_SYS_GONE &&
          inst_pfw_sys(1, 10, 0, 10240, 1) == INST_PFW_SYS_GONE,
          "10 x86 before 2004: not offered, an earlier one removed");
    CHECK(inst_pfw_sys(1, 10, 0, 19041, 1) == INST_PFW_SYS_FILTER &&
          inst_pfw_sys(1, 10, 0, 19045, 1) == INST_PFW_SYS_FILTER,
          "10 x86 2004 and later: offered, the filter role");
    CHECK(inst_pfw_sys(1, 10, 0, 19045, 0) == INST_PFW_SYS_NONE,
          "10 x64: not offered");
    CHECK(inst_pfw_sys(1, 6, 4, 0, 1) == INST_PFW_SYS_NONE &&
          inst_pfw_sys(1, 11, 0, 19045, 1) == INST_PFW_SYS_NONE,
          "an unknown NT 6 or later: nothing");
    CHECK(inst_pfw_sys(1, 6, 1, 0, 0) == INST_PFW_SYS_NONE &&
          inst_pfw_sys(1, 6, 2, 0, 0) == INST_PFW_SYS_NONE &&
          inst_pfw_sys(1, 10, 0, 19045, 0) == INST_PFW_SYS_NONE,
          "64-bit: not offered");

    /* The box: the one decision (the owner's Q9), the Thunderbolt box's
     * child, ticked with it on every system offered it. */
    CHECK(inst_pfw_box(INST_PFW_SYS_9X, 1, 1, 0, &shown, &open) && shown &&
          open, "98 SE and ME with the file, Thunderbolt ticked: ticked");
    CHECK(!inst_pfw_box(INST_PFW_SYS_9X, 1, 0, 0, &shown, &open) && shown &&
          !open, "Thunderbolt unticked: shown, greyed and unticked");
    CHECK(!inst_pfw_box(INST_PFW_SYS_SEEN, 1, 0, 0, &shown, &open) &&
          shown && !open, "XP to 10, Thunderbolt unticked: unticked");
    CHECK(!inst_pfw_box(INST_PFW_SYS_9X, 1, 1, 1, &shown, &open) && shown &&
          open, "/noxhcipfw: shown, open, unticked");
    CHECK(!inst_pfw_box(INST_PFW_SYS_9X, 0, 1, 0, &shown, &open) && !shown &&
          !open, "no file in the package: not shown");
    CHECK(!inst_pfw_box(INST_PFW_SYS_NONE, 1, 1, 0, &shown, &open) &&
          !shown && !open, "a system not offered it: not shown, never ticked");
    CHECK(inst_pfw_box(INST_PFW_SYS_ALWAYS, 1, 1, 0, &shown, &open) &&
          shown && open, "2000, Thunderbolt ticked: ticked");
    CHECK(inst_pfw_box(INST_PFW_SYS_SEEN, 1, 1, 0, &shown, &open) &&
          shown && open,
          "XP, Vista and 7, Thunderbolt ticked: ticked, seen or not");
    CHECK(!inst_pfw_box(INST_PFW_SYS_SEEN, 1, 1, 1, &shown, &open),
          "XP to 7, /noxhcipfw: unticked");
    CHECK(!inst_pfw_box(INST_PFW_SYS_GONE, 1, 1, 0, &shown, &open) &&
          !shown && !open, "10 x86: not shown, never ticked");
    CHECK(!inst_pfw_box(INST_PFW_SYS_GONE, 1, 0, 0, &shown, &open) &&
          !shown && !open, "10, Thunderbolt unticked: not shown");
    CHECK(inst_pfw_box(INST_PFW_SYS_FILTER, 1, 1, 0, &shown, &open) &&
          shown && open, "8 and 8.1, Thunderbolt ticked: ticked");
    CHECK(!inst_pfw_box(INST_PFW_SYS_FILTER, 1, 0, 0, &shown, &open) &&
          shown && !open, "8 and 8.1, Thunderbolt unticked: unticked");
    CHECK(!inst_pfw_box(INST_PFW_SYS_FILTER, 1, 1, 1, &shown, &open) &&
          shown && open, "8 and 8.1, /noxhcipfw: unticked");

    CHECK(inst_pfw_mode(INST_PFW_SYS_NONE, 0, 1, 1, 1) == INST_VSD_NONE &&
          inst_pfw_mode(INST_PFW_SYS_NONE, 1, 1, 1, 1) == INST_VSD_NONE,
          "not offered: nothing");
    CHECK(inst_pfw_mode(INST_PFW_SYS_9X, 1, 0, 0, 0) == INST_VSD_UNINSTALL &&
          inst_pfw_mode(INST_PFW_SYS_SEEN, 1, 1, 1, 1) == INST_VSD_UNINSTALL,
          "uninstall, whatever the Thunderbolt box");
    CHECK(inst_pfw_mode(INST_PFW_SYS_ALWAYS, 0, 0, 1, 1) ==
          INST_VSD_INACTIVE, "no file in the package: an installed one kept");
    CHECK(inst_pfw_mode(INST_PFW_SYS_9X, 0, 1, 0, 0) == INST_VSD_INACTIVE &&
          inst_pfw_mode(INST_PFW_SYS_SEEN, 0, 1, 0, 1) == INST_VSD_INACTIVE,
          "the Thunderbolt support out of the run: left as it is");
    CHECK(inst_pfw_mode(INST_PFW_SYS_SEEN, 0, 1, 1, 1) == INST_VSD_SELECTED,
          "ticked");
    CHECK(inst_pfw_mode(INST_PFW_SYS_9X, 0, 1, 1, 0) == INST_VSD_DESELECTED,
          "unticked: removed");
    CHECK(inst_pfw_mode(INST_PFW_SYS_GONE, 0, 1, 1, 1) ==
              INST_VSD_DESELECTED &&
          inst_pfw_mode(INST_PFW_SYS_GONE, 0, 1, 0, 0) ==
              INST_VSD_DESELECTED &&
          inst_pfw_mode(INST_PFW_SYS_GONE, 0, 0, 1, 1) ==
              INST_VSD_DESELECTED,
          "8 to 10 x86: an earlier one of this installer's removed, "
          "whatever the boxes or the package");
    CHECK(inst_pfw_mode(INST_PFW_SYS_GONE, 1, 1, 1, 1) == INST_VSD_UNINSTALL,
          "8 to 10 x86: the uninstall removes it");
    CHECK(inst_pfw_mode(INST_PFW_SYS_FILTER, 0, 1, 1, 1) ==
              INST_VSD_SELECTED &&
          inst_pfw_mode(INST_PFW_SYS_FILTER, 0, 1, 1, 0) ==
              INST_VSD_DESELECTED &&
          inst_pfw_mode(INST_PFW_SYS_FILTER, 0, 1, 0, 1) ==
              INST_VSD_INACTIVE &&
          inst_pfw_mode(INST_PFW_SYS_FILTER, 1, 1, 1, 1) ==
              INST_VSD_UNINSTALL,
          "8 and 8.1: as on Vista and 7");

    /* The root bus devnodes (record 32 section 11.5.2). */
    {
        static const char hw[] = "ACPI\\VEN_PNP&DEV_0A08\0ACPI\\PNP0A08\0*PNP0A08\0";
        static const char cp[] = "*PNP0A03\0";
        static const char other[] = "ACPI\\PNP0C0A\0*PNP0C0A\0";
        static const char low[] = "*pnp0a03\0";
        char out[64];
        size_t n;
        int added;

        CHECK(inst_pfw_rootbus(hw, sizeof(hw), NULL, 0),
              "a PCI Express root complex: a root bus");
        CHECK(inst_pfw_rootbus(other, sizeof(other), cp, sizeof(cp)),
              "*PNP0A03 among the compatible ids: a root bus");
        CHECK(inst_pfw_rootbus(other, sizeof(other), low, sizeof(low)),
              "compared without case");
        CHECK(!inst_pfw_rootbus(other, sizeof(other), NULL, 0),
              "a battery: no");

        n = inst_msz_append(NULL, 0, "xhcipfw", out, sizeof(out), &added);
        CHECK(n == 9 && added && memcmp(out, "xhcipfw\0\0", 9) == 0,
              "no filters: the one entry");
        n = inst_msz_append("partmgr\0\0", 9, "xhcipfw", out, sizeof(out),
                            &added);
        CHECK(n == 17 && added &&
              memcmp(out, "partmgr\0xhcipfw\0\0", 17) == 0,
              "another filter kept first, the entry appended");
        n = inst_msz_append("XHCIPFW\0a\0\0", 12, "xhcipfw", out,
                            sizeof(out), &added);
        CHECK(n == 11 && !added && memcmp(out, "XHCIPFW\0a\0\0", 11) == 0,
              "already there (without case): unchanged");
        n = inst_msz_append("partmgr\0\0", 9, "xhcipfw", out, 12, &added);
        CHECK(n == (size_t)-1, "too long: refused");
    }

    /* BootConfig: 7 x86's root port on the 4G-off layout, as read
     * (runs/run-44.md "44.49"): a memory range, a second, the 64-bit
     * prefetchable window (MemoryLarge, flags 0244h, start 7F_8000_0000),
     * a port range and an interrupt. */
    {
        static const unsigned char rp[] = {
            0x01,0,0,0, 0x05,0,0,0, 0,0,0,0, 0x01,0x00,0x01,0x00, 0x05,0,0,0,
            0x03,0x01,0x80,0x00, 0x00,0x10,0xA1,0xFE, 0,0,0,0, 0x00,0x10,0,0,
            0x03,0x01,0x40,0x00, 0x00,0x00,0x00,0xCE, 0,0,0,0, 0x00,0x00,0x80,0x30,
            0x07,0x01,0x44,0x02, 0x00,0x00,0x00,0x00, 0x80,0x7F,0x00,0x00, 0x00,0x00,0x00,0x40,
            0x01,0x01,0xA1,0x00, 0x00,0x10,0,0, 0,0,0,0, 0x00,0x60,0,0,
            0x02,0x03,0x00,0x00, 0x0B,0,0,0, 0x0B,0,0,0, 0xFF,0xFF,0xFF,0xFF
        };
        unsigned char b[sizeof(rp) + 16];

        CHECK(inst_pfw_bootcfg_high(rp, sizeof(rp)) == 1,
              "7's root port: the prefetchable window above 4 GB seen");
        CHECK(inst_pfw_bootcfg_high(rp, sizeof(rp) - 1) == 1,
              "the window's descriptor is before the cut");
        CHECK(inst_pfw_bootcfg_high(rp, 60) == 0,
              "cut before the window: not seen");
        memcpy(b, rp, sizeof(rp));
        b[20 + 32 + 2] = 0x40;     /* not prefetchable */
        CHECK(inst_pfw_bootcfg_high(b, sizeof(rp)) == 0,
              "a window above 4 GB, not prefetchable: not seen");
        memcpy(b, rp, sizeof(rp));
        b[20 + 32 + 2] = 0x04;     /* prefetchable, not a window: a BAR */
        CHECK(inst_pfw_bootcfg_high(b, sizeof(rp)) == 0,
              "a prefetchable range above 4 GB that is no window: not seen");
        memcpy(b, rp, sizeof(rp));
        b[20 + 32 + 8 + 1] = 0x00; /* start 0000_0000_0000_0000 */
        b[20 + 32 + 8 + 0] = 0x00;
        CHECK(inst_pfw_bootcfg_high(b, sizeof(rp)) == 0,
              "a prefetchable window below 4 GB: not seen");
        memcpy(b, rp, sizeof(rp));
        b[20] = 0x05;                   /* a device-specific first, size 0 */
        b[24] = 0; b[25] = 0; b[26] = 0; b[27] = 0;
        CHECK(inst_pfw_bootcfg_high(b, sizeof(rp)) == 1,
              "a device-specific descriptor skipped");
        b[24] = 0xFF;                   /* its size past the end */
        CHECK(inst_pfw_bootcfg_high(b, sizeof(rp)) == 0,
              "a device-specific size past the end: not a list");
        CHECK(inst_pfw_bootcfg_high(NULL, 0) == 0 &&
              inst_pfw_bootcfg_high(rp, 3) == 0, "no list");
    }
    CHECK(inst_base_differs(INST_PFW_FILE, INST_PFW_TMPNAME) == 0 &&
          inst_base_differs(INST_PFW_FILE, INST_VSD_FILE),
          "the .TMP beside it shares its base name; S98VSD.VXD's differs");
}

static void test_vsd(void)
{
    char *argv1[4];
    char err[256];
    INST_OPTS o;
    INST_VSDFACTS f;
    VMODEL m;
    int shown;
    int open;
    int cut;
    int r;

    /* The switches. */
    argv1[0] = "x";
    argv1[1] = "/s98vsd";
    argv1[2] = "/thunderbolt";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 0 &&
          o.Stor98Vxd && !o.NoStor98Vxd, "/s98vsd /thunderbolt");
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 1 &&
          strstr(err, "Stor98 in the run") != NULL,
          "/s98vsd alone: refused, it needs Stor98 in the run");
    argv1[2] = "/nos98vsd";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1,
          "/s98vsd /nos98vsd: a contradiction");
    argv1[1] = "/nos98vsd";
    argv1[2] = "/uninstall";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1,
          "/nos98vsd belongs to an install");
    argv1[2] = "/thunderbolt";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 0 &&
          o.NoStor98Vxd, "/nos98vsd /thunderbolt");
    argv1[1] = "/s98vsd";
    argv1[2] = "/thunderbolt";
    argv1[3] = "/nostor98";
    CHECK(inst_parse_args(4, argv1, &o, err, sizeof(err)) == 1,
          "/s98vsd /nostor98: refused");

    /* The box. */
    CHECK(inst_vsd_box(1, 1, 1, 0, &shown, &open) && shown && open,
          "98 SE and ME: ticked with Stor98");
    CHECK(!inst_vsd_box(1, 1, 1, 1, &shown, &open) && shown && open,
          "/nos98vsd: open, unticked");
    CHECK(!inst_vsd_box(1, 1, 0, 0, &shown, &open) && shown && !open,
          "Stor98 unticked: greyed and unticked");
    CHECK(!inst_vsd_box(0, 1, 1, 0, &shown, &open) && shown && !open,
          "another system: shown greyed, never ticked");
    /* The default on every route (owner, 2026-10-22): ticked whenever
     * Stor98 is, unticked with it, a fresh install and an update alike
     * (neither the box nor the mode reads what is installed). Stor98's
     * decision on 98 SE (tb in the run): the window and Enter give Y,
     * /yes Y, /stor98 Y, /nostor98 N; the layer follows. */
    {
        int askedS;
        int defS;
        int s98;
        int def98 = inst_s98_box_default(INST_S98SYS_98SE);

        s98 = inst_s98_decide(1, 1, 0, 0, 0, 0, def98, &askedS, &defS);
        CHECK(askedS && defS && inst_vsd_box(1, 1, defS, 0, &shown, &open),
              "window/console: Stor98 [Y], the layer ticked with it");
        s98 = inst_s98_decide(1, 1, 1, 0, 0, 0, def98, &askedS, &defS);
        CHECK(!askedS && s98 && inst_vsd_box(1, 1, s98, 0, &shown, &open),
              "/yes: Stor98 Y, the layer ticked");
        s98 = inst_s98_decide(1, 1, 0, 1, 0, 0, def98, &askedS, &defS);
        CHECK(!askedS && s98 && inst_vsd_box(1, 1, s98, 0, &shown, &open),
              "/stor98: Stor98 Y, the layer ticked");
        s98 = inst_s98_decide(1, 1, 1, 0, 1, 0, def98, &askedS, &defS);
        CHECK(!s98 && !inst_vsd_box(1, 1, s98, 0, &shown, &open) && !open,
              "/yes /nostor98: Stor98 N, the layer unticked and greyed");
        s98 = inst_s98_decide(1, 1, 1, 0, 0, 0, def98, &askedS, &defS);
        CHECK(s98 && !inst_vsd_box(1, 1, s98, 1, &shown, &open) && open,
              "/yes /nos98vsd: Stor98 Y, the layer unticked, open");
        s98 = inst_s98_decide(1, 0, 1, 0, 0, 0, def98, &askedS, &defS);
        CHECK(!s98 && !inst_vsd_box(1, 1, s98, 0, &shown, &open),
              "/yes without the Thunderbolt support: neither");
        CHECK(inst_vsd_mode(1, 0, 1, 1, 0, 1) == INST_VSD_SELECTED,
              "an update with Stor98 installed and ticked: selected");
        CHECK(inst_vsd_mode(1, 0, 1, 0, 1, 0) == INST_VSD_DESELECTED,
              "Stor98 unticked and removed: the layer removed with it");
        CHECK(inst_vsd_mode(1, 0, 1, 0, 0, 0) == INST_VSD_INACTIVE,
              "Stor98 unticked and kept: the layer kept with it");
    }
    CHECK(!inst_vsd_box(1, 0, 1, 0, &shown, &open) && !shown && !open,
          "no S98VSD.VXD in the package: not shown");

    /* The mode; D8: the layer follows Stor98. */
    CHECK(inst_vsd_mode(0, 0, 1, 1, 0, 1) == INST_VSD_NONE &&
          inst_vsd_mode(0, 1, 1, 1, 0, 1) == INST_VSD_NONE,
          "not 98 SE or ME: nothing");
    CHECK(inst_vsd_mode(1, 1, 0, 0, 0, 0) == INST_VSD_UNINSTALL, "uninstall");
    CHECK(inst_vsd_mode(1, 0, 1, 1, 0, 1) == INST_VSD_SELECTED &&
          inst_vsd_mode(1, 0, 1, 1, 0, 0) == INST_VSD_DESELECTED,
          "ticked, unticked");
    CHECK(inst_vsd_mode(1, 0, 1, 0, 0, 0) == INST_VSD_INACTIVE &&
          inst_vsd_mode(1, 0, 1, 0, 1, 0) == INST_VSD_DESELECTED,
          "Stor98 unticked: kept with it, removed with it");

    /* The rows, one fact set each. */
    f = vf(0, 0, INST_VF_ABSENT, INST_VF_ABSENT, INST_VE_ABSENT,
           INST_VSD_SELECTED, 0, 0, 0);
    CHECK(inst_vsd_next(&f) == INST_VOP_START, "nothing there: start");
    f.Mode = INST_VSD_INACTIVE;
    CHECK(inst_vsd_next(&f) == INST_VOP_DONE, "inactive: nothing");
    f = vf(0, 1, INST_VF_FILE, INST_VF_ABSENT, INST_VE_ABSENT,
           INST_VSD_SELECTED, 0, 1, 0);
    CHECK(inst_vsd_next(&f) == INST_VOP_DONE, "the package's bytes in place");
    f.PkgIsFile = 0;
    CHECK(inst_vsd_next(&f) == INST_VOP_START, "an older release: update");
    f.Mode = INST_VSD_DESELECTED;
    CHECK(inst_vsd_next(&f) == INST_VOP_DEL_DEST, "unticked: removed");
    f.Mode = INST_VSD_INACTIVE;
    CHECK(inst_vsd_next(&f) == INST_VOP_DONE, "greyed, Stor98 kept: kept");
    f = vf(0, 0, INST_VF_OTHER, INST_VF_ABSENT, INST_VE_ABSENT,
           INST_VSD_SELECTED, 0, 0, 0);
    CHECK(inst_vsd_next(&f) == INST_VOP_FOREIGN, "another's file left");
    f.Mode = INST_VSD_UNINSTALL;
    CHECK(inst_vsd_next(&f) == INST_VOP_FOREIGN,
          "the uninstall leaves another's file");
    f = vf(0, 0, INST_VF_ABSENT, INST_VF_ABSENT, INST_VE_CONFLICT,
           INST_VSD_SELECTED, 0, 0, 0);
    CHECK(inst_vsd_next(&f) == INST_VOP_BLOCK_INI,
          "another program's rename line: blocked");
    f.Mode = INST_VSD_INACTIVE;
    CHECK(inst_vsd_next(&f) == INST_VOP_DONE, "conflict, inactive: nothing");
    f = vf(0, 1, INST_VF_FILE, INST_VF_ABSENT, INST_VE_CONFLICT,
           INST_VSD_UNINSTALL, 0, 0, 0);
    CHECK(inst_vsd_next(&f) == INST_VOP_DEL_DEST,
          "conflict, uninstall: its own file removed, the line left");
    f = vf(0, 0, INST_VF_ABSENT, INST_VF_OTHER, INST_VE_ABSENT,
           INST_VSD_SELECTED, 0, 0, 0);
    CHECK(inst_vsd_next(&f) == INST_VOP_BLOCK_TMP,
          "a foreign S98VSD.TMP, nothing in flight: blocked");
    f = vf(0, 1, INST_VF_FILE, INST_VF_OTHER, INST_VE_ABSENT,
           INST_VSD_DESELECTED, 0, 0, 0);
    CHECK(inst_vsd_next(&f) == INST_VOP_DEL_DEST,
          "a foreign .TMP, unticked: its own file removed");

    /* Whole runs, the model applying each step. */
    memset(&m, 0, sizeof(m));
    r = vrun(&m, INST_VSD_SELECTED, 1);
    CHECK(r == INST_VOP_DONE && m.Dest == 1 && m.File == 1 && m.Pending == 0 &&
          m.Tmp == 0 && m.Entry == INST_VE_ABSENT, "a first placement");
    memset(&m, 0, sizeof(m));
    m.File = 2;
    m.Dest = 2;
    r = vrun(&m, INST_VSD_SELECTED, 1);
    CHECK(r == INST_VOP_QUEUED && m.Dest == 2 && m.Tmp == 1 && m.Pending == 1 &&
          m.Entry == INST_VE_EXACT, "an update queued for the start");
    vboot(&m);
    r = vrun(&m, INST_VSD_SELECTED, 1);
    CHECK(r == INST_VOP_DONE && m.Dest == 1 && m.File == 1 && m.Pending == 0 &&
          m.Tmp == 0, "after the start: promoted, nothing left");
    /* Update, then untick with no start between: the queued file never
     * arrives. */
    memset(&m, 0, sizeof(m));
    m.File = 2;
    m.Dest = 2;
    vrun(&m, INST_VSD_SELECTED, 1);
    r = vrun(&m, INST_VSD_DESELECTED, 1);
    vboot(&m);
    CHECK(r == INST_VOP_DONE && m.Dest == 0 && m.Tmp == 0 && m.Pending == 0 &&
          m.File == 0 && m.Entry == INST_VE_ABSENT,
          "update then untick: all withdrawn, nothing arrives");
    memset(&m, 0, sizeof(m));
    m.File = 2;
    m.Dest = 2;
    vrun(&m, INST_VSD_SELECTED, 1);
    r = vrun(&m, INST_VSD_UNINSTALL, 0);
    vboot(&m);
    CHECK(r == INST_VOP_DONE && m.Dest == 0 && m.Pending == 0 && m.File == 0,
          "update then uninstall: all withdrawn");
    /* A later package over a queued older one. */
    memset(&m, 0, sizeof(m));
    m.File = 2;
    m.Dest = 2;
    vrun(&m, INST_VSD_SELECTED, 3);
    r = vrun(&m, INST_VSD_SELECTED, 1);
    CHECK(r == INST_VOP_QUEUED && m.Pending == 1 && m.Tmp == 1,
          "a later package replaces the queued one");
    /* Inactive over a queued update: it stays queued. */
    memset(&m, 0, sizeof(m));
    m.File = 2;
    m.Dest = 2;
    vrun(&m, INST_VSD_SELECTED, 1);
    r = vrun(&m, INST_VSD_INACTIVE, 0);
    CHECK(r == INST_VOP_DONE && m.Pending == 1 && m.Tmp == 1 &&
          m.Entry == INST_VE_EXACT, "inactive: the queue stays");
    /* Every step of a placement and of an update cut short, then the next
     * run (ticked, and the uninstall) recovers. */
    for (cut = 1; cut <= 6; cut++) {
        memset(&m, 0, sizeof(m));
        m.Cut = cut;
        vrun(&m, INST_VSD_SELECTED, 1);
        m.Cut = 0;
        r = vrun(&m, INST_VSD_SELECTED, 1);
        CHECK(r == INST_VOP_DONE && m.Dest == 1 && m.File == 1 &&
              m.Pending == 0 && m.Tmp == 0,
              "a placement cut short, then a ticked run");
        memset(&m, 0, sizeof(m));
        m.Cut = cut;
        vrun(&m, INST_VSD_SELECTED, 1);
        m.Cut = 0;
        r = vrun(&m, INST_VSD_UNINSTALL, 0);
        CHECK(r == INST_VOP_DONE && m.Dest == 0 && m.File == 0 &&
              m.Pending == 0 && m.Tmp == 0 && m.Entry == INST_VE_ABSENT,
              "a placement cut short, then the uninstall");
        memset(&m, 0, sizeof(m));
        m.File = 2;
        m.Dest = 2;
        m.Cut = cut;
        vrun(&m, INST_VSD_SELECTED, 1);
        m.Cut = 0;
        r = vrun(&m, INST_VSD_SELECTED, 1);
        CHECK(r == INST_VOP_QUEUED && m.Pending == 1 && m.Tmp == 1 &&
              m.Entry == INST_VE_EXACT, "an update cut short, then a ticked "
              "run: queued");
        memset(&m, 0, sizeof(m));
        m.File = 2;
        m.Dest = 2;
        m.Cut = cut;
        vrun(&m, INST_VSD_SELECTED, 1);
        m.Cut = 0;
        r = vrun(&m, INST_VSD_UNINSTALL, 0);
        vboot(&m);
        CHECK(r == INST_VOP_DONE && m.Dest == 0 && m.Pending == 0 &&
              m.File == 0 && m.Tmp == 0, "an update cut short, then the "
              "uninstall");
    }
    /* File=A, Pending=B, the destination B (the start did it) and a
     * foreign .TMP: promoted, and the uninstall then removes it. */
    memset(&m, 0, sizeof(m));
    m.File = 2;
    m.Pending = 1;
    m.Dest = 1;
    m.Tmp = 9;
    r = vrun(&m, INST_VSD_UNINSTALL, 0);
    CHECK(r == INST_VOP_DONE && m.Dest == 0 && m.Tmp == 9 && m.Pending == 0 &&
          m.File == 0, "a done update beside a foreign .TMP: promoted, then "
          "removed; the .TMP left");
    /* Moved into place, then cut short before File (Pending=B, the
     * destination B, no .TMP), and another program's rename line naming
     * S98VSD.TMP: the uninstall removes the installer's destination. */
    memset(&m, 0, sizeof(m));
    m.File = 2;
    m.Pending = 1;
    m.Dest = 1;
    m.Entry = INST_VE_CONFLICT;
    r = vrun(&m, INST_VSD_UNINSTALL, 0);
    CHECK(r == INST_VOP_DONE && m.Dest == 0 && m.Pending == 0 && m.File == 0,
          "a moved-but-unpromoted file under a foreign line: removed");
    /* A queued update whose destination another installer replaced. */
    memset(&m, 0, sizeof(m));
    m.File = 2;
    m.Dest = 2;
    vrun(&m, INST_VSD_SELECTED, 1);
    m.Dest = 9;
    r = vrun(&m, INST_VSD_UNINSTALL, 0);
    CHECK(r == INST_VOP_FOREIGN && m.Dest == 9 && m.Tmp == 0 &&
          m.Pending == 0 && m.File == 0 && m.Entry == INST_VE_ABSENT,
          "a foreign destination: its file left, the owned queue withdrawn");
}

/* The other USB drivers: usb-wdm-free and usb-wdm-nonfree (record 26
 * section 6.7, roadmap-usb4 task 43.48). */
static void test_other_sets(void)
{
    INST_STAGE_LINE sl;
    INST_REC_LINE rl;
    INST_U32 mask;
    char hex[65];
    char rel[INST_OTHER_REL_CHARS];
    char pkg[64];
    char text[1024];
    char longp[200];
    INST_U32 w;

    /* The paths (package-common.ps1's Test-OtherSetRelPath). */
    CHECK(inst_other_rel_ok("serial/ch341/w98se-xp/WIN 1X/CH341SER.INF"),
          "rel: a space inside a part");
    CHECK(inst_other_rel_ok("a/b.c"), "rel: plain");
    CHECK(!inst_other_rel_ok(""), "rel: empty");
    CHECK(!inst_other_rel_ok("a/../b"), "rel: ..");
    CHECK(!inst_other_rel_ok("a/./b"), "rel: .");
    CHECK(!inst_other_rel_ok("a//b"), "rel: an empty part");
    CHECK(!inst_other_rel_ok("/a"), "rel: leading /");
    CHECK(!inst_other_rel_ok("a/"), "rel: trailing /");
    CHECK(!inst_other_rel_ok("a;b/c"), "rel: DevicePath's ;");
    CHECK(!inst_other_rel_ok("a\\b"), "rel: a backslash");
    CHECK(!inst_other_rel_ok("c:/a"), "rel: a drive");
    CHECK(!inst_other_rel_ok("a/b "), "rel: a trailing space");
    CHECK(!inst_other_rel_ok("a/ b"), "rel: a leading space");
    CHECK(!inst_other_rel_ok("a/b."), "rel: a trailing dot");
    CHECK(!inst_other_rel_ok("a/\xe9"), "rel: not ASCII");
    memset(longp, 'x', sizeof(longp));
    longp[140] = '\0';
    CHECK(inst_other_rel_ok(longp), "rel: 140 characters");
    longp[140] = 'x';
    longp[141] = '\0';
    CHECK(!inst_other_rel_ok(longp), "rel: 141 characters");

    /* The systems' tokens. */
    CHECK(inst_sys_token(0, 4, 10, 2222, 0) == 0 &&
          strcmp(inst_sys_name(0), "W98SE") == 0, "sys: 98 SE");
    CHECK(inst_sys_token(0, 4, 10, 1998, 0) == -1, "sys: 98 FE has none");
    CHECK(strcmp(inst_sys_name(inst_sys_token(0, 4, 90, 3000, 0)), "WME") == 0,
          "sys: ME");
    CHECK(strcmp(inst_sys_name(inst_sys_token(1, 5, 0, 2195, 0)), "W2K") == 0,
          "sys: 2000");
    CHECK(strcmp(inst_sys_name(inst_sys_token(1, 5, 1, 2600, 0)), "WXP") == 0,
          "sys: XP");
    CHECK(strcmp(inst_sys_name(inst_sys_token(1, 5, 2, 3790, 1)), "WXP64") ==
          0 && inst_sys_token(1, 5, 2, 3790, 0) == -1, "sys: XP x64, not 2003");
    CHECK(strcmp(inst_sys_name(inst_sys_token(1, 6, 0, 6002, 1)), "WVISTA64")
          == 0, "sys: Vista x64");
    CHECK(strcmp(inst_sys_name(inst_sys_token(1, 6, 1, 7601, 0)), "W7") == 0,
          "sys: 7");
    CHECK(strcmp(inst_sys_name(inst_sys_token(1, 6, 3, 9600, 1)), "W8X64") ==
          0, "sys: 8.1 x64");
    CHECK(strcmp(inst_sys_name(inst_sys_token(1, 10, 0, 19045, 1)), "W10X64")
          == 0 && inst_sys_token(1, 10, 0, 22000, 1) == -1, "sys: 10, not 11");
    CHECK(inst_sys_mask("W98SE WME", &mask) && mask == 3UL, "mask: two");
    CHECK(!inst_sys_mask("W98SE W95", &mask), "mask: an unknown token");
    CHECK(!inst_sys_mask("  ", &mask), "mask: none");

    /* STAGE.TXT (usb-wdm-nonfree v1.0.0's format). */
    CHECK(inst_stage_parse("# a comment", "non-free", &sl) == 0, "stage: #");
    CHECK(inst_stage_parse("   ", "non-free", &sl) == 0, "stage: blank");
    CHECK(inst_stage_parse("STAGE | W98SE WME        | non-free/serial/ftdi/"
                           "w98se-me/FTDIBUS.INF  | the USB device; on 2000 "
                           "ftdi/w2k wins (0403:6001)", "non-free", &sl) == 1 &&
          sl.Stage && sl.Systems == 3UL &&
          strcmp(sl.Inf, "serial/ftdi/w98se-me/FTDIBUS.INF") == 0,
          "stage: a STAGE line");
    CHECK(inst_stage_parse("COPY  | W98SE WME W2K WXP | non-free/serial/ch341/"
                           "w98se-xp/WIN 1X/CH341SER.INF | WCH's second copy",
                           "non-free", &sl) == 1 && !sl.Stage &&
          strcmp(sl.Inf, "serial/ch341/w98se-xp/WIN 1X/CH341SER.INF") == 0,
          "stage: a COPY line with a space in the path");
    CHECK(inst_stage_parse("STAGE | W98SE | free/a/B.INF | x", "non-free",
                           &sl) == -1, "stage: another set's prefix");
    CHECK(inst_stage_parse("STAGE | W98SE | free/a/B.INF | x", "free", &sl) ==
          1, "stage: the free set's prefix");
    CHECK(inst_stage_parse("STAGE | W98SE | non-free/a/B.INF", "non-free",
                           &sl) == -1, "stage: three fields");
    CHECK(inst_stage_parse("STAGE | W98SE | non-free/a/B.INF | x | y",
                           "non-free", &sl) == -1, "stage: five fields");
    CHECK(inst_stage_parse("stage | W98SE | non-free/a/B.INF | x", "non-free",
                           &sl) == -1, "stage: the action's case");
    CHECK(inst_stage_parse("STAGE | W95 | non-free/a/B.INF | x", "non-free",
                           &sl) == -1, "stage: an unknown system");
    CHECK(inst_stage_parse("STAGE | W98SE | non-free/../B.INF | x", "non-free",
                           &sl) == -1, "stage: a path it refuses");

    /* SHA256.TXT and READ.TXT. */
    CHECK(inst_sums_parse("692db360d876b53bd4bd1395c49a14e69858a6af8c01b97c68b1"
                          "c4262534af7d  ethernet/ax88179/w7/ax88179_178a.cat"
                          "\r\n", hex, rel, sizeof(rel)) == 1 &&
          strncmp(hex, "692DB360", 8) == 0 &&
          strcmp(rel, "ethernet/ax88179/w7/ax88179_178a.cat") == 0,
          "sums: a line");
    CHECK(inst_sums_parse("", hex, rel, sizeof(rel)) == 0, "sums: blank");
    CHECK(inst_sums_parse("692db360d876b53bd4bd1395c49a14e69858a6af8c01b97c68b1"
                          "c4262534af7d *a/b", hex, rel, sizeof(rel)) == -1,
          "sums: the binary marker");
    CHECK(inst_sums_parse("zz2db360d876b53bd4bd1395c49a14e69858a6af8c01b97c68b1"
                          "c4262534af7d  a/b", hex, rel, sizeof(rel)) == -1,
          "sums: not hex");
    CHECK(inst_read_parse("net/cdcnet | W98SE W2K", pkg, sizeof(pkg), &mask) ==
          1 && strcmp(pkg, "net/cdcnet") == 0 && mask == 5UL, "read: a line");
    CHECK(inst_read_parse("cdcnet | W98SE", pkg, sizeof(pkg), &mask) == -1,
          "read: no class");
    CHECK(inst_read_parse("# x", pkg, sizeof(pkg), &mask) == 0, "read: #");

    /* The record file. */
    inst_rec_header(text, sizeof(text), INST_SET_NONFREE, "C:\\XHCI98\\NONFREE");
    CHECK(strcmp(text, "XHCI98 SET RECORD 1 NonFree C:\\XHCI98\\NONFREE") == 0,
          "rec: the header");
    CHECK(inst_rec_parse("F 0A1B2C3D 692db360d876b53bd4bd1395c49a14e69858a6af8c"
                         "01b97c68b1c4262534af7d C:\\XHCI98\\NONFREE\\a b\\c.inf",
                         &rl) == 1 && rl.Kind == 'F' &&
          strcmp(rl.Token, "0A1B2C3D") == 0 &&
          strncmp(rl.Hash, "692DB360", 8) == 0 &&
          strcmp(rl.Path, "C:\\XHCI98\\NONFREE\\a b\\c.inf") == 0, "rec: F");
    CHECK(inst_rec_parse("D 0A1B2C3D C:\\XHCI98\\NONFREE\\c.inf", &rl) == 1 &&
          rl.Kind == 'D' && strcmp(rl.Token, "0A1B2C3D") == 0, "rec: D");
    CHECK(inst_rec_parse("T 0A1B2C3D C:\\x", &rl) == 1 &&
          inst_rec_parse("A 0A1B2C3D C:\\x", &rl) == 1, "rec: T and A");
    CHECK(inst_rec_parse("D 0a1b2c3d C:\\x", &rl) == -1, "rec: a lower-case token");
    CHECK(inst_rec_parse("D C:\\x", &rl) == -1, "rec: no token");
    CHECK(inst_rec_parse("D 0A1B2C3D relative\\path", &rl) == -1,
          "rec: not a full path");
    CHECK(inst_rec_parse("X 0A1B2C3D C:\\x", &rl) == -1, "rec: an unknown kind");
    CHECK(inst_rec_parse("F 0A1B2C3D 692d C:\\x", &rl) == -1, "rec: a short hash");

    /* The page's texts. */
    CHECK(strcmp(inst_other_folder(INST_SET_FREE), "USBDRV\\FREE") == 0 &&
          strcmp(inst_other_folder(INST_SET_NONFREE), "USBDRV\\NONFREE") == 0,
          "texts: the folders");
    inst_other_sentence(text, sizeof(text), INST_SET_FREE, "C:\\XHCI98", 1);
    CHECK(strstr(text, "C:\\XHCI98\\USBDRV\\FREE. ") != NULL &&
          strstr(text, INST_FREE_NOTICE) != NULL &&
          strcmp(INST_FREE_NOTICE, "These drivers are experimental. They may "
                 "not work with every device and can make Windows unstable.")
          == 0 && strstr(text, "github") == NULL,
          "texts: the free sentence, the owner's words of 2026-10-23 "
          "ending at unstable (2026-11-06)");
    inst_other_sentence(text, sizeof(text), INST_SET_NONFREE, "C:\\XHCI98", 0);
    CHECK(strcmp(text, "Setup will copy all drivers to "
                 "C:\\XHCI98\\USBDRV\\NONFREE.") == 0 &&
          strstr(text, "checkboxes") == NULL,
          "texts: the non-free sentence on 98 SE and ME names no boxes");
    inst_other_sentence(text, sizeof(text), INST_SET_NONFREE, "C:\\XHCI98", 1);
    CHECK(strcmp(text, "Setup will copy all drivers to "
                 "C:\\XHCI98\\USBDRV\\NONFREE.") == 0 &&
          strstr(text, "makers") == NULL &&
          strstr(text, "tested") == NULL,
          "texts: the non-free sentence, no testing warning (2026-10-24)");
    inst_other_stage_text(text, sizeof(text), 0);
    CHECK(strcmp(text, "None of them supports this Windows version.") == 0,
          "texts: no INF for this Windows");
    inst_other_stage_text(text, sizeof(text), 37);
    CHECK(strcmp(text, "37 drivers will be staged for this Windows.") == 0,
          "texts: 37 INFs");
    inst_other_stage_text(text, sizeof(text), 1);
    CHECK(strcmp(text, "1 driver will be staged for this Windows.") == 0,
          "texts: one INF");
    inst_other_line(text, sizeof(text), INST_SET_NONFREE, INST_OTHER_CARRIED, 1,
                    "3.2.0.0");
    CHECK(strcmp(text, "Latest version: https://github.com/yeokm1/usb-wdm-nonfree.")
          == 0, "line: carried");
    /* Owner, 2026-11-06 (task 47.2): the notice has no address, so the
     * free set's line gives it, as the non-free set's does. */
    inst_other_line(text, sizeof(text), INST_SET_FREE, INST_OTHER_CARRIED, 1,
                    "3.2.0.0");
    CHECK(strcmp(text, "Latest version: https://github.com/yeokm1/usb-wdm-free.")
          == 0, "line: the free set carried gives its address");
    inst_other_line(text, sizeof(text), INST_SET_NONFREE, INST_OTHER_ABSENT, 1,
                    "3.2.0.0");
    CHECK(strcmp(text, "Not in this download. Get them from "
                 "xhci98-3.2.0.0-nonfree.zip or "
                 "https://github.com/yeokm1/usb-wdm-nonfree.") == 0,
          "line: non-free absent");
    inst_other_line(text, sizeof(text), INST_SET_FREE, INST_OTHER_ABSENT, 1,
                    "3.2.0.0");
    CHECK(strcmp(text, "Not in this download. Get them from "
                 "xhci98-3.2.0.0-free.zip or "
                 "https://github.com/yeokm1/usb-wdm-free.") == 0,
          "line: free absent");
    inst_other_line(text, sizeof(text), INST_SET_FREE, INST_OTHER_ABSENT, 0,
                    "3.2.0.0");
    CHECK(strcmp(text, "Not yet released for this version: see "
                 "https://github.com/yeokm1/usb-wdm-free.")
          == 0, "line: a release with no free set");
    inst_other_line(text, sizeof(text), INST_SET_NONFREE, INST_OTHER_NOTOURS, 1,
                    "3.2.0.0");
    CHECK(strstr(text, "The USBDRV\\NONFREE folder next to Setup does not "
                       "match this release") != NULL &&
          strstr(text, "xhci98-3.2.0.0-nonfree.zip or https://github.com/"
                       "yeokm1/usb-wdm-nonfree.") != NULL, "line: not ours");
    inst_other_line(text, sizeof(text), INST_SET_FREE, INST_OTHER_NOTOURS, 0,
                    "3.2.0.0");
    CHECK(strstr(text, "Get them from https://github.com/"
                       "yeokm1/usb-wdm-free.") != NULL &&
          strstr(text, ".zip") == NULL, "line: not ours, no free set");

    /* The warnings on Next. */
    CHECK(inst_other_warnings(0, 0, 0, 0, 1) == 0,
          "warn: nothing ticked");
    CHECK(inst_other_warnings(1, 1, 1, 1, 0) == 0,
          "warn: both ticked and staged, 32-bit: no testing warning");
    CHECK(inst_other_warnings(1, 0, 1, 0, 1) == 0,
          "warn: no x64 line without a stage box");
    w = inst_other_warnings(1, 1, 1, 0, 1);
    CHECK(w == INST_OW_X64SIG, "warn: the signature note alone");
    inst_other_warn_text(w, text, sizeof(text));
    CHECK(strncmp(text, "On 64-bit Windows, a staged driver", 34) == 0 &&
          strstr(text, "(F8)") != NULL && strstr(text, "tested") == NULL &&
          strstr(text, "non-free") == NULL, "warn: the words");
    inst_other_warn_text(0, text, sizeof(text));
    CHECK(text[0] == '\0', "warn: none, no box");
    /* The compiled record of each set: each this release's (the free set's
     * from the usb-wdm-free release scripts\package\wdmfree-pin.txt names). */
    CHECK(strlen(inst_other_sums(INST_SET_NONFREE)) == 64 &&
          strlen(inst_other_sums(INST_SET_FREE)) == 64, "sums: the record");
}

/* Task 43.73: the controller names (record 23 section 5.13, record 21
 * section 3.3). Every name of both tables is within INST_NAME_MAX (98 SE's
 * Device Manager, measured), ASCII, the compact form, one row per id; the
 * NHI table is exactly xhcicm.inf's and xhcicm-amd64.inf's model lines,
 * string for string; xhci98.inf's class line keeps the one plain name. */
static int name_ok(const char *s)
{
    size_t i;

    if (strlen(s) == 0 || strlen(s) > INST_NAME_MAX) {
        return 0;
    }
    for (i = 0; s[i] != '\0'; i++) {
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E ||
            s[i] == '"' || s[i] == '%' || s[i] == ';') {
            return 0;
        }
    }
    return 1;
}

static void names_table(const INST_NAMEROW *t, int n, int nhi)
{
    char name[INST_NAME_MAX + 8];
    char what[160];
    int i;
    int j;

    for (i = 0; i < n; i++) {
        int got = nhi ? inst_nhi_name(t[i].Ven, t[i].Dev, name, sizeof(name))
                      : inst_xhci_name(t[i].Ven, t[i].Dev, name, sizeof(name));
        sprintf(what, "%04lX:%04lX is named, within %d characters: '%s'",
                (unsigned long)t[i].Ven, (unsigned long)t[i].Dev,
                INST_NAME_MAX, name);
        CHECK(got == 1 && name_ok(name), what);
        CHECK(strncmp(name, "xHCI98 ", 7) == 0 &&
              strstr(name, "Host") == NULL && strstr(name, "Thunderbolt") == NULL,
              "the compact form: xHCI98 first, no Host, TB2/TB3");
        CHECK(nhi ? t[i].Base != INST_NB_USB3 : t[i].Base == INST_NB_USB3,
              "the row's base fits its table");
        CHECK(t[i].Product != NULL && strchr(t[i].Product, ' ') != NULL,
              "the product carries its maker");
        for (j = 0; j < i; j++) {
            if (t[j].Ven == t[i].Ven && t[j].Dev == t[i].Dev) {
                CHECK(0, "one row per id");
            }
        }
    }
}

/* The model lines of one NHI INF against the table: every line's string is
 * the table's name for its id (the class line the plain name), and every
 * row of the table is a line of each models section. */
/* The Thunderbolt 1 rows (design record 22 section 19.9): bound by their
 * class form in the NT 6.x sections alone, never in a section Windows 98
 * SE, ME, 2000 or XP reads. */
static int names_tb1(INST_U32 ven, INST_U32 dev)
{
    return ven == 0x8086UL && (dev == 0x1513UL || dev == 0x1547UL);
}

static void names_inf(const char *text, const char *section, const char *label)
{
    const char *p = inst_inf_section(text, section);
    char line[512];
    char key[128];
    char value[256];
    char id[128];
    char desc[256];
    char name[INST_NAME_MAX + 8];
    char what[256];
    int seen[256];
    int lines = 0;
    int nt6;
    int tb1;
    int i;

    memset(seen, 0, sizeof(seen));
    CHECK(p != NULL && instNhiNameCount <= 256, label);
    while (p != NULL && (p = inst_inf_line(p, line, sizeof(line))) != NULL) {
        INST_U32 vd;
        INST_U32 ven;
        INST_U32 dev;
        if (!inst_inf_kv(line, key, sizeof(key), value, sizeof(value)) ||
            !inst_inf_field(value, 1, id, sizeof(id))) {
            continue;
        }
        lines++;
        CHECK(inst_inf_expand(text, key, desc, sizeof(desc)), "the token is defined");
        if (inst_eq(id, "PCI\\CC_0C0340")) {
            CHECK(strcmp(desc, INST_NAME_USB4) == 0, "the class line: the plain name");
            continue;
        }
        CHECK(inst_pci_vendev(id, &vd), "an NHI line binds a PCI id");
        ven = vd & 0xFFFFUL;
        dev = vd >> 16;
        sprintf(what, "%s %s: '%s' is the table's name", label, id, desc);
        CHECK(inst_nhi_name(ven, dev, name, sizeof(name)) == 1 &&
              strcmp(desc, name) == 0 && name_ok(desc), what);
        for (i = 0; i < instNhiNameCount; i++) {
            if (instNhiNames[i].Ven == ven && instNhiNames[i].Dev == dev) {
                seen[i] = 1;
            }
        }
    }
    nt6 = strstr(section, ".6.0") != NULL;
    tb1 = 0;
    for (i = 0; i < instNhiNameCount; i++) {
        tb1 += names_tb1(instNhiNames[i].Ven, instNhiNames[i].Dev);
    }
    CHECK_EQ(lines, instNhiNameCount + 1 - (nt6 ? 0 : tb1),
             "one line per row, and the class line");
    for (i = 0; i < instNhiNameCount; i++) {
        sprintf(what, "%s binds %04lX:%04lX", label,
                (unsigned long)instNhiNames[i].Ven,
                (unsigned long)instNhiNames[i].Dev);
        if (names_tb1(instNhiNames[i].Ven, instNhiNames[i].Dev) && !nt6) {
            CHECK(!seen[i], "a Thunderbolt 1 row: no line before NT 6.x");
        } else {
            CHECK(seen[i], what);
        }
    }
}

/* Task 44.5: xhci98.inf's controller lines against the table. With ids
 * (the 32-bit file's undecorated section, which 98 SE and ME read): every
 * id line's string is the table's name for its id, every row of the table
 * a line, the class line the plain name. Without (the NT 6.x sections and
 * the 64-bit file): the class line alone. */
static void names_xhci_inf(const char *text, const char *section, int ids,
                           const char *label)
{
    const char *p = inst_inf_section(text, section);
    char line[512];
    char key[128];
    char value[256];
    char sect[128];
    char id[128];
    char desc[256];
    char name[INST_NAME_MAX + 8];
    char what[256];
    int seen[256];
    int lines = 0;
    int i;

    memset(seen, 0, sizeof(seen));
    CHECK(p != NULL && instXhciNameCount <= 256, label);
    while (p != NULL && (p = inst_inf_line(p, line, sizeof(line))) != NULL) {
        INST_U32 vd;
        INST_U32 ven;
        INST_U32 dev;
        if (!inst_inf_kv(line, key, sizeof(key), value, sizeof(value)) ||
            !inst_inf_field(value, 0, sect, sizeof(sect)) ||
            !inst_inf_field(value, 1, id, sizeof(id)) ||
            strncmp(sect, "Xhci.Dev", 8) != 0) {
            continue;
        }
        lines++;
        CHECK(inst_inf_expand(text, key, desc, sizeof(desc)), "the token is defined");
        sprintf(what, "%s %s: one id a line", label, id);
        CHECK(!inst_inf_field(value, 2, sect, sizeof(sect)), what);
        if (inst_eq(id, "PCI\\CC_0C0330")) {
            CHECK(strcmp(desc, INST_NAME_USB3) == 0, "the class line: the plain name");
            continue;
        }
        sprintf(what, "%s %s: an id line only where 98 SE and ME read", label, id);
        CHECK(ids, what);
        CHECK(inst_pci_vendev(id, &vd), "a controller line binds a PCI id");
        ven = vd & 0xFFFFUL;
        dev = vd >> 16;
        sprintf(what, "%s %s: '%s' is the table's name", label, id, desc);
        CHECK(inst_xhci_name(ven, dev, name, sizeof(name)) == 1 &&
              strcmp(desc, name) == 0 && name_ok(desc), what);
        for (i = 0; i < instXhciNameCount; i++) {
            if (instXhciNames[i].Ven == ven && instXhciNames[i].Dev == dev) {
                seen[i] = 1;
            }
        }
    }
    CHECK_EQ(lines, ids ? instXhciNameCount + 1 : 1,
             "one line per row and the class line, or the class line alone");
    for (i = 0; ids && i < instXhciNameCount; i++) {
        sprintf(what, "%s binds %04lX:%04lX", label,
                (unsigned long)instXhciNames[i].Ven,
                (unsigned long)instXhciNames[i].Dev);
        CHECK(seen[i], what);
    }
}

/* Task 44.55: a document's table of the USB 3 controllers (the page
 * controller-names.md, its one copy since the release notes left it,
 * owner 2026-11-13) against the table: every table row of that section
 * that names "xHCI98 USB 3 Controller (...)" lists its ids as
 * `VVVV:DDDD`, each the table's id for that name, and every row of the
 * table is listed once.
 * The controllers that virtual machines emulate are the exception (owner,
 * 2026-11-06): the user documents leave them out, and target-standing.md's
 * "The virtual machines' controllers in the name table" lists them and
 * nothing else (names_doc's vm = 1). */
static const INST_U32 namesVm[][2] = {
    { 0x15AD, 0x0778 }, { 0x15AD, 0x0779 }, { 0x1B36, 0x000D }
};

static int names_is_vm(INST_U32 ven, INST_U32 dev)
{
    size_t i;

    for (i = 0; i < sizeof(namesVm) / sizeof(namesVm[0]); i++) {
        if (namesVm[i][0] == ven && namesVm[i][1] == dev) {
            return 1;
        }
    }
    return 0;
}

static int hex4(const char *s, INST_U32 *out)
{
    INST_U32 v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (INST_U32)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            v |= (INST_U32)(c - 'A' + 10);
        } else {
            return 0;
        }
    }
    *out = v;
    return 1;
}

static void names_doc(const char *text, const char *heading, const char *label,
                      int vm)
{
    const char *s = text != NULL ? strstr(text, heading) : NULL;
    const char *end;
    const char *p;
    char line[1024];
    char name[INST_NAME_MAX + 8];
    char what[256];
    int seen[256];
    int rows = 0;
    int i;

    memset(seen, 0, sizeof(seen));
    sprintf(what, "%s: the section and its table", label);
    CHECK(s != NULL && instXhciNameCount <= 256, what);
    if (s == NULL) {
        return;
    }
    end = strstr(s + 1, "\n## ");
    if (end == NULL) {
        end = s + strlen(s);
    }
    for (p = s; p < end; ) {
        const char *eol = strchr(p, '\n');
        const char *nm;
        const char *q;
        size_t len;
        int ids = 0;

        if (eol == NULL || eol > end) {
            eol = end;
        }
        len = (size_t)(eol - p);
        if (len >= sizeof(line)) {
            len = sizeof(line) - 1;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        p = eol < end ? eol + 1 : end;
        nm = strstr(line, INST_NAME_USB3 " (");
        if (line[0] != '|' || nm == NULL) {
            continue;
        }
        rows++;
        q = strchr(nm, ')');
        CHECK(q != NULL && (size_t)(q - nm) + 1 < sizeof(name), "the row's name");
        if (q == NULL || (size_t)(q - nm) + 1 >= sizeof(name)) {
            continue;
        }
        memcpy(name, nm, (size_t)(q - nm) + 1);
        name[(q - nm) + 1] = '\0';
        for (q = line; (q = strchr(q, '`')) != NULL; q++) {
            INST_U32 ven;
            INST_U32 dev;
            char want[INST_NAME_MAX + 8];

            if (strlen(q) < 11 || q[5] != ':' || q[10] != '`') {
                continue;
            }
            /* An id-shaped token must be one: upper-case hex, as written. */
            sprintf(what, "%s: the row '%s': an id in upper-case hex", label, name);
            CHECK(hex4(q + 1, &ven) && hex4(q + 6, &dev), what);
            if (!hex4(q + 1, &ven) || !hex4(q + 6, &dev)) {
                q += 10;
                continue;
            }
            ids++;
            sprintf(what, vm ? "%s: %04lX:%04lX is a virtual machine's controller"
                             : "%s: %04lX:%04lX, a virtual machine's controller, is left out",
                    label, (unsigned long)ven, (unsigned long)dev);
            CHECK(names_is_vm(ven, dev) == vm, what);
            sprintf(what, "%s: %04lX:%04lX is the table's '%s'", label,
                    (unsigned long)ven, (unsigned long)dev, name);
            CHECK(inst_xhci_name(ven, dev, want, sizeof(want)) == 1 &&
                  strcmp(want, name) == 0, what);
            for (i = 0; i < instXhciNameCount; i++) {
                if (instXhciNames[i].Ven == ven && instXhciNames[i].Dev == dev) {
                    sprintf(what, "%s: %04lX:%04lX listed once", label,
                            (unsigned long)ven, (unsigned long)dev);
                    CHECK(!seen[i], what);
                    seen[i] = 1;
                }
            }
            q += 10;
        }
        sprintf(what, "%s: the row '%s' lists its ids", label, name);
        CHECK(ids > 0, what);
    }
    sprintf(what, "%s: a table of the USB 3 controllers", label);
    CHECK(rows > 0, what);
    for (i = 0; i < instXhciNameCount; i++) {
        if (names_is_vm(instXhciNames[i].Ven, instXhciNames[i].Dev) != vm) {
            continue;
        }
        sprintf(what, "%s lists %04lX:%04lX", label,
                (unsigned long)instXhciNames[i].Ven,
                (unsigned long)instXhciNames[i].Dev);
        CHECK(seen[i], what);
    }
}

static void test_names(void)
{
    char name[INST_NAME_MAX + 8];
    char small[12];
    char *cm = load("..\\src\\cm\\xhcicm.inf");
    char *c64 = load("..\\src\\cm\\xhcicm-amd64.inf");
    char *x86 = load("..\\src\\xhci98.inf");
    char *a64 = load("..\\src\\xhci98-amd64.inf");
    char *readme = load("..\\docs\\using\\controller-names.md");
    char *standing = load("..\\docs\\contributing\\target-standing.md");
    size_t vi;
    int rec;
    static const char hwQ[] = "PCI\\VEN_1B36&DEV_000D&SUBSYS_11001AF4&REV_01\0"
                              "PCI\\VEN_1B36&DEV_000D\0";
    static const char ccQ[] = "PCI\\CC_0C0330\0PCI\\CC_0C03\0";
    static const char hwN[] = "PCI\\VEN_8086&DEV_15BF&SUBSYS_22B117AA\0";
    static const char ccN[] = "PCI\\CC_088000\0";
    static const char hwU[] = "PCI\\VEN_ABCD&DEV_0001\0PCI\\CC_0C0330\0";

    names_table(instXhciNames, instXhciNameCount, 0);
    names_table(instNhiNames, instNhiNameCount, 1);
    CHECK(name_ok(INST_NAME_USB3) && name_ok(INST_NAME_USB4) &&
          name_ok(INST_NAME_TB2) && name_ok(INST_NAME_TB3), "the plain names");

    CHECK(inst_xhci_name(0x1B36UL, 0x000DUL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)") == 0,
          "QEMU's controller");
    CHECK(inst_xhci_name(0x1B21UL, 0x3242UL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 USB 3 Controller (ASMedia ASM3242)") == 0,
          "the owner's example");
    CHECK(inst_xhci_name(0x1B21UL, 0x1142UL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 USB 3 Controller (ASMedia ASM1042A)") == 0,
          "1142 is the ASM1042A (Linux xhci-pci.c), 1242 the ASM1142");
    CHECK(inst_xhci_name(0x8086UL, 0x15C1UL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 USB 3 Controller (Intel Alpine Ridge)") == 0,
          "the emulator's Alpine Ridge xHCI");
    /* Task 44.55: the ASM4242's and AMD's ids, 2026-10-25. */
    CHECK(inst_xhci_name(0x1B21UL, 0x2426UL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 USB 3 Controller (ASMedia ASM4242)") == 0,
          "the ASM4242's xHCI");
    /* Task 45.46: the ASM4242's router named by its own INF line (the
     * table names_inf holds both NHI INFs to). */
    CHECK(inst_nhi_name(0x1B21UL, 0x2425UL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 USB4 Router (ASMedia ASM4242)") == 0,
          "the ASM4242's router: its INF line's name");
    CHECK(inst_nhi_name(0x1B21UL, 0x2142UL, name, sizeof(name)) == 0 &&
          strcmp(name, INST_NAME_USB4) == 0,
          "an NHI id the table lacks: the class line's plain name");
    {
        /* The FriendlyName 3.3.0.0's installer wrote on that router: an NHI
         * has no table name for a FriendlyName now (names_canon NULL), so
         * the update clears the recorded one, and a user's own stays. */
        int r;
        CHECK(inst_name_action(1, NULL, "xHCI98 USB4 Router (ASMedia ASM4242)",
                               "xHCI98 USB4 Router (ASMedia ASM4242)", &r) ==
                  INST_NAME_CLEAR && !r,
              "task 45.46: the router's recorded FriendlyName cleared at an update");
        CHECK(inst_name_action(1, NULL, "My router", NULL, &r) ==
                  INST_NAME_NONE && !r,
              "task 45.46: a user's name for the router stays");
    }
    {
        static const char hwA[] = "PCI\\VEN_1B21&DEV_2425&SUBSYS_24211B21\0";
        static const char ccA[] = "PCI\\CC_0C0340\0";
        CHECK(inst_dev_canon(hwA, sizeof(hwA), ccA, sizeof(ccA), "xhcicm", name,
                             sizeof(name)) &&
              strcmp(name, "xHCI98 USB4 Router (ASMedia ASM4242)") == 0,
              "XHCITR: the ASM4242's router under xhcicm");
    }
    CHECK(inst_xhci_name(0x1022UL, 0x43FDUL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 USB 3 Controller (AMD 800 Series)") == 0 &&
          inst_xhci_name(0x1022UL, 0x15B7UL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 USB 3 Controller (AMD Raphael)") == 0 &&
          inst_xhci_name(0x1022UL, 0x15B8UL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 USB 3 Controller (AMD Raphael USB 2)") == 0,
          "the AMD chipset and processor controllers");
    CHECK(inst_xhci_name(0xABCDUL, 0x0001UL, name, sizeof(name)) == 0 &&
          strcmp(name, INST_NAME_USB3) == 0, "an id the table lacks: the plain name");
    CHECK(inst_nhi_name(0x8086UL, 0x15BFUL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 TB3 Controller (Intel Alpine Ridge)") == 0,
          "the captured NHI");
    CHECK(inst_nhi_name(0x8086UL, 0x156AUL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 TB2 Controller (Intel Falcon Ridge)") == 0,
          "Falcon Ridge is TB2");
    CHECK(inst_nhi_name(0x8086UL, 0x7EC2UL, name, sizeof(name)) == 1 &&
          strcmp(name, "xHCI98 USB4 Router (Intel Meteor Lake)") == 0,
          "the owner's example");
    CHECK(inst_nhi_name(0x1B36UL, 0x00F4UL, name, sizeof(name)) == 0 &&
          strcmp(name, "xHCI98 USB4 Router") == 0, "any other USB4 host: the plain name");
    CHECK(inst_xhci_name(0x1B36UL, 0x000DUL, small, sizeof(small)) == 0 &&
          small[0] == '\0', "a buffer too small is left empty");

    CHECK(inst_dev_canon(hwQ, sizeof(hwQ), ccQ, sizeof(ccQ), "xhci98", name,
                         sizeof(name)) &&
          strcmp(name, "xHCI98 USB 3 Controller (Red Hat QEMU xHCI)") == 0,
          "ours: the canonical name");
    CHECK(!inst_dev_canon(hwQ, sizeof(hwQ), ccQ, sizeof(ccQ), "usbxhci", name,
                          sizeof(name)) && name[0] == '\0',
          "Windows' own driver: no name of ours");
    CHECK(!inst_dev_canon(hwQ, sizeof(hwQ), ccN, sizeof(ccN), "xhci98", name,
                          sizeof(name)), "xhci98 on no xHCI class: not a controller");
    CHECK(inst_dev_canon(hwN, sizeof(hwN), ccN, sizeof(ccN), "xhcicm", name,
                         sizeof(name)) &&
          strcmp(name, "xHCI98 TB3 Controller (Intel Alpine Ridge)") == 0,
          "the NHI under xhcicm");
    CHECK(inst_dev_canon(hwU, sizeof(hwU), "", 0, "xhci98", name, sizeof(name)) &&
          strcmp(name, INST_NAME_USB3) == 0, "ours, unknown id: the plain name");

    /* The reconciliation (record 23 section 5.13). */
    CHECK(inst_name_action(1, "A (x)", "", NULL, &rec) == INST_NAME_SET && rec,
          "ours, no name: set and recorded");
    CHECK(inst_name_action(1, "A (x)", "A (x)", "A (x)", &rec) == INST_NAME_NONE &&
          rec, "ours, already ours: nothing, still recorded");
    CHECK(inst_name_action(1, "A (x)", "A (x)", NULL, &rec) == INST_NAME_NONE &&
          rec, "the same name with no record: adopted");
    CHECK(inst_name_action(1, "A (y)", "A (x)", "A (x)", &rec) == INST_NAME_SET &&
          rec, "the table's name changed: set again");
    CHECK(inst_name_action(1, "A (x)", "My USB", NULL, &rec) == INST_NAME_NONE &&
          !rec, "a rename stays, not recorded");
    CHECK(inst_name_action(1, "A (x)", "My USB", "A (x)", &rec) == INST_NAME_NONE &&
          !rec, "a rename after ours stays, the record forgotten");
    CHECK(inst_name_action(1, NULL, "A (x)", "A (x)", &rec) == INST_NAME_CLEAR &&
          !rec, "an id the table no longer holds: ours cleared");
    CHECK(inst_name_action(0, "A (x)", "A (x)", "A (x)", &rec) == INST_NAME_CLEAR &&
          !rec, "another driver now: ours cleared");
    CHECK(inst_name_action(0, "A (x)", "Vendor name", "A (x)", &rec) ==
          INST_NAME_NONE && !rec, "another driver's own name stays");
    CHECK(inst_name_action(0, NULL, "A (x)", NULL, &rec) == INST_NAME_NONE && !rec,
          "never recorded: never touched");
    CHECK(inst_name_action(0, NULL, "", "A (x)", &rec) == INST_NAME_NONE && !rec,
          "already gone: forgotten");

    CHECK(cm != NULL && c64 != NULL && x86 != NULL && a64 != NULL,
          "the NHI and controller INFs read");
    if (cm != NULL && c64 != NULL) {
        names_inf(cm, "CmModels", "xhcicm.inf [CmModels]");
        names_inf(cm, "CmModels.NTx86.6.0", "xhcicm.inf [CmModels.NTx86.6.0]");
        names_inf(c64, "CmModels.NTamd64", "xhcicm-amd64.inf [CmModels.NTamd64]");
        names_inf(c64, "CmModels.NTamd64.6.0",
                  "xhcicm-amd64.inf [CmModels.NTamd64.6.0]");
    }
    if (x86 != NULL && a64 != NULL) {
        CHECK(inst_inf_expand(x86, "%XhciDesc%", name, sizeof(name)) &&
              strcmp(name, INST_NAME_USB3) == 0 &&
              inst_inf_expand(a64, "%XhciDesc%", name, sizeof(name)) &&
              strcmp(name, INST_NAME_USB3) == 0,
              "xhci98.inf's class line: the one plain name");
        names_xhci_inf(x86, "XhciModels", 1, "xhci98.inf [XhciModels]");
        names_xhci_inf(x86, "XhciModels.NTx86.6.0", 0,
                       "xhci98.inf [XhciModels.NTx86.6.0]");
        names_xhci_inf(a64, "XhciModels.NTamd64", 0,
                       "xhci98-amd64.inf [XhciModels.NTamd64]");
        names_xhci_inf(a64, "XhciModels.NTamd64.6.0", 0,
                       "xhci98-amd64.inf [XhciModels.NTamd64.6.0]");
    }
    for (vi = 0; vi < sizeof(namesVm) / sizeof(namesVm[0]); vi++) {
        CHECK(inst_xhci_name(namesVm[vi][0], namesVm[vi][1], name,
                             sizeof(name)) == 1,
              "each virtual machine's controller is in the table");
    }
    names_doc(readme, "# Controller names", "controller-names.md", 0);
    names_doc(standing, "## The virtual machines' controllers in the name table",
              "target-standing.md", 1);
    free(cm);
    free(c64);
    free(x86);
    free(a64);
    free(readme);
    free(standing);
}

/* Record 26 section 6.7.11 (draft 37): the driver packages of the other
 * sets, their words, their relations, the switches. */
static void test_other_pkgs(void)
{
    static const char *const nonfreePin[] = {
        "ethernet/ax88179", "ethernet/ax88772", "ethernet/rtl815x",
        "hid/x360c", "hid/x360wc", "serial/ch341", "serial/ft2232d",
        "serial/ftdi", "smartcard/usbccid"
    };
    static const char *const freeReadme[] = {
        "video/uvc98", "audio/uac2", "audio/usbmidi", "net/cdcnet",
        "net/rtl815x", "net/ax88179", "serial/usbcom", "hid/xpad98",
        "storage/usbflop", "generic/libusb0", "print/usbprn", "audio/uac1",
        "net/ax88772", "net/sr9700", "hid/absptr"
    };
    char ids[INST_PKG_MAX * INST_PKG_ID_CHARS];
    char pkg[INST_PKG_ID_CHARS];
    INST_STAGE_META m;
    INST_STAGE_LINE sl;
    INST_OPTS o;
    char err[256];
    char *argv1[4];
    int i;
    int j;
    int bad = 0;

    for (i = 0; i < instOtherPkgCount; i++) {
        const INST_OTHER_PKG *r = &instOtherPkgs[i];
        if (!inst_other_desc_ok(r->Kind, r->Desc) ||
            inst_other_heading_index(r->Heading) < 0 ||
            !inst_eq(instOtherHeadings[inst_other_heading_index(r->Heading)],
                     r->Heading)) {
            printf("package row %d (%s): %s\n", i, r->Id, r->Desc);
            bad = 1;
        }
        for (j = 0; j < i; j++) {
            if (instOtherPkgs[j].Set == r->Set &&
                inst_eq(instOtherPkgs[j].Id, r->Id)) {
                bad = 1;
            }
        }
        if (strstr(r->Desc, r->Id + (strchr(r->Id, '/') - r->Id) + 1) ==
            NULL) {
            printf("package row %s does not name its package\n", r->Id);
            bad = 1;
        }
    }
    CHECK(!bad, "every package row: its words by the rules, its heading "
          "listed, its id once, its package named in its words");
    for (i = 0; i < (int)(sizeof(nonfreePin) / sizeof(nonfreePin[0])); i++) {
        CHECK(inst_other_pkg(INST_SET_NONFREE, nonfreePin[i]) != NULL,
              nonfreePin[i]);
    }
    for (i = 0; i < (int)(sizeof(freeReadme) / sizeof(freeReadme[0])); i++) {
        CHECK(inst_other_pkg(INST_SET_FREE, freeReadme[i]) != NULL,
              freeReadme[i]);
    }
    CHECK(inst_other_pkg(INST_SET_FREE, "ethernet/ax88179") == NULL &&
          inst_other_pkg(INST_SET_NONFREE, "net/ax88179") == NULL,
          "a row belongs to its set");
    CHECK(strstr(inst_other_pkg(INST_SET_FREE, "video/uvc98")->Desc,
                 "USB Video Class 1.0 and 1.5") != NULL &&
          strstr(inst_other_pkg(INST_SET_FREE, "audio/uac2")->Desc,
                 "USB Audio Class 2.0") != NULL &&
          strstr(inst_other_pkg(INST_SET_FREE, "audio/uac1")->Desc,
                 "USB Audio Class 1.0") != NULL &&
          strstr(inst_other_pkg(INST_SET_FREE, "audio/usbmidi")->Desc,
                 "USB MIDI 1.0") != NULL,
          "the class drivers name their class versions (owner, 2026-10-23)");
    CHECK(inst_eq(inst_other_pkg(INST_SET_FREE, "hid/xpad98")->Desc,
                  "Game pads (xpad98): for Xbox 360 and Xbox One "
                  "controllers") &&
          inst_other_pkg(INST_SET_FREE, "hid/xpad98")->Kind == INST_PKG_DEVICE,
          "xpad98: the owner's compatibility wording, a DEVICE row");
    CHECK(!inst_other_desc_ok(INST_PKG_CLASS, "USB audio (uac2): sound cards "
                              "such as the Creative X4") &&
          inst_other_desc_ok(INST_PKG_DEVICE, "USB Ethernet (x): adapters "
                             "with an ASIX AX88179 chip") &&
          !inst_other_desc_ok(INST_PKG_CLASS, "USB Ethernet (x): adapters "
                              "with an ASIX AX88179 chip") &&
          inst_other_desc_ok(INST_PKG_CLASS, "Webcams (uvc98): USB Video "
                             "Class cameras, Asixth") &&
          !inst_other_desc_ok(INST_PKG_CLASS, "a | b") &&
          !inst_other_desc_ok(INST_PKG_CLASS, "") &&
          !inst_other_desc_ok(INST_PKG_DEVICE, "0123456789012345678901234567"
                              "8901234567890123456789012345678901234567890123"
                              "4567890"),
          "the words' rules: a brand refused in a class driver's, a chip "
          "family allowed in a device driver's, whole words only, no '|', "
          "not empty, at most 80 characters");
    inst_other_pkg_of("audio/uac2/w98se/uac2.inf", pkg, sizeof(pkg));
    CHECK(inst_eq(pkg, "audio/uac2"), "a path's package: its first two parts");
    CHECK(!inst_other_excludes(INST_SET_FREE, "audio/uac1", INST_SET_FREE,
                               "audio/usbmidi", 0) &&
          inst_other_excludes(INST_SET_FREE, "audio/uac1", INST_SET_FREE,
                              "audio/usbmidi", 1) &&
          inst_other_excludes(INST_SET_FREE, "audio/usbmidi", INST_SET_FREE,
                              "audio/uac1", 7) &&
          inst_other_excludes(INST_SET_NONFREE, "hid/x360wc", INST_SET_FREE,
                              "hid/xpad98", 0) &&
          !inst_other_excludes(INST_SET_FREE, "hid/xpad98", INST_SET_NONFREE,
                               "hid/x360c", 0) &&
          !inst_other_excludes(INST_SET_FREE, "audio/uac2", INST_SET_FREE,
                               "audio/usbmidi", 0) &&
          !inst_other_excludes(INST_SET_FREE, "audio/uac1", INST_SET_FREE,
                               "audio/usbmidi", -1),
          "EXCLUDES both ways and across the sets; COEXISTS bars nothing; "
          "uac1 and usbmidi side by side on 98 SE alone (task 47.42)");
    CHECK(inst_other_id_list("audio/uac2,Video/UVC98,audio/uac2", ids,
                             INST_PKG_MAX) == 2 &&
          inst_eq(ids, "audio/uac2") &&
          inst_eq(ids + INST_PKG_ID_CHARS, "video/uvc98") &&
          inst_other_id_list("audio", ids, INST_PKG_MAX) == -1 &&
          inst_other_id_list("audio/uac2/x", ids, INST_PKG_MAX) == -1 &&
          inst_other_id_list("audio/uac2,", ids, INST_PKG_MAX) == -1 &&
          inst_other_id_list("", ids, INST_PKG_MAX) == -1 &&
          inst_other_id_list("free:audio/uac2", ids, INST_PKG_MAX) == -1 &&
          inst_other_id_list("a/b,c/d", ids, 1) == -1,
          "a stage list: ids folded to lower case, once each; a bad form or "
          "too many refused");
    CHECK(inst_stage_meta("PACKAGE | audio/uac2 | CLASS | USB audio (uac2): "
                          "USB Audio Class 2.0 sound cards and DACs", &m) == 1 &&
          m.Kind == 'P' && inst_eq(m.A, "audio/uac2") &&
          inst_eq(m.B, "CLASS"), "a PACKAGE line read");
    CHECK(inst_stage_meta("PACKAGE | audio/uac2 | CLASS | sound cards like "
                          "the Creative X4", &m) == -1 &&
          inst_stage_meta("PACKAGE | audio/uac2 | WIDGET | x", &m) == -1 &&
          inst_stage_meta("NEEDS | audio/uac2 | free:audio/x | why", &m) == -1,
          "a brand in a CLASS line, an unknown kind and a NEEDS line refused");
    CHECK(inst_stage_meta("EXCLUDES | free:audio/uac1 | nonfree:hid/x360c | "
                          "W98SE WME | why", &m) == 1 && m.Kind == 'E' &&
          m.Systems == 3UL &&
          inst_stage_meta("COEXISTS | free:a/b | free:c/d | W7 | x", &m) == 1 &&
          m.Kind == 'C' &&
          inst_stage_meta("EXCLUDES | audio/uac1 | free:c/d | W7 | x", &m) ==
          -1,
          "EXCLUDES and COEXISTS read, their ids set-qualified");
    CHECK(inst_stage_meta("STAGE | W98SE | free/a/b/c.inf | x", &m) == 0 &&
          inst_stage_parse("PACKAGE | a/b | CLASS | USB floppy (b): UFI "
                           "floppy drives", "free", &sl) == 0 &&
          inst_stage_parse("NEEDS | a/b | free:c/d | x", "free", &sl) == -1,
          "the STAGE/COPY reader skips the metadata lines and refuses "
          "others");
    {
        char wt[1024];
        INST_U32 w = inst_other_warnings(0, 0, 1, 1, 2);
        CHECK((w & INST_OW_X64STORE) && !(w & INST_OW_X64SIG) &&
              (inst_other_warnings(0, 0, 1, 1, 1) & INST_OW_X64SIG) &&
              !(inst_other_warnings(0, 0, 1, 0, 2) & INST_OW_X64STORE),
              "64-bit 8 to 10: the store's words, not Vista's and 7's; only "
              "with a package staged");
        inst_other_warn_text(INST_OW_X64STORE, wt, sizeof(wt));
        CHECK(strstr(wt, "8 to 10") != NULL && strstr(wt, "F8") == NULL,
              "the 8 to 10 words");
    }
    CHECK(inst_stage_meta("EXCLUDES | free:a/b | free:c/d | W7 | one device",
                          &m) == 1 && inst_eq(m.Desc, "one device"),
          "a relation's why kept");
    argv1[0] = "x";
    argv1[1] = "/wdmfree:stage=audio/uac2,video/uvc98";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 &&
          o.Free && o.StageCount[INST_SET_FREE] == 2 &&
          inst_eq(o.StageIds[INST_SET_FREE] + INST_PKG_ID_CHARS,
                  "video/uvc98"),
          "/wdmfree:stage= names packages and implies the copy");
    argv1[1] = "/wdmnonfree:copy";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 && o.NonFree &&
          o.StageCount[INST_SET_NONFREE] == 0, "/wdmnonfree:copy");
    argv1[1] = "/wdmfree:trust";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 && o.FreeTrust &&
          o.Free, "/wdmfree:trust (its conditions are the read's)");
    argv1[1] = "/free";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 && o.Free,
          "/free kept as /wdmfree:copy");
    argv1[1] = "/freestage";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 1 &&
          strstr(err, "/wdmfree:stage=") != NULL,
          "/freestage refused with the new form named");
    argv1[1] = "/nonfreestage";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 1 &&
          strstr(err, "/wdmnonfree:stage=") != NULL, "/nonfreestage refused");
    argv1[1] = "/wdmnonfree:stage=hid/x360c";
    argv1[2] = "/wdmnonfree:stage=hid/x360wc";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1,
          "a stage switch twice refused");
    argv1[1] = "/wdmfree:stage=uac2";
    CHECK(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 1,
          "a stage list of a bad form refused");
    argv1[1] = "/wdmfree:stage=audio/uac2";
    argv1[2] = "/uninstall";
    CHECK(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1,
          "/wdmfree: belongs to an install");
    {
        /* As COMMAND.COM hands them on from INSTALL.BAT: split at '='
         * and at ','. */
        const char *s1[] = { "x", "/list", "/wdmfree:stage", "net/cdcnet" };
        const char *s2[] = { "x", "/WDMFREE:STAGE", "audio/uac2",
                             "video/uvc98" };
        const char *s3[] = { "x", "/wdmnonfree:stage", "hid/x360c",
                             "hid/x360wc", "net/ax88772" };
        const char *s4[] = { "x", "/wdmfree:stage", "net/cdcnet", "/list" };
        const char *s5[] = { "x", "/wdmfree:stage", "/list" };
        const char *s6[] = { "x", "/wdmfree:stage", "cdcnet" };
        const char *s7[] = { "x", "/wdmfree:stage=audio/uac2,video/uvc98",
                             "/list" };
        const char *s8[] = { "x", "/wdmfree:stage", "net/cdcnet",
                             "/wdmfree:stage=audio/uac2" };
        CHECK(parse(4, s1, &o) == 0 && o.List && o.Free &&
              o.StageCount[INST_SET_FREE] == 1 &&
              inst_eq(o.StageIds[INST_SET_FREE], "net/cdcnet"),
              "a split /wdmfree:stage= of one id rejoined");
        CHECK(parse(4, s2, &o) == 0 && o.StageCount[INST_SET_FREE] == 2 &&
              inst_eq(o.StageIds[INST_SET_FREE], "audio/uac2") &&
              inst_eq(o.StageIds[INST_SET_FREE] + INST_PKG_ID_CHARS,
                      "video/uvc98"),
              "a split list of two ids rejoined, any case");
        CHECK(parse(5, s3, &o) == 0 && o.NonFree && !o.Free &&
              o.StageCount[INST_SET_NONFREE] == 3 &&
              inst_eq(o.StageIds[INST_SET_NONFREE] + 2 * INST_PKG_ID_CHARS,
                      "net/ax88772"),
              "a split /wdmnonfree:stage= list of three ids rejoined");
        CHECK(parse(4, s4, &o) == 0 && o.List &&
              o.StageCount[INST_SET_FREE] == 1,
              "a switch after a split id is not swallowed");
        CHECK(parse(3, s5, &o) == 1, "/wdmfree:stage with no id refused");
        CHECK(parse(3, s6, &o) == 1, "a word that is no id is not taken");
        CHECK(parse(3, s7, &o) == 0 && o.List &&
              o.StageCount[INST_SET_FREE] == 2,
              "the unsplit = form unchanged");
        CHECK(parse(4, s8, &o) == 1, "split and unsplit forms twice refused");
    }
    {
        char pnf[64];
        CHECK(inst_other_pnf("C:\\XHCI98\\USBDRV\\FREE\\serial\\usbcom\\"
                             "w98se-me\\USBCOM.INF", pnf, sizeof(pnf)) == 1 &&
              inst_eq(pnf, "C:\\XHCI98\\USBDRV\\FREE\\serial\\usbcom\\"
                      "w98se-me\\USBCOM.PNF"),
              "a removed staged INF's .PNF named beside it");
        CHECK(inst_other_pnf("C:\\D\\usbcomp.inf", pnf, sizeof(pnf)) == 1 &&
              inst_eq(pnf, "C:\\D\\usbcomp.PNF"), "any case of .inf");
        CHECK(inst_other_pnf("C:\\D\\USBCOM.SYS", pnf, sizeof(pnf)) == 0 &&
              inst_other_pnf("C:\\D\\README.TXT", pnf, sizeof(pnf)) == 0 &&
              inst_other_pnf("C:\\D\\x.pnf", pnf, sizeof(pnf)) == 0 &&
              inst_other_pnf("C:\\D\\.inf", pnf, sizeof(pnf)) == 0 &&
              inst_other_pnf(".inf", pnf, sizeof(pnf)) == 0,
              "no .PNF for a file that is not an INF");
        CHECK(inst_other_pnf("C:\\D\\USBCOM.INF", pnf, 15) == 0 &&
              inst_other_pnf("C:\\D\\USBCOM.INF", pnf, 16) == 1,
              "a .PNF path that does not fit refused");
    }
    CHECK(inst_other_trust_plan(0, 0) == NULL &&
          inst_other_trust_plan(0, 1) == NULL,
          "no certificate line where the offer is not made");
    CHECK(strstr(inst_other_trust_plan(1, 1), "added to the Root and "
                 "TrustedPublisher stores") != NULL &&
          strstr(inst_other_trust_plan(1, 1), INST_WDMFREE_CER) != NULL,
          "/wdmfree:trust's plan line names the certificate and the stores");
    CHECK(strstr(inst_other_trust_plan(1, 0), "not added") != NULL,
          "the certificate left out said");
}

/* Task 45.60 (record 26 section 5.3.5): the Thunderbolt and USB4 page's
 * texts, the networking box's rule and its switches. */
/* Record 26 section 5.10 (task 45.66): Help on every page. */
static int help_has_ci(const char *text, const char *word)
{
    size_t n = strlen(word);
    const char *p;

    for (p = text; *p != '\0'; p++) {
        size_t i;
        for (i = 0; i < n; i++) {
            char a = p[i];
            char b = word[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a == '\0' || a != b) {
                break;
            }
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

/* The label's words before its first " (", the way a help line quotes a
 * label that goes on with a file name. */
static void help_label_head(const char *label, char *out, size_t cap)
{
    const char *par = strstr(label, " (");
    size_t n = par != NULL ? (size_t)(par - label) : strlen(label);

    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, label, n);
    out[n] = '\0';
}

/* A "\n" with no other beside it: a paragraph break that is no empty
 * line. */
static int help_lone_nl(const char *t)
{
    const char *p;

    for (p = t; *p != '\0'; p++) {
        if (*p == '\n' && p[1] != '\n' && (p == t || p[-1] != '\n')) {
            return 1;
        }
    }
    return 0;
}

static size_t help_paras(const char *t)
{
    size_t n = 1;
    const char *p = t;

    while ((p = strstr(p, "\n\n")) != NULL) {
        n++;
        p += 2;
    }
    return n;
}

static void test_help(void)
{
    /* Each topic's paragraphs, in INST_HELP_* order. */
    static const size_t paras[INST_HELP_COUNT] = {
        3, 3, 10, 8, 4, 3, 7, 8, 5, 3, 3, 4, 3, 2, 3, 3, 1, 3, 2
    };
    static const char *const banned[] = {
        "tested", "testing", "emulat", "virtual machine", "experimental"
    };
    static const struct {
        int Kind;
        int Un;
        int Var;
        int Topic;
    } routes[] = {
        { INST_HK_WELCOME, 0, 0, INST_HELP_WELCOME },
        { INST_HK_WELCOME, 1, 0, INST_HELP_UWELCOME },
        { INST_HK_COMP, 0, 0, INST_HELP_COMP },
        { INST_HK_TBT, 0, 0, INST_HELP_TBT },
        { INST_HK_FREE, 0, 0, INST_HELP_FREE },
        { INST_HK_NONFREE, 0, 0, INST_HELP_NONFREE },
        { INST_HK_SET_USB, 0, 0, INST_HELP_SET_USB },
        { INST_HK_SET_TB, 0, 0, INST_HELP_SET_TB },
        { INST_HK_SET_S98, 0, 0, INST_HELP_SET_S98 },
        { INST_HK_ADV, 0, 0, INST_HELP_ADV },
        { INST_HK_ADV_S98, 0, 0, INST_HELP_ADV_S98 },
        { INST_HK_SUMMARY, 0, 0, INST_HELP_SUMMARY },
        { INST_HK_SUMMARY, 1, 0, INST_HELP_USUMMARY },
        { INST_HK_PROGRESS, 0, INST_PV_NT, INST_HELP_PROGRESS },
        { INST_HK_PROGRESS, 1, INST_PV_NT, INST_HELP_PROGRESS },
        { INST_HK_PROGRESS, 0, INST_PV_9X, INST_HELP_PROGRESS_9X },
        { INST_HK_PROGRESS, 1, INST_PV_9X, INST_HELP_PROGRESS_9X },
        { INST_HK_STEPS, 0, 0, INST_HELP_STEPS },
        { INST_HK_FINISH, 0, INST_FV_RESTART, INST_HELP_FINISH },
        { INST_HK_FINISH, 0, INST_FV_NONE, INST_HELP_FINISH_NONE },
        { INST_HK_FINISH, 1, INST_FV_RESTART, INST_HELP_UFINISH },
        { INST_HK_FINISH, 1, INST_FV_NONE, INST_HELP_FINISH_NONE }
    };
    /* Each topic quotes the page's own labels: whole, or (Whole 0) their
     * words before the first parenthesis. */
    static const struct {
        int Topic;
        int Pt;
        int Whole;
    } labels[] = {
        { INST_HELP_COMP, INST_PT_DRIVERS, 0 },
        { INST_HELP_COMP, INST_PT_UAS, 0 },
        { INST_HELP_COMP, INST_PT_STOR, 0 },
        { INST_HELP_COMP, INST_PT_TRAY, 1 },
        { INST_HELP_COMP, INST_PT_FAST, 1 },
        { INST_HELP_TBT, INST_PT_PFW, 0 },
        { INST_HELP_TBT, INST_PT_S98, 0 },
        { INST_HELP_TBT, INST_PT_APPR_1, 1 },
        { INST_HELP_TBT, INST_PT_APPR_0, 1 },
        { INST_HELP_FINISH, INST_PT_NOW, 1 },
        { INST_HELP_FINISH, INST_PT_LATER, 1 },
        { INST_HELP_UFINISH, INST_PT_NOW, 1 },
        { INST_HELP_UFINISH, INST_PT_LATER, 1 }
    };
    static const struct {
        int Topic;
        const char *Words;
    } quotes[] = {
        { INST_HELP_COMP, "Replace the driver files Windows has stored for "
                          "this package" },
        /* Help names the box without its "Experimental" (owner,
         * 2026-11-02, task 46.18): the ban below holds. */
        { INST_HELP_COMP, "Thunderbolt / USB4 support" },
        { INST_HELP_COMP, "Settings..." },
        { INST_HELP_COMP, "Change folder..." },
        { INST_HELP_FREE, "Trust usb-wdm-free's 64-bit test certificate" },
        { INST_HELP_SET_USB, "XhciSilentInstall" },
        { INST_HELP_SET_USB, "XhciIntelPortSwitch" },
        { INST_HELP_SET_USB, "More >" },
        { INST_HELP_SET_USB, "Advanced..." },
        { INST_HELP_SET_TB, "XhciCmMode" },
        { INST_HELP_SET_TB, "XhciCmSafeEject" },
        { INST_HELP_SET_TB, "XhciCmAsm4242Structural" },
        { INST_HELP_SET_S98, "storwritethrough" },
        { INST_HELP_SET_S98, "storpollmode" },
        { INST_HELP_ADV_S98, "Value" },
        { INST_HELP_ADV, "Setting" },
        { INST_HELP_STEPS, "Save as text file..." },
        { INST_HELP_STEPS, "Open Device Manager" },
        { INST_HELP_TBT, "XHCIPFW.VXD on 98 SE and ME" },
        { INST_HELP_TBT, "XHCIPFW.SYS on 2000 to the 32-bit 8.1" },
        { INST_HELP_PROGRESS, "Found New Hardware" },
        { INST_HELP_PROGRESS_9X, "Add New Hardware Wizard" }
    };
    int reached[INST_HELP_COUNT];
    char head[128];
    char msg[160];
    int i;

    memset(reached, 0, sizeof(reached));
    for (i = 0; i < INST_HELP_COUNT; i++) {
        const char *t = inst_help_text(i);
        const unsigned char *p;
        size_t b;
        int ascii = 1;
        sprintf(msg, "help topic %d: a title and a text", i);
        CHECK(inst_help_title(i)[0] != '\0' && t[0] != '\0', msg);
        for (p = (const unsigned char *)t; *p != '\0'; p++) {
            if (*p > 126 || (*p < 32 && *p != '\n')) {
                ascii = 0;
            }
        }
        sprintf(msg, "help topic %d: ASCII, lines by \\n alone", i);
        CHECK(ascii, msg);
        /* A paragraph a choice, one empty line between paragraphs, none
         * at either end (owner, 2026-10-31); a text past the window's
         * rows scrolls there, and stays within two windows. */
        sprintf(msg, "help topic %d: paragraphs apart by one empty line", i);
        CHECK(t[0] != '\n' && t[strlen(t) - 1] != '\n' &&
              strstr(t, "\n\n\n") == NULL && help_lone_nl(t) == 0, msg);
        sprintf(msg, "help topic %d: %lu paragraphs, %lu pinned", i,
                (unsigned long)help_paras(t), (unsigned long)paras[i]);
        CHECK(help_paras(t) == paras[i], msg);
        sprintf(msg, "help topic %d: %lu lines at %lu columns, at most %lu",
                i, (unsigned long)inst_text_lines(t,
                                                  inst_page_cols(INST_HELP_W)),
                (unsigned long)inst_page_cols(INST_HELP_W),
                (unsigned long)(2 * inst_page_rows(INST_HELP_H)));
        CHECK(inst_text_lines(t, inst_page_cols(INST_HELP_W)) <=
              2 * inst_page_rows(INST_HELP_H), msg);
        for (b = 0; b < sizeof(banned) / sizeof(banned[0]); b++) {
            sprintf(msg, "help topic %d: no \"%s\"", i, banned[b]);
            CHECK(!help_has_ci(t, banned[b]), msg);
        }
    }
    CHECK(inst_help_text(-1)[0] == '\0' &&
          inst_help_text(INST_HELP_COUNT)[0] == '\0' &&
          inst_help_title(INST_HELP_COUNT)[0] == '\0',
          "help: no text out of range");
    CHECK(strcmp(inst_help_title(INST_HELP_TBT), "Thunderbolt and USB4") ==
          0 && strcmp(inst_help_title(INST_HELP_COMP), "Components") == 0 &&
          strcmp(inst_help_title(INST_HELP_UWELCOME), "Uninstall") == 0,
          "help: titles in topic order");
    for (i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
        int t = inst_help_topic(routes[i].Kind, routes[i].Un, routes[i].Var);
        sprintf(msg, "help route %d: kind %d mode %d variant %d -> %d", i,
                routes[i].Kind, routes[i].Un, routes[i].Var, routes[i].Topic);
        CHECK(t == routes[i].Topic, msg);
        if (t >= 0 && t < INST_HELP_COUNT) {
            reached[t] = 1;
        }
    }
    CHECK(inst_help_topic(INST_HK_COUNT, 0, 0) == -1 &&
          inst_help_topic(-1, 0, 0) == -1, "help: an unknown kind, none");
    for (i = 0; i < INST_HELP_COUNT; i++) {
        sprintf(msg, "help topic %d reached by a page", i);
        CHECK(reached[i], msg);
    }
    for (i = 0; i < (int)(sizeof(labels) / sizeof(labels[0])); i++) {
        const char *l = inst_page_text(labels[i].Pt);
        if (labels[i].Whole) {
            strncpy(head, l, sizeof(head) - 1);
            head[sizeof(head) - 1] = '\0';
        } else {
            help_label_head(l, head, sizeof(head));
        }
        sprintf(msg, "help topic %d quotes page text %d", labels[i].Topic,
                labels[i].Pt);
        CHECK(head[0] != '\0' &&
              strstr(inst_help_text(labels[i].Topic), head) != NULL, msg);
    }
    for (i = 0; i < (int)(sizeof(quotes) / sizeof(quotes[0])); i++) {
        sprintf(msg, "help topic %d quotes \"%.60s\"", quotes[i].Topic,
                quotes[i].Words);
        CHECK(help_has_ci(inst_help_text(quotes[i].Topic), quotes[i].Words),
              msg);
    }
    /* The XHCIPFW box says what it does, no file name (owner,
     * 2026-10-31; record 26 draft 56); its Help names the files. */
    CHECK(strcmp(inst_page_text(INST_PT_PFW), "Find Thunderbolt / USB4 "
                 "controllers and devices placed above 4 GB") == 0 &&
          strcmp(inst_page_text(INST_PT_PFW_SYS),
                 inst_page_text(INST_PT_PFW)) == 0 &&
          strstr(inst_page_text(INST_PT_PFW), "XHCIPFW") == NULL,
          "help: the XHCIPFW box's label, the same on every system");
    /* Each system's Progress text names its own hardware wizard alone. */
    CHECK(!help_has_ci(inst_help_text(INST_HELP_PROGRESS_9X), "Found New") &&
          !help_has_ci(inst_help_text(INST_HELP_PROGRESS), "Add New") &&
          strcmp(inst_help_title(INST_HELP_PROGRESS_9X),
                 inst_help_title(INST_HELP_PROGRESS)) == 0,
          "help: the Progress texts, one hardware wizard each");
    /* The Welcome page's wordings are the owner's (2026-11-02, task
     * 46.18): no pointer to Help or to the readme. */
    CHECK(strstr(inst_page_text(INST_PT_WELCOME), "readme") == NULL &&
          strcmp(inst_page_text(INST_PT_WELCOME_CM),
                 inst_page_text(INST_PT_WELCOME)) == 0 &&
          strstr(inst_page_text(INST_PT_WELCOME_TOOLS), "readme") == NULL,
          "the Welcome page's wordings, the owner's");
}

static void test_tbt_page(void)
{
    INST_TBNET d;
    INST_OPTS o;
    char err[256];
    const char *a1[] = { "x", "/tbnet" };
    const char *a2[] = { "x", "/notbnet", "/yes" };
    const char *a3[] = { "x", "/tbnet", "/notbnet" };
    const char *a4[] = { "x", "/notbnet", "/uninstall" };
    const char *a5[] = { "x", "/notbnet", "/devices" };
    const char *a6[] = { "x", "/tbnet", "/thunderbolt", "/yes" };
    const char *a7[] = { "x", "/tbnet", "/cmonly" };
    const char *a8[] = { "x", "/set", "XhciCmNet=1" };
    static const int ids[] = {
        INST_PT_TBT_HEAD, INST_PT_APPR_HEAD, INST_PT_APPR_1, INST_PT_APPR_1_T,
        INST_PT_APPR_0, INST_PT_APPR_0_T, INST_PT_APPR_MIXED, INST_PT_APPR_NEW,
        INST_PT_TBNET, INST_PT_TBNET_T
    };
    int i;
    int sw;
    int inForce;

    for (i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++) {
        const char *s = inst_page_text(ids[i]);
        CHECK(s != NULL && s[0] != '\0', "a 45.60 page text is there");
        CHECK(s != NULL && strstr(s, "test") == NULL &&
              strstr(s, "experimental") == NULL, "no testing status");
    }
    CHECK(strstr(inst_page_text(INST_PT_ADVANCED), "tester") == NULL,
          "the Advanced heading names no testing");
    CHECK(strstr(inst_page_text(INST_PT_TB), "Experimental") == inst_page_text(INST_PT_TB),
          "the Components box keeps the owner's word");
    CHECK(strstr(inst_page_text(INST_PT_TB_T), "memory") == NULL &&
          strstr(inst_page_text(INST_PT_APPR_1_T), "memory") != NULL,
          "what a device can reach moved to the approval row");
    CHECK(strcmp(inst_page_text(INST_PT_TBNET), "Thunderbolt networking") == 0,
          "the networking box's words");
    CHECK_EQ(INST_TBNET_WIRED, 1, "networking wired (task 45.59 step S11)");
    CHECK(inst_value_find("XhciCmNet") < 0,
          "XhciCmNet not in the values table: the box is the setting");
    {
        /* Record 35 section 9.1: what it does, nothing of testing. */
        const char *s = inst_page_text(INST_PT_TBNET_T);
        /* The owner's words of 2026-11-02 (task 46.18): what it does;
         * file sharing and turning it off are in its Help. */
        CHECK(s != NULL && strstr(s, "another computer") != NULL &&
              strstr(inst_help_text(INST_HELP_TBT), "file sharing") != NULL,
              "the networking sentence says what it does");
    }
    /* The network driver's own slot, after Stor98's (record 35 section
     * 9.5): an interrupted run's slot numbers keep their meaning. */
    CHECK(INST_INF_NET == 5 && INST_INF_COUNT == 6 && INST_INF_S98 == 4,
          "the network INF slot appended");
    CHECK(inst_own_service("xhcinet"), "xhcinet is a service of the package");
    CHECK_EQ(inst_remove_rank("XHCICM\\TBNET\\1&2&3", "xhcinet", 0), 0,
             "the network adapter removed first, before the NHI");
    CHECK(inst_remove_rank("XHCICM\\TBNET\\1&2&3", "xhcinet", 0) <
              inst_remove_rank("PCI\\VEN_8086&DEV_15EB\\3&1", "xhcicm", 0),
          "the adapter before its NHI");
    CHECK_EQ(inst_remove_rank("XHCICM\\TBNET\\1&2&3", "", 0), 0,
             "an adapter whose install failed is removed too");
    CHECK(inst_cm_child_id("XHCICM\\TBNET\\1&2&3") &&
          !inst_cm_child_id("PCI\\VEN_8086&DEV_15EB\\3&1"),
          "the connection manager's children by their enumerator");
    {
        static const char netInf[] =
            "[Version]\r\nSignature=\"$CHICAGO$\"\r\nProvider=%Provider%\r\n"
            "[Net.Dev.NTx86.Services]\r\nAddService=xhcinet,0x00000002,"
            "Net.Service\r\n[Strings]\r\nProvider=\"Yeo Kheng Meng\"\r\n";
        CHECK(inst_inf_is_ours(netInf), "xhcinet.inf is this package's INF");
    }
    /* The source map's rows (record 35 section 9.5): cm\tbnet\ in each
     * family, 98-me\ without xhcinet6.sys, the catalog on x64 alone. */
    CHECK(inst_pkg_rel(INST_FAM_9X, "xhcinet.inf") != NULL &&
          strcmp(inst_pkg_rel(INST_FAM_9X, "xhcinet.inf"),
                 "cm\\tbnet\\xhcinet.inf") == 0 &&
          strcmp(inst_pkg_rel(INST_FAM_NT86, "xhcinet.sys"),
                 "cm\\tbnet\\xhcinet.sys") == 0 &&
          strcmp(inst_pkg_rel(INST_FAM_NT64, "xhcinet.cat"),
                 "cm\\tbnet\\xhcinet.cat") == 0,
          "the network driver's places in the download");
    CHECK(inst_pkg_rel(INST_FAM_9X, "xhcinet6.sys") == NULL &&
          inst_pkg_rel(INST_FAM_NT86, "xhcinet6.sys") != NULL &&
          inst_pkg_rel(INST_FAM_NT86, "xhcinet.cat") == NULL,
          "98-me without xhcinet6.sys, nt-x86 without a catalog");

    /* The gate off: nothing shown, open or written, whatever is asked. */
    for (sw = -1; sw <= 1; sw++) {
        for (inForce = -1; inForce <= 1; inForce++) {
            inst_tbnet_decide(0, 1, sw, inForce, 1, 1, 1, &d);
            CHECK(!d.Shown && !d.Enabled && !d.First && !d.Write && !d.ToNhi &&
                  !d.ToMw, "the gate off: nothing");
        }
    }
    /* Wired, no driver for this system: shown greyed, nothing written. */
    inst_tbnet_decide(1, 0, 1, 1, 1, 1, 1, &d);
    CHECK(d.Shown && !d.Enabled && !d.First && !d.Write,
          "no driver: greyed, nothing written");
    /* Wired and carried: the first state. */
    inst_tbnet_decide(1, 1, -1, -1, -1, 1, 0, &d);
    CHECK(d.Shown && d.Enabled && !d.First && !d.Write,
          "a new install: unticked, nothing written (the INF's 0)");
    inst_tbnet_decide(1, 1, -1, 1, -1, 1, 0, &d);
    CHECK(d.First && !d.Write, "1 in force: ticked, left as it is");
    inst_tbnet_decide(1, 1, -1, 0, -1, 1, 0, &d);
    CHECK(!d.First && !d.Write, "0 in force: unticked, left as it is");
    inst_tbnet_decide(1, 1, 1, 0, -1, 1, 0, &d);
    CHECK(d.First && d.Write && d.Value == 1 && d.ToNhi && !d.ToMw,
          "/tbnet over 0: ticked and written to the controller's key");
    inst_tbnet_decide(1, 1, 0, -1, -1, 1, 0, &d);
    CHECK(!d.First && d.Write && d.Value == 0 && d.ToNhi,
          "/notbnet makes the box changed: 0 written");
    /* A tick and an untick against each value in force. */
    inst_tbnet_decide(1, 1, -1, -1, 1, 1, 0, &d);
    CHECK(d.Write && d.Value == 1 && d.ToNhi && !d.ToMw, "ticked over none");
    inst_tbnet_decide(1, 1, -1, 0, 1, 1, 0, &d);
    CHECK(d.Write && d.Value == 1, "ticked over 0");
    inst_tbnet_decide(1, 1, -1, 1, 1, 1, 0, &d);
    CHECK(!d.Write, "ticked over 1: nothing");
    inst_tbnet_decide(1, 1, -1, 1, 0, 1, 0, &d);
    CHECK(d.Write && d.Value == 0 && d.ToNhi, "unticked over 1: 0 written");
    inst_tbnet_decide(1, 1, -1, 0, 0, 1, 0, &d);
    CHECK(!d.Write, "unticked over 0: nothing");
    inst_tbnet_decide(1, 1, -1, -1, 0, 1, 0, &d);
    CHECK(!d.Write, "unticked over none: nothing");
    /* The targets. */
    inst_tbnet_decide(1, 1, -1, -1, 1, 0, 0, &d);
    CHECK(d.Write && !d.ToNhi && d.ToMw, "no host interface: machine-wide");
    inst_tbnet_decide(1, 1, -1, 0, 1, 1, 1, &d);
    CHECK(d.Write && d.ToNhi && d.ToMw,
          "a machine-wide value there: both places");
    /* Record 35's sequence: staged-only /tbnet, then a host interface,
     * /notbnet, /tbnet again: 1 in both places at the end. */
    inst_tbnet_decide(1, 1, 1, -1, -1, 0, 0, &d);
    CHECK(d.Write && d.Value == 1 && !d.ToNhi && d.ToMw, "staged-only /tbnet");
    inst_tbnet_decide(1, 1, 0, 1, -1, 1, 1, &d);
    CHECK(d.Write && d.Value == 0 && d.ToNhi && d.ToMw,
          "then /notbnet with a host interface: 0 in both");
    inst_tbnet_decide(1, 1, 1, 0, -1, 1, 1, &d);
    CHECK(d.Write && d.Value == 1 && d.ToNhi && d.ToMw,
          "then /tbnet again: 1 in both");
    /* Its mirror: ticked on the page over a machine-wide 0. */
    inst_tbnet_decide(1, 1, -1, 0, 1, 1, 1, &d);
    CHECK(d.Write && d.Value == 1 && d.ToNhi && d.ToMw,
          "the box over a machine-wide 0: 1 in both");

    /* The switches, wired (record 35 section 9.2): /tbnet needs the
     * Thunderbolt support, as /stor98 does; /notbnet stands alone. */
    CHECK(inst_parse_args(2, (char **)a1, &o, err, sizeof(err)) != 0 &&
              strstr(err, "/tbnet needs /thunderbolt") != NULL,
          "/tbnet alone refused: it needs the Thunderbolt support");
    CHECK(inst_parse_args(4, (char **)a6, &o, err, sizeof(err)) == 0 &&
              o.TbNet && !o.NoTbNet, "/tbnet with /thunderbolt accepted");
    CHECK(inst_parse_args(3, (char **)a7, &o, err, sizeof(err)) == 0 &&
              o.TbNet, "/tbnet with /cmonly accepted");
    CHECK(inst_parse_args(3, (char **)a2, &o, err, sizeof(err)) == 0 &&
              o.NoTbNet && !o.TbNet, "/notbnet accepted");
    CHECK(inst_parse_args(3, (char **)a8, &o, err, sizeof(err)) != 0 &&
              strstr(err, "use /tbnet or /notbnet") != NULL,
          "/set XhciCmNet= refused: the box is the setting");
    CHECK(inst_parse_args(3, (char **)a3, &o, err, sizeof(err)) != 0 &&
              strstr(err, "contradict") != NULL, "both: refused");
    CHECK(inst_parse_args(3, (char **)a4, &o, err, sizeof(err)) != 0 &&
              strstr(err, "/notbnet belongs to an install") != NULL,
          "with /uninstall: refused");
    CHECK(inst_parse_args(3, (char **)a5, &o, err, sizeof(err)) != 0,
          "with /devices: refused");
}

/* A run of items (weight, found late), a gap of weight 1 between each two,
 * the last the run's end (inst_prog_last): every step in thousandths into
 * st[], their count returned; mono 0 when a position went back. */
static int prog_seq(const INST_U32 *w, const int *late, int n, INST_U32 *st,
                    int *mono)
{
    INST_PROG p;
    INST_U32 last = 0;
    INST_U32 v;
    int i;
    int k = 0;

    inst_prog_init(&p);
    for (i = 0; i < n; i++) {
        if (!late[i]) {
            inst_prog_plan(&p, w[i]);
        }
    }
    *mono = 1;
    for (i = 0; i < n; i++) {
        if (i > 0) {
            inst_prog_add(&p, INST_PROG_W_SMALL);
            inst_prog_begin(&p, INST_PROG_W_SMALL, 0);
            inst_prog_end(&p, INST_PROG_W_SMALL);
        }
        if (late[i]) {
            inst_prog_add(&p, w[i]);
        }
        if (i == n - 1) {
            inst_prog_last(&p, w[i], 0);
        } else {
            inst_prog_begin(&p, w[i], 0);
        }
        inst_prog_end(&p, w[i]);
        v = inst_prog_pos(&p, 0);
        if (v < last) {
            *mono = 0;
        }
        st[k++] = v - last;
        last = v;
    }
    return k;
}

static INST_U32 prog_median(const INST_U32 *st, int n)
{
    INST_U32 s[64];
    INST_U32 x;
    int i;
    int j;

    for (i = 0; i < n; i++) {
        s[i] = st[i];
    }
    for (i = 1; i < n; i++) {
        for (j = i; j > 0 && s[j - 1] > s[j]; j--) {
            x = s[j];
            s[j] = s[j - 1];
            s[j - 1] = x;
        }
    }
    return s[n / 2];
}

/* Record 26 section 5.0, draft 68: a resident image replaced on 98 SE and
 * ME needs a restart; the rename refused over a file in place is queued. */
static void test_resident(void)
{
    CHECK(inst_resident_restart(0, 1, 0) == 1,
          "resident: 98 SE/ME, a different file in place: restart");
    CHECK(inst_resident_restart(0, 1, 1) == 0,
          "resident: the package's file already in place: no restart");
    CHECK(inst_resident_restart(0, 0, 0) == 0,
          "resident: no file in place (never loaded): no restart");
    CHECK(inst_resident_restart(1, 1, 0) == 0,
          "resident: 2000 and later unload it: no restart from this rule");
    CHECK(inst_resident_way(0, 1, 0) == INST_RES_PUT,
          "resident: a new file copied now: put");
    CHECK(inst_resident_way(1, 0, 1) == INST_RES_QUEUE &&
              inst_resident_way(1, 1, 1) == INST_RES_QUEUE,
          "resident: a file in place: always queued for the next start");
    CHECK(inst_resident_way(1, 1, 0) == INST_RES_FAILED &&
              inst_resident_way(0, 0, 1) == INST_RES_FAILED,
          "resident: the queue refused over a file in place, or a new "
          "file not copied: failed");
    /* Draft 69: a queued replacement found beside the file. */
    CHECK(inst_resident_queued(0, 0, 0, INST_VE_ABSENT, 0) == INST_RQ_NONE,
          "resident: nothing queued");
    CHECK(inst_resident_queued(1, 1, 0, INST_VE_EXACT, 0) == INST_RQ_KEEP &&
              inst_resident_queued(1, 1, 0, INST_VE_ABSENT, 0) ==
                  INST_RQ_KEEP,
          "resident: this package's copy queued (or its line lost): kept");
    CHECK(inst_resident_queued(1, 0, 0, INST_VE_EXACT, 0) == INST_RQ_BACK,
          "resident: another package's copy queued: taken back");
    CHECK(inst_resident_queued(1, 1, 1, INST_VE_EXACT, 0) == INST_RQ_BACK,
          "resident: the package's bytes already in place: the queue goes");
    CHECK(inst_resident_queued(0, 0, 0, INST_VE_EXACT, 0) == INST_RQ_BACK,
          "resident: a line whose copy is gone: taken back");
    CHECK(inst_resident_queued(1, 1, 0, INST_VE_EXACT, 1) == INST_RQ_BACK &&
              inst_resident_queued(1, 0, 0, INST_VE_EXACT, 1) ==
                  INST_RQ_BACK,
          "resident: the uninstall withdraws whatever this installer queued");
    CHECK(inst_resident_queued(1, 1, 0, INST_VE_CONFLICT, 0) ==
                  INST_RQ_HOLD &&
              inst_resident_queued(1, 1, 0, INST_VE_CONFLICT, 1) ==
                  INST_RQ_HOLD,
          "resident: another's line, or an unreadable one: nothing touched");
    /* Record 35 section 9.5 (revision 17): the network image is placed
     * where its 9x INF section names it, SYSTEM. */
    CHECK(strcmp(inst_place9x_dir("xhcinet.sys"), "SYSTEM") == 0 &&
              strcmp(inst_place9x_dir("XHCINET.SYS"), "SYSTEM") == 0,
          "place9x: xhcinet.sys in SYSTEM, any case");
    CHECK(strcmp(inst_place9x_dir("xhciuas.sys"), "SYSTEM32\\DRIVERS") == 0 &&
              strcmp(inst_place9x_dir("xhcicm.sys"), "SYSTEM32\\DRIVERS") ==
                  0 &&
              strcmp(inst_place9x_dir("xhcinet6.sys"),
                     "SYSTEM32\\DRIVERS") == 0,
          "place9x: the others in SYSTEM32\\DRIVERS");
}

/* Draft 67: the reserve shrunk by the share of known work left. */
static void test_prog_shrink(void)
{
    /* The legs' sequences ("46.8" in runs/run-46.md): 98 SE's helper jobs
     * and settles, XP's waits, both with the waits found late. */
    static const INST_U32 w98[] = { 4, 4, 6, 6, 4, 4, 4, 4, 4, 6, 4, 4, 4,
                                    4, 2, 2, 3 };
    static const int l98[] = { 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                               1, 0 };
    static const INST_U32 wxp[] = { 4, 4, 4, 4, 4, 4, 6, 4, 4, 6, 6, 4, 4,
                                    4, 2, 3 };
    static const int lxp[] = { 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,
                               0 };
    INST_U32 st[64];
    INST_PROG p;
    INST_U32 r;
    int mono;
    int n;

    n = prog_seq(w98, l98, (int)(sizeof(w98) / sizeof(w98[0])), st, &mono);
    CHECK(mono && st[n - 1] <= prog_median(st, n - 1),
          "prog: 98 SE-like, the run's end's step no larger than the "
          "median item's");
    n = prog_seq(wxp, lxp, (int)(sizeof(wxp) / sizeof(wxp[0])), st, &mono);
    CHECK(mono && st[n - 1] <= prog_median(st, n - 1),
          "prog: XP-like, the run's end's step no larger than the median "
          "item's");
    /* Whole at the start, a small part near the end, never 0 while weight
     * is left. */
    inst_prog_init(&p);
    inst_prog_plan(&p, 100);
    CHECK_EQ(inst_prog_reserve_milli(&p), INST_PROG_RESERVE * 1000UL,
             "prog: the reserve whole with nothing done");
    p.WeightLeft = 1;
    p.WeightDone = 400;
    r = inst_prog_reserve_milli(&p);
    CHECK(r > 0 && r < INST_PROG_RESERVE * 1000UL / 10,
          "prog: the reserve a small part of itself near the end, not 0");
    p.WeightDone = 4000000UL;
    CHECK(inst_prog_reserve_milli(&p) >= INST_PROG_RESERVE,
          "prog: the reserve never 0 while weight is left");
    /* The uninstall's shape: the run's end planned, then forty removals
     * found late, each with its gap: every one still has a share. */
    {
        int k;
        int ok = 1;

        inst_prog_init(&p);
        inst_prog_plan(&p, INST_PROG_W_FINISH);
        for (k = 0; k < 80; k++) {
            inst_prog_add(&p, INST_PROG_W_SMALL);
            inst_prog_begin(&p, INST_PROG_W_SMALL, 0);
            if (p.OpenShare == 0) {
                ok = 0;
            }
            inst_prog_end(&p, INST_PROG_W_SMALL);
        }
        CHECK(ok, "prog: forty removals and their gaps, each with a share");
    }
    p.WeightLeft = 5;
    p.WeightDone = 100;
    p.Reserve = 90;
    CHECK_EQ(inst_prog_reserve_milli(&p), 10000UL,
             "prog: the reserve never above twice the known work left");
    inst_prog_begin(&p, 1, 0);
    CHECK(p.OpenShare > 0 && p.OpenShare < INST_PROG_SPACE,
          "prog: an item then still leaves room");
}

/* Record 26 section 5.8.1 (task 46.8): the bar by the work left. */
static void test_prog(void)
{
    INST_PROG p;
    INST_U32 last = 0;
    INST_U32 v;
    int i;
    int mono = 1;

    /* A first install on XP: the copy, two controllers, a wait each, the
     * finish. */
    inst_prog_init(&p);
    inst_prog_plan(&p, INST_PROG_W_COPY);
    inst_prog_plan(&p, 2 * INST_PROG_W_DEVICE);
    inst_prog_plan(&p, INST_PROG_W_FINISH);
    CHECK_EQ(inst_prog_pos(&p, 0), 0, "prog: starts at 0");
    inst_prog_begin(&p, INST_PROG_W_COPY, 0);
    v = inst_prog_pos(&p, 60000);
    CHECK(v > 0 && v < 1000, "prog: the copy creeps with no bound");
    inst_prog_end(&p, INST_PROG_W_COPY);
    last = inst_prog_pos(&p, 0);
    CHECK(last >= v, "prog: the copy's end at least its creep");
    for (i = 0; i < 2; i++) {
        inst_prog_begin(&p, INST_PROG_W_DEVICE, 1000);
        v = inst_prog_pos(&p, 1000 + 600000);
        if (v < last) {
            mono = 0;
        }
        inst_prog_end(&p, INST_PROG_W_DEVICE);
        inst_prog_add(&p, INST_PROG_W_WAIT);
        inst_prog_begin(&p, INST_PROG_W_WAIT, 0);
        inst_prog_bound(&p, 600000, 0);
        v = inst_prog_pos(&p, 300000);
        if (v < inst_prog_pos(&p, 0)) {
            mono = 0;
        }
        inst_prog_end(&p, INST_PROG_W_WAIT);
        v = inst_prog_pos(&p, 0);
        if (v < last) {
            mono = 0;
        }
        last = v;
    }
    CHECK(mono, "prog: never back");
    CHECK(last > 500 && last < 1000, "prog: the devices most of the bar");
    inst_prog_begin(&p, INST_PROG_W_FINISH, 0);
    inst_prog_end(&p, INST_PROG_W_FINISH);
    v = inst_prog_pos(&p, 0);
    CHECK(v >= last && v <= 999, "prog: below the end until done");
    inst_prog_done(&p);
    CHECK_EQ(inst_prog_pos(&p, 0), 1000, "prog: the end only at done");
    /* An item found after every known one ended still moves the bar; a
     * run of late devices leaves room (the reserve grows). */
    inst_prog_init(&p);
    inst_prog_plan(&p, INST_PROG_W_DEVICE);
    inst_prog_begin(&p, INST_PROG_W_DEVICE, 0);
    inst_prog_end(&p, INST_PROG_W_DEVICE);
    last = inst_prog_pos(&p, 0);
    for (i = 0; i < 20; i++) {
        inst_prog_add(&p, INST_PROG_W_DEVICE);
        inst_prog_begin(&p, INST_PROG_W_DEVICE, 0);
        inst_prog_end(&p, INST_PROG_W_DEVICE);
    }
    v = inst_prog_pos(&p, 0);
    /* Draft 67: each a third of what is left, so the bar reads 999 by now,
     * with room left in millionths. */
    CHECK(v > last && p.Pos < INST_PROG_SPACE - 1, "prog: twenty late "
          "devices move it and leave room");
    inst_prog_add(&p, INST_PROG_W_DEVICE);
    inst_prog_begin(&p, INST_PROG_W_DEVICE, 0);
    CHECK(p.OpenShare > 0, "prog: the next late item still has a share");
    /* The creep: a bounded wait at most 0.9 of its share; an unbounded one
     * moving and stopping short. */
    inst_prog_init(&p);
    inst_prog_plan(&p, INST_PROG_W_WAIT);
    inst_prog_begin(&p, INST_PROG_W_WAIT, 0);
    inst_prog_bound(&p, 10000, 0);
    v = inst_prog_pos(&p, 10000);
    CHECK(v == 180,
          "prog: a bounded wait's creep stops at half of 0.9 of its share");
    inst_prog_init(&p);
    inst_prog_plan(&p, INST_PROG_W_DEVICE);
    inst_prog_begin(&p, INST_PROG_W_DEVICE, 4000000000UL);
    CHECK(inst_prog_pos(&p, 4000000000UL + 10000UL) == 56 &&
          inst_prog_pos(&p, 4000000000UL + 30000UL) == 112 &&
          inst_prog_pos(&p, 4000000000UL + 600000UL) == 214 &&
          inst_prog_pos(&p, 0) == 222,
          "prog: an unbounded item creeps, the clock's wrap included, short "
          "of its share");
    /* The 98 SE helper's sequence inside one device item: unbounded, a
     * 15 s reappearance poll, a 600 s settle, unbounded again; never back,
     * moving in each. */
    inst_prog_init(&p);
    inst_prog_plan(&p, INST_PROG_W_DEVICE);
    inst_prog_begin(&p, INST_PROG_W_DEVICE, 0);
    last = inst_prog_pos(&p, 30000);
    inst_prog_bound(&p, 15000, 30000);
    v = inst_prog_pos(&p, 45000);
    mono = v > last;
    last = v;
    inst_prog_bound(&p, 600000, 45000);
    v = inst_prog_pos(&p, 345000);
    mono = mono && v > last;
    last = v;
    inst_prog_bound(&p, 0, 345000);
    v = inst_prog_pos(&p, 400000);
    mono = mono && v >= last && v <= 450;
    CHECK(mono, "prog: waits of other bounds in one item keep what was "
          "crept and go on, short of the share");
    /* An update with nothing to do: no item, the end at done. */
    inst_prog_init(&p);
    CHECK_EQ(inst_prog_pos(&p, 0), 0, "prog: nothing planned, 0");
    inst_prog_done(&p);
    CHECK_EQ(inst_prog_pos(&p, 0), 1000, "prog: nothing planned, done");
    /* A dropped item leaves the weight with no movement. */
    inst_prog_init(&p);
    inst_prog_plan(&p, 3 * INST_PROG_W_DEVICE);
    inst_prog_drop(&p, INST_PROG_W_DEVICE);
    CHECK(p.WeightLeft == 2 * INST_PROG_W_DEVICE && inst_prog_pos(&p, 0) == 0,
          "prog: a dropped item");
    /* The run's end takes the reserve (draft 66): after twenty late
     * devices as after none, its share all the space left, its creep
     * short of it, 999 at its end, done one more. */
    for (i = 0; i <= 20; i += 20) {
        int k;

        inst_prog_init(&p);
        inst_prog_plan(&p, INST_PROG_W_COPY + INST_PROG_W_FINISH);
        inst_prog_begin(&p, INST_PROG_W_COPY, 0);
        inst_prog_end(&p, INST_PROG_W_COPY);
        for (k = 0; k < i; k++) {
            inst_prog_add(&p, INST_PROG_W_WAIT);
            inst_prog_begin(&p, INST_PROG_W_WAIT, 0);
            inst_prog_end(&p, INST_PROG_W_WAIT);
        }
        last = inst_prog_pos(&p, 0);
        inst_prog_last(&p, INST_PROG_W_FINISH, 0);
        CHECK(p.Open && p.OpenShare == INST_PROG_SPACE - p.Pos,
              "prog: the run's end's share is all the space left");
        v = inst_prog_pos(&p, 600000);
        CHECK(v > last && v < 999, "prog: the run's end creeps, short of "
              "the end");
        inst_prog_end(&p, INST_PROG_W_FINISH);
        CHECK_EQ(inst_prog_pos(&p, 0), 999, "prog: 999 at the run's end");
        inst_prog_add(&p, INST_PROG_W_SMALL);
        inst_prog_begin(&p, INST_PROG_W_SMALL, 0);
        CHECK(p.OpenShare <= 1 && inst_prog_pos(&p, 600000) == 999,
              "prog: an item after the run's end finds no space");
        inst_prog_done(&p);
        CHECK_EQ(inst_prog_pos(&p, 0), 1000, "prog: done one more");
    }
    /* A wait inside the run's end (the mark service's, up to 30 s): it
     * creeps on from what was crept, short of the end, and the end is
     * still 999. */
    inst_prog_init(&p);
    inst_prog_plan(&p, INST_PROG_W_FINISH);
    inst_prog_last(&p, INST_PROG_W_FINISH, 0);
    last = inst_prog_pos(&p, 5000);
    inst_prog_bound(&p, 30000, 5000);
    v = inst_prog_pos(&p, 35000);
    CHECK(last > 0 && v > last && v < 999,
          "prog: a wait inside the run's end creeps on");
    inst_prog_bound(&p, 0, 35000);
    CHECK(inst_prog_pos(&p, 40000) >= v, "prog: and never back after it");
    inst_prog_end(&p, INST_PROG_W_FINISH);
    CHECK_EQ(inst_prog_pos(&p, 0), 999, "prog: the run's end still 999");
    /* After done the run's end does nothing. */
    inst_prog_done(&p);
    inst_prog_last(&p, INST_PROG_W_FINISH, 0);
    CHECK(!p.Open && inst_prog_pos(&p, 0) == 1000,
          "prog: the run's end after done: nothing");
}

/* Record 26 section 6.7.15 (task 46.22): the packages copied only on this
 * Windows, as greyed rows, from a fixture STAGE.TXT of made-up packages. */
static void test_copy_rows_4622(void)
{
    static const char *const fixture[] = {
        "# made-up packages, not a real pack's",
        "STAGE | W98SE WXP | free/zclass/alpha/w98-xp/ALPHA.INF | staged",
        "COPY  | W7X64     | free/zclass/alpha/w7-x64/ALPHA.INF | copy",
        "COPY  | W7X64     | free/zclass/alpha/w7-x64/ALPHACI.INF | helper",
        "COPY  | W7X64     | free/zclass/alpha/w7-x64/ALPHA2.INF | twin",
        "COPY  | WXP       | free/zclass/alpha/w98-xp/ALPHACI.INF | helper",
        "COPY  | W2K       | free/zclass/beta/w2k/BETACI.INF | helper only",
        "COPY  | W2K       | free/zclass/gamma/one/G.INF | one",
        "COPY  | W2K       | free/zclass/gamma/two/G.INF | two",
        "COPY  | W2K       | free/zclass/delta/a/D.INF | a",
        "COPY  | W2K       | free/zclass/delta/b/D.INF | b",
        "COPY  | W2K       | free/zclass/delta/c/D.INF | c",
        "COPY  | W2K       | free/zclass/delta/d/D.INF | d"
    };
    static const unsigned char utf16[] = {
        0xFF, 0xFE, '[', 0, 'V', 0, 'e', 0, 'r', 0, ']', 0, '\r', 0, '\n', 0,
        ' ', 0, '[', 0, 'M', 0, 'a', 0, 'n', 0, 'u', 0, 'f', 0, 'a', 0,
        'c', 0, 't', 0, 'u', 0, 'r', 0, 'e', 0, 'r', 0, ']', 0, ' ', 0,
        ';', 0, 'x', 0, '\r', 0, '\n', 0
    };
    static const unsigned char utf16no[] = {
        0xFF, 0xFE, '[', 0, 'D', 0, 'e', 0, 'f', 0, 'a', 0, 'u', 0, 'l', 0,
        't', 0, 'I', 0, 'n', 0, 's', 0, 't', 0, 'a', 0, 'l', 0, 'l', 0,
        ']', 0, '\r', 0, '\n', 0
    };
    static const char ascii[] = "[Version]\r\nSignature=\"$Windows NT$\"\r\n"
                                "[MANUFACTURER]\r\n%M%=Models\r\n";
    static const char coinst[] = "[Version]\r\n[DefaultInstall]\r\n"
                                 "; [Manufacturer] in a comment\r\n"
                                 "AddReg=X ; [Manufacturer]\r\n";
    INST_STAGE_LINE lines[16];
    char folders[INST_OTHER_ROW_FOLDERS][INST_OTHER_REL_CHARS];
    char row[1200];
    INST_U32 w2k = 0;
    INST_U32 wxp = 0;
    INST_U32 w7x64 = 0;
    int n = 0;
    int more;
    int got;
    int k;

    CHECK(inst_inf_has_models((const unsigned char *)ascii, sizeof(ascii) - 1),
          "copy rows: an ASCII INF's [MANUFACTURER], any case");
    CHECK(!inst_inf_has_models((const unsigned char *)coinst,
                               sizeof(coinst) - 1),
          "copy rows: DefaultInstall alone, a header in a comment: none");
    CHECK(inst_inf_has_models(utf16, sizeof(utf16)),
          "copy rows: a UTF-16LE INF's header, spaces and a comment around");
    CHECK(!inst_inf_has_models(utf16no, sizeof(utf16no)),
          "copy rows: a UTF-16LE INF without one");
    CHECK(!inst_inf_has_models(NULL, 0), "copy rows: no bytes: none");

    CHECK(inst_sys_mask("W2K", &w2k) && inst_sys_mask("WXP", &wxp) &&
          inst_sys_mask("W7X64", &w7x64), "copy rows: the systems' bits");
    for (k = 0; k < (int)(sizeof(fixture) / sizeof(fixture[0])); k++) {
        int r = inst_stage_parse(fixture[k], "free", &lines[n]);
        CHECK(r >= 0, "copy rows: a fixture line read");
        if (r > 0) {
            /* What the reader learns from each INF: the helpers name no
             * models. */
            if (strstr(lines[n].Inf, "CI.INF") != NULL) {
                lines[n].Models = 0;
            }
            n++;
        }
    }
    CHECK(n == 12 && lines[1].Models == 1, "copy rows: twelve lines, models "
          "by default");

    got = inst_other_copy_folders(lines, n, "zclass/alpha", wxp, folders,
                                  INST_OTHER_ROW_FOLDERS, &more);
    CHECK(got == 0, "copy rows: staged on this Windows: no row");
    got = inst_other_copy_folders(lines, n, "zclass/alpha", w7x64, folders,
                                  INST_OTHER_ROW_FOLDERS, &more);
    CHECK(got == 1 && !more && strcmp(folders[0], "zclass/alpha/w7-x64") == 0,
          "copy rows: copied only, two driver INFs in one folder: one folder");
    got = inst_other_copy_folders(lines, n, "zclass/beta", w2k, folders,
                                  INST_OTHER_ROW_FOLDERS, &more);
    CHECK(got == 0, "copy rows: only a co-installer copied: no row");
    got = inst_other_copy_folders(lines, n, "zclass/alpha", w2k, folders,
                                  INST_OTHER_ROW_FOLDERS, &more);
    CHECK(got == 0, "copy rows: no line for this Windows: no row");

    got = inst_other_copy_folders(lines, n, "zclass/gamma", w2k, folders,
                                  INST_OTHER_ROW_FOLDERS, &more);
    inst_other_copy_row(row, sizeof(row), "Made-up gadget (gamma)",
                        "C:\\XHCI98\\USBDRV\\FREE",
                        (const char (*)[INST_OTHER_REL_CHARS])folders, got,
                        more);
    CHECK(got == 2 && strcmp(row, "Made-up gadget (gamma): copied only on "
          "this Windows. Install it by hand from C:\\XHCI98\\USBDRV\\FREE\\"
          "zclass\\gamma\\one or C:\\XHCI98\\USBDRV\\FREE\\zclass\\gamma\\"
          "two.") == 0, "copy rows: two folders, A or B");

    got = inst_other_copy_folders(lines, n, "zclass/delta", w2k, folders,
                                  INST_OTHER_ROW_FOLDERS, &more);
    inst_other_copy_row(row, sizeof(row), "Made-up (delta)", "D:\\X\\NF",
                        (const char (*)[INST_OTHER_REL_CHARS])folders, got,
                        more);
    CHECK(got == 3 && more && strcmp(row, "Made-up (delta): copied only on "
          "this Windows. Install it by hand from D:\\X\\NF\\zclass\\delta\\a, "
          "D:\\X\\NF\\zclass\\delta\\b, D:\\X\\NF\\zclass\\delta\\c or "
          "another folder listed in D:\\X\\NF\\STAGE.TXT.") == 0,
          "copy rows: four folders, three named and STAGE.TXT");

    folders[0][0] = 'x';
    inst_other_copy_row(row, sizeof(row), "None", "C:\\X",
                        (const char (*)[INST_OTHER_REL_CHARS])folders, 0, 0);
    CHECK(row[0] == '\0', "copy rows: no folder, no row");
    inst_other_copy_row(row, 40, "Made-up gadget (gamma)", "C:\\XHCI98",
                        (const char (*)[INST_OTHER_REL_CHARS])folders, 1, 0);
    CHECK(strlen(row) == 39, "copy rows: held to its buffer");
}

/* The log's place (record 23 section 7.5.1). */
static const char *logOpens[4];
static int logOpenCount;
static const char *logTried[4];
static int logTriedCount;

static int log_open_stub(void *ctx, const char *path)
{
    int i;

    (void)ctx;
    if (logTriedCount < 4) {
        logTried[logTriedCount++] = path;
    }
    for (i = 0; i < logOpenCount; i++) {
        if (strcmp(logOpens[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

/* nt 0: 98 SE and ME; nt 1: 2000 to 10 (revision 57). */
static int log_pick_on(int nt, const char *given, const char *temp,
                       const char *a, const char *b, const char **path)
{
    logOpenCount = 0;
    logTriedCount = 0;
    if (a != NULL) {
        logOpens[logOpenCount++] = a;
    }
    if (b != NULL) {
        logOpens[logOpenCount++] = b;
    }
    return inst_log_pick(nt, given, "C:\\XHCIINST.LOG", temp, log_open_stub,
                         NULL, path);
}

static int log_pick(const char *given, const char *temp, const char *a,
                    const char *b, const char **path)
{
    return log_pick_on(0, given, temp, a, b, path);
}

static void test_log_place(void)
{
    static const char root[] = "C:\\XHCIINST.LOG";
    static const char temp[] = "C:\\WINDOWS\\TEMP\\XHCIINST.LOG";
    static const char given[] = "D:\\L.LOG";
    const char *path;
    char out[600];
    char big[600];

    CHECK(inst_log_temp_path(out, sizeof(out), "C:\\WINDOWS\\TEMP\\", root) &&
          strcmp(out, temp) == 0, "the temp log: the folder as Windows gives it");
    CHECK(inst_log_temp_path(out, sizeof(out), "C:\\WINDOWS\\TEMP", root) &&
          strcmp(out, temp) == 0, "the temp log: no last backslash");
    CHECK(!inst_log_temp_path(out, sizeof(out), "", root) && out[0] == '\0',
          "the temp log: no folder");
    CHECK(!inst_log_temp_path(out, sizeof(out), "c:\\", root) &&
          out[0] == '\0', "the temp log: the root file itself, without case");
    CHECK(!inst_log_temp_path(out, sizeof(out), "C:", root) && out[0] == '\0',
          "the temp log: the root file itself, the backslash added");
    CHECK(inst_log_temp_path(out, sizeof(out), "D:\\", root) &&
          strcmp(out, "D:\\XHCIINST.LOG") == 0,
          "the temp log: another drive's root");
    CHECK(inst_log_temp_path(out, 29, "C:\\WINDOWS\\TEMP\\", root) &&
          !inst_log_temp_path(out, 28, "C:\\WINDOWS\\TEMP\\", root) &&
          out[0] == '\0', "the temp log: held to its buffer");
    CHECK(inst_log_temp_path(out, 29, "C:\\WINDOWS\\TEMP", NULL) &&
          !inst_log_temp_path(out, 28, "C:\\WINDOWS\\TEMP", NULL),
          "the temp log: the added backslash counted");
    memset(big, 'P', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    memcpy(big, "C:\\", 3);
    CHECK(!inst_log_temp_path(out, sizeof(out), big, root) && out[0] == '\0',
          "the temp log: an overlong folder");

    CHECK(log_pick("", temp, root, temp, &path) == INST_LOG_ROOT &&
          path != NULL && strcmp(path, root) == 0 && logTriedCount == 1,
          "the log: the root file, the temp folder not tried");
    CHECK(log_pick(NULL, temp, NULL, temp, &path) == INST_LOG_TEMP &&
          path != NULL && strcmp(path, temp) == 0 && logTriedCount == 2 &&
          strcmp(logTried[0], root) == 0 && strcmp(logTried[1], temp) == 0,
          "the log: the root file first, then the temp folder");
    CHECK(log_pick("", temp, NULL, NULL, &path) == INST_LOG_NONE &&
          path == NULL && logTriedCount == 2, "the log: both failing");
    CHECK(log_pick("", "", NULL, NULL, &path) == INST_LOG_NONE &&
          path == NULL && logTriedCount == 1 &&
          log_pick("", NULL, NULL, NULL, &path) == INST_LOG_NONE &&
          logTriedCount == 1, "the log: no temp path to try");
    CHECK(log_pick(given, temp, given, temp, &path) == INST_LOG_GIVEN &&
          path != NULL && strcmp(path, given) == 0 && logTriedCount == 1,
          "the log: /log");
    CHECK(log_pick(given, temp, root, temp, &path) == INST_LOG_NONE &&
          path == NULL && logTriedCount == 1 &&
          strcmp(logTried[0], given) == 0,
          "the log: a /log that fails, nothing else tried");

    /* 2000 to 10: the root file or no log, the temp place never tried
     * even when a path is passed (revision 57). */
    CHECK(log_pick_on(1, "", temp, root, temp, &path) == INST_LOG_ROOT &&
          path != NULL && strcmp(path, root) == 0 && logTriedCount == 1,
          "the log on NT: the root file");
    CHECK(log_pick_on(1, "", temp, NULL, temp, &path) == INST_LOG_NONE &&
          path == NULL && logTriedCount == 1 &&
          strcmp(logTried[0], root) == 0,
          "the log on NT: the root failing, the temp folder not tried");
    CHECK(log_pick_on(1, NULL, "", NULL, NULL, &path) == INST_LOG_NONE &&
          path == NULL && logTriedCount == 1,
          "the log on NT: the root failing, no temp path");
    CHECK(log_pick_on(1, given, temp, given, temp, &path) == INST_LOG_GIVEN &&
          path != NULL && strcmp(path, given) == 0 && logTriedCount == 1,
          "the log on NT: /log");
    CHECK(log_pick_on(1, given, temp, root, temp, &path) == INST_LOG_NONE &&
          path == NULL && logTriedCount == 1 &&
          strcmp(logTried[0], given) == 0,
          "the log on NT: a /log that fails, nothing else tried");

    CHECK(!inst_log_fail_text(out, sizeof(out), INST_LOG_ROOT, "", root,
                              temp) && out[0] == '\0' &&
          !inst_log_fail_text(out, sizeof(out), INST_LOG_GIVEN, given, "",
                              "") && out[0] == '\0',
          "the log: nothing said of the first place taken");
    CHECK(inst_log_fail_text(out, sizeof(out), INST_LOG_TEMP, "", root,
                             temp) &&
          strcmp(out, "Cannot write the log to C:\\XHCIINST.LOG. Using "
                      "C:\\WINDOWS\\TEMP\\XHCIINST.LOG.") == 0 &&
          strlen(out) <= 79, "the log: the temp folder used, said");
    CHECK(inst_log_fail_text(out, sizeof(out), INST_LOG_NONE, NULL, root,
                             temp) &&
          strcmp(out, "Cannot write the log to C:\\XHCIINST.LOG or "
                      "C:\\WINDOWS\\TEMP\\XHCIINST.LOG.\nContinuing without "
                      "a log.") == 0 &&
          strchr(out, '\n') - out <= 79, "the log: both failing, said");
    CHECK(inst_log_fail_text(out, sizeof(out), INST_LOG_NONE, "", root, "") &&
          strcmp(out, "Cannot write the log to C:\\XHCIINST.LOG. Continuing "
                      "without a log.") == 0,
          "the log: the root failing, no temp path (as on NT), said");
    CHECK(inst_log_fail_text(out, sizeof(out), INST_LOG_NONE, given, root,
                             temp) &&
          strcmp(out, "Cannot write the log to D:\\L.LOG. Continuing "
                      "without a log.") == 0,
          "the log: a /log that fails, said with its own path alone");
    CHECK(inst_log_fail_text(out, 20, INST_LOG_TEMP, "", root, temp) &&
          strcmp(out, "Cannot write the lo") == 0,
          "the log: the text held to its buffer");
}

/* Task 49.38 (the owner, 2026-11-12): every value is a row of Setup's
 * table too, with the new system rules, mode 4, the routing rows' Setup
 * side, the drive rows Setup never handles and the text row. */
/* Task 49.38: XHCIINST /set /show [text]'s listing, with a reader that
 * says every value is set machine-wide to 1 (a string: auto). */
static int fake_mw(int index, INST_U32 *v, char *text, size_t cap)
{
    if (instValues[index].Kind == INST_VK_TEXT) {
        inst_copy(text, "auto", cap);
        return 2;
    }
    *v = 1;
    return 1;
}

/* A reader that says every value is set machine-wide to its own number,
 * 4 for XhciInterruptMode and 2 for XhciCmRingInterrupt (the owner's
 * decision of 2026-11-13: what 98 SE and ME do with them). */
static int fake_mw_nine(int index, INST_U32 *v, char *text, size_t cap)
{
    if (instValues[index].Kind == INST_VK_TEXT) {
        inst_copy(text, "auto", cap);
        return 2;
    }
    if (inst_eq(instValues[index].Name, "XhciInterruptMode")) {
        *v = 4;
    } else if (inst_eq(instValues[index].Name, "XhciCmRingInterrupt")) {
        *v = 2;
    } else {
        *v = 1;
    }
    return 1;
}

/* Setup's listing and its readings of a stored 4 or 2 on 98 SE and ME, and
 * the same on NT (unchanged). */
static void test_set_show_nine(void)
{
    static char out[65536];
    char t[160];
    INST_ENV e98 = env(0, 4, 10, INST_ARCH_X86);
    INST_ENV eme = env(0, 4, 90, INST_ARCH_X86);
    INST_ENV exp = env(1, 5, 1, INST_ARCH_X86);
    const INST_VALUE *im = &instValues[inst_value_find("XhciInterruptMode")];
    const INST_VALUE *ri = &instValues[inst_value_find("XhciCmRingInterrupt")];

    CHECK(inst_value_nine_unoffered(im, &e98) == 0x10UL &&
          inst_value_nine_unoffered(ri, &e98) == 0x4UL &&
          inst_value_nine_unoffered(im, &eme) == 0x10UL &&
          inst_value_nine_unoffered(im, &exp) == 0 &&
          inst_value_nine_unoffered(ri, &exp) == 0 &&
          inst_value_nine_unoffered(im, NULL) == 0,
          "nine: mode 4 and ring 2 on 98 SE and ME alone");
    CHECK(inst_value_nine_text(im, &e98, 4, t, sizeof(t)) &&
          strcmp(t, "4 (the driver uses its default, 0 - automatic)") == 0,
          "nine: a stored 4 read as the USB Tree reads it");
    CHECK(inst_value_nine_text(ri, &eme, 2, t, sizeof(t)) &&
          strcmp(t, "2 (the driver uses its default, 0 - no: check every "
                    "XhciCmPollIdleMs)") == 0,
          "nine: a stored ring 2 read as the USB Tree reads it");
    t[0] = 'x';
    CHECK(!inst_value_nine_text(im, &e98, 2, t, sizeof(t)) &&
          !inst_value_nine_text(ri, &e98, 1, t, sizeof(t)) &&
          !inst_value_nine_text(im, &exp, 4, t, sizeof(t)) &&
          !inst_value_nine_text(im, &e98, 99, t, sizeof(t)) && t[0] == 'x',
          "nine: any other value, or NT, as it is");

    (void)inst_settings_list("XhciInterruptMode", &e98, fake_mw_nine, out,
                             sizeof(out));
    CHECK(strstr(out, "XhciInterruptMode: ") == out &&
              strstr(out, "Allowed: 0 automatic; 1 interrupt only; 2 always "
                          "poll; 3 MSI only.") != NULL &&
              strstr(out, "MSI on 2000") == NULL &&
              strstr(out, "Machine-wide: 4 (the driver uses its default, 0 - "
                          "automatic).") != NULL,
          "/set /show on 98 SE: no 4 offered, a stored 4 explained");
    (void)inst_settings_list("XhciCmRingInterrupt", &eme, fake_mw_nine, out,
                             sizeof(out));
    CHECK(strstr(out, "Allowed: 0 no: check every XhciCmPollIdleMs; 1 yes.") !=
              NULL &&
              strstr(out, "by message") == NULL &&
              strstr(out, "Machine-wide: 2 (the driver uses its default, 0 - "
                          "no: check every XhciCmPollIdleMs).") != NULL,
          "/set /show on ME: no ring 2 offered, a stored 2 explained");
    (void)inst_settings_list("XhciInterruptMode", &exp, fake_mw_nine, out,
                             sizeof(out));
    CHECK(strstr(out, "4 MSI on 2000 / XP, no fallback.") != NULL &&
              strstr(out, "Machine-wide: 4.") != NULL &&
              strstr(out, "uses its default") == NULL,
          "/set /show on XP: as before");
    (void)inst_settings_list("XhciCmRingInterrupt", &exp, fake_mw_nine, out,
                             sizeof(out));
    CHECK(strstr(out, "2 yes, by message.") != NULL &&
              strstr(out, "Machine-wide: 2.") != NULL,
          "/set /show on XP: ring 2 as before");

    /* The console's questions and answers: neither value is asked on any
     * system (Asked 0), so neither is in a question or in /list, and on 98
     * SE and ME no answer or /set takes 4 or 2. */
    {
        int imi = inst_value_find("XhciInterruptMode");
        int rii = inst_value_find("XhciCmRingInterrupt");
        INST_U32 a;
        INST_ENV *s[2];
        int k;
        s[0] = &e98;
        s[1] = &eme;
        for (k = 0; k < 2; k++) {
            s[k]->AnyXhci = 1;
            s[k]->CmNhi = 1;
            CHECK(!inst_value_applies(imi, s[k]) && !inst_value_applies(rii, s[k]),
                  "9x: neither value is asked, a controller and an NHI present");
            CHECK((inst_value_asked_mask(imi, s[k]) & 0x10UL) == 0 &&
                  (inst_value_asked_mask(rii, s[k]) & 0x4UL) == 0,
                  "9x: neither 4 nor 2 in what a question would offer");
            CHECK(inst_value_answer("4", inst_value_asked_mask(imi, s[k]), &a) == -1 &&
                  inst_value_answer("2", inst_value_asked_mask(rii, s[k]), &a) == -1,
                  "9x: an answer of 4 or 2 refused");
            CHECK(!inst_value_allowed(imi, s[k], 4) &&
                  !inst_value_allowed(rii, s[k], 2) &&
                  inst_value_allowed(rii, s[k], 1),
                  "9x: /set refuses 4 and 2, takes ring 1");
        }
        exp.AnyXhci = 1;
        exp.CmNhi = 1;
        CHECK(inst_value_allowed(imi, &exp, 4) && inst_value_allowed(rii, &exp, 2),
              "XP: /set takes 4 and 2 as before");
        CHECK(inst_value_asked_mask(imi, &exp) == instValues[imi].Asked &&
              inst_value_asked_mask(rii, &exp) == instValues[rii].Asked,
              "XP: the asked masks as before");
    }
}

static void test_set_show_4938(void)
{
    static char out[65536];
    int n;
    int idx;
    INST_U32 v;
    INST_OPTS o;
    char err[256];
    char *argv1[4];

    n = inst_settings_list("", NULL, fake_mw, out, sizeof(out));
    CHECK(n == instValueCount - 2, "every row but a drive's two");
    CHECK(strstr(out, "XhciUasNoAtaPassThrough") == NULL, "no drive row");
    n = inst_settings_list("tunnelbridge", NULL, fake_mw, out, sizeof(out));
    CHECK(n == 1 && strstr(out, "XhciCmTunnelBridge: ") == out &&
              strstr(out, "Machine-wide: auto.") != NULL, "one row, its string");
    n = inst_settings_list("MSIAVOID", NULL, fake_mw, out, sizeof(out));
    CHECK(n == 1 && strstr(out, "Machine-wide: 1.") != NULL, "any case");
    n = inst_settings_list("zzz-none", NULL, fake_mw, out, sizeof(out));
    CHECK(n == 0 && strcmp(out, "No setting matches \"zzz-none\".\n") == 0,
          "no match: one line");
    CHECK(inst_value_matches(&instValues[inst_value_find("XhciTolerance")],
                             "leaving a device dead"), "a meaning matches");
    argv1[0] = "xhciinst";
    argv1[1] = "/set";
    argv1[2] = "/show";
    argv1[3] = "quirk";
    memset(&o, 0, sizeof(o));
    CHECK(inst_parse_args(4, argv1, &o, err, sizeof(err)) == 0 && o.SetShow &&
              strcmp(o.SetFind, "quirk") == 0 && o.SetCount == 0,
          "/set /show <text> parsed");
    (void)idx;
    (void)v;
}

/* XhciEventRingTrbs: Setup's number row of two picks, written by the
 * INFs (256). */
static void test_event_ring_4938(void)
{
    char t[128];
    int er = inst_value_find("XhciEventRingTrbs");

    CHECK(er >= 0, "the event ring's row");
    if (er < 0) {
        return;
    }
    CHECK(inst_value_in_range(er, 256) && inst_value_in_range(er, 1024),
          "256 and 1024");
    CHECK(!inst_value_in_range(er, 512) && !inst_value_in_range(er, 0) &&
              !inst_value_in_range(er, 1025), "nothing else");
    inst_value_range_text(er, t, sizeof(t));
    CHECK(strcmp(t, "256 or 1024") == 0, "its range text");
    CHECK(instValues[er].Loc == INST_LOC_MW && !instValues[er].NoInf,
          "Services\\USB, written by the INFs");
    CHECK_EQ(inst_value_find("XhciEventRing"), -1, "the 49.35 name is gone");
}

/* The BIOS-held drive (DESIGN-BIOSHELD): Stor98's find report parsed, and
 * the refusal; the owner's desktop's R5 memory ranges as the member's. */
static void test_bios_held(void)
{
    static const char *taken =
        "port 3.0.0 slot 0 range AC000000:00000000 len 16384; read 256 class "
        "010802 bars AC000004 00000000 00000000 00000000 00000000 00000000; "
        "found 4.0.0; cmd 0006 cc 00460001 csts 00000001 taken; result 1";
    static const char *bios =
        "port 3.0.0 slot 0 range AC000000:00000000 len 16384; read 256 class "
        "010802 bars AC000004 00000000 00000000 00000000 00000000 00000000; "
        "found 4.0.0; cmd 0006 cc 00460001 csts 00000001 left: already set "
        "up; result 0";
    static const char *biosMe =
        "port 3.0.0 slot 0 range AC000000:00000000 len 16384; read 256 class "
        "010802 bars AC000004 00000000 00000000 00000000 00000000 00000000; "
        "found 4.0.0; cmd 0006 cc 00460001 csts 00000001 left: already set "
        "up, StorTakeFirmware ignored on Windows ME; result 0";
    static const char *noAns = "...; found 4.0.0; cmd 0006 cc 00000000 csts "
                               "00000000 left: no answer; result 0";
    static const char *notRest = "...; found 4.0.0; cmd 0004 left: command "
                                 "not restored; result 0";
    static const INST_U32 other4[1] = { 0xB0000000UL };
    static const char *unread = "...; found 4.0.0; cmd 0006 left: unreadable "
                                "reach 2; result 0";
    static const char *over = "...; found 4.0.0; cmd 0006 taken: already set "
                              "up, overridden by StorTakeFirmware; result 1";
    /* The R5 member: "Memory AC000000-AC003FFF, AC005000-AC005FFF,
     * AC004000-AC004FFF" (allocated, as the boot configuration). */
    static const INST_U32 lo[3] = { 0xAC000000UL, 0xAC005000UL, 0xAC004000UL };
    static const INST_U32 hi[3] = { 0, 0, 0 };
    const char *names[2];
    const char *texts[2];
    INST_STORFIND f;
    INST_U32 b;
    INST_U32 d;
    INST_U32 fn;

    inst_storfind_parse(bios, &f);
    CHECK(f.State == INST_SF_BIOS && f.HaveFound && f.Bus == 4 && f.Dev == 0 &&
              f.Fn == 0 && f.HaveBar && f.BarLo == 0xAC000000UL &&
              f.BarHi == 0, "left: already set up, its BDF and BAR0");
    inst_storfind_parse(biosMe, &f);
    CHECK_EQ(f.State, INST_SF_BIOS, "the ME form is left to the BIOS too");
    inst_storfind_parse(taken, &f);
    CHECK_EQ(f.State, INST_SF_TAKEN, "taken");
    inst_storfind_parse(over, &f);
    CHECK_EQ(f.State, INST_SF_TAKEN, "taken: overridden");
    inst_storfind_parse(noAns, &f);
    CHECK_EQ(f.State, INST_SF_NOANSWER, "no answer");
    inst_storfind_parse(notRest, &f);
    CHECK_EQ(f.State, INST_SF_NOTRESTORED, "command not restored");
    inst_storfind_parse(unread, &f);
    CHECK_EQ(f.State, INST_SF_UNREADABLE, "unreadable");
    inst_storfind_parse("found none", &f);
    CHECK(f.State == INST_SF_UNKNOWN && !f.HaveFound, "nothing known");
    inst_storfind_parse("port 1.0.0 slot 0 range FE600000:00000001 len 16384; "
                        "read 256 class 060400 bars 00000000 00000000 "
                        "00000000 00000000 00000000 00000000; found 1.2.3; cmd "
                        "0006 taken; result 1", &f);
    CHECK(f.HaveBar && f.BarLo == 0xFE600000UL && f.BarHi == 1 && f.Bus == 1 &&
              f.Dev == 2 && f.Fn == 3,
          "the range's high half; a found function past the port's slot");
    inst_storfind_parse("port 1.0.0 slot 0 range 00000000:00000000 len 0; "
                        "found none", &f);
    CHECK(!f.HaveBar && !f.HaveFound, "no range, nothing found");
    CHECK(inst_storfind_name("StorFind 115.31.7", &b, &d, &fn) && b == 115 &&
              d == 31 && fn == 7, "the name, decimal");
    CHECK(!inst_storfind_name("StorFind", &b, &d, &fn) &&
              !inst_storfind_name("StorFind 1.32.0", &b, &d, &fn) &&
              !inst_storfind_name("StorFindX 1.0.0", &b, &d, &fn),
          "the last-find value and bad names are no function's");

    names[0] = "StorFind 4.0.0";
    texts[0] = bios;
    CHECK(inst_bios_held(0, 1, 10, lo, hi, 3, names, texts, 1),
          "98 SE, NVMe, Code 10, left to the BIOS, its BAR: refused");
    CHECK(!inst_bios_held(1, 1, 10, lo, hi, 3, names, texts, 1), "NT: not");
    CHECK(!inst_bios_held(0, 0, 10, lo, hi, 3, names, texts, 1), "not NVMe");
    CHECK(!inst_bios_held(0, 1, 0, lo, hi, 3, names, texts, 1),
          "started (a stale value): not");
    CHECK(!inst_bios_held(0, 1, 28, lo, hi, 3, names, texts, 1),
          "another problem: not");
    CHECK(!inst_bios_held(0, 1, 10, lo, hi, 3, names, texts, 0),
          "no StorFind value: not");
    CHECK(!inst_bios_held(0, 1, 10, lo, hi, 0, names, texts, 1),
          "no memory read: not");
    texts[0] = taken;
    CHECK(!inst_bios_held(0, 1, 10, lo, hi, 3, names, texts, 1), "taken: not");
    texts[0] = noAns;
    CHECK(!inst_bios_held(0, 1, 10, lo, hi, 3, names, texts, 1),
          "no answer: not");
    texts[0] = bios;
    names[0] = "StorFind 5.0.0";
    CHECK(!inst_bios_held(0, 1, 10, lo, hi, 3, names, texts, 1),
          "the name's BDF not the text's: not");
    names[0] = "StorFind";
    CHECK(!inst_bios_held(0, 1, 10, lo, hi, 3, names, texts, 1),
          "the last-find value alone: not");
    names[0] = "StorFind 4.0.0";
    {
        static const INST_U32 other[1] = { 0xB0000000UL };
        /* The port's slot unrelated (a bridge's zero BARs), the scan's
         * found function at the member's range: refused. */
        static const char *scanned =
            "port 3.0.0 slot 0 range AC000000:00000000 len 16384; read 256 "
            "class 060400 bars 00000000 00000000 00000000 00000000 00000000 "
            "00000000; found 4.0.0; cmd 0006 cc 00460001 csts 00000001 left: "
            "already set up; result 0";
        CHECK(!inst_bios_held(0, 1, 10, other, hi, 1, names, texts, 1),
              "another range (another boot's or another drive's): not");
        texts[0] = scanned;
        CHECK(inst_bios_held(0, 1, 10, lo, hi, 3, names, texts, 1),
              "found by a scan past the port's slot: refused by its range");
        texts[0] = bios;
    }
    names[1] = names[0];
    texts[1] = texts[0];
    names[0] = "StorFind 9.0.0";
    texts[0] = taken;
    CHECK(inst_bios_held(0, 1, 10, lo, hi, 3, names, texts, 2),
          "the matching one among several");
    CHECK(strstr(instBiosHeldDialog, "MS-DOS compatibility mode") != NULL &&
              strncmp(instBiosHeldConsole, "Not stopped: ", 13) == 0,
          "the owner's two texts");

    /* It fails closed (the merge review): each report classified. */
    CHECK_EQ(inst_storfind_report(INST_SFE_OK, "StorFind 4.0.0", 1, bios, lo,
                                  hi, 3), INST_SFR_MATCH, "a match");
    CHECK_EQ(inst_storfind_report(INST_SFE_OK, "StorFind 4.0.0", 1, taken,
                                  lo, hi, 3), INST_SFR_NOMATCH, "taken");
    CHECK_EQ(inst_storfind_report(INST_SFE_OK, "StorPollMode", 0, "", lo, hi,
                                  3), INST_SFR_SKIP, "another value");
    CHECK_EQ(inst_storfind_report(INST_SFE_OK, "StorFind", 1, bios, lo, hi,
                                  3), INST_SFR_SKIP, "the last-find value");
    CHECK_EQ(inst_storfind_report(INST_SFE_MORE, "StorFind 4.0.0", 1, bios,
                                  lo, hi, 3), INST_SFR_BAD,
             "a value too long to read: cannot tell");
    CHECK_EQ(inst_storfind_report(INST_SFE_ERROR, "", 0, "", lo, hi, 3),
             INST_SFR_BAD, "a value that did not read: cannot tell");
    CHECK_EQ(inst_storfind_report(INST_SFE_OK, "StorFind 4.0.0", 0, bios, lo,
                                  hi, 3), INST_SFR_BAD, "not REG_SZ");
    CHECK_EQ(inst_storfind_report(INST_SFE_OK, "StorFind 4.0.0", 1,
                                  "garbled", lo, hi, 3), INST_SFR_BAD,
             "a text that does not parse");
    CHECK_EQ(inst_storfind_report(INST_SFE_OK, "StorFind 5.0.0", 1, bios, lo,
                                  hi, 3), INST_SFR_BAD,
             "a found function not its name's");
    CHECK_EQ(inst_storfind_report(INST_SFE_OK, "StorFind 4.0.0", 1,
                                  "port 4.0.0 range AC000000:00000000; found "
                                  "4.0.0; cmd 0006 left:", lo, hi, 3),
             INST_SFR_BAD, "a truncated state: cannot tell");
    CHECK_EQ(inst_storfind_report(INST_SFE_OK, "StorFind 4.0.0", 1,
                                  "port 4.0.0 range AC000000:00000000; found "
                                  "4.0.0; result 1", lo, hi, 3),
             INST_SFR_BAD, "no state at all: cannot tell");
    CHECK_EQ(inst_bios_verdict(0, 1, 1, 1, 1,
                               inst_storfind_report(INST_SFE_OK,
                                   "StorFind 4.0.0", 1,
                                   "port 4.0.0 range AC000000:00000000; "
                                   "found 4.0.0; cmd 0006 left:", lo, hi, 3)
                                   == INST_SFR_BAD, 0),
             INST_BH_UNSURE, "the verdict over a truncated state: refused");
    CHECK_EQ(inst_storfind_report(INST_SFE_OK, "StorFind 4.0.0", 1, bios, lo,
                                  hi, 0), INST_SFR_NOMATCH,
             "no memory to compare (the verdict says unsure)");
    /* The verdict. */
    CHECK_EQ(inst_bios_verdict(0, 1, 1, 1, 1, 0, 0), INST_BH_GO,
             "read to the end, no match: go on");
    CHECK_EQ(inst_bios_verdict(0, 1, 1, 1, 1, 0, 1), INST_BH_HELD, "a match");
    CHECK_EQ(inst_bios_verdict(0, 1, 0, 1, 1, 0, 1), INST_BH_HELD,
             "a match whatever else failed");
    CHECK_EQ(inst_bios_verdict(0, 1, 0, 1, 1, 0, 0), INST_BH_UNSURE,
             "memory not read (or overflowing, or none): refused");
    CHECK_EQ(inst_bios_verdict(0, 1, 1, -1, 1, 0, 0), INST_BH_UNSURE,
             "the key not opened: refused");
    CHECK_EQ(inst_bios_verdict(0, 1, 1, 1, 0, 0, 0), INST_BH_UNSURE,
             "the list not read to the end: refused");
    CHECK_EQ(inst_bios_verdict(0, 1, 1, 1, 1, 1, 0), INST_BH_UNSURE,
             "a report that cannot be told: refused");
    CHECK_EQ(inst_bios_verdict(0, 1, 0, 0, 1, 0, 0), INST_BH_GO,
             "Stor98 not installed (no key): go on");
    CHECK_EQ(inst_bios_verdict(0, 0, 0, -1, 0, 3, 0), INST_BH_GO,
             "no NVMe function at Code 10 behind the port: go on");
    CHECK_EQ(inst_bios_verdict(1, 1, 0, -1, 0, 3, 0), INST_BH_GO, "NT");
    /* Every function at Code 10 needs a report of its own (the merge
     * review of 2026-11-13): none is cannot-tell, whatever else. */
    CHECK_EQ(inst_storfind_base("StorFind 4.0.0", taken, lo, hi, 3), 0,
             "a taken report names the first base");
    CHECK_EQ(inst_storfind_base("StorFind 4.0.0", bios, other4, hi, 1), -1,
             "a report of another range names none");
    CHECK_EQ(inst_storfind_base("StorFind", taken, lo, hi, 3), -1,
             "the last-find value is no function's report");
    CHECK_EQ(inst_bios_verdict2(0, 1, 1, 1, 1, 0, 0, 1), INST_BH_UNSURE,
             "a Code 10 function with no report of its own: refused");
    CHECK_EQ(inst_bios_verdict2(0, 2, 1, 1, 1, 0, 0, 1), INST_BH_UNSURE,
             "one of two with no report: refused");
    CHECK_EQ(inst_bios_verdict2(0, 1, 1, 1, 1, 0, 0, 0), INST_BH_GO,
             "every function reported (taken): go on");
    CHECK_EQ(inst_bios_verdict2(0, 1, 1, 0, 1, 0, 0, 1), INST_BH_GO,
             "no Stor98 key: go on, whatever is uncovered");
    CHECK_EQ(inst_bios_verdict2(0, 1, 1, 1, 1, 0, 1, 1), INST_BH_HELD,
             "a match stays the proven case");
    CHECK_EQ(inst_bios_verdict2(0, 0, 1, 1, 1, 0, 0, 3), INST_BH_GO,
             "no function at Code 10: go on");
    CHECK_EQ(inst_bios_verdict2(1, 1, 1, 1, 1, 0, 0, 1), INST_BH_GO, "NT");
    CHECK(strstr(instBiosUnsureDialog, "could not tell") != NULL &&
              strncmp(instBiosUnsureConsole, "Not stopped: ", 13) == 0,
          "the cannot-tell texts");
}

static void test_every_row_4938(void)
{
    INST_ENV e;
    INST_U32 v;
    char text[INST_TEXT_CHARS];
    char longText[96];
    int idx;
    int im = inst_value_find("XhciInterruptMode");
    int nr = inst_value_find("XhciIntelNoRouteIrq");
    int ir = inst_value_find("XhciIntelIrqRoute");
    int st = inst_value_find("XhciMsiStrategy");
    int pl = inst_value_find("XhciMsiPools");
    int irq = inst_value_find("XhciMsiIsaIrq");
    int na = inst_value_find("XhciUasNoAtaPassThrough");
    int tb = inst_value_find("XhciCmTunnelBridge");

    CHECK(im >= 0 && nr >= 0 && ir >= 0 && st >= 0 && pl >= 0 && irq >= 0 &&
          na >= 0 && tb >= 0, "49.38: the rows");
    if (im < 0 || nr < 0 || ir < 0 || st < 0 || pl < 0 || irq < 0 ||
        na < 0 || tb < 0) {
        return;
    }
    /* The text row's form, as the driver parses it. */
    CHECK(inst_bdf_list_ok("auto") && inst_bdf_list_ok("AUTO") &&
          inst_bdf_list_ok(""), "auto and empty");
    CHECK(inst_bdf_list_ok("4:0.0") && inst_bdf_list_ok("04:1f.7") &&
          inst_bdf_list_ok("04:00.0,05:00.0,0a:1F.3,ff:00.0"), "lists");
    CHECK(!inst_bdf_list_ok("04:20.0") && !inst_bdf_list_ok("04:00.8") &&
          !inst_bdf_list_ok("104:00.0") && !inst_bdf_list_ok("04:00") &&
          !inst_bdf_list_ok("04:00.0,") && !inst_bdf_list_ok(" 04:00.0") &&
          !inst_bdf_list_ok("04:00.0, 05:00.0") &&
          !inst_bdf_list_ok("1:0.0,2:0.0,3:0.0,4:0.0,5:0.0") &&
          !inst_bdf_list_ok("g4:00.0"), "what the driver would refuse");
    CHECK(inst_value_set("XhciCmTunnelBridge=auto", &idx, &v) == -3,
          "a text row is not a number");
    CHECK(inst_value_set_text("XhciCmTunnelBridge=04:00.0", &idx, text,
                              sizeof(text)) == 1 &&
          idx == tb && strcmp(text, "04:00.0") == 0, "/set takes a list");
    CHECK(inst_value_set_text("XhciCmTunnelBridge=04:00", &idx, text,
                              sizeof(text)) == -1, "and refuses a bad one");
    CHECK(inst_value_set_text("XhciCmMode=1", &idx, text, sizeof(text)) == 0,
          "a number row is not a text row");
    /* Mode 4: 2000 to XP x64 alone. */
    e = env(0, 4, 10, INST_ARCH_X86);
    e.AnyXhci = 1;
    CHECK(inst_value_set("XhciInterruptMode=4", &idx, &v) == 1,
          "the table takes 4");
    CHECK(!inst_value_allowed(im, &e, 4), "98 SE: not 4");
    e = env(1, 5, 1, INST_ARCH_X86);
    e.AnyXhci = 1;
    CHECK(inst_value_allowed(im, &e, 4), "XP: 4");
    e = env(1, 6, 1, INST_ARCH_X86);
    e.AnyXhci = 1;
    CHECK(!inst_value_allowed(im, &e, 4) && inst_value_allowed(im, &e, 3),
          "7: 3, not 4");
    /* The mode-4 values: 2000/XP/XP x64; the pool on the 32-bit pair. */
    e = env(1, 5, 2, INST_ARCH_AMD64);
    e.AnyXhci = 1;
    CHECK(inst_value_relevant(st, &e) && !inst_value_greyed(st, &e),
          "XP x64: the strategy");
    CHECK(inst_value_greyed(pl, &e) && inst_value_set_mask(pl, &e) == 0,
          "XP x64: no pool");
    CHECK(strcmp(inst_value_only(pl), "32-bit Windows 2000 and XP only") == 0,
          "its text");
    CHECK(!inst_value_allowed(irq, &e, 24) && inst_value_allowed(irq, &e, 11),
          "XP x64: no input above 23");
    e = env(1, 5, 1, INST_ARCH_X86);
    e.AnyXhci = 1;
    CHECK(inst_value_allowed(irq, &e, 24) && !inst_value_allowed(irq, &e, 9),
          "XP: 24 up, not 9");
    e = env(0, 4, 10, INST_ARCH_X86);
    e.AnyXhci = 1;
    CHECK(inst_value_greyed(st, &e) && !inst_value_allowed(irq, &e, 10),
          "98 SE: greyed and refused");
    /* The routing rows: hidden with no reviewed controller; NoRouteIrq 0
     * not offered by Setup on Windows 2000. */
    e = env(1, 5, 0, INST_ARCH_X86);
    e.AnyXhci = 1;
    CHECK(!inst_value_relevant(ir, &e) && !inst_value_greyed(ir, &e),
          "no reviewed controller: no routing row");
    e.PirqController = 1;
    CHECK(inst_value_relevant(ir, &e) && inst_value_relevant(nr, &e),
          "a reviewed controller");
    CHECK_EQ(inst_value_set_mask(nr, &e), 0x6, "2000: NoRouteIrq 1 and 2");
    e.Minor = 1;
    CHECK_EQ(inst_value_set_mask(nr, &e), 0x7, "XP: 0 to 2");
    e = env(1, 6, 1, INST_ARCH_X86);
    e.AnyXhci = 1;
    e.PirqController = 1;
    CHECK(inst_value_greyed(nr, &e) && inst_value_set_mask(nr, &e) == 0,
          "7: NoRouteIrq greyed");
    /* The drive rows: the USB Tree's alone. */
    CHECK(instValues[na].Loc == INST_LOC_DRIVE && instValues[na].Asked == 0,
          "a drive row: never asked, a drive's key");
    CHECK(inst_value_set("XhciUasNoAtaPassThrough=1", &idx, &v) == 0,
          "and not Setup's /set");
    inst_caution_text(tb, 0, longText, sizeof(longText));
    CHECK(strncmp(longText, "a list replaces", 15) == 0, "its caution");
}

int main(void)
{
    test_every_row_4938();
    test_set_show_4938();
    test_set_show_nine();
    test_event_ring_4938();
    test_bios_held();
    test_log_place();
    test_copy_rows_4622();
    test_prog();
    test_prog_shrink();
    test_resident();
    test_tbt_page();
    test_help();
    test_versions();
    test_models_rule();
    test_cm_only();
    test_decorations();
    test_reg_hex();
    test_real_infs();
    test_cm_infs();
    test_stor98_infs();
    test_parser();
    test_ids();
    test_long_ids();
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
    test_tb_lock_4622();
    test_cm_xhci_4612();
    test_board_4612();
    test_cmf_place();
    test_family();
    test_tray();
    test_fast();
    test_wrap();
    test_questions();
    test_dir_check();
    test_to_switch();
    test_other_pkgs();
    test_gate();
    test_di_helper();
    test_mw_values();
    test_greyed();
    test_summary_head();
    test_uas();
    test_uas_absent();
    test_takeover();
    test_uninstall_entry();
    test_page_texts();
    test_secure_boot_refusals();
    test_s98();
    test_s98_inf_defaults();
    test_s98_tunnel();
    test_marks();
    test_storage();
    test_vsd();
    test_pfw_inst();
    test_vsd_names();
    test_wininit_text();
    test_other_sets();
    test_names();
    test_tmp_named();
    test_stor_short();
    test_9x_stage();
    printf("test_inst: %d checks, %d failures\n", checks, failures);
    return failures;
}
