/*
 * xhci_log.h - the optional in-memory log ring (src/xhci_log.c), roadmap
 * tasks 11-V.7 (the carrier) and 11-V.9 (what it carries, and its two sinks).
 *
 * ## The two values, and the one rule that makes them work (task 13-L.2)
 *
 * 11-V.7 built the carrier. 11-V.9 added the producer set - connect,
 * disconnect, reset, slot address, endpoint open, route string, error
 * completions - and two sinks. **Task 13-L.2 removed one of those sinks and
 * separated what is RECORDED from what is EMITTED**, which is the defect the
 * rest of this file used to have:
 *
 *   - **`XhciLogVerbosity`** (`DWORD`, **0** by default) is the 0-4 ladder and
 *     it is the whole switch: **0 is off outright** - the PassThru read channel
 *     (`xhciPassThru` in src/xhci_dispatch.c, the wire format in src/xhci.h)
 *     answers exactly `MP_STATUS_NOT_SUPPORTED`, which is what a binary built
 *     without it would say - **1 engages the channel with the ring still off**,
 *     **2 is the recording switch** and fills the note ring, and 3 and 4 raise
 *     what the tool's plain-text companion publishes. The raw window itself
 *     carries the whole extension, kernel addresses included, at any level
 *     from 1 up; only the ring's ADDRESS records and the plain-text companion
 *     hold them back below 4. The channel is in
 *     **every** build flavour; what the flavour decides is how much there is to
 *     read, not whether the door exists.
 *   - **`XhciLogDebugView`** (`DWORD`, 0 by default) is an **emission** switch
 *     and nothing else: it hands the ring to `DbgPrint` **from the PASSIVE
 *     flush only**, never as live mirroring. See the box on it below.
 *   - **`XhciLogSnapshot` was retired** and its one job - consent
 *     to the read channel - became rung 1 of the ladder above. It was a pure
 *     consent bit: the channel serves everything or nothing, so the value
 *     carried no width, and consent and depth **nest** (you cannot want depth
 *     without consent, and consent-without-ring is a rung rather than an axis),
 *     so folding it in is not the axis-conflation this driver spent task 13-L.1
 *     undoing one level up. Design record 08 section 13.2's dated amendment
 *     carries the reasoning and the security posture.
 *   - **`XhciLogFile` is retired**, with `ZwCreateFile`, `ZwWriteFile` and
 *     `ZwClose`. Not because those imports were dangerous - they resolve on the
 *     E460 - but because **the file sink never wrote a byte on any real
 *     machine, on either target**: task 11-V.7 ran on the 2a and 2b guests, so
 *     both halves of its record are virtual-machine readings. The log file
 *     itself moved to ring 3, where `XHCISNAP` writes it and names it from the
 *     command line.
 *   - **`XhciLogEnable` was retired earlier**, by 11-V.9. A redundant master
 *     switch is how two switches end up disagreeing.
 *
 * **RECORDING IS NOT EMISSION, and gating the first on the second was the
 * defect.** `Enabled` used to be `FileEnabled || DebugViewEnabled`, so naming a
 * sink was what switched recording on - and on Windows 98 neither sink can
 * honestly be selected, so the ring this project built to hold exactly the
 * evidence a user would send stayed empty on the one operating system it was
 * built for. The instrument's own first operating trap was a maintainer setting
 * `XhciLogDebugView` to 1 for a sink he knew was dead, purely to make the ring
 * fill. That is not an instruction a user can be given, and this is what
 * replaces it.
 *
 * The ring, the record format, the flush decision, the wrap accounting, the
 * per-code budget and the switch arithmetic are here and in src/xhci_log.c. The
 * registry read, the IRQL measurement and the `DbgPrint` emit are in
 * src/xhci_dispatch.c. **There is no file I/O anywhere in this driver.**
 *
 * ## Why this exists
 *
 * Until now the driver's only trace channel is `DbgPrint` reaching a user-mode
 * capture agent, i.e. DebugView - and on Windows 98 bare metal DebugView
 * **bugchecks the machine** as soon as any device is plugged in, at real
 * interrupt rates (roadmap task 12.2; `docs/contributing/build-and-test.md`, "Getting a
 * trace off a bare-metal machine"). So an end user who hits a fault today has
 * no way to produce anything a maintainer can read. This is that way.
 *
 * **And on Windows 98 it is now that way too, through a different door.** Batch
 * 11-V measured that neither sink delivers there: the file sink declined by
 * design (in a guest - see above), and the DebugView sink is handed the ring
 * only at `StopController`, which on that system is only ever the shutdown, by
 * which time Windows has already closed the capture program. **Neither of those
 * changed.** What changed in task 13-L.2 is that the ring no longer needs a
 * sink to be worth filling: it is inside `XHCI_EXTENSION`, and the PassThru
 * snapshot channel reads the extension off a running machine from user mode -
 * through a door `usbport.sys` owns, which is why this driver can use it
 * without owning a driver object of its own. So on Windows 98 the answer is
 * `XHCISNAP`, not a sink; on Windows 2000 the DebugView sink also works at a
 * stop that is a Device Manager disable. `docs/using/release-notes.md`.
 *
 * ## The shape is forced, not chosen
 *
 * Two independent constraints decide it, and both are load-bearing rules rather
 * than preferences:
 *
 *   1. **A file sink would mean `Zw*` imports**, each of which needs its own
 *      Windows 98 evidence before it may appear in the binary
 *      (`scripts/import-gate/xhci98-imports.allow`). Task 13-L.2 removed the
 *      sink and its three imports, so this constraint is now met by having no
 *      file I/O at all rather than by paying it.
 *   2. **There is no PASSIVE-level worker available to this driver.**
 *      `IoAllocateWorkItem`/`IoQueueWorkItem` are on the import gate's deny
 *      list as absent on Win98, and `UsbPortRequestAsyncCallback` callbacks run
 *      at **DISPATCH_LEVEL** (`docs/usb-xhci-info/usbport-miniport-abi.md` section 7,
 *      confirmed in the NUSB binary at `0002785E`).
 *
 * The only shape that survives both is a **bounded in-memory ring, appended
 * from any IRQL, handed over to its sink only from a PASSIVE-level lifecycle
 * callback** - never a per-line write. That is what this file is.
 *
 * *(This said "flushed to the file" until the post-Phase 13 review rounds. Task 13-L.2 retired the
 * ring-0 file sink; the surviving sink is the bulk `DbgPrint`
 * hand-over in `src/xhci_dispatch.c`. **Every other word of that shape is
 * unchanged and is what the constraints above buy** - bounded, any-IRQL append,
 * one PASSIVE hand-over, no per-line write - which is why only the destination
 * is corrected.)*
 *
 * ## What is here and what is next door
 *
 * This file and `src/xhci_log.c` are **pure core** in the design doc 03 section
 * 2 sense: computation over caller-supplied state, no MMIO, no DDK, no IRQL, no
 * usbport service, no lock, and above all **no file I/O**. The ring, its wrap
 * accounting, the record formatting and the flush *decision* live here so the
 * host suite can drive every one of them.
 *
 * The things that need the DDK live in `src/xhci_dispatch.c`:
 *
 *   - reading the switch, through the registration packet's
 *     `UsbPortGetMiniportRegistryKeyValue` (**no new import** - see the task
 *     11-V.7 box in `docs/usb-xhci-info/usbport-miniport-abi.md` section 6);
 *   - measuring the IRQL at the flush site;
 *   - the `DbgPrint` hand-over the surviving sink is made of.
 *
 * *(There was a third: `ZwCreateFile`/`ZwWriteFile`/`ZwClose`. They left with
 * the file sink in task 13-L.2 and this list still named them until
 * the merge, contradicting the "no file I/O anywhere" sentence above it.)*
 *
 * ## The IRQL of a flush is measured, not assumed
 *
 * Task 11-V.7 says to derive which callbacks are PASSIVE from the binaries
 * rather than assuming `StartController`/`StopController` are. That derivation
 * was **started and not finished** - see the note in `src/xhci_log.c` - so this
 * driver does not rest on it: the flush reads `KeGetCurrentIrql()` and refuses
 * above PASSIVE_LEVEL, counting the refusal. A diagnostic path that blocks from
 * DISPATCH is a bugcheck *in the diagnostic path*, which is worse than having
 * no diagnostic path at all (11-V.7's own stop rule says so), and a measured
 * refusal is the only form of that guarantee that cannot be wrong about the
 * other side.
 *
 * *(The hazard here was named as "a driver that writes a file from DISPATCH"
 * until the post-Phase 13 review rounds, which is a call this binary has not made since task 13-L.2
 * retired the file sink. **The guard did not change and neither
 * did the reason for measuring rather than assuming**; what it protects is now
 * the `DbgPrint` hand-over, and `src/xhci_dispatch.c`'s flush ordering already
 * said so - "the hazard the retired `ZwCreateFile` used to stand for here, and
 * the one that outlived it". The two files describe one guard and now name one
 * hazard.)*
 *
 * The consequence is deliberate and is what batch 11-V measures: if
 * `LogFlushesRefusedIrql` is nonzero on a target and `LogFlushes` is zero
 * there, then no PASSIVE flush context exists on it and 11-V.7's stop rule -
 * publish the limitation - is taken **with a measurement behind it**.
 *
 * ## Both builds carry it, and both fill it the same way
 *
 * The ring is in every flavour, and that is the whole point rather than a
 * convenience. Shipping a binary whose diagnostics are a per-line trace would
 * reproduce the exact fault this task exists to route around: per-line
 * `DbgPrint` from DPC and ISR contexts is what bugchecks Win98 metal. So the
 * ring gets a small always-on producer set - the explicit
 * `XhciLogNote`/`XhciLogNoteLocked` call sites, which never call `DbgPrint` -
 * and that set is identical in all three flavours.
 *
 * *(This paragraph said "the release build as well as the debug one" and named
 * debug-build `DbgPrint` as the hazard, until the post-Phase 13 review rounds. Since task 13-L.1 the
 * live trace belongs to the `qemu` flavour alone, so the shipping `debug` build
 * has no trace to keep out and the argument is about the profile rather than
 * about a flavour. What it says about the two channels being disjoint is
 * unchanged and is the point.)*
 *
 * **The traced build's own output does not enter the ring**, and an earlier draft
 * of this comment said it did. `XhciDbgEmit` (src/xhci_dbg.c) writes port `0xE9`
 * and `DbgPrint` and touches no log; `XHCI_DBG_*` compiles to nothing in either
 * published build. So the two channels are disjoint - the `qemu` build has a
 * live per-event trace *as well as* the ring, not a copy of one in the other -
 * and a `XHCI_DBG_VALUE` site is **not** evidence in a stored log. Anything the
 * log has to carry needs its own `XhciLogNote`, in every flavour, which is why
 * the producer set is chosen rather than inherited.
 *
 * C89 only.
 */

