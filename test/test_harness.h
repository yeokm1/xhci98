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

#define CHECK_EQ(got, want, what) \
    check_eq_impl((unsigned long)(got), (unsigned long)(want), (what), \
                  __FILE__, __LINE__)

static void check_eq_impl(unsigned long got, unsigned long want,
                          const char *what, const char *file, int line)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s:%d: %s (got %lu / 0x%lX, want %lu / 0x%lX)\n",
               file, line, what, got, got, want, want);
    }
}

#endif /* XHCI_TEST_HARNESS_H */
