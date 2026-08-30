/*
 * xhci_dbg.h - the `qemu` flavour's trace channel.
 *
 * *(Titled "debug-build trace channel" until the post-Phase 13 review rounds, and the two sentences
 * that followed it said the imports were "real in the two checked flavours" -
 * which contradicted the parenthetical four lines below and has been wrong since
 * task 13-L.1. `debug` is a checked build and gets none of this.)*
 *
 * Everything here compiles to nothing outside `qemu`, so **neither published
 * binary carries a call site from this file**. The imports below are real in
 * `qemu` alone and must be cleared by the post-link import gate (Phase 3
 * task 5) before either VM sees the binary:
 *
 *   NTOSKRNL.EXE  DbgPrint                from HERE, qemu only  (the trace)
 *   HAL.DLL       KeGetCurrentIrql        from HERE, qemu only  (the IRQL field)
 *   HAL.DLL       WRITE_PORT_UCHAR        from HERE, qemu only  (the 0xE9 mirror)
 *
 * "qemu only" is about THIS FILE's call sites, not about the binary's import
 * table: the allowlist carries the first two as `all required` and only
 * WRITE_PORT_UCHAR as `qemu required`. The parenthesis below is why, and the
 * distinction is worth the words - reading this table as an import list is how
 * a bisect step came to claim `XHCI_DBG_NO_IRQL` still removes a symbol.
 *
 * (Both of the first two are imported by every flavour anyway, for reasons
 * next door: `DbgPrint` for the log's PASSIVE bulk hand-over and
 * `KeGetCurrentIrql` for the guard that measures whether it may run. This file
 * adds neither of them to a published binary; it only adds call sites.)
 *
 * **THE WHOLE OF THIS FILE IS THE `qemu` FLAVOUR'S, and that is roadmap task
 * 13-L.1's rule rather than a tidy-up.** Two things used to hang off `DBG`
 * together and had to stop:
 *
 *   - the port-0xE9 mirror, which needs `WRITE_PORT_UCHAR` - the sole import
 *     delta between `0.0.0.4`'s two published binaries, of which the debug one
 *     gives the ThinkPad E460 a **Code 2** and loads nothing at all. **Why that
 *     build failed is not established**; that is defect 2b;
 *   - the **live per-line trace**, which is what bugchecks Windows 98 on metal
 *     - `0028:C208D79D` (hub), `0028:C207B26D` (USB Audio), `0028:C20A3F4D`
 *     (Low-Speed mouse), three device classes, all on the E460.
 *     That is a *runtime* hazard where the first is a *load-time*
 *     one, and they are not the same failure.
 *
 * `debug` is the flavour a user with a problem is told to install, so it must
 * survive both. It therefore carries **neither**: `XHCI_DBG_LIVE` is defined by
 * `src/sources` for the `qemu` flavour and by nothing else, and without it
 * every macro below compiles to nothing. What `debug` keeps is the DDK's own
 * `DBG`, and what that is worth here is **smaller than it sounds** - this
 * comment said "asserts, no optimisation" until the post-Phase 13 review rounds and both halves were
 * wrong. **Both flavours build `/Oxs`**: the checked build differs by `/Oy-`
 * against `/Oy`, so it keeps frame pointers and is otherwise optimised the same
 * (compare `src/buildchk.log` and `src/buildfre.log`, which print the line).
 * And this driver has **no runtime `ASSERT`** - every `XHCI_C_ASSERT` in it is
 * compile-time and fires in every flavour. What `DBG` buys, then, is the
 * readable stack `/Oy-` leaves, `VS_FF_DEBUG` in the version resource, the
 * `XHCI_SNAPSHOT_B_DEBUG` bit a snapshot reports, and whatever the DDK's own
 * checked headers change - and the measured compiler delta is exactly `-DDBG=1`,
 * `-DFPO=0` against `-DFPO=1`, the flavour define, and `/Oy-` against `/Oy`.
 * **The ESP probe gate in `xhci_dispatch.c` was in this list until the post-Phase 13 review rounds
 * and is not `DBG`'s any more**: it reports through `XHCI_DBG_VALUE`, which
 * compiles to nothing without `XHCI_DBG_LIVE`, so under `DBG` alone `debug`
 * spent the instructions on a report it could not make. It is now guarded on
 * `XHCI_DBG_TRACE` with the rest of this channel, which makes it `qemu`'s.
 * *(Two of those five were missed when this paragraph was written, and
 * it also called `VS_FF_DEBUG` the only thing that tells two published binaries
 * apart - the `XHCI98_FLAVOUR_*` marker does too, and is the one that can name
 * three flavours rather than two.)* What `debug` is *for* is
 * `src/xhci_log.c`'s ring, which is recorded from any IRQL and handed over in
 * exactly one bulk dump from the PASSIVE flush. **Recording is not emission**,
 * and this file is the emission half.
 *
 * *(Design record 08 section 5's table has said "debug: live per-line
 * emission - no" since the split was designed. It was not true in the first
 * implementation of it, which gated only the `0xE9` half; a Codex review of
 * commit `efa86a3` caught that, and this is the repair.)*
 *
 * `XHCI_DBG_NO_IRQL` survives as a negative and is **not** a flavour axis -
 * but it is no longer the fallback it was built as. It once removed
 * `HAL.dll!KeGetCurrentIrql` from the image; since task 11-V.7 the log flush
 * guard (`xhciLogAtPassive`, `src/xhci_dispatch.c`) calls that symbol in every
 * flavour, so the allowlist carries it as `all required` and this define now
 * changes only what a trace line prints in its `irql=` field - `0xFF` instead
 * of the measured value. It cannot be used to bisect a target that rejects
 * that symbol. *(This comment called it a fallback for exactly that until
 * a later review.)*
 *
 * Output goes to two places at once (docs/contributing/build-and-test.md "Debugging"):
 * DbgPrint, which DebugView captures inside a Win9x guest and a kernel
 * debugger captures on Win2000, and I/O port 0xE9, which QEMU writes straight
 * to the host with no guest-side setup. **Whether real hardware ignores that
 * port is NOT established**; it is one of the two readings defect 2b is still
 * owed (`docs/contributing/runs/run-13e.md` P6).
 *
 * C89 only. IRQL: any - no allocation, no waits, no dereference of anything
 * the caller did not already own.
 */