#ifndef XHCI_LOG_H
#define XHCI_LOG_H

#include "xhci_compat.h"

/*
 * The ring, in bytes. Sized against what it has to survive rather than picked,
 * and **resized by task 11-V.9 from 4,096 to 16,384 for a measured reason**:
 * 4 KB at roughly 40 bytes a record is about 100 records, and the producer set
 * that task adds spends that before an ordinary machine is idle. One hub plus
 * three devices enumerating is, per device, a connect, a reset begin and end, a
 * slot enable, an address, a route string and one record per endpoint - and the
 * counter block appended at flush is a little over 1 KB on top. 16 KB holds a
 * whole start, a four-class enumeration and the flush record with room for a
 * fault sequence in front of it.
 *
 * It lives in the miniport extension, which usbport allocates and zeroes - this
 * driver allocates no pool (AGENTS.md) - so this is 12 KB of non-paged pool per
 * controller, spent whether or not the log is switched on. That is the honest
 * cost and it is stated in docs/using/release-notes.md rather than left here.
 *
 * **Changing it moves the extension layout**, so both offset tables
 * (scripts\vm-matrix\gen-offsets.ps1 and scripts\local\offsets.txt) have to be
 * regenerated and agree, and the staged install media rebuilt, in the same
 * change. `MiniPortExtensionSize` in the first trace of a session is the
 * cross-check.
 *
 * A power of two on purpose: the wrap is a mask, so no division reaches a
 * compiler helper that may not exist on Win98's kernel.
 */
