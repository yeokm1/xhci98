/*
 * test_harness.h - the one copy of the check helpers the host suites share.
 *
 * There used to be twelve, one per test file, and they had diverged: three
 * different failure formats, a hard-coded file-name string literal in each
 * `printf` instead of the standard macro, and a condition normalised to 0 or 1
 * in three files (`test_desc.c`, `test_log.c`, `test_topo.c`) and passed
 * straight into an `int` parameter in the other nine. Nothing had diverged
 * behaviourally on this host, where `ULONG` is 32 bits and the truncation into
 * `int` is bit-preserving - but the un-normalised form is only accidentally
 * right, and a value wider than `int` passed to it is false on every multiple
 * of 2^32. That is the 2026-09-07 audit's G15.
 *
 * Header-only and all-static on purpose. Each test is a separate program with
 * its own `main`, built and run separately by `test\run-host-tests.cmd`, so
 * each translation unit gets its own `checks` and `failures` and there is
 * nothing to link. Per design record 03 this file contains no expectations of
 * its own: it only counts and reports.
 *
 * C89, no framework.
 *
 *   CHECK(cond, what)          a truth check
 *   CHECK_EQ(got, want, what)  an equality check; both operands are printed in
 *                              decimal and hex, because half the expectations
 *                              in these suites are register values and half
 *                              are counts
 *
 * Both macros pass `__FILE__` and `__LINE__` from the expansion site, so a
 * failure names the .c file and the line inside it rather than this header.
 * `check_impl` and `check_eq_impl` are also called directly where a check is
 * made inside a helper and should report the CALLER's position; pass
 * `__FILE__` and `__LINE__` yourself there.
 *
 * Each file still prints its own summary line and returns `failures` from
 * `main`: the runner reads the exit code, and one file names itself there.
 */

#ifndef XHCI_TEST_HARNESS_H
#define XHCI_TEST_HARNESS_H

#include <stdio.h>

static int failures;
static int checks;

/*
 * `(cond) != 0` rather than `(cond)`: the parameter is an `int`, and a caller
 * passing a wider type would otherwise have it truncated at the call. The
 * comparison is made in the caller's own type, where it is exact.
 */
/*
 * **Both macros take a `what` the failure line prints**, and design record 03
 * section 5 describes them as `CHECK(cond)` / `CHECK_EQ(got, want)`. The
 * description is the one that is wrong; the third argument is what makes a
 * failure readable without opening the file (the 2026-09-16 audit's C6).
 */
#define CHECK(cond, what) \
    check_impl(((cond) != 0), (what), __FILE__, __LINE__)

static void check_impl(int cond, const char *what, const char *file,
                       int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s:%d: %s\n", file, line, what);
    }
}

/*
 * `unsigned long` is 32 bits on both architectures MSVC targets, so on the
 * amd64 leg a pointer-sized operand (a `sizeof`, an offset past 4 GB, a
 * `ULONG_PTR`) would be truncated at the cast and two values differing only
 * above bit 31 would compare equal (the 2026-09-17 audit's E2). The operand
 * type follows the pointer width; the x86 leg's type and output are the ones
 * it always had.
 */
#ifdef _WIN64
typedef unsigned __int64 check_eq_t;
#define CHECK_EQ_FMT "FAIL %s:%d: %s (got %I64u / 0x%I64X, want %I64u / 0x%I64X)\n"
#else
typedef unsigned long check_eq_t;
#define CHECK_EQ_FMT "FAIL %s:%d: %s (got %lu / 0x%lX, want %lu / 0x%lX)\n"
#endif

#define CHECK_EQ(got, want, what) \
    check_eq_impl((check_eq_t)(got), (check_eq_t)(want), (what), \
                  __FILE__, __LINE__)

static void check_eq_impl(check_eq_t got, check_eq_t want,
                          const char *what, const char *file, int line)
{
    checks++;
    if (got != want) {
        failures++;
        printf(CHECK_EQ_FMT, file, line, what, got, got, want, want);
    }
}

#endif /* XHCI_TEST_HARNESS_H */