#ifndef XHCI_DBG_H
#define XHCI_DBG_H

#include "xhci_compat.h"

/* How many times each individual trace site prints before going quiet. The
 * periodic callbacks (CheckController every 500 ms, Get32BitFrameNumber per
 * URB) would otherwise bury the lifecycle sequence the spike is there to
 * read. */
#define XHCI_DBG_CALL_LIMIT 4

/*
 * The same bound for a value that is worth watching change - a counter, a
 * state word. Larger than XHCI_DBG_CALL_LIMIT because the interesting part of
 * such a value is usually not its first four samples, and the CHANGED macro
 * only spends the budget when the value actually moves.
 */
#define XHCI_DBG_VALUE_LIMIT 32

/*
 * **`DBG` is not enough on its own** - see the header note. `qemu` is the only
 * flavour that defines `XHCI_DBG_LIVE`, and `debug` reaching this block would
 * put the per-line profile that bugchecks Windows 98 metal into the binary a
 * user is told to install.
 *
 * `XHCI_DBG_TRACE` is that compound condition under one name, and it exists
 * because **a file that calls `XhciDbg*` directly rather than through a macro
 * has to test the same thing**. `src/xhci_probe.c` has two such sites - they
 * want the callback line that carries `irql=`, which no macro produces - and
 * they were guarded on `#if DBG` alone, so the first cut of the three-flavour
 * split left them referring to functions the debug build no longer compiles.
 * Use `#ifdef XHCI_DBG_TRACE`, never `#if DBG`, for anything that touches this
 * channel.
 */
#if DBG && defined(XHCI_DBG_LIVE)
#define XHCI_DBG_TRACE 1
#endif