#define XHCI_LOG_RING_BYTES 16384
#define XHCI_LOG_RING_MASK  (XHCI_LOG_RING_BYTES - 1)

/*
 * The longest **label** the formatter will emit. A label longer than this is
 * truncated rather than refused - a truncated line is still a readable line,
 * and a caller that silently produced nothing would be worse.
 *
 * **It is not the longest record**, and the name is older than that
 * distinction: only the label is capped here, so a full record is at most this
 * many bytes plus `=`, eight hex digits and a CRLF - 106 in all. Nothing
 * depends on a record-length bound (the ring wraps by mask and drops the oldest
 * byte, whatever a record's shape), which is why the constant is left named as
 * it is rather than renamed across the tree; what is corrected is the claim.
 */
#define XHCI_LOG_MAX_RECORD 96

/*
 * Why a flush was asked for. Recorded in the flush record so a capture holding
 * two flushes says which lifecycle edge produced each.
 *
 * **`SuspendController` is deliberately not a flush site**, and that is a
 * measurement rather than taste: Win98's usbport idle-suspends this controller
 * within about half a second of the last transfer and did it **29 times in a
 * single idle run** (the Phase 4 checkpoint runs), so a flush there is a bulk
 * hand-over of the whole ring every half second for the life of an idle
 * machine. The measured Win98
 * shutdown sequence is `Suspend -> DisableInterrupts -> Stop`, so stopping is
 * both the user-reachable trigger and the one that happens once. A Device
 * Manager disable reaches `Stop` on **Windows 2000**; on Windows 98 that
 * disable bugchecks the machine before the teardown completes, so the
 * user-reachable stop there is the shutdown (docs/contributing/build-and-test.md, "Do not
 * disable the controller in Device Manager").
 *
 * *(Both paragraphs said "a file" and "a file write" until the post-Phase 13 review rounds; there has
 * been no file since task 13-L.2. **The 29-suspend measurement and the
 * conclusion are unchanged** - what a flush at Suspend would cost is now a bulk
 * `DbgPrint` hand-over rather than a write, which is cheaper but is still the
 * whole ring, on a repeating half-second edge, from a callback whose IRQL this
 * driver measures rather than assumes.)*
 */
