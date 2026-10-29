/*
 * test_mark.c - the mark service's pure half (xhcimark\mark_core.c) run on
 * the build host through its own self-test, mk_selftest: the answers'
 * parsers, the write's checks, the restart's wish, the reconciliation, the
 * records, the Stor98 fallback's rules and, since task 45.12, the bridge
 * inventory and the bridge marks' decisions (design record 33 section 4.3,
 * section 10.1's service vectors). The same cases XHCIMARK.EXE -selftest
 * runs in the gated build, here without running a project executable.
 */

#include <stdio.h>
#include "../xhcimark/mark_core.h"
#include "test_harness.h"

static void report(int ok, const char *what)
{
    check_impl(ok != 0, what, __FILE__, __LINE__);
}

int main(void)
{
    int fails;

    fails = mk_selftest(report);
    CHECK_EQ((unsigned long)fails, 0, "mk_selftest reports no failure");
    printf("test_mark: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