#ifdef XHCI_DBG_TRACE

VOID XhciDbgText(const char *msg);
VOID XhciDbgValue(const char *msg, ULONG value);
VOID XhciDbgCallback(const char *name, ULONG a, ULONG b, ULONG c);
VOID XhciDbgWords(const char *msg, const ULONG *words, ULONG count);

#define XHCI_DBG_TEXT(m)        XhciDbgText(m)
/*
 * Unbounded: one line every time it is reached. Correct for a site that runs
 * once per start or once per DriverEntry, wrong for anything a timer or a
 * per-URB callback can reach - use XHCI_DBG_VALUE_CHANGED there. The Phase 3
 * spike learned this the expensive way: two unbounded counter lines in
 * CheckController, which usbport calls every 500 ms, wrote 14,000 lines in an
 * hour on an idle guest and buried the lifecycle sequence the log existed for.
 */
#define XHCI_DBG_VALUE(m, v)    XhciDbgValue((m), (ULONG)(ULONG_PTR)(v))
#define XHCI_DBG_WORDS(m, p, n) XhciDbgWords((m), (p), (n))

/*
 * Trace a value only when it differs from the last one this site printed, at
 * most XHCI_DBG_VALUE_LIMIT times. Both halves matter: "on change" is what
 * makes an idle periodic callback silent, and the cap is what keeps a value
 * that changes on every call from replacing one flood with another. The state
 * is per expansion, like XHCI_DBG_CB's counter, so sites quiet down
 * independently. The first sample always prints, including a zero one.
 *
 * **The state being per expansion also means it is per *driver image*, not per
 * controller**, and that is a trap on a machine with two xHCI controllers. Both
 * bind this one image, so both reach the same expansion with their own
 * extension's value: if the two values differ, successive calls alternate,
 * every sample "changed", and the site spends its whole budget in the time it
 * takes to make 32 calls - about eight seconds for a 500 ms callback. A value
 * that is monotone or quiet *per controller* is not therefore quiet at the
 * site. Every XHCI_DBG_VALUE_CHANGED in xhciCheckController has this property;
 * it costs nothing on the single-controller test VMs and is a real limit on
 * multi-controller hardware (docs/contributing/design/03-host-unit-tests.md). Where a
 * site must survive that, gate it on something semantic and drop the budget
 * entirely, as the first-decode site in src/xhci_rh.c does.
 */
#define XHCI_DBG_VALUE_CHANGED(m, v)                                \
    {                                                               \
        static ULONG xhciDbgSeen = 0;                               \
        static ULONG xhciDbgLast = 0;                               \
        ULONG xhciDbgNow = (ULONG)(ULONG_PTR)(v);                   \
        if (xhciDbgSeen < XHCI_DBG_VALUE_LIMIT &&                   \
            (xhciDbgSeen == 0 || xhciDbgNow != xhciDbgLast)) {      \
            xhciDbgSeen++;                                          \
            xhciDbgLast = xhciDbgNow;                               \
            XhciDbgValue((m), xhciDbgNow);                          \
        }                                                           \
    }