#define XHCI_LOG_REASON_STOP     0
#define XHCI_LOG_REASON_FAILURE  1

/* What XhciLogFlushBegin decided. */
#define XHCI_LOG_FLUSH_GO         0  /* drain and write                      */
#define XHCI_LOG_FLUSH_DISABLED   1  /* the switch is off                    */
#define XHCI_LOG_FLUSH_EMPTY      2  /* nothing has been appended            */

/*
 * ------------------------------------------------------------------
 * The verbosity ladder (task 13-L.2)
 * ------------------------------------------------------------------
 *
 * `XhciLogVerbosity`, `REG_DWORD`, default **0**. **Level 0 is off outright**;
 * above it each level is the one below **plus** one thing, so an engaged level
 * is always a superset and never a different report:
 *
 *   | Value | Adds                          | Audience                      |
 *   |-------|-------------------------------|-------------------------------|
 *   | 0     | NOTHING - channel answers 6   | every machine until asked     |
 *   | 1     | the channel + the counter     | ordinary user; ring still off |
 *   |       | block                         |                               |
 *   | 2     | + the note ring               | ordinary user - the log       |
 *   | 3     | + the PORTSC array            | text prints no addresses      |
 *   | 4     | + address records in the ring | text prints addresses; bench  |
 *
 * **Five rungs and not four, and the reason is rung 1** (the snapshot-value merge
 * amendment). Folding `XhciLogSnapshot` in as "1 = ring on" would have deleted
 * the one configuration that matters twice: **channel open, ring off**. That is
 * the reading whose empty ring proves the recording default applied, and it is
 * the minimal-perturbation reading a bench wants precisely because the append
 * sites' cost at real interrupt rates on Windows 98 metal is UNMEASURED - which
 * is the reason the recording default is off at all. So the ladder shifted
 * rather than collapsing, and every configuration of the three-value design
 * survives under a new number.
 *
 * **What the ladder gates, and what it deliberately does not**, because each
 * gate has exactly one owner and an earlier draft gave two of them to this one:
 *
 *   - **The read channel is gated here, and only at rung 0.** At level 0
 *     `xhciPassThru` returns exactly `MP_STATUS_NOT_SUPPORTED` having written
 *     nothing; at 1 and above it is engaged.
 *   - **Recording is gated here: the ring fills at level >= 2.** That is the
 *     property the whole task exists to create, and it is the one a sink-gated
 *     tree could not express.
 *   - **AN ENGAGED CHANNEL IS NOT GATED BY THE LEVEL AT ALL. It serves both its
 *     regions WHOLE at every level from 1 up, and this sentence gets louder
 *     rather than quieter now that one value carries both consent and depth.**
 *     The temptation to read "verbosity" as a serving ceiling is stronger with
 *     one value than it was with two, and a serving ceiling still cannot be
 *     built as the ladder is written: levels 1 and 2 name payloads that are not
 *     regions - the counter block is flush-time formatting over counters
 *     scattered through the whole extension, and the note ring is a struct deep
 *     inside it - so a ceiling on the channel's two regions would serve nothing
 *     at all below level 3, while a level-1 dump rightly comes back with the
 *     counter block and an empty ring. Building it anyway would need either a
 *     gather table in the driver duplicating `offsets.txt` or new wire regions
 *     and a schema bump, and both are bigger mechanisms than the thing they
 *     would gate. Design record 08 section 13.2.
 *   - **Publication** is gated here: what the PASSIVE flush hands to DebugView,
 *     and what `XHCISNAP`'s plain-text companion prints. The tier boundary
 *     between 3 and 4 is addresses, and it is a *publication* line rather than
 *     a transport or a security one - it is about what a maintainer may
 *     reasonably ask a stranger to paste into a public issue.
 *
 * **Out of range is REFUSED, not clamped**, and the refusal is recorded: a
 * value outside 0-4 falls back to the default and says in the counter block
 * that it did, so a dump never silently reports a tier nobody asked for.
 * Refusing rather than guessing is this project's house rule for these values -
 * it is what the retired `XhciLogFile`'s path rules did, and it is the half of
 * that value worth keeping after the value itself is gone.
 */