/*
 * Trace a value on every occurrence, at most XHCI_DBG_VALUE_LIMIT times.
 *
 * The site this exists for is edge-driven and its *value repeats*: a Port
 * Status Change event names a port, and an unplug always names the port its
 * plug did, so XHCI_DBG_VALUE_CHANGED suppressed every unplug as "unchanged"
 * and the Phase 4 checkpoint's "plugged/unplugged" clause was carried by the
 * event counter alone on all three VMs (LESSONS). "Changed" is the
 * right gate for a value that is polled and boring; it is the wrong gate for an
 * occurrence that happens to carry a repeating value. The bound is what keeps
 * it from becoming the 14,000-lines-an-hour flood XHCI_DBG_VALUE would be on a
 * bouncing port - and because the bound *does* eventually silence the site, the
 * matching counter belongs in the CheckController block, where it stays
 * reconcilable after this has gone quiet.
 *
 * **That last clause is a requirement, not advice, and Phase 5 task 7 is why.**
 * The budget is a driver-image static that no start, stop or resume resets, so
 * it is spent for the life of the load - and a site whose value only a debug
 * build can see, with nothing behind it, is evidence that disappears exactly
 * when a long run needs it. Win98 idle-suspends within about half a second of a
 * start and every resume re-seeds the root hub, so a per-port site can burn all
 * 32 before an operator plugs anything in. When adding a _LIMITED site, give it
 * a durable witness in the same change; the host suite cannot catch its absence
 * (docs/contributing/design/03-host-unit-tests.md).
 *
 * **A counter in xhciCheckController is the usual witness but not the
 * strongest, and XHCI_DBG_VALUE_CHANGED does not exempt it from any of this.**
 * A counter that moves for reasons unrelated to the thing being diagnosed
 * spends 32 prints on churn and is just as silent, and the per-expansion state
 * noted above makes it per image rather than per controller. Phase 5 task 7
 * took four attempts; the three rejected ones are worth naming, because each
 * looked sufficient when written:
 *
 *   - a running total, which moved on every resume - the root hub is rebuilt
 *     and every connected port re-decoded - and Win98 idle-suspends within
 *     about half a second of a start;
 *   - a tally of what is attached *now*, which is no better: a resume really
 *     does take the bus down (HCRST clears PP on every port, the ports are
 *     re-powered, and a device is not re-detected the instant the seed reads
 *     it), so the tally dips and returns. Change-gating does not save it once
 *     more than one port is populated, because several distinct values cycle
 *     round and each differs from the one before it;
 *   - any periodic print of a per-controller value at all, once a second xHCI
 *     controller exists - see the per-image note on XHCI_DBG_VALUE_CHANGED.
 *
 * What works is a gate that is **semantic rather than budgetary**: a value that
 * is monotone within a start, such as a set of "speeds seen", changes at most
 * once per member however the bus behaves afterwards, so the site needs no
 * budget and therefore needs no witness behind it either - which is why task
 * 7's first-decode site in src/xhci_rh.c has no xhciCheckController counter and
 * should not be given one. Ask what else moves the value, not just whether the
 * interesting event moves it. And note that a set makes its own gate untestable
 * - OR is idempotent, so a site firing on every occurrence leaves the same
 * value - so count the firings too if the gate is what you are relying on.
 */
#define XHCI_DBG_VALUE_LIMITED(m, v)                                \
    {                                                               \
        static ULONG xhciDbgSeen = 0;                               \
        if (xhciDbgSeen < XHCI_DBG_VALUE_LIMIT) {                   \
            xhciDbgSeen++;                                          \
            XhciDbgValue((m), (ULONG)(ULONG_PTR)(v));               \
        }                                                           \
    }

/*
 * Trace a callback entry, at most XHCI_DBG_CALL_LIMIT times per site. The
 * counter is per expansion, so every callback quiets down independently and
 * the first few of each still appear no matter what order usbport calls them
 * in. Arguments are logged as values only - never dereferenced.
 */
#define XHCI_DBG_CB(name, a, b, c)                                  \
    {                                                               \
        static ULONG xhciDbgSeen = 0;                               \
        if (xhciDbgSeen < XHCI_DBG_CALL_LIMIT) {                    \
            xhciDbgSeen++;                                          \
            XhciDbgCallback((name),                                 \
                            (ULONG)(ULONG_PTR)(a),                  \
                            (ULONG)(ULONG_PTR)(b),                  \
                            (ULONG)(ULONG_PTR)(c));                 \
        }                                                           \
    }

#else /* every flavour but qemu */

#define XHCI_DBG_TEXT(m)
#define XHCI_DBG_VALUE(m, v)
#define XHCI_DBG_VALUE_CHANGED(m, v)
#define XHCI_DBG_VALUE_LIMITED(m, v)
#define XHCI_DBG_WORDS(m, p, n)
#define XHCI_DBG_CB(name, a, b, c)

#endif /* XHCI_DBG_TRACE */

#endif /* XHCI_DBG_H */