#define XHCI_LOG_VERBOSITY_OFF      0  /* the channel answers 6; ring off     */
#define XHCI_LOG_VERBOSITY_COUNTERS 1  /* channel on, counters; ring still off*/
#define XHCI_LOG_VERBOSITY_RING     2  /* + the note ring - this is the log   */
#define XHCI_LOG_VERBOSITY_PORTSC   3  /* + the PORTSC array; no addresses    */
#define XHCI_LOG_VERBOSITY_FULL     4  /* + address records in the ring       */

#define XHCI_LOG_VERBOSITY_MAX      4
/*
 * **The default is 0 and it is a polarity decision, not a taste.** Task
 * 13-L.1's whole finding is that the safe configuration must be the default and
 * the convenience must be the thing asked for by name, and a ring filling on
 * every machine that ever installs this driver is not the safe configuration.
 * **Since the snapshot-value merge, level 0 also shuts the read channel**, so the
 * default is now the whole of "this driver carries a diagnostic it was not
 * asked for" - and the enable step is what consents to it. And the reason to
 * take seriously: **what the append sites cost at real interrupt rates on
 * Windows 98 metal is UNMEASURED** - "cheap and safe at any IRQL" is an
 * argument from construction (bounded ring, no lock, no allocation, no service
 * call, no hardware) and a good one, but it is not a measurement, and this
 * batch exists because an unmeasured assumption about a debug facility shipped
 * in a release. Rung 1 is what that argument buys: a reader who wants the
 * counters and not the append cost has a level to ask for.
 *
 * The cost of the default is real and it is paid in the tool rather than by the
 * user: a capture of a bug that has already happened cannot be re-taken at a
 * higher level, so an intermittent fault needs a second reproduction.
 * `XHCISNAP` sets the value from ring 3 - no import, no `Set` service, no boot
 * path - and there is exactly one value to set, so the published sequence is
 * four steps and `regedit` appears nowhere in it.
 */
#define XHCI_LOG_VERBOSITY_DEFAULT  XHCI_LOG_VERBOSITY_OFF

/*
 * How many records one completion code is worth before the counter carries it.
 * The fourth producer tier: "budgeted, then counted". Four is the same number
 * XHCI_DBG_CALL_LIMIT uses, deliberately - a second budget shape would be a
 * second thing to reason about, and the argument for four is the same one
 * (enough to see a pattern, few enough that a storm cannot fill the ring).
 *
 * The slot count covers every completion code Table 6-90 assigns; the vendor
 * ranges (192-255) fold onto the last slot, so a vendor-defined storm is
 * budgeted as one class rather than as 64.
 */
#define XHCI_LOG_ERROR_BUDGET 4
#define XHCI_LOG_ERROR_SLOTS  64

typedef struct _XHCI_LOG {
    /*
     * **The recording switch, and what it is derived from changed in task
     * 13-L.2.** It used to be `FileEnabled || DebugViewEnabled` - naming a sink
     * was what switched recording on - and on Windows 98 neither sink can
     * honestly be selected, so the ring stayed empty on the one operating
     * system it exists for. It is now a function of the verbosity level alone:
     * nonzero at level >= XHCI_LOG_VERBOSITY_RING, whatever any sink says.
     *
     * Every append site tests this one field, so a producer needs no knowledge
     * of the ladder and adding a level touches no producer. Zero until the
     * values have been read, so an append made before that costs one test and
     * produces nothing - which is what makes "off" the behaviour on a machine
     * whose INF never ran, on one whose owner set nothing, and during the
     * start's own preamble.
     */
    ULONG Enabled;
    /*
     * **Set while the flush is composing the counter block, and the ONLY thing
     * that bypasses `Enabled`.** The ladder says level 0 publishes the counter
     * block, so the counter block has to reach the ring even on a machine whose
     * ordinary producers are switched off. Doing it with a flag rather than a
     * second append entry point keeps one spelling of the record format, which
     * is the reason `XhciLogHex32` is shared in the first place; doing it
     * around a short, locked, PASSIVE-level composition is what keeps the
     * bypass from being reachable by anything else.
     */
    ULONG Publishing;

    /* The MPSTATUS each registry read returned. usbport collapses every failure
     * to one code (`MP_STATUS_UNSUCCESSFUL`), so these say "read or not" and
     * never why - see the ABI box. Kept anyway: on a target where a value was
     * set and the log is silent, this is what separates "the value never
     * arrived" from "the flush refused". (There were three until the merge;
     * `XhciLogSnapshot`'s left with the value.) */
    ULONG SwitchStatusVerbosity;  /* XhciLogVerbosity's read                  */
    ULONG SwitchStatusDebugView;  /* XhciLogDebugView's read                  */
    /*
     * Nonzero once `xhciLogReadValues` has RUN - and that is all it says. It is
     * set on that function's first line, before the registry service is even
     * tested, so every ordinary "nothing was configured" case (no INF, a
     * hand-copied driver, no registry service in the packet) leaves it at 1.
     *
     * **The absent-versus-explicit-zero distinction is the two `SwitchStatus`
     * fields above**, not this one: SUCCESS beside a value of 0 is a zero
     * somebody set, any other status beside 0 is nothing found. The driver
     * treats all of them identically and must, because every default is 0; the
     * distinction is for the reader of a dump.
     *
     * *(This comment, and eight other places in the tree, credited this field
     * with that distinction until the post-Phase 13 review rounds.)*
     */
    ULONG SwitchRead;

    /*
     * The ladder (XHCI_LOG_VERBOSITY_*), and since `0.0.0.6` it is the whole
     * of the switch: it gates the read channel at rung 0, recording at rung 2,
     * and publication above that. Two fields because they differ exactly when
     * something went wrong, and that difference is the diagnosis:
     * `VerbosityRead` is what the registry gave and `Verbosity` is what the
     * driver applied. They differ only when the value was out of range, which
     * is REFUSED rather than clamped - `VerbosityRefused` says so, and both
     * travel in every snapshot header, so a dump can never report a tier
     * nobody asked for.
     *
     * *(A separate `SnapshotEnabled` field stood below these until the merge,
     * holding `XhciLogSnapshot`. It was a pure consent bit and consent nests
     * inside depth, so it is rung 0 of this ladder now: `Verbosity == 0` is
     * exactly what `SnapshotEnabled == 0` was.)*
     */
    ULONG Verbosity;
    ULONG VerbosityRead;
    ULONG VerbosityRefused;

    /*
     * The one surviving sink, and it is an EMISSION switch only: it decides
     * what leaves at the PASSIVE flush and has nothing to do with what is
     * recorded. On Windows 2000 this is the one that delivers, because a Device
     * Manager disable reaches `StopController` with a capture program still
     * running; on Windows 98 the stop is the shutdown and the answer is the
     * snapshot channel instead.
     */
    ULONG DebugViewEnabled;
    /* What the DebugView sink actually emitted, so a target can tell a sink
     * that was on and silent from one that was never selected. */
    ULONG DebugViewEmits;
    ULONG DebugViewBytes;

    ULONG Head;          /* next byte to write                              */
    ULONG Used;          /* bytes held, <= XHCI_LOG_RING_BYTES              */

    ULONG Appends;       /* records accepted                                */
    ULONG Truncated;     /* records whose LABEL hit XHCI_LOG_MAX_RECORD;    */
                         /* such a record loses its value as well           */
    ULONG BytesDropped;  /* bytes overwritten by the wrap before a flush    */
    /*
     * Append calls the ladder turned away, and there are now **two** reasons a
     * call is one of them: the recording switch being off (verbosity below
     * XHCI_LOG_VERBOSITY_RING), and an ADDRESS record below
     * XHCI_LOG_VERBOSITY_FULL - which is refused even at the levels where
     * recording is otherwise on. So a nonzero `Suppressed` on a machine at the
     * log level is the ordinary case rather than a sign the switch was off, and
     * this comment said the first reason alone until the merge. `Enabled`
     * beside it is what distinguishes them.
     */
    ULONG Suppressed;

    ULONG Flushes;              /* flushes that drained something           */
    ULONG FlushesRefusedIrql;   /* flush sites reached above PASSIVE_LEVEL  */
    /* No sink, or nothing to write. The second reason (XHCI_LOG_FLUSH_EMPTY)
     * is unreachable from the driver: the flush appends the counter block
     * before it asks, so the ring is never empty at that point; only the
     * host suite reaches it. */
    ULONG FlushesRefusedState;
    ULONG FlushBytes;           /* bytes handed to the sink, cumulative     */
    ULONG FlushFailures;        /* the sink said no                         */

    /*
     * The fourth producer tier's budget: how many records each completion code
     * has already spent. A byte per code rather than a ULONG because the cap is
     * 4, and an array rather than scalars because nothing reads one of these
     * out of a live guest by name; the reading that matters is the ring's own
     * records plus the existing counters. (The comparison here used to be with
     * the file sink's path-probe statuses, which were scalars for exactly that
     * reason and left with the sink in task 13-L.2.)
     */
    UCHAR ErrorRecords[XHCI_LOG_ERROR_SLOTS];

    UCHAR Ring[XHCI_LOG_RING_BYTES];
} XHCI_LOG, *PXHCI_LOG;

/*
 * Append one record. `label` is copied verbatim; when `hasValue` is nonzero
 * `value` follows it as `=XXXXXXXX`. Every record ends CRLF, because the file
 * is read on the guest with tools that are unhappy with bare LF.
 *
 * Silently does nothing when the switch is off (counted in `Suppressed`), so a
 * call site needs no guard of its own and the disabled path costs one test.
 * `Publishing` is the one exception and it belongs to the flush - see the field.
 *
 * IRQL: any. Takes no lock; the caller's discipline is the controller lock -
 * see the note in src/xhci_log.c.
 */
VOID XhciLogAppend(PXHCI_LOG log,
                   const char *label,
                   ULONG value,
                   ULONG hasValue);

/*
 * Append a record whose VALUE IS A KERNEL ADDRESS. Identical to
 * `XhciLogAppend` except that it also requires level 4
 * (`XHCI_LOG_VERBOSITY_FULL`). *(This sentence said "level 3" until the merge,
 * from before the rung shift the rest of this block already describes.)*
 *
 * **This is what makes "no addresses below the top level" a property of the
 * driver rather than a promise in a document**, and it exists because the
 * promise was made first and was not true: the ladder's tier boundary between 3
 * and 4 is addresses, `XHCISNAP`'s plain-text companion is what a stranger
 * pastes into a public issue, and two records written as soon as recording is
 * on - the `USBPORT_RESOURCES` pointer at the start and the mapped register
 * base after it - carried exactly what that boundary is drawn around. A Codex
 * review of commit `85c0072` found the documentation and the code disagreeing;
 * this is the half that had to move. *(The boundary was between 2 and 3 until
 * the snapshot-value merge shifted every rung by one. The behaviour is unchanged
 * under a new number.)*
 *
 * It is a separate entry point rather than a flag on the existing one so that
 * **the decision is made where the value is**: a call site that knows it is
 * logging an address says so by which function it calls, and a reader auditing
 * the tier boundary greps for one name instead of reading every value.
 *
 * `Suppressed` counts a refusal here exactly as it does there, so a log below
 * the address level still accounts for what it did not record.
 *
 * IRQL: any. Same lock discipline as XhciLogAppend.
 */
VOID XhciLogAppendAddress(PXHCI_LOG log, const char *label, ULONG value);

/*
 * Decide whether a flush may proceed, and record the reason line if it may.
 * Returns one of XHCI_LOG_FLUSH_*. The caller has already established that the
 * IRQL is right; this is the state half of the decision, and it is separate so
 * the host suite can drive every branch of it.
 *
 * **It tests the SINK, not `Enabled`** (task 13-L.2), and that is the whole
 * shape of the ladder at this end: recording is gated by verbosity and
 * publication by whether anything is listening. A machine at level 1 with the
 * DebugView sink on has an empty note ring and a counter block, and that is a
 * real report rather than a refusal.
 *
 * IRQL: any (the caller's guard is what makes it PASSIVE in the driver).
 */
ULONG XhciLogFlushBegin(PXHCI_LOG log, ULONG reason);

/*
 * Take up to `capacity` of the **oldest** bytes out of the ring into `out`, and
 * remove exactly those. Returns the number copied; 0 means the ring is empty.
 *
 * It is a FIFO take rather than a whole-ring drain, and that is what makes the
 * caller a loop over a small stack buffer instead of a single copy of the whole
 * ring. The reason is not style: a 4 KB local in a kernel function makes MSVC
 * emit a `__chkstk` call the Win2000 DDK's driver libraries do not provide, and
 * a driver has about 12 KB of stack in the first place. The chunk size is the
 * caller's, in src/xhci_dispatch.c.
 *
 * Repeated calls return the ring in order. Appends made between calls land
 * after what has already been taken, so an interleaving costs ordering nothing.
 *
 * The caller must keep going until this returns 0 even after its writer fails -
 * see XhciLogFlushEnd. A failed write that left the bytes in place would
 * re-offer the same content at every later flush and, on a target where the
 * file can never be created, turn a bounded ring into a permanently full one
 * that drops every new record.
 *
 * IRQL: any.
 */
ULONG XhciLogDrain(PXHCI_LOG log, UCHAR *out, ULONG capacity);

/*
 * Note a flush the caller could not perform because the IRQL was too high.
 * Separate from XhciLogFlushBegin because it is the one refusal that says
 * something about the *platform* rather than about this driver's state, and
 * batch 11-V reads it on its own.
 *
 * IRQL: any.
 */
VOID XhciLogFlushRefusedIrql(PXHCI_LOG log);

/*
 * Record what the sink did. `bytes` is what it reported **taking**, and `ok` is
 * nonzero when the whole request reached it.
 *
 * *(The two NTSTATUS parameters this took until task 13-L.2 were the file
 * sink's create and write statuses. They left with it, along with the fields
 * they were stored in: the surviving sink is `DbgPrint`, which does not report
 * a status.)*
 *
 * **`bytes` is what landed, not what was offered, and the two are not the same
 * number.** A create that failed took nothing, so it passes 0; a write that
 * succeeded short took some of it, so it passes that. `FlushBytes` therefore
 * accumulates on the failure path too - it answers "how much of this log did
 * the file system accept", while `Flushes` and `FlushFailures` carry the
 * verdict. Making the byte count conditional on `ok` would report a truncated
 * file as an empty one.
 *
 * **It counts nothing that survives the driver.** usbport zeroes the whole
 * miniport extension before every `StartController`, so this restarts while
 * whatever captured the output does not. The comparison it supports is the
 * narrow one: within a single controller lifetime, bytes accepted against bytes
 * offered.
 *
 * IRQL: any.
 */
VOID XhciLogFlushEnd(PXHCI_LOG log, ULONG bytes, ULONG ok);

/*
 * Format `value` as eight upper-case hex digits into `out`, which must hold at
 * least 8 bytes. Exposed because the flush record's header uses it too, and one
 * spelling of a formatter is how the two stay the same.
 *
 * IRQL: any.
 */
VOID XhciLogHex32(UCHAR *out, ULONG value);

/*
 * ------------------------------------------------------------------
 * Task 13-L.2: the two switches and the per-code budget
 * ------------------------------------------------------------------
 *
 * *(This is where task 11-V.9's path validation, path composition and path
 * probe used to be - `XhciLogPathVerdict`, `XhciLogPathProbed`,
 * `XhciLogPathToUse`, `XhciLogPathValidate`, `XhciLogPathCompose`,
 * `XhciLogSelectSinks` and `XhciLogAppendPath`. All seven left with the file
 * sink. They were the whole of the first untrusted input this driver ever took,
 * and it no longer takes one.)*
 */

/*
 * Take the two registry answers and decide what this driver records and what
 * it emits. Sets `Verbosity`, `VerbosityRead`, `VerbosityRefused`,
 * `DebugViewEnabled` and `Enabled`. Returns `Enabled`.
 *
 * *(It took three until the merge, the third being `XhciLogSnapshot`. There is
 * no successor parameter and no successor field: the read channel's consent is
 * `Verbosity != XHCI_LOG_VERBOSITY_OFF`, tested where the channel is.)*
 *
 * Each value is what the read produced, with **0 for a value that was absent,
 * unreadable, or genuinely zero** - the caller cannot tell those apart, because
 * `UsbPortGetMiniportRegistryKeyValue` collapses every failure to one code, and
 * it does not need to: all three mean off, which is now the default anyway.
 * The two `SwitchStatus` fields preserve the difference for a human reading a
 * dump - SUCCESS beside 0 is a zero somebody set, anything else beside 0 is
 * nothing found. `SwitchRead` only records that the reader ran.
 *
 * The three rules, each of which is a refusal and none of which is a repair:
 *
 *   - a verbosity above XHCI_LOG_VERBOSITY_MAX is **refused, not clamped**: the
 *     level falls back to the default and `VerbosityRefused` is set, so a dump
 *     never reports a tier nobody asked for. The default is OFF, so a refused
 *     value shuts the channel rather than opening it at some other rung;
 *   - `Enabled` is a function of the level and of **nothing else** - naming a
 *     sink does not switch recording on, which is the defect task 13-L.2 exists
 *     to end;
 *   - nothing here can fail a start. There is no error return, no allocation
 *     and no service call; a driver whose registry service was absent entirely
 *     arrives here with two zeroes and starts normally.
 *
 * IRQL: any.
 */
ULONG XhciLogApplySwitches(PXHCI_LOG log,
                           ULONG verbosityValue,
                           ULONG debugViewValue);

/*
 * May this completion code be recorded? The fourth producer tier: returns
 * nonzero for the first XHCI_LOG_ERROR_BUDGET occurrences of each code and 0
 * for every one after, so a storm costs a bounded number of records and the
 * existing counters carry the rest.
 *
 * Codes at or above XHCI_LOG_ERROR_SLOTS - the vendor-defined ranges - share
 * the last slot rather than being dropped or given one each.
 *
 * IRQL: any.
 */
ULONG XhciLogErrorBudget(PXHCI_LOG log, ULONG completionCode);

#endif /* XHCI_LOG_H */
