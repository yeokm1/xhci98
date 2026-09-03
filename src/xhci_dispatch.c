/*
 * xhci_dispatch.c - DriverEntry, the usbport registration packet, and the
 * miniport callback surface.
 *
 * Phase 3 (the go/no-go spike) proved the Option A model here: this file
 * registers with whichever usbport.sys build the target has, then answers
 * every callback that build can reach with a signature-correct, IRQL-safe
 * implementation. The contract it is written against is
 * docs/usb-xhci-info/usbport-miniport-abi.md, declared in src/xhci_usbport.h.
 *
 * Phase 3's rule for this file was **no MMIO** - not one register read, not
 * one write - so that "can this driver be a usbport miniport at all" could
 * fail separately from "is the xHCI init sequence right". Phase 4 lifts it one
 * callback at a time rather than all at once, and the remaining stubs still
 * say so. As of task 5, StartController runs the real init sequence and leaves
 * the controller *running* with its managed ports powered (src/xhci_init.c),
 * StopController halts it again, and InterruptService/InterruptDpc run the real
 * interrupt path (src/xhci_evt.c) - all through the accessors in
 * src/xhci_pci.c; every other callback is unchanged. Each one names the task
 * that wires it up.
 *
 * After registration the miniport never sees an IRP: usbport replaces AddDevice
 * and seven MajorFunction entries on our driver object - CREATE, CLOSE,
 * DEVICE_CONTROL, INTERNAL_DEVICE_CONTROL, POWER, SYSTEM_CONTROL and PNP
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 1). There is therefore no
 * dispatch table, no AddDevice, and no PnP code in this driver.
 *
 * C89 only. Every function carries its IRQL requirement.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/*
 * The packet must live in static storage: usbport writes its 16 service
 * pointers into *this* object before copying it, and the miniport calls those
 * services through it for the life of the driver. A stack-local packet would
 * lose them the moment DriverEntry returned.
 *
 * File-scope but not `static`, because src/xhci_pci.c reaches UsbPortWait and
 * UsbPortReadWriteConfigSpace through it (declared in xhci_hw.h). The storage
 * duration - which is the part that matters above - is unchanged.
 */
USBPORT_REGISTRATION_PACKET XhciRegPacket;

/* Forward: task 13-R.1's in-place recovery is *requested* by ResetController and
 * *armed* by the health poll, which sits above it in this file. The two are a
 * long way apart on purpose - see xhciArmRecovery for why the arming may not
 * happen where the request is raised. */
static VOID xhciArmRecovery(PXHCI_EXTENSION ext);

/*
 * Distinctive values written into the five fields the ABI calls Reserved.
 * usbport must not touch any of them; anything else means either a layout
 * mismatch or a build that reads a field ReactOS believes is unused. Chosen to
 * be greppable in a raw memory dump and obviously not a pointer, a count, or
 * a status.
 */
#define XHCI_CANARY_1 0xC0DE0001UL
#define XHCI_CANARY_2 0xC0DE0002UL
#define XHCI_CANARY_3 0xC0DE0003UL
#define XHCI_CANARY_4 0xC0DE0004UL
#define XHCI_CANARY_5 0xC0DE0005UL

/*
 * First probe values, from docs/usb-xhci-info/usbport-miniport-interface.md "Target ABI
 * record" - what both primary targets' own usbehci.sys declares:
 *
 *   MiniPortVersion 3 (EHCI), not 4 (XHCI). ReactOS defines an XHCI value, but
 *   whether a 2195.x binary tolerates it is a runtime question and the field is
 *   consumed later than registration. Start with the combination the shipping
 *   binaries demonstrably accept; revisit once the spike passes.
 *
 *   MiniPortFlags 0x95 = INTERRUPT | MEMORY_IO | USB2 | POLLING, deliberately
 *   without WAKE_SUPPORT (0x200): Win2000 acts on that flag and this driver has
 *   no wake behaviour yet. NO_DMA (0x100) must never appear - it silently zeros
 *   MiniPortResourcesSize and skips the DMA adapter entirely.
 */
#define XHCI_MINIPORT_VERSION USB_MINIPORT_VERSION_EHCI
#define XHCI_MINIPORT_FLAGS                                     \
    (USB_MINIPORT_FLAGS_INTERRUPT | USB_MINIPORT_FLAGS_MEMORY_IO | \
     USB_MINIPORT_FLAGS_USB2 | USB_MINIPORT_FLAGS_POLLING)

XHCI_C_ASSERT(miniport_flags_have_no_dma_bit,
              (XHCI_MINIPORT_FLAGS & USB_MINIPORT_FLAGS_NO_DMA) == 0);

/*
 * XHCI_PROBE_RESOURCES_SIZE - a diagnostic override for the declared controller
 * common-buffer size, set from the build environment
 * (set XHCI_EXTRA_DEFINES=-DXHCI_PROBE_RESOURCES_SIZE=4096, see src\sources).
 *
 * It exists because the size usbport is asked for is the one Phase 3 variable
 * that cannot be changed without a rebuild, and the Win98 disable/re-enable
 * bugcheck found in task 8 needs it bisected: 409,600 bytes is 2.25x what the
 * shipping usbehci.sys requests, and the allocation succeeding says nothing
 * about the standard path. Never set for a deploy build - the layout carver
 * still computes the real worst case, so a probe binary reports a layout that
 * does not fit the buffer it was actually given, on purpose.
 */
#ifdef XHCI_PROBE_RESOURCES_SIZE
#define XHCI_DECLARED_RESOURCES_SIZE ((ULONG)XHCI_PROBE_RESOURCES_SIZE)
#else
#define XHCI_DECLARED_RESOURCES_SIZE XHCI_HC_RESOURCES_SIZE
#endif

/*
 * Kept in both debug and release images so make-package.ps1 can reject a
 * diagnostic artifact without understanding PE instructions or trusting a
 * filename. The volatile read in DriverEntry prevents the compiler/linker from
 * dropping this otherwise diagnostic-only string.
 *
 * The condition is deliberately **wider than the resource override**. `sources`
 * defines XHCI_DIAGNOSTIC_BUILD for any nonempty XHCI_EXTRA_DEFINES, so every
 * diagnostic binary carries the marker, not just the one kind that happened to
 * have been built when the mechanism was written (the repo audit,
 * finding 4). XHCI_PROBE_RESOURCES_SIZE stays in the condition on its own so a
 * resource override reaching the compiler by some other route than the
 * environment variable is still caught.
 */
#if defined(XHCI_DIAGNOSTIC_BUILD) || defined(XHCI_PROBE_RESOURCES_SIZE)
#define XHCI_EMITS_DIAGNOSTIC_MARKER 1
static volatile const char XhciProbeBuildMarker[] =
    "XHCI98_PROBE_BUILD_DO_NOT_DEPLOY";
#endif

/*
 * The failed-start artifact's own marker (roadmap task 12.3), and it is a
 * *second* string rather than a reuse of the one above because the packaging
 * exception that stages this artifact must not widen to any other diagnostic
 * build. `make-package.ps1 -FailStartArtifact` requires both markers: the one
 * above says "this is diagnostic", this one says "and it is the artifact that
 * was asked for". A resource-size probe carries only the first and is still
 * refused, with or without the switch.
 *
 * Volatile-read in DriverEntry with its neighbour, for the same reason.
 */
#ifdef XHCI_FAIL_START_CONTROLLER
/*
 * A review's finding 2: presence of this marker was taken by the
 * packager to mean "this image is task 12.3's artifact", when all it could mean
 * was "this image *includes* task 12.3's define". A build made with both
 * diagnostic defines carries both markers and satisfies -FailStartArtifact
 * while behaving like neither artifact - the resource-size probe can refuse the
 * start long before the injected step ever runs, and the VM run then measures a
 * refusal nobody asked for.
 *
 * **This covers one other define, and cannot cover the rest**: C can ask what
 * is defined, never what else was. Round 2 finding 4 - `-DXHCI_DBG_NO_E9`,
 * as the negative was then spelled, beside the artifact's define compiled
 * clean and carried both markers. (That define is retired: task 13-L.1 made
 * the port-0xE9 mirror an opt-IN `XHCI_DBG_E9` the `qemu` flavour sets, so it
 * no longer travels through XHCI_EXTRA_DEFINES at all. The finding stands as
 * the reason for the rule, which is about the shape of the escape hatch and
 * not about that one define.) The
 * general rule is a string comparison and lives where the string is:
 * `src/sources` refuses any XHCI_EXTRA_DEFINES that is not exactly
 * `-DXHCI_FAIL_START_CONTROLLER`, which binds a bare `build` from a DDK prompt
 * as well as scripts\build-driver.cmd. This #error stays because the
 * resource-size probe is the combination someone would actually reach for, and
 * a diagnosis at the compiler beats one from a makefile.
 */
#ifdef XHCI_PROBE_RESOURCES_SIZE
#error "task 12.3's failed-start artifact must be built alone: set XHCI_EXTRA_DEFINES to -DXHCI_FAIL_START_CONTROLLER and nothing else. Combining it with XHCI_PROBE_RESOURCES_SIZE produces an image carrying both markers, which make-package.ps1 -FailStartArtifact would stage as the artifact."
#endif
#define XHCI_EMITS_FAILSTART_MARKER 1
static volatile const char XhciFailStartMarker[] =
    "XHCI98_FAILSTART_ARTIFACT_TASK_12_3";
#endif

/*
 * ------------------------------------------------------------------
 * The flavour marker (roadmap task 13-L.1)
 * ------------------------------------------------------------------
 *
 * Which of the three binaries this is, readable from the file alone with an
 * ASCII scan and no PE knowledge - the same mechanism as the two markers
 * above, for a different question.
 *
 * **`VS_FF_DEBUG` cannot answer it**: `debug` and `qemu` are both *checked*
 * DDK builds and would both set it, and those are exactly the two that must
 * never be confused - one ships as the diagnostic download and one must never
 * be published at all. A user sending a capture has to be able to say which
 * build produced it, `make-package.ps1` has to be able to refuse one by
 * reading the image rather than by trusting a path, and `docs/contributing/runs/run-13e.md`
 * already warns that candidate binary sizes collide at a bench.
 *
 * The define comes from `src/sources`, which derives it from `BUILD_ALT_DIR` -
 * the same variable that decides the output tree - so an image cannot carry a
 * marker that disagrees with the directory it was linked in. A build with no
 * flavour define at all is a build `src/sources` already refused, and the
 * #error here is the second half of that refusal for anyone compiling a single
 * file by hand.
 *
 * Volatile-read in DriverEntry with its neighbours, so the linker keeps it.
 */
#if defined(XHCI_FLAVOUR_QEMU)
#define XHCI_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#define XHCI_FLAVOUR_CODE XHCI_SNAPSHOT_FLAVOUR_QEMU
#elif defined(XHCI_FLAVOUR_DEBUG)
#define XHCI_FLAVOUR_NAME "XHCI98_FLAVOUR_DEBUG"
#define XHCI_FLAVOUR_CODE XHCI_SNAPSHOT_FLAVOUR_DEBUG
#elif defined(XHCI_FLAVOUR_RELEASE)
#define XHCI_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#define XHCI_FLAVOUR_CODE XHCI_SNAPSHOT_FLAVOUR_RELEASE
#elif defined(XHCI_HOST_TEST)
/*
 * The host suite links no driver image, so there is no marker for anything to
 * scan and no `sources` to have set the define. It gets a name of its own
 * rather than borrowing `release`'s: a vector that reads this must not be able
 * to mistake a host build for a shipping flavour, and the value travels in the
 * snapshot header where exactly that confusion would matter.
 */
#define XHCI_FLAVOUR_NAME "XHCI98_FLAVOUR_HOSTTEST"
#define XHCI_FLAVOUR_CODE XHCI_SNAPSHOT_FLAVOUR_HOSTTEST
#else
#error "no build flavour was defined. src/sources derives XHCI_FLAVOUR_RELEASE, _DEBUG or _QEMU from BUILD_ALT_DIR; build through scripts\build-driver.cmd, or through a DDK prompt whose setenv.bat has run. An image with no flavour marker cannot be identified from the file, which is what task 13-L.1 exists to make impossible."
#endif

#ifndef XHCI_HOST_TEST
static volatile const char XhciFlavourMarker[] = XHCI_FLAVOUR_NAME;
#endif

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/*
 * Is this really our extension? Cheap enough to run in every callback, and it
 * is the difference between a diagnosable log line and a page fault three
 * callbacks later if the packet layout is wrong.
 *
 * IRQL: any.
 */
static ULONG xhciExtensionValid(PXHCI_EXTENSION ext)
{
    if (ext == NULL) {
        return 0;
    }
    if (ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return 0;
    }
    if (ext->TrailingSignature != XHCI_EXTENSION_TRAILING) {
        return 0;
    }
    return 1;
}

/*
 * Zero the packet without RtlZeroMemory/memset: the DDK routes those to a
 * compiler intrinsic or an ntoskrnl import depending on flags, and this driver
 * decides its import list deliberately rather than by build-flag accident.
 *
 * IRQL: PASSIVE_LEVEL (DriverEntry only).
 */
static VOID xhciZeroPacket(VOID)
{
    ULONG *word;
    ULONG i;

    word = (ULONG *)&XhciRegPacket;
    for (i = 0; i < sizeof(XhciRegPacket) / sizeof(ULONG); i++) {
        word[i] = 0;
    }
}

XHCI_C_ASSERT(packet_is_whole_words,
              sizeof(USBPORT_REGISTRATION_PACKET) % sizeof(ULONG) == 0);

/* ------------------------------------------------------------------ */
/* Task 11-V.7 - the log's DDK half (the file sink is gone, 13-L.2)    */
/* ------------------------------------------------------------------ */

/*
 * Everything about the log that needs the DDK is here, and nothing else is:
 * the ring, its accounting and the flush decision are pure core in
 * src/xhci_log.c, which the host suite drives directly.
 *
 * Three things live on this side, and each is the reason its own clause of task
 * 11-V.7 is a gate rather than a feature:
 *
 *   the switch     - through the registration packet, which costs **no new
 *                    import**. Derived argument-by-argument out of both
 *                    shipping usbport builds, and out of both shipping
 *                    usbehci.sys builds, which use the same service in the same
 *                    callback for the same purpose (docs/usb-xhci-info/usbport-miniport-abi.md
 *                    section 6, the task 11-V.7 box).
 *   the IRQL guard - measured, never assumed. See the long note in
 *                    src/xhci_log.c on the derivation that was *not* finished.
 *   the emission   - one bulk DbgPrint hand-over from the PASSIVE flush, and
 *                    nothing else. *(This was "the file write - three Zw*
 *                    imports" until the post-Phase 13 review rounds: task 13-L.2 retired the ring-0
 *                    file sink and ZwCreateFile, ZwWriteFile and
 *                    ZwClose left the binary with it. The driver performs no
 *                    file I/O anywhere.)*
 */

/*
 * The two value names and their key. `BOOL = TRUE` selects
 * `IoOpenDeviceRegistryKey(..., PLUGPLAY_REGKEY_DRIVER, ...)` - the driver's own
 * software key, which is what a plain `AddReg` under an INF's install section
 * writes, and which is the key the shipping usbehci.sys reads its own parameter
 * from. `src/xhci98.inf` supplies the default (0) for both on **both**
 * install paths, and scripts/inf-gate/check-inf.ps1 checks both - a value
 * present on only one path is exactly the failure shape that gate exists to
 * catch.
 *
 * The SIZE_T is the name's length in BYTES INCLUDING the terminating NUL:
 * (characters + 1) * 2, so `L"XhciLogVerbosity"` is (16 + 1) * 2. Read off the
 * shipping caller rather than guessed (it passes 0x2C for a 21-character name),
 * and the service does not clamp its own copy length.
 *
 * **Task 13-L.2 replaced `XhciLogFile` with two DWORDs, and the later
 * amendment folded one of those back into the other.** `XhciLogVerbosity` is
 * now the whole switch - rung 0 shuts the PassThru read channel, rung 2 is the
 * recording switch - and `XhciLogDebugView` survives unchanged as an emission
 * switch. Both are `REG_DWORD` and both default to 0, which makes the
 * defaulting trivially consistent: absent, unreadable and an explicit zero all
 * land in the same place. **The driver no longer takes an untrusted string from
 * the registry at all** - that was the file sink's path, and it left with it.
 *
 * *(`XhciLogSnapshot` stood here as a third `REG_DWORD` until the merge. There
 * is no successor name: a machine still carrying the value in its software key
 * is simply one this driver no longer reads, which costs a start nothing.)*
 */
#define XHCI_LOG_VERBOSITY_VALUE_NAME  L"XhciLogVerbosity"
#define XHCI_LOG_VERBOSITY_VALUE_BYTES (17 * 2)
#define XHCI_LOG_DBGVIEW_VALUE_NAME    L"XhciLogDebugView"
#define XHCI_LOG_DBGVIEW_VALUE_BYTES   (17 * 2)

/*
 * How much of the ring one DebugView emit carries. Small and a loop, not one
 * buffer the size of the ring: MSVC emits a `__chkstk` probe for a local of a
 * page or more and the Win2000 DDK's driver libraries do not provide one -
 * which was the compiler catching a real defect, a driver having about 12 KB of
 * stack in total.
 */
#define XHCI_LOG_CHUNK 256

#ifndef XHCI_HOST_TEST

/*
 * Is this a context the ring may be handed over from? The whole question, and
 * the only honest way to ask it from inside the driver.
 *
 * **It outlived the sink it was written for.** Task 11-V.7 added it so that no
 * static claim about which lifecycle callbacks are PASSIVE could reach
 * `ZwCreateFile`; those calls are gone, and the guard matters at least as much
 * now, because what the flush hands the ring to is `DbgPrint` - and `DbgPrint`
 * from DPC and ISR contexts at real interrupt rates is the other thing this
 * project has measured bugchecking Windows 98 metal.
 *
 * IRQL: any - this is the call that finds out.
 */
static ULONG xhciLogAtPassive(VOID)
{
    return (KeGetCurrentIrql() == PASSIVE_LEVEL) ? 1UL : 0UL;
}

/*
 * The DebugView sink: one bulk hand-over of the ring at the PASSIVE flush.
 *
 * **Never per line, and never from a DPC or an ISR.** That profile is what
 * bugchecks Windows 98 on bare metal across three device classes
 * (`0028:C208D79D` hub, `0028:C207B26D` USB Audio, `0028:C20A3F4D` Low-Speed
 * mouse; E460, three observations), and it is the reason AGENTS.md's
 * rule about `DbgPrint` outside `#if DBG` has exactly one exception and this is
 * it. Do not add a second call site.
 *
 * Whether even the bulk dump is **safe** on Windows 98 metal is not established
 * and is not claimed here: the boot that would have discriminated it was
 * dropped, and "dropped" is not evidence in either direction. It
 * is also no longer the only way off that machine - task 13-L.2's snapshot
 * channel is - which is what makes leaving the question open acceptable rather
 * than merely unavoidable.
 *
 * `"%s"` rather than the buffer as the format string, because the ring holds
 * records a device produced fields for and a stray `%` in one of them would
 * otherwise be a format specifier reading the stack. A chunk boundary can split
 * a record across two calls, which costs nothing: the capture concatenates and
 * every record still ends CRLF.
 *
 * IRQL: PASSIVE_LEVEL. The caller has already measured it.
 */
static VOID xhciLogEmitDebugView(const UCHAR *bytes, ULONG count)
{
    UCHAR line[XHCI_LOG_CHUNK + 1];
    ULONG i;

    for (i = 0; i < count && i < XHCI_LOG_CHUNK; i++) {
        line[i] = bytes[i];
    }
    line[i] = 0;

    DbgPrint("%s", (const char *)line);
}

#else /* XHCI_HOST_TEST */

/*
 * The host stand-ins. The suite owns both, because "the flush refused at
 * DISPATCH" and "the flush handed these bytes over" are exactly the two
 * behaviours worth testing and neither is reachable through a real registry or
 * a real capture agent on the build host. src/xhci_log.c - where every decision
 * actually lives - needs no stand-in at all.
 *
 * *(Task 13-L.2 removed a dozen of these with the file sink: the writer's ok /
 * take / status knobs, the three path forms' probe statuses under two masks
 * each, and the raw-path buffer the registry stand-in copied out. The suite
 * they served was the biggest thing in test_log.c and it is gone with the code
 * it drove.)*
 */
ULONG XhciLogHostAtPassive = 1;
/*
 * **There is no stand-in for the registry read any more, and that is a gain.**
 * There used to be one because the retired `XhciLogFile` was a string and the
 * suite needed to control the bytes usbport's unclamped copy left behind. Plain
 * DWORDs need nothing of the kind, so the driver's own reader compiles into the
 * host build and the suite drives it through a stand-in **service** in the
 * registration packet instead - which is strictly stronger, because it also
 * checks the value names and the name lengths the driver asks with, and those
 * are what decide which key a value comes out of.
 */
ULONG XhciLogHostDebugViewCalls = 0;
UCHAR XhciLogHostDebugViewBuffer[XHCI_LOG_RING_BYTES * 2];
ULONG XhciLogHostDebugViewTotal = 0;

static ULONG xhciLogAtPassive(VOID)
{
    return XhciLogHostAtPassive;
}

static VOID xhciLogEmitDebugView(const UCHAR *bytes, ULONG count)
{
    ULONG i;

    XhciLogHostDebugViewCalls++;
    for (i = 0; i < count; i++) {
        if (XhciLogHostDebugViewTotal >= sizeof(XhciLogHostDebugViewBuffer)) {
            break;
        }
        XhciLogHostDebugViewBuffer[XhciLogHostDebugViewTotal] = bytes[i];
        XhciLogHostDebugViewTotal++;
    }
}

#endif /* XHCI_HOST_TEST */

/*
 * Read the two values. Called once per start, from StartController, which is
 * the one callback this driver already knows is PASSIVE_LEVEL - it calls
 * `UsbPortWait` there and nowhere else, and that has run under Driver Verifier
 * with Force IRQL Checking on the 2b VM since Phase 4. The registry service is
 * PASSIVE-only (the ABI box), so no new site and no new import.
 *
 * usbport zeroes the miniport extension before every start, so these are
 * re-read each time rather than cached: there is nothing to cache them in.
 *
 * **NOTHING IN THIS PATH MAY FAIL A START**, and the three cases it has to pass
 * through without so much as a changed code path are the value **missing**, the
 * read **failing**, and `UsbPortGetMiniportRegistryKeyValue` itself being
 * **NULL in the packet**. Each leaves that value at 0 and the driver starts
 * normally. That is not a hypothetical set: a machine whose INF never ran, a
 * driver copied in by hand at a bench, an upgrade over an older INF, and task
 * 13-L.3's own binary swaps all reach it - and 0 is exactly the right behaviour
 * there, because it is the default anyway.
 *
 * **Neither read can say why it failed.** The service collapses "value absent",
 * "buffer too small" and "key would not open" into one code, so an unreadable
 * value and an unset one are one answer here.
 *
 * **`SwitchStatusVerbosity` and `SwitchStatusDebugView` are what keep that
 * distinguishable for a reader**, not `SwitchRead`: a status of
 * MP_STATUS_SUCCESS beside a value of 0 means the driver read a zero somebody
 * set, and any other status beside 0 means it found nothing to read. All three
 * of the cases named above - no INF, a hand-copied driver, no registry service
 * in the packet - reach this function and therefore set `SwitchRead` to 1;
 * they are told apart by the statuses.
 *
 * **`SwitchRead` says only that this routine ran at all.** It is set on the
 * first line, before the service is even tested, so 0 means the start path
 * never reached here - which a served dump can barely show, since serving is
 * gated on the verbosity this function reads. It stays because a zero in a
 * dump would then be a real signal about the start path rather than about the
 * registry. *(This comment credited `SwitchRead` with the absent-versus-zero
 * distinction until the post-Phase 13 review rounds, and eight other places in the tree repeated
 * it.)*
 *
 * IRQL: PASSIVE_LEVEL.
 */
static VOID xhciLogReadValues(PXHCI_EXTENSION ext,
                              ULONG *verbosityValue,
                              ULONG *debugViewValue)
{
    ULONG value;
    MPSTATUS status;

    ext->Log.SwitchRead = 1;
    *verbosityValue = 0;
    *debugViewValue = 0;

    if (XhciRegPacket.UsbPortGetMiniportRegistryKeyValue == NULL) {
        /*
         * Not a failure of this driver: it means the packet's out-half is not
         * what the ABI record says. The registration verifier already reports
         * that as ABI-SUSPECT; here it just leaves everything off.
         */
        ext->Log.SwitchStatusVerbosity = MP_STATUS_FAILURE;
        ext->Log.SwitchStatusDebugView = MP_STATUS_FAILURE;
        return;
    }

    value = 0;
    status = XhciRegPacket.UsbPortGetMiniportRegistryKeyValue(
        ext, TRUE, XHCI_LOG_VERBOSITY_VALUE_NAME,
        XHCI_LOG_VERBOSITY_VALUE_BYTES, &value, sizeof(value));
    ext->Log.SwitchStatusVerbosity = (ULONG)status;
    if (status == MP_STATUS_SUCCESS) {
        *verbosityValue = value;
    }

    value = 0;
    status = XhciRegPacket.UsbPortGetMiniportRegistryKeyValue(
        ext, TRUE, XHCI_LOG_DBGVIEW_VALUE_NAME, XHCI_LOG_DBGVIEW_VALUE_BYTES,
        &value, sizeof(value));
    ext->Log.SwitchStatusDebugView = (ULONG)status;
    if (status == MP_STATUS_SUCCESS) {
        *debugViewValue = value;
    }
}


/*
 * The counter block, appended at flush time.
 *
 * This is task 11-V.7's "small always-on subset" clause, answered by naming the
 * counters rather than by compiling the traced build's XHCI_DBG_* sites into a
 * published one. Two properties are what make it worth the duplication:
 *
 *   - it costs **nothing** on a machine with the switch off, because
 *     XhciLogAppend returns on the first test;
 *   - it is bounded and fixed, so a log's size is a function of the run's
 *     length only through the ring, never through the traffic.
 *
 * Keep it short deliberately. The full counter set is reachable with a debugger
 * or with `scripts\local\readcounters.ps1` against `offsets.txt`; what belongs
 * here is the set that answers "the device does not work" and "the transfer
 * path is wrong", which is exactly what 11-V.7 says a stop-flushed ring is
 * worth and all it is worth.
 *
 * Called with the controller lock held. IRQL: DISPATCH_LEVEL while held.
 */
static VOID xhciLogCountersLocked(PXHCI_EXTENSION ext)
{
    XhciLogAppend(&ext->Log, "ext.flags", ext->Flags, 1);
    XhciLogAppend(&ext->Log, "init.step", ext->InitStep, 1);
    XhciLogAppend(&ext->Log, "init.status", ext->InitStatus, 1);
    XhciLogAppend(&ext->Log, "ctrl.failed", ext->ControllerFailed, 1);
    /* Task 13-R.1. `ctrl.failed` on its own says the latch is closed *now*;
     * these say whether anything ever opened it, which is the difference
     * between a controller that stalled and one that died. */
    XhciLogAppend(&ext->Log, "ctrl.reset.calls", ext->ResetControllerCalls, 1);
    XhciLogAppend(&ext->Log, "ctrl.recover.attempts", ext->RecoveryAttempts, 1);
    XhciLogAppend(&ext->Log, "ctrl.recover.done", ext->RecoveryCompletions, 1);
    XhciLogAppend(&ext->Log, "isr.entries", ext->InterruptCount, 1);
    XhciLogAppend(&ext->Log, "isr.claimed", ext->InterruptsClaimed, 1);
    XhciLogAppend(&ext->Log, "dpc.count", ext->DpcCount, 1);
    XhciLogAppend(&ext->Log, "events.total", ext->EventsTotal, 1);
    XhciLogAppend(&ext->Log, "psc.events",
                  ext->EventCounts[XHCI_EVENT_TYPE_INDEX(
                      XHCI_TRB_TYPE_PORT_STATUS_CHANGE)], 1);
    XhciLogAppend(&ext->Log, "slots.enabled", ext->SlotsEnabled, 1);
    XhciLogAppend(&ext->Log, "devices.addressed", ext->DevicesAddressed, 1);
    XhciLogAppend(&ext->Log, "opens.total", ext->OpensTotal, 1);
    XhciLogAppend(&ext->Log, "opens.accepted", ext->OpensAccepted, 1);
    XhciLogAppend(&ext->Log, "opens.refused", ext->OpenRefusals, 1);
    XhciLogAppend(&ext->Log, "xfer.submitted", ext->TransfersSubmitted, 1);
    XhciLogAppend(&ext->Log, "xfer.completed", ext->TransfersCompleted, 1);
    XhciLogAppend(&ext->Log, "xfer.cancelled", ext->TransfersCancelled, 1);
    XhciLogAppend(&ext->Log, "xfer.refused", ext->TransfersRefused, 1);
    /* Read beside `xfer.refused`, never alone: a refusal means "ask again" and
     * a failure means "there is nothing here to ask" (batch 6-V). */
    XhciLogAppend(&ext->Log, "xfer.failed.gone", ext->TransfersFailedGone, 1);
    XhciLogAppend(&ext->Log, "cmd.issued", ext->CommandsIssued, 1);
    XhciLogAppend(&ext->Log, "cmd.timedout", ext->CommandsTimedOut, 1);
    XhciLogAppend(&ext->Log, "cmd.abandoned", ext->CommandsAbandoned, 1);
    XhciLogAppend(&ext->Log, "quiesce.failures", ext->QuiesceFailures, 1);
    /*
     * The restore's half of save step 2, and it is here rather than only in
     * `CheckController` because the trace macros are empty in a release build
     * and this block is not. Audit round 6 found the field described in
     * `xhci.h` as "the only evidence, on a machine" reachable from neither -
     * and the machine that will read it is a bare-metal Phase 13 one, where
     * the stored log is the channel that exists.
     *
     * `restore.fatal` is the count of events that made the restore refuse and
     * should read zero. **It is not "the Host Controller Event", which is what
     * this comment said until audit round 8**: the drain refuses on several
     * kinds now, so the count alone cannot name one. `restore.lastfatal.kind` is
     * the `XHCI_RESTORE_FATAL_*` value and `restore.lastfatal.code` the
     * completion code it carried - the two that let a machine tell them apart,
     * which round 8 found no release-flavour reader could do.
     *
     * **`lastfatal`, not `fatal`, and audit round 9 asked the question that
     * renamed them.** Both fields are written only by a refusing drain and are
     * never cleared, so they survive every later *successful* restore: a snapshot
     * taken after a good resume still carries whichever refusal came last. That
     * is the intended reading - the evidence of a refusal is worth more than a
     * field that reads zero - but a label saying `restore.fatal.kind` beside a
     * `restore.fatal` count of zero invites a reader to pair them, and they are
     * not a pair. The count is this-run cumulative; these two are historical.
     */
    XhciLogAppend(&ext->Log, "restore.stale.dropped",
                  ext->RestoreEventsDiscarded, 1);
    XhciLogAppend(&ext->Log, "restore.fatal", ext->RestoreEventsFatal, 1);
    XhciLogAppend(&ext->Log, "restore.lastfatal.kind", ext->RestoreFatalKind, 1);
    XhciLogAppend(&ext->Log, "restore.lastfatal.code", ext->RestoreFatalCode, 1);
    /*
     * Task 11-V.9's four. The first two are the producer set's own backstop:
     * `log.dropped` says the ring wrapped, so what a reader has is a window
     * rather than the run, and `log.errors.budgeted` says a completion code
     * stopped producing records and the counters above carry it from there.
     * The last two are the tiers this task deliberately refuses to give a
     * record apiece - Windows 98 idle-suspends this controller 29 times in a
     * single idle run - so they arrive as a count with one first-occurrence
     * record in the ring above.
     */
    XhciLogAppend(&ext->Log, "log.dropped", ext->Log.BytesDropped, 1);
    XhciLogAppend(&ext->Log, "log.errors.overbudget", ext->LogErrorsOverBudget,
                  1);
    XhciLogAppend(&ext->Log, "power.suspends", ext->SuspendCount, 1);
    XhciLogAppend(&ext->Log, "power.resumes", ext->ResumeReinits, 1);
    XhciLogAppend(&ext->Log, "log.appends", ext->Log.Appends, 1);
    XhciLogAppend(&ext->Log, "log.suppressed", ext->Log.Suppressed, 1);
}

/*
 * Hand the ring to the DebugView sink, if there is anything to flush and this
 * is a context that may emit. *(This said "flush the ring to the file" until
 * a later review; task 13-L.2 retired the ring-0 file sink and the
 * three Zw* imports left the binary with it. There is one sink now.)*
 *
 * The order is the whole of it and each step is there for a reason a previous
 * shape got wrong somewhere else in this driver:
 *
 *   1. **Measure the IRQL first**, before deciding anything, so a wrong static
 *      reading about which lifecycle callbacks are PASSIVE cannot reach a
 *      `DbgPrint` from a DPC - the hazard the retired `ZwCreateFile` used to
 *      stand for here, and the one that outlived it. Counted separately from
 *      every other refusal, because it is
 *      the one that says something about the *platform*: batch 11-V reads
 *      `LogFlushesRefusedIrql` nonzero with `LogFlushes` zero as 11-V.7's stop
 *      rule firing, with a measurement behind it.
 *   2. **Decide and drain under the lock**, since the ring is ordinary shared
 *      extension state and a DISPATCH-level callback can be appending to it on
 *      another CPU.
 *   3. **Emit outside the lock.** `DbgPrint` reaches a ring-3 capture agent and
 *      is not a bounded call; holding a spin lock across it would raise IRQL to
 *      DISPATCH and then wait there, which is the "no usbport service and no
 *      bounded wait under the lock" rule in
 *      docs/contributing/design/05-locking-model.md applied to something worse than a
 *      usbport service. *(This step said "write outside the lock" and named
 *      ZwCreateFile as the blocking call until the post-Phase 13 review rounds. Task 13-L.2 retired
 *      that sink; the rule and the ordering are unchanged, only which call is
 *      the one that must not be made under the lock.)*
 *
 * **The staging buffer is deliberately small and the drain is a loop.** The
 * first version staged the whole 4 KB ring in one stack local, and the DDK
 * build refused to link it: MSVC emits a `__chkstk` probe for a local of a page
 * or more and the Win2000 DDK's driver libraries do not provide one. That was
 * the compiler catching a real defect rather than a nuisance - a driver has
 * about 12 KB of stack in total. `XhciLogDrain` is therefore a FIFO take, and
 * the ring's size is now free to change without touching this function - which
 * is what let task 11-V.9 take it from 4 KB to 16 KB by editing one constant.
 *
 * **One sink, one drain.** Task 11-V.9 wrote this loop for two sinks fed from
 * one pass, because the ring may be drained exactly once; task 13-L.2 retired
 * the file sink, and what is left is the DebugView emit, still fed chunk by
 * chunk from that single drain. The ring ends empty whether or not anything
 * received the bytes: a sink that cannot take them must not leave the ring
 * permanently full and dropping every later record.
 *
 * **Every exit traces, and that is the one thing the counters cannot do for
 * this function.** The flush's own counters are written *by* the flush, which
 * runs inside `StopController` - and usbport zeroes the whole miniport
 * extension before every `StartController`, so the next boot or re-enable
 * erases them. On Windows 2000 there is a narrow between-disable-and-enable
 * window where a monitor read may still reach them; on Windows 98 the stop
 * *is* the shutdown and there is no window at all, which left the three
 * reasons a file can be absent - the switch off, the IRQL refusal, and a
 * failed create - indistinguishable from the guest afterwards
 * (`docs/contributing/runs/run-11v.md`, stage C, which names this as owed host-side work). The
 * trace is host-side and survives the guest, so these four lines are what
 * separate them. Bounded, because `XHCI_DBG_VALUE_LIMITED` is, and a stop is
 * rare in any case.
 *
 * IRQL: PASSIVE_LEVEL expected; safe to call at any IRQL, which is the point.
 */
static VOID xhciLogFlush(PXHCI_EXTENSION ext, ULONG reason)
{
    UCHAR staging[XHCI_LOG_CHUNK];
    KIRQL oldIrql;
    ULONG decision;
    ULONG chunk;
    ULONG emitted;

    if (!xhciExtensionValid(ext)) {
        return;
    }

    /*
     * **The SINK is tested here, not `Enabled`** (task 13-L.2), and the order
     * relative to the IRQL check still matters: on a machine with no sink
     * selected - every ordinary machine - this must cost one load and one
     * branch, and `LogFlushesRefusedIrql` must stay a reading about a platform
     * that was actually asked to hand something over.
     *
     * Testing `Enabled` here would have made a level-0 machine publish nothing
     * even with the sink on, and the ladder says level 0 publishes the counter
     * block. Recording and publication are separate gates with separate owners;
     * this is the publication one.
     */
    if (!ext->Log.DebugViewEnabled) {
        XHCI_DBG_VALUE_LIMITED("log: flush skipped - no sink selected", reason);
        return;
    }

    if (!xhciLogAtPassive()) {
        XhciControllerLockAcquire(&oldIrql);
        XhciLogFlushRefusedIrql(&ext->Log);
        XhciControllerLockRelease(oldIrql);
        XHCI_DBG_VALUE_LIMITED("log: flush refused - above PASSIVE_LEVEL",
                               reason);
        return;
    }

    /*
     * **`Publishing` is raised across the composition and lowered before the
     * drain**, and it is the only thing in this driver that bypasses the
     * recording switch. The ladder says level 0 publishes the counter block, so
     * the counter block has to reach the ring on a machine whose ordinary
     * producers are off - and the bypass is scoped to this one short, locked,
     * PASSIVE-level pass rather than given to a second append entry point that
     * would be a second spelling of the record format.
     *
     * The flag is cleared inside the same lock hold. A path that left it raised
     * would silently turn the recording switch off for the rest of the
     * controller's life, in the direction that fills the ring rather than the
     * one that empties it - so there is exactly one place it is set and one
     * place it is cleared, with nothing between them that can return.
     */
    XhciControllerLockAcquire(&oldIrql);
    ext->Log.Publishing = 1;
    xhciLogCountersLocked(ext);
    decision = XhciLogFlushBegin(&ext->Log, reason);
    ext->Log.Publishing = 0;
    XhciControllerLockRelease(oldIrql);

    if (decision != XHCI_LOG_FLUSH_GO) {
        XHCI_DBG_VALUE_LIMITED("log: flush declined - nothing to write",
                               decision);
        return;
    }

    emitted = 0;

    for (;;) {
        XhciControllerLockAcquire(&oldIrql);
        chunk = XhciLogDrain(&ext->Log, staging, sizeof(staging));
        XhciControllerLockRelease(oldIrql);

        if (chunk == 0) {
            break;
        }

        /*
         * **The drain runs to empty whatever the sink does**, and that survives
         * the file sink it was written for: a failed hand-over that left the
         * bytes in place would re-offer the same content at every later flush
         * and turn a bounded ring into a permanently full one that drops every
         * new record.
         */
        xhciLogEmitDebugView(staging, chunk);
        emitted += chunk;
    }

    XhciControllerLockAcquire(&oldIrql);
    if (emitted != 0) {
        ext->Log.DebugViewEmits++;
        ext->Log.DebugViewBytes += emitted;
    }
    /*
     * **The surviving sink cannot fail**, so this is always the success arm and
     * `FlushFailures` is now a counter with no producer - kept because it is a
     * published field a reader may already be looking for, and because a sink
     * that can fail is exactly the kind of thing this driver has had before.
     * `DbgPrint` reports nothing and a capture agent that is not listening is
     * indistinguishable from one that is, which is a property of the platform
     * rather than something to model as an error.
     */
    XhciLogFlushEnd(&ext->Log, emitted, 1);
    XhciControllerLockRelease(oldIrql);

    XHCI_DBG_VALUE_LIMITED("log: flush emitted bytes to DebugView", emitted);
}

/*
 * Read the two values, apply them, and record what was applied as the log's
 * own first records.
 *
 * **This is the whole of the start-time log path now, and its shortness is the
 * point.** Task 11-V.9's version validated an untrusted path string in the pure
 * core, then opened three candidate roots twice each on the boot path before
 * deciding anything - a sequence whose every step was load-bearing because one
 * of those opens **hung a Windows 98 boot** rather than answering. None of that
 * survives: there is no path, no file, no open, and nothing here that can block.
 * Two DWORD reads, one arithmetic function in the pure core, four notes.
 *
 * **The records are still written**, and for the reason they always were: the
 * registry read carries no diagnosis, so a user who set a value and sees
 * nothing needs the driver to state what it actually applied. `log.verbosity`
 * is what the ring's presence or absence is explained by, and it is written
 * before anything else can fill the ring.
 *
 * IRQL: PASSIVE_LEVEL.
 */
static VOID xhciLogStart(PXHCI_EXTENSION ext)
{
    ULONG verbosityValue;
    ULONG debugViewValue;

    verbosityValue = 0;
    debugViewValue = 0;
    xhciLogReadValues(ext, &verbosityValue, &debugViewValue);

    (VOID)XhciLogApplySwitches(&ext->Log, verbosityValue, debugViewValue);

    XHCI_DBG_VALUE_LIMITED("log: verbosity applied", ext->Log.Verbosity);
    XHCI_DBG_VALUE_LIMITED("log: verbosity read", ext->Log.VerbosityRead);
    XHCI_DBG_VALUE_LIMITED("log: DebugView sink", ext->Log.DebugViewEnabled);

    /*
     * The first records in the ring, and they are about the log itself. Below
     * the recording rung they are suppressed like every other append, which is
     * correct: the ring is empty there by design, and the same numbers travel
     * in every snapshot header where a reader at any engaged level can see
     * them.
     */
    XhciLogNote(ext, "log.verbosity", ext->Log.Verbosity);
    XhciLogNote(ext, "log.verbosity.read", ext->Log.VerbosityRead);
    XhciLogNote(ext, "log.verbosity.refused", ext->Log.VerbosityRefused);
    XhciLogNote(ext, "log.debugview", ext->Log.DebugViewEnabled);
}

/* ------------------------------------------------------------------ */
/* Controller lifecycle callbacks                                      */
/* ------------------------------------------------------------------ */

/*
 * StartController - the callback the whole gate turns on.
 *
 * usbport has already connected the interrupt, mapped BAR0, and allocated and
 * zeroed the MiniPortResourcesSize common buffer by the time this runs. This
 * function takes delivery of all of it and records it; XhciInitController
 * (src/xhci_init.c) is the hardware sequence.
 *
 * The split is deliberate. Everything up to and including the resource log has
 * to happen whether or not the controller can be initialised - a refusal that
 * printed nothing about what it was handed would be the hardest kind to
 * diagnose remotely - so the logging is here and the sequence, with its own
 * per-step refusal record, is in one place next door.
 *
 * IRQL: PASSIVE_LEVEL (usbport calls it from its start-device path; UsbPortWait
 * is legal here and nowhere else in this file).
 */
static MPSTATUS NTAPI xhciStartController(PVOID miniPortExtension,
                                          PUSBPORT_RESOURCES resources)
{
    PXHCI_EXTENSION ext;
    ULONG baseStatus;
    MPSTATUS status;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("StartController", miniPortExtension, resources, 0);

    if (ext == NULL || resources == NULL) {
        XHCI_DBG_TEXT("StartController: NULL argument - refusing");
        return MP_STATUS_FAILURE;
    }

    /*
     * The one deliberate dereference of a usbport-owned structure in the spike.
     * docs/usb-xhci-info/usbport-miniport-abi.md section 9 item 4 asks for exactly this: the
     * raw 52 bytes usbport actually passes, so USBPORT_RESOURCES' layout is
     * confirmed from the target rather than assumed from the transcription.
     * 13 words is sizeof(USBPORT_RESOURCES) / 4, asserted in xhci_usbport.h.
     */
    XHCI_DBG_WORDS("resources", (const ULONG *)resources,
                   sizeof(USBPORT_RESOURCES) / sizeof(ULONG));

    /*
     * The command engine's lock and start epoch are created here and only here.
     * This is the one moment usbport guarantees nothing else is inside this
     * extension, which is what a function that initializes a spin lock without
     * holding it needs - see XhciCommandInit. The reinitialization path
     * invalidates instead.
     *
     * **Before the signatures, not after.** The signature pair is what every
     * callback tests to decide the extension is one of ours, so publishing it
     * first would let a stale async callback - one usbport armed for a previous
     * start and cannot cancel - pass its bracket and reach a lock that has not
     * been initialized. The order costs nothing and closes that window: while
     * the signatures are absent, every callback in the driver declines.
     *
     * **The host suite cannot distinguish this ordering from its absence**, and
     * that is recorded rather than left to look like coverage: swapping the two
     * fails zero checks (measured), because the epoch this function stores is
     * still usbport's zero at the instant a callback could arrive, and no
     * context ever carries zero - so the epoch check rejects it either way. The
     * ordering is the belt to that pair of braces, and it is what keeps the
     * guarantee if the epoch is ever stored earlier.
     */
    XhciCommandInit(ext);

    ext->Signature = XHCI_EXTENSION_SIGNATURE;
    ext->TrailingSignature = XHCI_EXTENSION_TRAILING;

    /*
     * The log's two values, read after the signatures because the service takes
     * this extension and every callback in the driver tests those first, and
     * before anything that might want to note something. usbport zeroed the
     * extension on the way in, so the log starts empty and off on every start.
     * xhciLogStart is the whole of it: read the two DWORDs, apply the switches,
     * record the decision. *(It also probed a path here until task 13-L.2
     * retired `XhciLogFile` and the ring-0 file sink with it; there is no file
     * for the driver to name any more.)*
     */
    xhciLogStart(ext);
    /*
     * **`XhciLogNoteAddress`, because that value is a kernel pointer.** It is
     * usbport's own `USBPORT_RESOURCES` block, and the ladder's boundary
     * between levels 3 and 4 is exactly this: what a maintainer may reasonably
     * ask a stranger to paste into a public issue. Recording it at level 1 put
     * it in the companion text `XHCISNAP` tells a user to paste, while both the
     * release notes and the generated readme promised no addresses below the
     * top level - which is a promise the driver has to keep rather than the
     * documents. *(This paragraph said "levels 2 and 3" and "below level 3"
     * until the merge, from before `XhciLogSnapshot` joined the ladder
     * and shifted every rung by one.)*
     */
    XhciLogNoteAddress(ext, "start", (ULONG)(ULONG_PTR)resources);

    ext->ResourcesTypes = resources->ResourcesTypes;
    ext->ResourceBase = (ULONG_PTR)resources->ResourceBase;
    ext->IoSpaceLength = resources->IoSpaceLength;
    ext->StartVA = resources->StartVA;
    ext->StartPA = resources->StartPA;
    ext->InterruptVector = resources->InterruptVector;
    ext->InterruptLevel = (ULONG)resources->InterruptLevel;
    ext->InterruptMode = resources->InterruptMode;

    /*
     * The two facts the Phase 3 task 2 memory model rests on and that only a
     * live target can supply: that usbport really hands over a page-aligned
     * StartVA *and* StartPA, and that it granted the 404 KB the packet asked
     * for at all. XhciInitController re-checks the first one and refuses on
     * it; logging it here means the value is in the trace either way.
     */
    baseStatus = XhciCheckResourceBase(ext->StartVA, ext->StartPA);
    XHCI_DBG_VALUE("StartVA", ext->StartVA);
    XHCI_DBG_VALUE("StartPA", ext->StartPA);
    XHCI_DBG_VALUE("resource base check", baseStatus);
    XHCI_DBG_VALUE("ResourceBase (BAR0 VA)", ext->ResourceBase);
    XHCI_DBG_VALUE("IoSpaceLength", ext->IoSpaceLength);

    status = XhciInitController(ext, resources);
    if (status != MP_STATUS_SUCCESS) {
        /*
         * Returning failure is what usbport needs to hear: it tears the start
         * down rather than calling EnableInterrupts on a controller that was
         * never programmed. The reason is in ext->InitStep / ext->InitStatus
         * as well as in the trace, because a release build has only the
         * former.
         */
        XhciLogNote(ext, "start.refused.step", ext->InitStep);
        XhciLogNote(ext, "start.refused.status", ext->InitStatus);
        /*
         * Flushed here rather than left for the stop, because a start that
         * refuses is the single case an end user is most likely to be reporting
         * and there is no guarantee a StopController follows one on either
         * target. This is still PASSIVE - it is the same callback.
         */
        xhciLogFlush(ext, XHCI_LOG_REASON_FAILURE);
        return status;
    }

    (VOID)XhciControllerUpdateFlags(ext, 0, XHCI_EXT_FLAG_STARTED);
    /* The mapped register base - an address, for the reason above. */
    XhciLogNoteAddress(ext, "start.ok", ext->ResourceBase);
    return MP_STATUS_SUCCESS;
}

/*
 * IRQL: PASSIVE_LEVEL.
 *
 * Through task 4 this callback did nothing but drop flags, and that was safe on
 * an argument rather than by accident: the controller had been initialised but
 * never run, and a halted xHC performs no DMA into the common buffer usbport is
 * about to reclaim. Task 5 sets R/S, which ends that argument, so the halt is
 * now here.
 *
 * XhciStopController is the whole ordered teardown (task 8): it takes port
 * power off the ports this driver powered - which must happen while the
 * controller still runs, since PORTSC is unwritable once it halts - and then
 * quiesces, which masks the interrupt sources, retires the command generation,
 * clears R/S and waits for HCHalted.
 *
 * Quiesce masks both interrupt enables and clears INITIALIZED under the stable
 * controller lock before it begins the halt. That closes ISR/DPC admission only
 * after the shared line cannot still be asserted, and prevents a queued DPC from
 * publishing ERDP or re-arming IMAN while the controller heads toward D3. The
 * remaining lifecycle flags go down after the halt attempt.
 */
static VOID NTAPI xhciStopController(PVOID miniPortExtension,
                                     BOOLEAN isDoDisableInterrupts)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("StopController", miniPortExtension, isDoDisableInterrupts, 0);

    if (!xhciExtensionValid(ext)) {
        return;
    }

    if (!XhciStopController(ext)) {
        /*
         * Task 11-V.7: flush **before** the fail-closed bugcheck, not after -
         * XhciFailClosedDma does not return. A quiesce that could prove nothing
         * is the one fault where a user most wants a file to send, and it is
         * also the one where the ring would otherwise die with the machine.
         */
        xhciLogFlush(ext, XHCI_LOG_REASON_FAILURE);
        /*
         * usbport reclaims the common buffer after this returns and the callback
         * has no way to tell it not to - it returns void, and Option A gives the
         * miniport no ownership of that allocation to withhold. **Making the
         * state legible was not enough**, which is what this call corrects: a
         * trace and a counter describe the corruption, they do not prevent it,
         * and the corruption they describe lands in whatever driver the pool
         * hands those pages to next.
         *
         * So the choice here is between a silent, arbitrary, misattributed
         * failure and a loud one at a known instruction, and only the second is
         * something a bug report can be written about.
         * docs/contributing/implementation-invariants.md, "DMA Teardown" already required a
         * cold boot in this state; XhciFailClosedDma is what makes the machine
         * take one. Whichever running flag admitted the quiesce
         * (XHCI_EXT_FLAG_RUNNING, or XHCI_EXT_FLAG_HW_RUNNING for a controller
         * this driver found already executing) stays set, and QuiesceFailures
         * is incremented first, so the record survives for the dump.
         */
        XhciFailClosedDma(ext);
    }

    /*
     * Flags is a shared RMW word. Keep this final bookkeeping transition under
     * the same lock as quiesce admission so a callback updating INTERRUPTS
     * cannot restore the INITIALIZED value it observed before the mask.
     *
     * Through the helper rather than a hand-rolled bracket. Task 9's static
     * review found this the only *standalone* Flags transition carrying its own
     * acquire/release pair - the three inline ones (quiesce admission,
     * Enable/DisableInterrupts) sit inside larger locked transactions and
     * cannot use the helper at all, since acquiring it there would take this
     * DISPATCH-level spin lock twice on one CPU. So this was the one site where
     * the shape was a choice (docs/contributing/design/05-locking-model.md).
     */
    (VOID)XhciControllerUpdateFlags(ext,
                                    XHCI_EXT_FLAG_STARTED |
                                        XHCI_EXT_FLAG_INTERRUPTS |
                                        XHCI_EXT_FLAG_INITIALIZED,
                                    0);

    /*
     * Task 11-V.7's one ordinary flush site, and the last thing this callback
     * does so the counter block records the state the teardown actually
     * reached rather than the state it started from.
     *
     * A Device Manager disable reaches here on **Windows 2000**, and the Win98
     * shutdown reaches it through the measured
     * `Suspend -> DisableInterrupts -> Stop` sequence - which is what makes the
     * log producible without a debugger, the whole point of the task. It is NOT
     * reached by a Win98 disable: that bugchecks the machine before the teardown
     * completes, for any USB controller devnode including Microsoft's own.
     */
    xhciLogFlush(ext, XHCI_LOG_REASON_STOP);
}

/*
 * SuspendController / ResumeController.
 *
 * The bodies are XhciSuspendController / XhciResumeController in
 * src/xhci_init.c, next to the rest of the controller lifecycle and where the
 * host suite can drive them. The long note on why this pair is not yet the real
 * xHCI save/restore protocol - and why implementing half of it would be worse
 * than what is here - lives with them.
 *
 * IRQL: PASSIVE_LEVEL. Win98's NUSB usbport issues these pairs repeatedly at
 * idle; native Win2000 usbport never idle-suspended the controller at all.
 */
static VOID NTAPI xhciSuspendController(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("SuspendController", miniPortExtension, 0, 0);

    if (xhciExtensionValid(ext)) {
        XhciSuspendController(ext);
    }
}

/* IRQL: PASSIVE_LEVEL. */
static MPSTATUS NTAPI xhciResumeController(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("ResumeController", miniPortExtension, 0, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_FAILURE;
    }
    return XhciResumeController(ext);
}

/*
 * InterruptService - the miniport ISR body, at DIRQL.
 *
 * Task 4 replaced the Phase 3 stub, which returned FALSE unconditionally
 * because nothing had enabled an interrupt yet. The real body is XhciIsr in
 * src/xhci_evt.c; it claims the interrupt only when USBSTS.EINT proves the
 * controller raised it, which is what keeps a shared PCI line workable (on
 * both target VMs an EHCI controller sits on the same IRQ).
 *
 * Deliberately does not trace, and neither does XhciIsr. It can run at high
 * frequency on a shared line, and printing from DIRQL on the 9x kernel is not
 * worth the risk; ext->InterruptCount and ext->LastIsrStatus are dumped from
 * CheckController, which can afford to print.
 *
 * IRQL: DIRQL.
 */
static BOOLEAN NTAPI xhciInterruptService(PVOID miniPortExtension)
{
    return XhciIsr((PXHCI_EXTENSION)miniPortExtension);
}

/*
 * IRQL: DISPATCH_LEVEL, under usbport's MiniportInterruptsSpinLock - which is
 * NOT the lock the endpoint and submit callbacks run under (task 9 owns that
 * lock scope once there is shared state to protect).
 *
 * The body is XhciEventDpc in src/xhci_evt.c. Both signature and flag checks
 * are repeated inside it, because it is also the function the tests drive
 * directly; the check here is the one that keeps xhciExtensionValid's full
 * bracket test - which the DIRQL path deliberately does not pay for - on the
 * DPC path.
 */
static VOID NTAPI xhciInterruptDpc(PVOID miniPortExtension,
                                   BOOLEAN enableInterrupts)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("InterruptDpc", miniPortExtension, enableInterrupts, 0);

    if (xhciExtensionValid(ext)) {
        XhciEventDpc(ext, enableInterrupts);
    }
}

/*
 * usbport calls this immediately after StartController returns success, under
 * MiniportSpinLock. The body is XhciEnableInterrupts in src/xhci_evt.c, which
 * releases Event Handler Busy and then unmasks IMAN.IE and USBCMD.INTE -
 * acknowledging nothing, for the reason argued at its definition.
 *
 * XHCI_EXT_FLAG_INTERRUPTS is set *before* the hardware write, not after, so
 * that a resume arriving on the heels of this call cannot read the flag as
 * "usbport did not want interrupts" and hand back a controller that never
 * interrupts again. It is usbport's state, recorded here because the resume
 * path is the only other place that has to know it.
 *
 * IRQL: DISPATCH_LEVEL.
 */
static VOID NTAPI xhciEnableInterrupts(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("EnableInterrupts", miniPortExtension, 0, 0);

    if (xhciExtensionValid(ext)) {
        ext->InterruptEnables++;
        XhciEnableInterrupts(ext);
    }
}

/*
 * The inverse, and it really is only the mask: USBCMD.INTE before IMAN.IE, so
 * an ISR racing this - it runs at DIRQL, which MiniportSpinLock does not
 * exclude - costs a spurious interrupt rather than a delivered one
 * (docs/contributing/implementation-invariants.md, "Interrupt Ordering"). Nothing is
 * acknowledged, so an interrupt the controller is already holding survives the
 * disable and is delivered when usbport enables again.
 *
 * IRQL: DISPATCH_LEVEL.
 */
static VOID NTAPI xhciDisableInterrupts(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("DisableInterrupts", miniPortExtension, 0, 0);

    if (xhciExtensionValid(ext)) {
        XhciDisableInterrupts(ext);
    }
}

/*
 * The ReactOS mirror has no call site for this one, but all three shipping
 * builds do: their device-power completion routine calls it on the successful
 * D0 path, holding neither miniport lock. XhciFlushInterrupts therefore
 * deliberately touches no register - the argument is at its definition in
 * src/xhci_evt.c, and the binary evidence in
 * tools/*-extracted/usbport-flushinterrupts-disasm.txt.
 *
 * Unlike the other two callbacks, the counter lives in the body rather than
 * here, because the body is the whole behaviour and a wrapper that counted
 * separately would just be a second place to keep in step.
 *
 * IRQL: <= DISPATCH_LEVEL, arbitrary thread, no usbport lock held.
 */
static VOID NTAPI xhciFlushInterrupts(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("FlushInterrupts", miniPortExtension, 0, 0);

    if (xhciExtensionValid(ext)) {
        XhciFlushInterrupts(ext);
    }
}

/*
 * The mid-TD "still outstanding" term, read as one coherent snapshot.
 *
 * The four totals satisfy `retired == tails + dropped + censored + outstanding`
 * only when they are read together: the DPC advances them one at a time under
 * the controller lock. Taking the lock here and returning a single value keeps
 * the identity exact and keeps the unsigned subtraction from underflowing into
 * a nine-digit print (repo audit round 5, finding 2).
 *
 * Takes the controller lock and calls no usbport service while holding it.
 * IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciMidTdOutstanding(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    ULONG retired;
    ULONG tails;
    ULONG dropped;
    ULONG censored;

    if (ext == NULL) {
        return 0;
    }
    XhciControllerLockAcquire(&oldIrql);
    retired = ext->MidTdShortRetiresTotal;
    tails = ext->MidTdShortTailsTotal;
    dropped = ext->MidTdTailsDroppedTotal;
    censored = ext->MidTdTailsCensoredTotal;
    XhciControllerLockRelease(oldIrql);

    /*
     * The identity is exact, so this cannot go negative on a coherent snapshot.
     * It is still checked, because "cannot happen" is what the caller would be
     * printing if a later change broke the identity, and a huge number is a
     * worse thing to print than an honest zero.
     */
    if (tails + dropped + censored > retired) {
        return 0;
    }
    return retired - tails - dropped - censored;
}

/*
 * Health poll, driven by usbport's 500 ms timer, under MiniportSpinLock.
 *
 * **It now polls, rather than only reporting.** Through task 7 this callback
 * read no register at all - a Phase 3 shape that outlived its reason, since
 * there had been no MMIO to check when it was written - while
 * docs/contributing/implementation-invariants.md, "Fatal Errors" had required USBSTS.HCE and
 * HSE on every invocation the whole time. The decision half is
 * XhciControllerHealthPoll in src/xhci_cmd.c, which takes the controller lock
 * because a check of ControllerFailed followed by unsynchronized MMIO is the
 * defect the fourth review round closed everywhere else.
 *
 * The escalation is performed here, after that function has returned and its
 * lock is dropped: UsbPortInvalidateController is a usbport service, and no
 * usbport service may be called while this driver's lock is held.
 *
 * Asking for a reset from *inside* a usbport callback raises no new lock-order
 * question, which is worth recording because it is the first thing to ask about
 * this call. usbport invokes this callback under MiniportSpinLock
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 6), and EnableInterrupts and
 * DisableInterrupts - which run under that same lock - have escalated exactly
 * this way since the ninth and eleventh review rounds. The RESET branch itself
 * queues a DPC and takes a busy reference under FdoExtension+0xA0, which is
 * neither of the miniport locks.
 *
 * IRQL: DISPATCH_LEVEL.
 */
static VOID NTAPI xhciCheckController(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;
    ULONG escalate;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("CheckController", miniPortExtension, 0, 0);

    if (!xhciExtensionValid(ext)) {
        return;
    }

    /*
     * Counted here, above every gate below, because this is the only place that
     * can answer "did usbport call us at all" - see CheckCallbacks in src/xhci.h
     * for the reading it exists to separate.
     */
    ext->CheckCallbacks++;

    /*
     * Change-gated, not unbounded: this callback runs every 500 ms for the
     * life of the driver, so a plain XHCI_DBG_VALUE pair here wrote 14,000
     * lines an hour on an idle guest during the Phase 3 spike and buried
     * everything else in the log. What is worth seeing is the counters
     * *moving* - which is exactly what an idle guest does not produce.
     */
    /*
     * What the start decided. All four already print once from the init
     * sequence - as `XHCI_DBG_VALUE`, which `scripts\vm-matrix\gen-offsets.ps1`
     * deliberately does not match - so before these lines they were in no
     * offset table and the live-guest counter reader could not name them at
     * all (`docs/contributing/runs/run-11v.md`, stage D, which names this as owed host-side
     * work). They are read here rather than only at init because a run reads a
     * *running* controller: the init line has scrolled past by then, and on a
     * two-controller machine it is not even clear which controller it was.
     *
     *   init step / init status  - where a refused start stopped, and why. On a
     *                              controller that started they are the last
     *                              step and 0.
     *   MaxSlotsEn               - the declared ceiling task 11-V.2's
     *                              slot-exhaustion clause is measured against.
     *                              The *refusal* is that clause's reading; this
     *                              is the number the refusal must arrive at.
     *   scratchpad buffers       - what this xHC asked for, against the same
     *                              declared cap, which is the other half of
     *                              that task's resource pair.
     */
    XHCI_DBG_VALUE_CHANGED("init step", ext->InitStep);
    XHCI_DBG_VALUE_CHANGED("init status", ext->InitStatus);
    XHCI_DBG_VALUE_CHANGED("MaxSlotsEn", ext->Layout.MaxSlotsEn);
    XHCI_DBG_VALUE_CHANGED("scratchpad buffers", ext->HcInfo.ScratchpadCount);

    XHCI_DBG_VALUE_CHANGED("isr count", ext->InterruptCount);
    XHCI_DBG_VALUE_CHANGED("isr claimed", ext->InterruptsClaimed);
    XHCI_DBG_VALUE_CHANGED("dpc count", ext->DpcCount);
    XHCI_DBG_VALUE_CHANGED("events consumed", ext->EventsTotal);
    /*
     * The count, not the port: this one moves on every edge, so it stays a
     * positive witness of the checkpoint's plug/unplug clause after the bounded
     * site in xhciHandleEvent has spent its budget, and it is the number the
     * per-edge lines reconcile against.
     */
    XHCI_DBG_VALUE_CHANGED("port status change events",
                           ext->EventCounts[XHCI_EVENT_TYPE_INDEX(
                               XHCI_TRB_TYPE_PORT_STATUS_CHANGE)]);
    XHCI_DBG_VALUE_CHANGED("interrupt mask failures",
                           ext->InterruptMaskFailures);
    XHCI_DBG_VALUE_CHANGED("interrupt masks over a degraded window",
                           ext->InterruptMaskDegraded);
    XHCI_DBG_VALUE_CHANGED("mask escalations", ext->MaskEscalations);
    XHCI_DBG_VALUE_CHANGED("unmask escalations", ext->UnmaskEscalations);
    XHCI_DBG_VALUE_CHANGED("ISR IMAN acknowledges made from a literal",
                           ext->IsrImanLiteralAcks);
    XHCI_DBG_VALUE_CHANGED("interrupter re-arm failures",
                           ext->InterruptRearmFailures);
    XHCI_DBG_VALUE_CHANGED("re-arm escalations", ext->RearmEscalations);
    XHCI_DBG_VALUE_CHANGED("interrupt unmask failures",
                           ext->InterruptUnmaskFailures);
    XHCI_DBG_VALUE_CHANGED("host controller event resets",
                           ext->HostControllerEventResets);
    XHCI_DBG_VALUE_CHANGED("fatal controller status", ext->FatalStatusDetected);
    /*
     * Task 13-R.1's ladder, and the three of these are read together or not at
     * all. `ResetControllerCalls` is how many times the latch closed;
     * `RecoveryCompletions` is how many times it opened again. Equal, nonzero
     * values are the repair working - a stall that recovered. A nonzero
     * `ResetControllerCalls` with `RecoveryCompletions` at 0 is the pre-13-R
     * behaviour, and `RecoveryLastStep`/`RecoveryLastStatus` then say which
     * step of the reinitialization refused.
     */
    XHCI_DBG_VALUE_CHANGED("controller resets requested of usbport",
                           ext->ResetControllerCalls);
    XHCI_DBG_VALUE_CHANGED("in-place recovery attempts",
                           ext->RecoveryAttempts);
    XHCI_DBG_VALUE_CHANGED("in-place recoveries completed",
                           ext->RecoveryCompletions);
    XHCI_DBG_VALUE_CHANGED("in-place recoveries refused",
                           ext->RecoveryFailures);
    /* The one the cap reads. Read beside `RecoveryFailures`: the total says how
     * often a recovery could not be completed, this says whether they were
     * consecutive, and only the second decides whether another is armed. */
    XHCI_DBG_VALUE_CHANGED("in-place recovery consecutive refusals",
                           ext->RecoveryFailuresConsecutive);
    XHCI_DBG_VALUE_CHANGED("in-place recovery callbacks with nothing to do",
                           ext->RecoveryStaleCallbacks);
    XHCI_DBG_VALUE_CHANGED("in-place recovery, refusing init step",
                           ext->RecoveryLastStep);
    XHCI_DBG_VALUE_CHANGED("in-place recovery, refusing init status",
                           ext->RecoveryLastStatus);
    XHCI_DBG_VALUE_CHANGED("commands aborted by the poll's own ladder",
                           ext->CommandAgeAborts);
    XHCI_DBG_VALUE_CHANGED("commands outstanding past every watchdog",
                           ext->CommandAgeResets);
    XHCI_DBG_VALUE_CHANGED("commands the engine gave up on",
                           ext->CommandOwnerLost);
    XHCI_DBG_VALUE_CHANGED("command ages recovered by the poll",
                           ext->OpAgeRecoveries);
    /* The device layer's numbers, and each one is the release build's only reading
     * of a Phase 6 clause: an EP0 open this driver would not serve, a
     * SET_ADDRESS taken off the transfer path, and the MPS0 correction that
     * actually needed an Evaluate Context. */
    XHCI_DBG_VALUE_CHANGED("slots enabled", ext->SlotsEnabled);
    XHCI_DBG_VALUE_CHANGED("devices addressed", ext->DevicesAddressed);
    XHCI_DBG_VALUE_CHANGED("EP0 opens refused", ext->OpenRefusals);
    /* The share of those that is a topology this driver cannot address yet - an
     * address-0 open with no unspent root-port reset behind it, which is what a
     * device behind a hub looks like until task 7b-A.3 fills Route Strings. Read
     * beside the line above: equal counts mean every refused open was a hub
     * child and nothing else went wrong. */
    XHCI_DBG_VALUE_CHANGED("EP0 opens refused - no route",
                           ext->OpenRefusalsNoClaim);
    /*
     * Task 7b-A.1.1, and it is read *with* the line above rather than alone: this
     * counts usbhub's second port reset per enumeration - the one before
     * SET_ADDRESS - being denied the right to re-arm a claim nothing would spend.
     * On an ordinary bus it should track the number of enumerations, and a run
     * where it stays 0 while devices enumerate is the rule not firing at all.
     */
    XHCI_DBG_VALUE_CHANGED("mid-enumeration port resets re-arming nothing",
                           ext->EnumResetsSuppressed);
    /*
     * The accounting pair and the two refusals it uncovered (task 7b-A.1.0).
     * `opens seen` must equal `opens accepted` plus every refusal line in this
     * block and in the endpoint block below - the identity is written out on
     * `OpensTotal` in XHCI_EXTENSION - and it is printed first so a run can be
     * read as "these many opens, accounted for like this" rather than as a list
     * of counters that happen to have moved. A shortfall is a return path nobody
     * counted, which is exactly what the batch 7a-V 2a run could not name.
     */
    XHCI_DBG_VALUE_CHANGED("endpoint opens seen", ext->OpensTotal);
    XHCI_DBG_VALUE_CHANGED("endpoint opens accepted", ext->OpensAccepted);
    XHCI_DBG_VALUE_CHANGED("endpoint opens refused - unusable buffer",
                           ext->OpenRefusalsBuffer);
    XHCI_DBG_VALUE_CHANGED("endpoint opens refused - malformed call",
                           ext->OpenRefusalsMalformed);
    /*
     * Task 7b-A.1's hub topology graph, and this block is the whole of what a
     * release build can say about whether snooping reconstructs the tree -
     * which is the question that subphase's stop rule turns on ("if neither
     * direct fields nor snooping can reconstruct the path reliably on
     * **both** shipping usbport builds, stop this subphase and record hub
     * support as an explicit Option A limitation").
     *
     * Read in this order, because each line only means something given the one
     * above it:
     *
     *   hubs identified   - nonzero says the snoop channel carried hub-class
     *                       traffic at all. Zero with a hub on the bus means
     *                       the channel is gone, not that the hub is missing.
     *   hub descriptors   - the port count and TT think time landed. A run with
     *                       hubs identified but no descriptors folded is a
     *                       reply-capture failure, not a snooping failure, and
     *                       the two need different fixes.
     *   port resets       - the parent half. Every behind-hub enumeration
     *                       should produce one.
     *   parents claimed   - resets that an address-0 open actually consumed.
     *                       This against `port resets` is the reconstruction
     *                       reading: equal means every enumeration behind a hub
     *                       was placed, a shortfall means the graph knew the
     *                       parent and nothing asked for it.
     *   parents unclaimed - an address-0 open with no armed reset behind it,
     *                       which is the *ordinary* answer for every root-port
     *                       device and is only a finding when a hub is on the
     *                       bus and it is climbing.
     *   deepest tier      - 0 for a bus with hubs only on root ports; the first
     *                       nonzero reading is the two-tier path no run has yet
     *                       reached (task 7b-A.1.2's owed measurement).
     *   too deep          - paths past the Route String's five tiers, refused
     *                       rather than truncated.
     *   disconnects       - the port-status folds that saw a device leave.
     *   nodes dropped     - the graph ran out of room, so the readings above
     *                       describe part of the bus. Never silent.
     */
    XHCI_DBG_VALUE_CHANGED("topology: hubs identified",
                           ext->Topology.Promotions);
    XHCI_DBG_VALUE_CHANGED("topology: hub descriptors folded",
                           ext->Topology.Descriptors);
    XHCI_DBG_VALUE_CHANGED("topology: hub descriptors malformed",
                           ext->Topology.DescriptorsBad);
    /* Self-consistent and unusable: a hub descriptor naming no ports, which
     * task 7b-A.2 will not mark a slot from. Its own line because it is the one
     * way a hub can be identified, folded, and still never marked. */
    XHCI_DBG_VALUE_CHANGED("topology: hub descriptors with no ports",
                           ext->Topology.DescriptorsNoPorts);
    XHCI_DBG_VALUE_CHANGED("topology: hub port resets seen",
                           ext->Topology.Resets);
    XHCI_DBG_VALUE_CHANGED("topology: hub port resets on an unknown hub",
                           ext->Topology.ResetsUnknownHub);
    XHCI_DBG_VALUE_CHANGED("topology: hub port resets overwritten",
                           ext->Topology.ResetsOverwritten);
    XHCI_DBG_VALUE_CHANGED("topology: parents claimed", ext->Topology.Claims);
    XHCI_DBG_VALUE_CHANGED("topology: parents unclaimed",
                           ext->Topology.ClaimsUnarmed);
    /* A claim that was armed and consumed and could not answer (Phase 7
     * review, B9): a channel failure or a lifetime race, never the ordinary
     * root-port shape the line above counts. */
    XHCI_DBG_VALUE_CHANGED("topology: claims consumed unusable",
                           ext->Topology.ClaimsUnusable);
    XHCI_DBG_VALUE_CHANGED("topology: parents past five tiers",
                           ext->Topology.ClaimsTooDeep);
    XHCI_DBG_VALUE_CHANGED("topology: deepest tier", ext->Topology.MaxTier);
    XHCI_DBG_VALUE_CHANGED("topology: port statuses folded",
                           ext->Topology.PortStatuses);
    XHCI_DBG_VALUE_CHANGED("topology: disconnects seen",
                           ext->Topology.Disconnects);
    XHCI_DBG_VALUE_CHANGED("topology: swaps only the change word saw",
                           ext->Topology.Reconnects);
    XHCI_DBG_VALUE_CHANGED("topology: multi-TT alternate settings",
                           ext->Topology.AltSettings);
    XHCI_DBG_VALUE_CHANGED("topology: nodes dropped", ext->Topology.Dropped);
    XHCI_DBG_VALUE_CHANGED("topology: port-power sweeps seen",
                           ext->Topology.PowerSweeps);
    /* Its own comment in xhci_topo.h says it exists so the >31-port shortfall
     * is not clamped silently; without this line a release-build run was
     * exactly that silence (Phase 7 review, B7). */
    XHCI_DBG_VALUE_CHANGED("topology: port statuses past the connect window",
                           ext->Topology.PortStatusesWide);
    XHCI_DBG_VALUE_CHANGED("topology: nodes pruned", ext->Topology.Prunes);
    XHCI_DBG_VALUE_CHANGED("topology: nodes migrated to a new address",
                           ext->Topology.Migrations);
    XHCI_DBG_VALUE_CHANGED("topology: stale nodes pruned at a re-address",
                           ext->Topology.MigrationsStale);
    /*
     * Task 7b-A.2, and read against `topology: hub descriptors folded` above:
     * every hub whose descriptor landed should end up marked, so a shortfall is
     * a hub the controller is not being told about.
     *
     *   hub slots marked    - Hub / Number of Ports / TTT / MTT reached a Slot
     *                         Context, by either route.
     *   marking commands    - Configure Endpoints issued for the marking alone.
     *                         The marking goes out in the same pass that folds
     *                         the descriptor, so this is normally one per hub
     *                         enumeration; roughly `slots marked` + `markings
     *                         lost` is the healthy shape, and a large excess is
     *                         the command engine refusing submissions.
     *   markings lost       - a hub re-addressed, which rewrites the whole
     *                         Output Slot Context and costs the marking. Tracks
     *                         the re-enumeration cycle.
     *   marking failures    - the xHC refused, or the command was lost. Each one
     *                         is a hub whose downstream FS/LS devices the
     *                         controller is scheduling without a TT.
     */
    XHCI_DBG_VALUE_CHANGED("topology: hub slots marked", ext->HubSlotsMarked);
    XHCI_DBG_VALUE_CHANGED("topology: hub marking commands",
                           ext->HubMarkCommands);
    XHCI_DBG_VALUE_CHANGED("topology: hub markings lost to a re-address",
                           ext->HubMarksLostToAddress);
    XHCI_DBG_VALUE_CHANGED("topology: hub marking failures",
                           ext->HubMarkFailures);
    /*
     * Task 7b-A.3, and this is the block that answers the subphase's stop rule
     * rather than merely describing the graph: whether a reconstructed path was
     * good enough to *address a device through*.
     *
     *   behind-hub opens      - address-0 opens served from a hub claim. Before
     *                           this task every one of these was a refusal, so a
     *                           zero here with a hub on the bus and `topology:
     *                           parents claimed` nonzero means the claim was
     *                           taken and the record refused - read the three
     *                           refusal lines below for which.
     *   behind-hub addressed  - and how many of them reached an address. Equal to
     *                           the line above on a healthy bus; a gap is an
     *                           enumeration that started one tier down and did
     *                           not finish, which is what the command counters
     *                           then explain.
     *   too deep              - a path past the five-tier Route String, refused
     *                           rather than truncated. A *correct* answer about a
     *                           legal USB topology, so nonzero is not a defect.
     *   speed unnameable      - a device speed with no Protocol Speed ID on this
     *                           controller, or one this driver does not address.
     *   no record             - the device table was full.
     *   devices gone          - torn down because the hub above reported the port
     *                           empty, or because the root port went down. The
     *                           only channel a behind-hub unplug has.
     *   TT programmed         - Address Device Input Slot Contexts carrying a
     *                           Parent (TT) Hub Slot ID and Port; two per
     *                           behind-hub FS/LS enumeration behind a High-Speed
     *                           hub, and **zero on QEMU**, whose `usb-hub` is
     *                           Full-Speed and has no transaction translator.
     *   TT unresolved         - a High-Speed ancestor with no Slot ID, which
     *                           fails the record rather than addressing it with
     *                           the fields cleared.
     *   TT pairs agreed /     - the graph's derivation against what usbport said
     *   TT pairs disagreed      (`HubAddr`/`PortNumber`). **A disagreement is
     *                           expected on QEMU** - usbport names the Full-Speed
     *                           hub as a translator it does not have - and is the
     *                           reading worth having on real hardware, where the
     *                           two should agree.
     */
    XHCI_DBG_VALUE_CHANGED("topology: behind-hub opens", ext->BehindHubOpens);
    XHCI_DBG_VALUE_CHANGED("topology: behind-hub devices addressed",
                           ext->BehindHubAddressed);
    XHCI_DBG_VALUE_CHANGED("topology: behind-hub refused - too deep",
                           ext->BehindHubTooDeep);
    XHCI_DBG_VALUE_CHANGED("topology: behind-hub refused - speed unnameable",
                           ext->BehindHubNoSpeed);
    XHCI_DBG_VALUE_CHANGED("topology: behind-hub refused - no record",
                           ext->BehindHubNoRecord);
    XHCI_DBG_VALUE_CHANGED("topology: behind-hub devices gone",
                           ext->BehindHubGone);
    XHCI_DBG_VALUE_CHANGED("topology: pending parents dropped by a root reset",
                           ext->Topology.PendingDropped);
    /* Task 7b-A.1.1's rule one tier down (Phase 7 review, A6): usbhub's second
     * hub-port reset per bracket denied its re-arm. Tracks behind-hub
     * enumerations the way the root-tier line tracks root ones. */
    XHCI_DBG_VALUE_CHANGED("topology: hub-tier resets re-arming nothing",
                           ext->Topology.ResetsSuppressed);
    XHCI_DBG_VALUE_CHANGED("topology: TT pairs programmed", ext->TtProgrammed);
    XHCI_DBG_VALUE_CHANGED("topology: TT pairs unresolved", ext->TtUnresolved);
    XHCI_DBG_VALUE_CHANGED("topology: TT pairs agreeing with usbport",
                           ext->TtPairsAgreed);
    XHCI_DBG_VALUE_CHANGED("topology: TT pairs disagreeing with usbport",
                           ext->TtPairsDisagreed);
    XHCI_DBG_VALUE_CHANGED("SET_ADDRESS interceptions",
                           ext->SetAddressIntercepts);
    XHCI_DBG_VALUE_CHANGED("EP0 max packet size corrections",
                           ext->Mps0Corrections);
    XHCI_DBG_VALUE_CHANGED("EP0 max packet size corrections refused as stale",
                           ext->Mps0CorrectionsStale);
    /* **One string literal, and it has to be.** `gen-offsets.ps1` matches a
     * single quoted string followed by the comma, so a label split across
     * adjacent literals - which C concatenates perfectly well - is invisible to
     * it and the field silently leaves `offsets.txt` with `SIZEOF` unchanged, so
     * nothing downstream notices. That is what a review round caught here: the
     * label was widened to name this counter's second cause and wrapped in the
     * doing, which cost the counter its only way of being read out of a live
     * guest. It names the shared thing instead, which is what the two causes
     * have in common and what the header beside the field explains. */
    XHCI_DBG_VALUE_CHANGED("device commands whose tenancy was re-enumerated",
                           ext->CommandsStaleTenancy);
    /* The re-enumeration re-entry (finding A2). `slots reset to Default` is
     * nonzero only when an already-enumerated device is re-enumerated **in
     * place** after reaching Addressed or Configured; a zero on fresh-device
     * churn or on a replug is the legal `ADDRESS_BSR` branch, measured
     * by task 12.5's leg A' (0 here, 12 reopens, 0 unreadable). Read it
     * with `slot states unreadable`, which is expected 0 - that is the reading
     * that would say the slot-state read had failed. */
    XHCI_DBG_VALUE_CHANGED("slots reset to Default", ext->SlotsResetToDefault);
    XHCI_DBG_VALUE_CHANGED("slot states unreadable", ext->SlotStatesUnreadable);
    XHCI_DBG_VALUE_CHANGED("endpoint contexts dropped by a re-enumeration",
                           ext->EndpointContextsDroppedByReset);
    XHCI_DBG_VALUE_CHANGED("hub root ports resolved from the graph",
                           ext->HubRootPortsFromGraph);
    XHCI_DBG_VALUE_CHANGED("device command failures", ext->CommandFailures);
    XHCI_DBG_VALUE_CHANGED("restore-state failures", ext->RestoreFailures);
    /* Save step 2 performed late. A nonzero reading is not a fault - it says a
     * suspend abandoned a command the controller had already completed - but it
     * is the only evidence a machine can give that the collision audit round 5
     * found was ever live. `RestoreEventsFatal` beside it is the half that *is*
     * a fault: an event the drain may not discard was waiting and the restore
     * refused. **It was labelled "host controller event waiting" until audit
     * round 8**, which is one of the kinds it counts and no longer the only one -
     * the kind and the completion code are the two lines below it.
     *
     * **Those two say "last fatal" rather than "refused", and audit round 9's
     * question is why.** `RestoreEventsFatal` is cumulative for the run;
     * `RestoreFatalKind`/`RestoreFatalCode` are written only by a drain that
     * refused and are never cleared, so they outlive every later *successful*
     * restore. That is deliberate - a refusal's kind is worth keeping, and a
     * resume that then worked does not make it uninteresting - but under a label
     * reading "refused" a machine cannot tell whether they describe the attempt
     * it is looking at. The label now says which question they answer. */
    XHCI_DBG_VALUE_CHANGED("restore: stale events dropped",
                           ext->RestoreEventsDiscarded);
    XHCI_DBG_VALUE_CHANGED("restore: refused - a fatal event was waiting",
                           ext->RestoreEventsFatal);
    XHCI_DBG_VALUE_CHANGED("restore: last fatal - XHCI_RESTORE_FATAL_*",
                           ext->RestoreFatalKind);
    XHCI_DBG_VALUE_CHANGED("restore: last fatal - completion code carried",
                           ext->RestoreFatalCode);
    /*
     * The transfer half, and it is here because **running batch 6-V's own probe
     * found that it was not**. The Phase 6 checkpoint names "correct Short
     * Packet/residual handling" and "no double completion" as things the *logs*
     * must prove, and the first Win98 run produced a log in which neither was
     * expressible: a 255-byte string-descriptor request had certainly returned
     * short, and nothing in a release build could say so. Instrumentation
     * that cannot answer the question the run exists to ask is the same
     * defect as an absent counter, one level up.
     *
     * `TransfersSubmitted` against `TransfersCompleted` plus `TransfersCancelled`
     * is the double-completion reading: completions may never exceed the
     * submissions that were not intercepted, and a gap at idle is a transfer
     * nobody answered. `TransferEventsForeign` is the other end - an event that
     * named a slot or endpoint no record has open, which is what a stale slot
     * after a reconnect would look like from here.
     *
     * `SlotsDisabled` beside `SlotsEnabled` is the slot-leak reading the header
     * comment on those two fields has always described and which no build could
     * previously take, since only one of the pair was printed.
     */
    XHCI_DBG_VALUE_CHANGED("slots disabled", ext->SlotsDisabled);
    XHCI_DBG_VALUE_CHANGED("devices torn down", ext->DevicesTornDown);
    XHCI_DBG_VALUE_CHANGED("devices invalidated by a resume",
                           ext->DevicesInvalidated);
    XHCI_DBG_VALUE_CHANGED("devices abandoned without evidence",
                           ext->DevicesAbandoned);
    XHCI_DBG_VALUE_CHANGED("transfers submitted", ext->TransfersSubmitted);
    XHCI_DBG_VALUE_CHANGED("transfers completed", ext->TransfersCompleted);
    XHCI_DBG_VALUE_CHANGED("transfers cancelled", ext->TransfersCancelled);
    XHCI_DBG_VALUE_CHANGED("transfers refused for retry", ext->TransfersRefused);
    /*
     * Task 8-A.1's backpressure pair, and the reason it is a pair rather than a
     * total: a bulk pipe at full speed refuses continuously and that is what
     * working looks like. What would not be working is the first of these
     * climbing while the second stands still - the re-offer never asked for, the
     * pipe running on usbport's 500 ms timer instead of on completions.
     */
    XHCI_DBG_VALUE_CHANGED("transfers refused - ring full",
                           ext->TransfersRefusedRingFull);
    XHCI_DBG_VALUE_CHANGED("endpoint re-offers asked for",
                           ext->EndpointRetriesAsked);
    /* The pair that must be read together: a refusal says "ask again", a
     * failure says "there is nothing here to ask". A refusal count that climbs
     * without the submitted count moving is the livelock this counter split was
     * added for. */
    XHCI_DBG_VALUE_CHANGED("transfers failed - endpoint gone",
                           ext->TransfersFailedGone);
    /* And the net under both of them (task 7b-A.0): records the health poll gave
     * up on because they refused, placed nothing and had no command in flight.
     * Nonzero is this bound working - a device that would otherwise have been
     * refused for ever is now a yellow bang - so read it against the pair above
     * rather than as an error count. */
    XHCI_DBG_VALUE_CHANGED("records failed - no progress",
                           ext->DevicesStalledOut);
    /* Read as a pair. `disowned` is usbport's view, taken the moment it gave
     * the port up; `torn down by a port disable` is the release that follows
     * once the port confirms. A gap between them at idle is a port whose
     * disable never landed - which the batch 6-V Win2000 run showed is the
     * ordinary case, not the exception. */
    XHCI_DBG_VALUE_CHANGED("devices disowned by a port disable",
                           ext->DevicesDisownedOut);
    XHCI_DBG_VALUE_CHANGED("devices torn down by a port disable",
                           ext->DevicesDisabledOut);
    XHCI_DBG_VALUE_CHANGED("transfer events for no open endpoint",
                           ext->TransferEventsForeign);
    XHCI_DBG_VALUE_CHANGED("short packets", ext->ShortPacketsTotal);
    XHCI_DBG_VALUE_CHANGED("short transfers reported as Success",
                           ext->ShortSuccessesTotal);
    XHCI_DBG_VALUE_CHANGED("intermediate Success events",
                           ext->IntermediateEventsTotal);
    /*
     * Read beside `short packets`: the subset this driver retired without
     * waiting for the second event p.175 promises.
     *
     * **Nonzero here does not mean the controller is at fault** - the earlier
     * wording said it did, and it was wrong (the repo audit): the retire
     * is taken on the *first* event, which every controller sends. The three
     * lines together are the verdict, and only when the third is 0:
     *
     * **A verdict needs `voided` 0 AND `outstanding` 0.** Only then:
     *
     *   tails == retired -> two-event controller, spec conforming
     *   tails 0          -> one event only; QEMU's xHC, and the reason the
     *                       departure exists at all
     *
     * `voided` is sticky and says something was dropped (more early retires
     * outstanding than the record ring holds) or censored (a record given up
     * because its TRBs were re-let, or lost with its queue). It gates rather
     * than the two totals because those wrap.
     *
     * `outstanding` is the fourth state and the one easiest to misread: a tail
     * that has not arrived *yet*. This poll runs every 500 ms, straight into
     * that window, so without it a single early retire reads as the one-event
     * signature a few microseconds after it happens (repo audit round 4). It is
     * derived from the identity
     *   retired == tails + dropped + censored + outstanding
     * rather than stored, so it cannot drift from the counters it summarises.
     *
     * **That expectation was pre-9-0.2 and is now inverted.** It used to be
     * that a bulk IN repost loop made `censored` large even on a conforming
     * controller, because the next receive was published over the very TRBs
     * whose tail was still outstanding. Since the retire moved to the end of an
     * observed-empty drain pass, a conforming controller resolves its deferral
     * in-band and produces **no early-retire record to censor at all**. A large
     * count now means QEMU's one-event xHC, or the residual
     * tail-written-after-the-empty-read window - either way something to
     * investigate on real silicon, not to accept as normal.
     */
    XHCI_DBG_VALUE_CHANGED("short packets retired mid-TD",
                           ext->MidTdShortRetiresTotal);
    XHCI_DBG_VALUE_CHANGED("mid-TD retires whose tail event arrived",
                           ext->MidTdShortTailsTotal);
    XHCI_DBG_VALUE_CHANGED("mid-TD tail records evicted unanswered",
                           ext->MidTdTailsDroppedTotal);
    XHCI_DBG_VALUE_CHANGED("mid-TD tail observations censored",
                           ext->MidTdTailsCensoredTotal);
    XHCI_DBG_VALUE_CHANGED("mid-TD tail conformance verdict voided",
                           ext->MidTdVerdictVoided);
    /*
     * Derived from a **snapshot taken under the controller lock**, not from
     * four separate reads of live counters. The DPC updates them one at a time
     * under that lock while this callback runs under usbport's different
     * `MiniportSpinLock`, so on SMP an unlocked reader can take `retired`
     * before a DPC and `tails` after it - and the subtraction is unsigned, so
     * the print would be a nine-digit number rather than a small wrong one
     * (repo audit round 5). A torn read of a diagnostic is still a diagnostic
     * that lies, and this one lies loudest exactly when the machine is busiest.
     */
    XHCI_DBG_VALUE_CHANGED("mid-TD tails still outstanding",
                           xhciMidTdOutstanding(ext));
    /*
     * Task 9-0.2's partition, and it is now the **primary** reading - the five
     * lines above are the residue of the one window the fix did not close, and
     * need every one of their qualifications; these do not.
     *
     *   tailed == deferrals            -> conforming: the promised tail always
     *                                     arrived in-band, and nothing was ever
     *                                     retired early.
     *   spurious == deferrals,         -> two events, but the second carried
     *   tailed == 0                       Success where p.175 says Short
     *                                     Packet. Linux's known host quirk; the
     *                                     transfers are correct either way.
     *   retired > 0 with both tailed   -> one event only, which is QEMU's xHC
     *   counters 0                        and is what batch 8-V.2 measured.
     *
     * `lost` is neither verdict - a teardown, an abort, **or a refused retire**
     * ended the observation, and the two refusal counters below are what
     * separate the last cause from the first two. `pending` is exact only
     * immediately after a settle pass, which is where the identity below is
     * checked. `accounting broken` nonzero makes every line in this block
     * unsafe to read, including the five above.
     */
    XHCI_DBG_VALUE_CHANGED("mid-TD short deferrals armed",
                           ext->MidTdDeferralsTotal);
    XHCI_DBG_VALUE_CHANGED("mid-TD deferrals ended by their promised tail",
                           ext->MidTdDeferralsTailedTotal);
    XHCI_DBG_VALUE_CHANGED("mid-TD deferrals ended by a Success-code tail",
                           ext->MidTdDeferralsTailedSpuriousTotal);
    /* Teardown, abort, or a **settle** the ring refused - all of them end the
     * observation without answering it, so they share a bucket.
     *
     * The two refusal counters below are **not** subtractable from this one.
     * They count divergences from both routes, and an *event-time* divergence
     * never armed a deferral in the first place, so it moves them without
     * moving this - subtracting them can underflow. Only a settle-time refusal
     * is in both. There is no exact teardown/abort share available; read this
     * line as "the observation ended for a reason that is not a verdict". */
    XHCI_DBG_VALUE_CHANGED("mid-TD deferrals lost - teardown, abort or refusal",
                           ext->MidTdDeferralsLostTotal);
    XHCI_DBG_VALUE_CHANGED("mid-TD settle passes",
                           ext->MidTdSettlePasses);
    XHCI_DBG_VALUE_CHANGED("mid-TD transfers settled by them",
                           ext->MidTdSettles);
    /* Defensive: retires the ring refused, **from either route** - the Transfer
     * Event or the settle. Nonzero means a record/ring divergence was found,
     * the transfer was left queued, and a stop-then-drain was *asked for*; it
     * counts the request, not a completed repositioning, so read it against the
     * endpoint quiescence counters rather than as proof the hardware was
     * reprogrammed. It can rise with `deferrals armed` at zero, because an
     * event-time divergence never armed one. */
    XHCI_DBG_VALUE_CHANGED("mid-TD retires the ring refused",
                           ext->MidTdRefusedRetires);
    /* The refusals that could not even ask, because the endpoint's chain had
     * already failed. Nonzero means a transfer waits on an HCRST or a Disable
     * Slot - `xhciEpQuiesceFail`'s policy, not this path's. */
    XHCI_DBG_VALUE_CHANGED("mid-TD refusals on an already-failed endpoint",
                           ext->MidTdRefusedRetiresUnarmable);
    XHCI_DBG_VALUE_CHANGED("mid-TD deferrals still armed",
                           ext->MidTdDeferPending);
    XHCI_DBG_VALUE_CHANGED("mid-TD deferral accounting broken",
                           ext->MidTdDeferAccountingBroken);
    /* Trailing and already-completed events, the channel the departure's
     * no-double-completion argument relies on. */
    XHCI_DBG_VALUE_CHANGED("events matching no outstanding transfer",
                           ext->UnmatchedEventsTotal);
    /*
     * The endpoint half (batches 7a-A and 7a-B), and it is here for the reason
     * batch 6-V's own run established one level up: a counter no build prints
     * cannot answer the question the run exists to ask. Both batches added
     * their counters to the extension and neither added a print, so the whole
     * endpoint and quiescence family was unreadable in the run that is its
     * first execution on a target.
     *
     * The pairs, because the totals on their own say much less:
     *   `EndpointsOpened` vs `EndpointsConfigured` - pipes usbport believes in
     *   that the xHC never accepted, i.e. a HID device that enumerates and then
     *   stays silent.
     *   `EndpointStops` vs `EndpointStoppedEvents` - **not** one-to-one, since a
     *   stop of an already-Halted ring forces no event (4.6.9 p.124), but a zero
     *   here on a run with ordinary stops says the controller does not force
     *   them at all and the placement is running against an unannounced stop.
     *   `EndpointHalts` vs `EndpointResets` - a gap at idle is a pipe nobody
     *   reset.
     *   `EndpointRestarts` vs `EndpointRestartsByPoll` - the second is **not**
     *   expected zero (see XHCI_EXTENSION): SetEndpointState is edge-triggered
     *   on usbport's own state in both builds, so a stop this driver took by
     *   itself is never announced back. A value that keeps climbing against
     *   idle traffic is the alarm, not a value above zero.
     *   `TransfersNoOpped` vs `TransfersAborted` - the cancellation actually
     *   reclaiming TRBs rather than only bookkeeping.
     * `EndpointSpeedMismatches` and `EndpointIntervalsFloored` are **expected
     * nonzero** with any FS/LS device attached (Phase 5 task 7), so a zero there
     * means that untruth was removed, not that nothing happened.
     */
    XHCI_DBG_VALUE_CHANGED("endpoints opened", ext->EndpointsOpened);
    XHCI_DBG_VALUE_CHANGED("endpoints reopened", ext->EndpointsReopened);
    XHCI_DBG_VALUE_CHANGED("endpoints configured", ext->EndpointsConfigured);
    XHCI_DBG_VALUE_CHANGED("endpoints released", ext->EndpointsReleased);
    XHCI_DBG_VALUE_CHANGED("endpoint refusals - type",
                           ext->EndpointRefusalsType);
    XHCI_DBG_VALUE_CHANGED("endpoint refusals - no device",
                           ext->EndpointRefusalsNoDevice);
    XHCI_DBG_VALUE_CHANGED("endpoint refusals - params",
                           ext->EndpointRefusalsParams);
    XHCI_DBG_VALUE_CHANGED("endpoint refusals - ring pool",
                           ext->EndpointRefusalsPool);
    /* The pool's own split of the line above (Phase 7 review, B7/B8): "a
     * device offering a fifth endpoint", "a machine over the declared ring
     * limit" and "a fairness hold" are three different diagnoses that read
     * identically without these. PeakInUse is the sizing reading. */
    XHCI_DBG_VALUE_CHANGED("ring pool refusals - empty",
                           ext->RingPool.AcquireFailuresEmpty);
    XHCI_DBG_VALUE_CHANGED("ring pool refusals - fairness",
                           ext->RingPool.AcquireFailuresFairness);
    XHCI_DBG_VALUE_CHANGED("ring pool refusals - per-device cap",
                           ext->RingPool.AcquireFailuresCap);
    XHCI_DBG_VALUE_CHANGED("ring pool peak in use", ext->RingPool.PeakInUse);
    XHCI_DBG_VALUE_CHANGED("endpoint refusals - not ready",
                           ext->EndpointRefusalsNotReady);
    XHCI_DBG_VALUE_CHANGED("endpoint speed mismatches",
                           ext->EndpointSpeedMismatches);
    XHCI_DBG_VALUE_CHANGED("endpoint intervals floored",
                           ext->EndpointIntervalsFloored);
    XHCI_DBG_VALUE_CHANGED("endpoints refused - no bandwidth",
                           ext->EndpointsNoBandwidth);
    XHCI_DBG_VALUE_CHANGED("endpoints refused - no resources",
                           ext->EndpointsNoResources);
    XHCI_DBG_VALUE_CHANGED("endpoint configure failures",
                           ext->EndpointConfigureFailures);
    XHCI_DBG_VALUE_CHANGED("endpoint reconfigures", ext->EndpointReconfigures);
    XHCI_DBG_VALUE_CHANGED("endpoint context restores",
                           ext->EndpointContextRestores);
    XHCI_DBG_VALUE_CHANGED("endpoint removes held", ext->EndpointRemovesHeld);
    XHCI_DBG_VALUE_CHANGED("EP0 removes on a superseded handle",
                           ext->Ep0RemovesSuperseded);
    XHCI_DBG_VALUE_CHANGED("endpoint removes with work queued",
                           ext->RemovesWithWork);
    XHCI_DBG_VALUE_CHANGED("endpoint stops", ext->EndpointStops);
    XHCI_DBG_VALUE_CHANGED("endpoint stopped events",
                           ext->EndpointStoppedEvents);
    XHCI_DBG_VALUE_CHANGED("endpoint stop failures", ext->EndpointStopFailures);
    XHCI_DBG_VALUE_CHANGED("endpoint halts", ext->EndpointHalts);
    XHCI_DBG_VALUE_CHANGED("endpoint resets", ext->EndpointResets);
    XHCI_DBG_VALUE_CHANGED("endpoint reset failures",
                           ext->EndpointResetFailures);
    XHCI_DBG_VALUE_CHANGED("endpoint resets on a ring not halted",
                           ext->EndpointResetsNotHalted);
    XHCI_DBG_VALUE_CHANGED("endpoint dequeue sets", ext->EndpointDequeueSets);
    XHCI_DBG_VALUE_CHANGED("endpoint dequeue failures",
                           ext->EndpointDequeueFailures);
    XHCI_DBG_VALUE_CHANGED("endpoint dequeue re-arms", ext->EndpointDequeueRearms);
    XHCI_DBG_VALUE_CHANGED("endpoint placement failures",
                           ext->EndpointPlacementFailures);
    XHCI_DBG_VALUE_CHANGED("endpoint restarts", ext->EndpointRestarts);
    XHCI_DBG_VALUE_CHANGED("endpoint restarts by the poll",
                           ext->EndpointRestartsByPoll);
    XHCI_DBG_VALUE_CHANGED("endpoint quiescence lost",
                           ext->EndpointQuiesceLost);
    XHCI_DBG_VALUE_CHANGED("endpoint quiescence unavailable",
                           ext->EndpointQuiesceUnavailable);
    /* Every entry into the sticky failed state, which is the one the save gate
     * declines on permanently - not the sum of the two above. See xhci.h. */
    XHCI_DBG_VALUE_CHANGED("endpoint quiescence failures",
                           ext->EndpointQuiesceFailures);
    XHCI_DBG_VALUE_CHANGED("endpoints revived by a resume",
                           ext->EndpointsRevivedByResume);
    /* Table 6-90 p.468's Disable Slot recovery, performed. A nonzero reading is
     * a device this controller will not talk to, not a driver fault. */
    XHCI_DBG_VALUE_CHANGED("incompatible devices, slot disabled",
                           ext->IncompatibleDeviceTeardowns);
    XHCI_DBG_VALUE_CHANGED("teardowns without a stop", ext->TeardownsWithoutStop);
    XHCI_DBG_VALUE_CHANGED("transfers on a halted endpoint",
                           ext->TransfersOnHaltedEndpoint);
    XHCI_DBG_VALUE_CHANGED("transfers aborted", ext->TransfersAborted);
    XHCI_DBG_VALUE_CHANGED("transfers No-Opped by a cancellation",
                           ext->TransfersNoOpped);
    XHCI_DBG_VALUE_CHANGED("aborts unmatched", ext->AbortsUnmatched);
    XHCI_DBG_VALUE_CHANGED("aborts taken off the completion list",
                           ext->AbortsFromCompletionList);
    XHCI_DBG_VALUE_CHANGED("aborts racing a completion",
                           ext->AbortsDuringCompletion);
    XHCI_DBG_VALUE_CHANGED("aborts before the endpoint stopped",
                           ext->AbortsBeforeStopped);
    /* The counter whose own comment says it makes AbortTransfer's byte count
     * "a measurement rather than the zero the record was created with" - a
     * claim only a print can carry to a target (Phase 7 review, B7). */
    XHCI_DBG_VALUE_CHANGED("stopped-event lengths latched",
                           ext->StoppedLengthsLatched);
    XHCI_DBG_VALUE_CHANGED("endpoint status queries",
                           ext->EndpointStatusQueries);
    XHCI_DBG_VALUE_CHANGED("endpoint status RUN requests",
                           ext->EndpointStatusRunRequests);
    XHCI_DBG_VALUE_CHANGED("endpoint status other requests",
                           ext->EndpointStatusOtherRequests);
    XHCI_DBG_VALUE_CHANGED("endpoint data toggle resets",
                           ext->EndpointDataToggleResets);
    XHCI_DBG_VALUE_CHANGED("endpoint invalidates", ext->EndpointInvalidates);
    /* Times a drain declined to deliver because a `SubmitTransfer` was on the
     * stack. Expected **nonzero** on any run where a device goes away with a
     * read posted - that is the fix working, not a fault - and its pair is
     * `transfers failed - endpoint gone`, which is what those completions are.
     * A climbing count with that pair frozen would be the opposite reading: a
     * completion held by a depth nothing decrements. */
    XHCI_DBG_VALUE_CHANGED("completions held by a submit",
                           ext->CompletionsHeldBySubmit);
    /* Finding A7's second hold: passes that found the depth at 0 but had
     * begun while the bracket was still open. Expected rare - it needs a
     * drain to be mid-loop at the moment a submit returns. */
    XHCI_DBG_VALUE_CHANGED("completions held for the next pass",
                           ext->CompletionsHeldByPass);
    XHCI_DBG_VALUE_CHANGED("deferred-work re-entries declined",
                           ext->DeferredReentries);
    XHCI_DBG_VALUE_CHANGED("submit brackets closed with none open",
                           ext->SubmitUnderflows);
    /*
     * Task 6-V.1's probe. These are the numbers the run is *read* from rather
     * than a health check: the two shape words say which properties of the
     * transfer and endpoint contracts this controller has actually shown, and
     * the four counters beside them are the anomalies the static ABI record
     * could not rule out - elements not ascending by SgOffset, a list that does
     * not tile its buffer, a physical address above 4 GB, and a bounce mapping.
     * A run that ends with all four at zero and the shape words populated is
     * the probe confirming the record; any nonzero one is the finding.
     *
     * `probe hub-class setups` and `probe endpoints with a TT` are Phase 7b's
     * feasibility measurement and are expected to be **zero** in a VM run, for
     * a measured reason rather than an optimistic one - QEMU's only hub model is
     * USB 1.1, which is the topology usbport's own GetTt defect bugchecks on
     * (Phase 5 task 7's carried defect), so no hub can be on the bus to produce
     * either.
     */
    XHCI_DBG_VALUE_CHANGED("probe transfers", ext->ProbeTransfers);
    XHCI_DBG_VALUE_CHANGED("probe transfer shape", ext->ProbeSgShape);
    XHCI_DBG_VALUE_CHANGED("probe endpoint shape", ext->ProbeEpShape);
    XHCI_DBG_VALUE_CHANGED("probe max SG elements", ext->ProbeSgElementsMax);
    XHCI_DBG_VALUE_CHANGED("probe SG lists out of SgOffset order",
                           ext->ProbeSgDisordered);
    XHCI_DBG_VALUE_CHANGED("probe SG lists that do not tile",
                           ext->ProbeSgGapped);
    XHCI_DBG_VALUE_CHANGED("probe SG elements above 4 GB",
                           ext->ProbeSgHighDwords);
    XHCI_DBG_VALUE_CHANGED("probe bounce-mapped transfers", ext->ProbeSgMapped);
    XHCI_DBG_VALUE_CHANGED("probe class setups (any recipient)",
                           ext->ProbeClassSetups);
    XHCI_DBG_VALUE_CHANGED("probe hub-class setups", ext->ProbeHubClassSetups);
    XHCI_DBG_VALUE_CHANGED("probe endpoints with a TT", ext->ProbeEpWithTt);
    /*
     * Task 7b-A.1.2. The *values* are the deliverable and they are not printable
     * here - a pair is a number per entry, and this site's budget is 32 lines
     * for the whole image. These two say whether the table is worth reading and
     * whether it is complete. The tracked vm-matrix table exposes only `Count`
     * and `Dropped`; the entry rows themselves are decoded by the host-local
     * `scripts\local\readcounters.ps1` (design record 02), and **no fresh-clone
     * decoder for them is tracked** - a structured row needs a base, a count, a
     * stride and four member offsets, which the scalar table does not carry.
     * *(Round 14: the day before, this comment was rewritten to say
     * a clone takes the pairs through `gen-offsets.ps1`. It cannot; the
     * generator only ever emitted the two scalars above.)*
     */
    XHCI_DBG_VALUE_CHANGED("probe topology pairs", ext->ProbeTtPairs.Count);
    XHCI_DBG_VALUE_CHANGED("probe topology pairs dropped",
                           ext->ProbeTtPairs.Dropped);
    /*
     * `OpensTotal` against `ProbeEpEvents[OPEN] + ProbeEpEvents[REOPEN]` is the
     * second open-accounting reading `src/xhci.h` names, and the generator
     * derives the offset table from these sites - so the two terms need sites
     * of their own. *(Round 13: neither had one, and the generator
     * also skipped every subscripted site, so none of this family was in
     * `offsets.txt`; the 2b reading came through the local roster.)*
     */
    XHCI_DBG_VALUE_CHANGED("probe OpenEndpoint calls",
                           ext->ProbeEpEvents[XHCI_PROBE_EVENT_OPEN]);
    XHCI_DBG_VALUE_CHANGED("probe ReopenEndpoint calls",
                           ext->ProbeEpEvents[XHCI_PROBE_EVENT_REOPEN]);
    XHCI_DBG_VALUE_CHANGED("probe CloseEndpoint calls",
                           ext->ProbeEpEvents[XHCI_PROBE_EVENT_CLOSE]);
    XHCI_DBG_VALUE_CHANGED("probe GetEndpointState calls",
                           ext->ProbeEpEvents[XHCI_PROBE_EVENT_GET_STATE]);
    XHCI_DBG_VALUE_CHANGED("probe setup keys dropped",
                           ext->ProbeSetups.Overflows);

    /*
     * ------------------------------------------------------------------
     * The families that were incremented and never printed (repo audit C1)
     * ------------------------------------------------------------------
     *
     * Phase 4 task 8's rule, applied to itself: **a counter no build prints
     * cannot answer the question the run exists to ask.** Seventy-seven
     * extension counters were reachable in both flavours and printed in
     * neither, so the only channel for them was reading the extension out of a
     * live guest through the QEMU monitor (`scripts\counters\`) - which is no
     * channel at all on the bare-metal validation targets Phase 13 requires.
     *
     * They go here rather than beside the code that increments them, and that
     * is the same decision `XHCI_DBG_VALUE_LIMITED`'s own contract records: an
     * event-line site is bounded at 32 prints for the life of the image and a
     * HID device exhausts one in seconds, so the durable witness belongs in
     * this block, where a family stays reconcilable after its event lines have
     * gone quiet. This block's sites are change-gated and capped the same way
     * every other counter here is; the *precise* reading is still the live
     * extension, and the two are meant to be used together.
     *
     * Grouped by the question each family answers, because a lone number here
     * is worth much less than its neighbours.
     */

    /* Root hub: what usbhub asked for, what the ports did about it, and the
     * three ways an operation can end without being carried out. `Refusals`
     * against `PortsBusy` says whether a port is wedged armed; the
     * `*Unconfirmed` pair says a PORTSC write was issued and never observed to
     * land, which is the disown/PortDisabled split's own failure mode. */
    XHCI_DBG_VALUE_CHANGED("RH port status queries", ext->RhPortStatusQueries);
    XHCI_DBG_VALUE_CHANGED("RH hub status queries", ext->RhHubStatusQueries);
    XHCI_DBG_VALUE_CHANGED("RH invalid port index", ext->RhInvalidPort);
    XHCI_DBG_VALUE_CHANGED("RH refusals", ext->RhRefusals);
    XHCI_DBG_VALUE_CHANGED("RH first decodes", ext->RhFirstDecodes);
    XHCI_DBG_VALUE_CHANGED("RH ports powered", ext->RhPortsPowered);
    XHCI_DBG_VALUE_CHANGED("RH ports unpowered", ext->RhPortsUnpowered);
    XHCI_DBG_VALUE_CHANGED("RH ports disabled", ext->RhPortsDisabled);
    XHCI_DBG_VALUE_CHANGED("RH ports suspended", ext->RhPortsSuspended);
    XHCI_DBG_VALUE_CHANGED("RH ports reset", ext->RhPortsReset);
    XHCI_DBG_VALUE_CHANGED("RH resets completed", ext->RhResetsCompleted);
    XHCI_DBG_VALUE_CHANGED("RH reset timeouts", ext->RhResetTimeouts);
    XHCI_DBG_VALUE_CHANGED("RH ports resumed", ext->RhPortsResumed);
    XHCI_DBG_VALUE_CHANGED("RH resumes completed", ext->RhResumesCompleted);
    XHCI_DBG_VALUE_CHANGED("RH resumes abandoned", ext->RhResumesAbandoned);
    XHCI_DBG_VALUE_CHANGED("RH ports busy", ext->RhPortsBusy);
    XHCI_DBG_VALUE_CHANGED("RH stale timers", ext->RhStaleTimers);
    XHCI_DBG_VALUE_CHANGED("RH timer failures", ext->RhTimerFailures);
    XHCI_DBG_VALUE_CHANGED("RH operations retired by age", ext->RhAgeRetires);
    /*
     * The other retirement cause, and it is one of the two witnesses task
     * 11-V.1's "stop while callbacks are armed" clause turns on: the quiesce
     * found a reset or a resume still armed and retired it, i.e. the stop
     * landed inside one. Its trace line in `src/xhci_rh.c` is an
     * `XHCI_DBG_VALUE` and is therefore in no offset table; this is the same
     * fact as a number a counter read can reach.
     *
     * Read it the way `docs/contributing/runs/run-11v.md` stage G says: on the measured Windows
     * 98 shutdown the quiesce runs inside `SuspendController` ahead of the
     * stop, and the next start zeroes the extension - so the trace keeps it and
     * a later counter read does not.
     */
    XHCI_DBG_VALUE_CHANGED("RH operations retired by the quiesce",
                           ext->RhOperationsRetired);
    /*
     * Expected **zero** on both candidate resume paths (roadmap task 11-V.1),
     * which is exactly why it needs a change-gated site: this macro prints its
     * first sample even when that sample is zero, so the expected reading stops
     * being an absent line. The resume itself needs bare-metal Windows 2000,
     * which this project has no vehicle for; it is published as a limitation.
     */
    XHCI_DBG_VALUE_CHANGED("RH ports driven out of U3",
                           ext->RhPortsDriventoU0);
    XHCI_DBG_VALUE_CHANGED("RH resets not confirmed",
                           ext->RhResetsUnconfirmed);
    XHCI_DBG_VALUE_CHANGED("RH preempts not confirmed",
                           ext->RhPreemptsUnconfirmed);
    XHCI_DBG_VALUE_CHANGED("RH resume retries", ext->RhResumeRetries);
    XHCI_DBG_VALUE_CHANGED("RH port power pending", ext->RhPortPowerPending);
    XHCI_DBG_VALUE_CHANGED("RH port power stuck", ext->RhPortPowerStuck);
    XHCI_DBG_VALUE_CHANGED("RH changes cleared", ext->RhChangesCleared);
    XHCI_DBG_VALUE_CHANGED("RH chirps", ext->RhChirps);
    XHCI_DBG_VALUE_CHANGED("RH irq gate closes", ext->RhIrqGateCloses);
    XHCI_DBG_VALUE_CHANGED("RH irq gate opens", ext->RhIrqGateOpens);
    /* A reset or resume ended by a power-off or a disable arriving on top of it
     * (audit finding B2: it had no print site). Expected 0; nonzero says usbhub
     * and this driver were driving the same port at once. */
    XHCI_DBG_VALUE_CHANGED("RH operations preempted",
                           ext->RhOperationsPreempted);
    /* Finding A5's counter: a restore-success resume that left U3 ports alone
     * rather than driving them out. Expected 0 until a controller with FSC >= 1
     * completes a restore, which no vehicle does today. */
    XHCI_DBG_VALUE_CHANGED("RH U3 passes skipped after a restore",
                           ext->RhU3PassSkippedAfterRestore);
    /* A Port Status Change Event naming a port this driver does not manage is
     * the USB3-companion case and is expected nonzero; one that resolves to no
     * port at all is not. */
    XHCI_DBG_VALUE_CHANGED("port events unmapped", ext->PortEventsUnmapped);
    /*
     * `PortEventsMapped` against `PortEventChanges` is the reading
     * `run-13e.md` and the roadmap tell the next session to take - Mapped
     * exceeding Changes is `XhciRootHubPortEvent`'s uncounted early return
     * caught in the act. *(Round 13: the mapped side had no print
     * site, so it was absent from `scripts/vm-matrix/offsets.txt` and the
     * tracked reader could not name it. The bench read it through
     * the git-ignored `scripts/local/offsets.c`, which is exactly the
     * hand-kept roster `gen-offsets.ps1` exists to replace. `RootHubInvalidatesOwed`
     * was read the same way and is published here for the same reason.)*
     */
    XHCI_DBG_VALUE_CHANGED("port events mapped", ext->PortEventsMapped);
    XHCI_DBG_VALUE_CHANGED("port event changes", ext->PortEventChanges);
    XHCI_DBG_VALUE_CHANGED("root hub invalidates owed",
                           ext->RootHubInvalidatesOwed);
    XHCI_DBG_VALUE_CHANGED("root hub invalidates", ext->RootHubInvalidates);
    XHCI_DBG_VALUE_CHANGED("root hub invalidates gated",
                           ext->RootHubInvalidatesGated);

    /* Lifecycle. `SuspendCount` against `ResumeReinits` is the reading Win98's
     * ~0.5 s idle suspend makes the ordinary case; `SaveAttempts` against
     * `SaveFailures`/`SaveRestoreTimeouts` says whether CSS/CRS is doing
     * anything on this controller or the error path is carrying every cycle. */
    XHCI_DBG_VALUE_CHANGED("teardowns", ext->TeardownCount);
    /*
     * **The port-power family, all of it** (audit finding B2). Only the
     * `-suspended` row below had a print site, which is the shared-counter shape
     * this repository banned in a different disguise: `PortTeardownSkipped` and
     * `PortTeardownSkippedSuspended` are documented in `src/xhci.h` as having
     * "opposite diagnoses" - a stop that arrived on a suspended controller is the
     * measured ordinary shutdown, a stop on a controller this driver never ran is
     * somebody else's port power - and printing one of a pair makes the other's
     * value unreachable rather than absent.
     *
     * `PortsPowered` against `PortPowerFailures` is the start-path reading, and
     * `PortsUnpowered` is the USB 3.x half the port strategy requires: a zero
     * there on a controller with SuperSpeed ports means devices are training onto
     * ports nothing services. `PortsUnpoweredAtStop` and `PortTeardownFailures`
     * are the same pair at the other end.
     */
    XHCI_DBG_VALUE_CHANGED("ports powered at start", ext->PortsPowered);
    XHCI_DBG_VALUE_CHANGED("ports unpowered at start", ext->PortsUnpowered);
    XHCI_DBG_VALUE_CHANGED("port power failures", ext->PortPowerFailures);
    XHCI_DBG_VALUE_CHANGED("ports unpowered at stop",
                           ext->PortsUnpoweredAtStop);
    XHCI_DBG_VALUE_CHANGED("port teardown failures", ext->PortTeardownFailures);
    XHCI_DBG_VALUE_CHANGED("port teardowns skipped - not running",
                           ext->PortTeardownSkipped);
    XHCI_DBG_VALUE_CHANGED("port teardowns skipped - suspended",
                           ext->PortTeardownSkippedSuspended);
    XHCI_DBG_VALUE_CHANGED("DMA failures closed", ext->DmaFailClosed);
    /*
     * The variant that *survives* to be read, and `src/xhci.h` describes it as
     * "the evidence a later unexplained corruption would want": a fail-closed
     * that could not be performed because the service was unavailable. It had no
     * print site, so the evidence existed only inside an extension dump.
     */
    XHCI_DBG_VALUE_CHANGED("DMA fail-closed unavailable",
                           ext->DmaFailClosedUnavailable);
    XHCI_DBG_VALUE_CHANGED("DMA fail-closed deferred (in-place recovery)",
                           ext->DmaFailClosedDeferred);
    /* The quiesce's last resort. `BusMasterClearRetries` nonzero says the bit
     * did not stay clear on the first write, which is a controller arguing with
     * its own config space. */
    XHCI_DBG_VALUE_CHANGED("bus master cleared", ext->BusMasterCleared);
    XHCI_DBG_VALUE_CHANGED("bus master clear retries",
                           ext->BusMasterClearRetries);
    XHCI_DBG_VALUE_CHANGED("suspends", ext->SuspendCount);
    XHCI_DBG_VALUE_CHANGED("suspend failures", ext->SuspendFailures);
    XHCI_DBG_VALUE_CHANGED("resume reinitialisations", ext->ResumeReinits);
    XHCI_DBG_VALUE_CHANGED("resume failures", ext->ResumeFailures);
    XHCI_DBG_VALUE_CHANGED("state save attempts", ext->SaveAttempts);
    XHCI_DBG_VALUE_CHANGED("state save failures", ext->SaveFailures);
    XHCI_DBG_VALUE_CHANGED("state save/restore timeouts",
                           ext->SaveRestoreTimeouts);
    /*
     * The two declines, which had no print site until task 12.1 and so could not
     * be read out of any machine - `scripts\vm-matrix\gen-offsets.ps1` derives
     * the readable field set from these very pairs, so a counter without one is
     * not a counter. `SavesDeclinedNoFsc` is the one the published limitation
     * names: nonzero means this controller does not declare FSC, so every
     * suspend/resume here reinitialises the bus rather than restoring it, and it
     * is the difference between that limitation applying to a machine and not.
     * `SavesDeclinedCommandBusy` is expected to stay 0 - see xhciSaveState.
     */
    XHCI_DBG_VALUE_CHANGED("state saves declined - no FSC",
                           ext->SavesDeclinedNoFsc);
    XHCI_DBG_VALUE_CHANGED("state saves declined - command ring busy",
                           ext->SavesDeclinedCommandBusy);

    /* Event path. `DrainBoundHits` nonzero means a pass hit its four-lap bound;
     * usually the ring was still non-empty, which is safe by construction but
     * says the interrupter is producing faster than the DPC retires. The subset
     * where the ring was empty anyway is `DrainBoundEmptyHits` above, and that
     * subset does settle. `EventsVendor` is legal and ignorable,
     * `EventsUnknown` is not. */
    XHCI_DBG_VALUE_CHANGED("events - vendor defined", ext->EventsVendor);
    XHCI_DBG_VALUE_CHANGED("events - unknown type", ext->EventsUnknown);
    XHCI_DBG_VALUE_CHANGED("drain bound hits", ext->DrainBoundHits);
    /* Of those, the ones whose ring was empty anyway. Read beside the line
     * above: it is the exit that would have stranded a mid-TD deferral before
     * the settle's gate stopped asking which exit was taken. */
    XHCI_DBG_VALUE_CHANGED("drain bound hits with the ring already empty",
                           ext->DrainBoundEmptyHits);
    XHCI_DBG_VALUE_CHANGED("DPCs after failure", ext->DpcsAfterFailure);
    XHCI_DBG_VALUE_CHANGED("interrupt disables", ext->InterruptDisables);
    XHCI_DBG_VALUE_CHANGED("interrupt flushes", ext->InterruptFlushes);
    XHCI_DBG_VALUE_CHANGED("enables with events pending",
                           ext->EnablesWithEventsPending);
    /* Read beside the endpoint state changes further down, but NOT as an
     * equality: only one of usbport's four SetEndpointState call sites queues
     * the endpoint and asks for this, so the floor is well under the state
     * changes. What it is worth reading is the excess - every walker pass that
     * could not yet retire the head endpoint asks again, so a count running
     * away from the state changes is a frame number that is not moving. See
     * xhciInterruptNextSOF. */
    XHCI_DBG_VALUE_CHANGED("interrupt next-SOF requests",
                           ext->InterruptNextSofRequests);

    /* Command engine. `CommandsIssued` against `CommandsCompleted` is the
     * whole engine in two numbers; everything under them is a way the pair can
     * fail to reconcile, and `NoOpWitnessFired` is the Phase 4 checkpoint's
     * pointer-match clause as a release-build reading (one per start and one
     * per resume reinitialisation, never raised by ordinary traffic). */
    /* Above the health poll rather than beside it: this one counts the
     * *callback*, the next one counts the polls that got past its gate, and the
     * pair is only interesting when they disagree - which is what a suspended
     * controller produces if usbport keeps calling. Change-gated like the rest,
     * so it spends its per-site budget in the first sixteen seconds and then
     * goes quiet; the live-guest reader is what reads it after that. */
    XHCI_DBG_VALUE_CHANGED("check callbacks", ext->CheckCallbacks);
    XHCI_DBG_VALUE_CHANGED("health polls", ext->HealthPolls);
    XHCI_DBG_VALUE_CHANGED("health polls - controller dead",
                           ext->HealthPollsDead);
    XHCI_DBG_VALUE_CHANGED("commands issued", ext->CommandsIssued);
    XHCI_DBG_VALUE_CHANGED("commands completed", ext->CommandsCompleted);
    XHCI_DBG_VALUE_CHANGED("commands unmatched", ext->CommandsUnmatched);
    XHCI_DBG_VALUE_CHANGED("commands with a bad completion code",
                           ext->CommandsBadCompletion);
    XHCI_DBG_VALUE_CHANGED("commands timed out", ext->CommandsTimedOut);
    /* Finding T's discriminator. `CommandsIssued` minus this is the number of
     * watchdogs that were armed and never came back; this at 0 with commands
     * that provably hung means usbport's timer service dropped them, which is a
     * different fault from one that fires late. */
    XHCI_DBG_VALUE_CHANGED("command watchdog callbacks that arrived",
                           ext->CommandTimeoutArrivals);
    XHCI_DBG_VALUE_CHANGED("commands aborted", ext->CommandsAborted);
    /*
     * The abort ladder's own two, which had no print site (audit finding B2) -
     * and on a run tail they separate two opposite investigations.
     * `CommandAbortsNotWritten` is "the abort was never written", i.e. this
     * driver could not even ask; a nonzero `CommandAbortWaits` with it at 0 is
     * "the abort was written and the controller ignored it", which is the
     * controller's fault and escalates differently.
     */
    XHCI_DBG_VALUE_CHANGED("command aborts not written",
                           ext->CommandAbortsNotWritten);
    XHCI_DBG_VALUE_CHANGED("command abort waits", ext->CommandAbortWaits);
    XHCI_DBG_VALUE_CHANGED("command ring stops", ext->CommandRingStops);
    XHCI_DBG_VALUE_CHANGED("command ring diverged", ext->CommandRingDiverged);
    XHCI_DBG_VALUE_CHANGED("commands abandoned", ext->CommandsAbandoned);
    XHCI_DBG_VALUE_CHANGED("command stale callbacks",
                           ext->CommandStaleCallbacks);
    XHCI_DBG_VALUE_CHANGED("command timer failures",
                           ext->CommandTimerFailures);
    XHCI_DBG_VALUE_CHANGED("command reset requests",
                           ext->CommandResetRequests);
    /* The part of that total the controller *reported* rather than the part this
     * driver *inferred* from a ring position it could not adopt - audit round 10
     * found the two sharing one reading. */
    XHCI_DBG_VALUE_CHANGED("command reset requests from a fatal completion code",
                           ext->CommandsFatal);
    XHCI_DBG_VALUE_CHANGED("health poll clock ms", ext->PollClockMs);
    XHCI_DBG_VALUE_CHANGED("health poll clock stalls", ext->PollClockStalls);
    XHCI_DBG_VALUE_CHANGED("commands after failure", ext->CommandsAfterFailure);
    XHCI_DBG_VALUE_CHANGED("commands with reserved bits set",
                           ext->CommandsReservedBitsSet);
    XHCI_DBG_VALUE_CHANGED("No Op self-test witnesses", ext->NoOpWitnessFired);

    /* Device and frame. `FrameStalls`/`FrameReadFailures` matter more than
     * their size suggests: usbport's own endpoint-state gate is driven by
     * Get32BitFrameNumber, so a frame number that stops advancing strands
     * every cancellation (batch 6-0). */
    XHCI_DBG_VALUE_CHANGED("devices reopened", ext->DevicesReopened);
    XHCI_DBG_VALUE_CHANGED("open refusals - no record",
                           ext->OpenRefusalsNoRecord);
    XHCI_DBG_VALUE_CHANGED("frame number stalls", ext->FrameStalls);
    XHCI_DBG_VALUE_CHANGED("frame number read failures",
                           ext->FrameReadFailures);

    /* Probe firings, which are what say whether the shape words above are a
     * measurement or a silence. */
    XHCI_DBG_VALUE_CHANGED("probe SG shape firings", ext->ProbeSgShapeFirings);
    XHCI_DBG_VALUE_CHANGED("probe SG length gaps", ext->ProbeSgLengthGaps);
    XHCI_DBG_VALUE_CHANGED("probe SG dumps", ext->ProbeSgDumps);
    XHCI_DBG_VALUE_CHANGED("probe split transfers", ext->ProbeSplitTransfers);
    XHCI_DBG_VALUE_CHANGED("probe endpoint shape firings",
                           ext->ProbeEpShapeFirings);
    XHCI_DBG_VALUE_CHANGED("probe TT observations", ext->ProbeTtObservations);

    /* The per-queue transfer diagnostics, folded up at queue teardown so an
     * unplugged device no longer takes its evidence with it (repo audit C2).
     * `OrphanedGroups` is the only record of a state this engine has no path
     * to; `PlacementFailures` is the ring refusing an explicit dequeue
     * position, which is where a cancellation stops being recoverable. */
    XHCI_DBG_VALUE_CHANGED("orphaned TD groups", ext->OrphanedGroupsTotal);
    XHCI_DBG_VALUE_CHANGED("dequeue placement failures",
                           ext->PlacementFailuresTotal);
    XHCI_DBG_VALUE_CHANGED("transfers swept without an event",
                           ext->SweptTransfersTotal);
    XHCI_DBG_VALUE_CHANGED("residuals rejected", ext->ResidualRejectsTotal);
    XHCI_DBG_VALUE_CHANGED("length overruns", ext->LengthOverrunsTotal);
    XHCI_DBG_VALUE_CHANGED("TRB length sum failures", ext->SumFailuresTotal);
    XHCI_DBG_VALUE_CHANGED("residuals ignored", ext->ResidualIgnoredTotal);
    /* The other seven of the same block, which had no reader at all until the
     * repo audit's finding B3. `foreign transfer events` is the one to read
     * first: xhci_xfer.c declines to escalate an event naming another slot or
     * DCI *because* "the visible failure is the counter", and until this row
     * existed it was not visible. `TDs submitted` against `TDs completed` is the
     * stall shape - the first climbing while the second is frozen. */
    XHCI_DBG_VALUE_CHANGED("foreign transfer events", ext->ForeignEventsTotal);
    XHCI_DBG_VALUE_CHANGED("Event Data events", ext->EventDataEventsTotal);
    XHCI_DBG_VALUE_CHANGED("unassigned completion codes", ext->BadCodesTotal);
    XHCI_DBG_VALUE_CHANGED("transfer error events", ext->QueueErrorsTotal);
    XHCI_DBG_VALUE_CHANGED("transfer recoveries", ext->QueueRecoveriesTotal);
    XHCI_DBG_VALUE_CHANGED("TDs submitted", ext->QueueSubmittedTotal);
    XHCI_DBG_VALUE_CHANGED("TDs completed", ext->QueueCompletedTotal);
    XHCI_DBG_VALUE_CHANGED("stopped codes refused by the transfer layer",
                           ext->StoppedRefusedTotal);

    /*
     * **Batch 9-A's isochronous block, which had no print site at all** - the
     * counters were added and never given a channel, so the one thing task 9-V
     * exists to read was readable only by dumping the extension out of a live
     * guest. That is repo audit finding C1's shape ("77 printless counters"),
     * repeated by the batch that came after it, and it is fixed here rather than
     * left for the run that would have discovered it.
     *
     * `IsoSubmits` against `IsoPacketsSubmitted` is the URB shape a real audio
     * driver uses; the refusal rows are each expected 0 for a different reason
     * (see XHCI_EXTENSION); the underrun/overrun pair is **not** a fault and
     * only `IsoEventsUnattributed` rising says the driver and the controller
     * disagree about whether the pipe is running - `IsoDoorbellsSuppressed`
     * beside it is the *healthy* half of the same event, split out for exactly
     * that reason.
     */
    XHCI_DBG_VALUE_CHANGED("iso submits", ext->IsoSubmits);
    XHCI_DBG_VALUE_CHANGED("iso packets submitted", ext->IsoPacketsSubmitted);
    XHCI_DBG_VALUE_CHANGED("iso submits with a Frame ID",
                           ext->IsoSubmitsWithFrameId);
    XHCI_DBG_VALUE_CHANGED("iso cadence mismatches - assumed interval",
                           ext->IsoCadenceMismatches);
    XHCI_DBG_VALUE_CHANGED("iso cadence mismatches - derived interval",
                           ext->IsoCadenceMismatchesDerived);
    XHCI_DBG_VALUE_CHANGED("iso TRB error recoveries",
                           ext->IsoTrbErrorRecoveries);
    XHCI_DBG_VALUE_CHANGED("iso TRB errors unarmable",
                           ext->IsoTrbErrorsUnarmable);
    XHCI_DBG_VALUE_CHANGED("iso refusals - too large", ext->IsoRefusalsTooLarge);
    XHCI_DBG_VALUE_CHANGED("iso refusals - malformed",
                           ext->IsoRefusalsMalformed);
    XHCI_DBG_VALUE_CHANGED("iso submits on the wrong endpoint type",
                           ext->IsoSubmitsWrongType);
    XHCI_DBG_VALUE_CHANGED("iso ring underruns", ext->IsoRingUnderruns);
    XHCI_DBG_VALUE_CHANGED("iso ring overruns", ext->IsoRingOverruns);
    XHCI_DBG_VALUE_CHANGED("iso events unattributed",
                           ext->IsoEventsUnattributed);
    XHCI_DBG_VALUE_CHANGED("iso restart doorbells suppressed",
                           ext->IsoDoorbellsSuppressed);
    XHCI_DBG_VALUE_CHANGED("iso packets answered", ext->IsoPacketsAnsweredTotal);
    XHCI_DBG_VALUE_CHANGED("iso packet errors", ext->IsoPacketErrorsTotal);
    XHCI_DBG_VALUE_CHANGED("iso missed service errors",
                           ext->IsoMissedServiceTotal);
    XHCI_DBG_VALUE_CHANGED("iso groups awaiting a tail event",
                           ext->IsoGroupsAwaitingTailTotal);
    XHCI_DBG_VALUE_CHANGED("frame resync skew", ext->FrameResyncSkew);
    XHCI_DBG_VALUE_CHANGED("frame samples", ext->FrameSamples);
    XHCI_DBG_VALUE_CHANGED("frame samples stale at a claim",
                           ext->FrameSampleStale);

    /*
     * Task 9-A.2's configuration-descriptor snoop, and the two lines that carry
     * the verdict are `intervals derived` against `intervals assumed`: an
     * isochronous endpoint that reached the second is one whose cadence is still
     * this driver's guess. `intervals agreed` beside them is what decides
     * whether the guess was ever wrong on the hardware this project can reach -
     * equal to `derived` means every isochronous endpoint whose context was
     * built AND derived agreed with the assumed cadence. It says nothing about
     * a device that never bound or never opened the endpoint: the Sound
     * Blaster X4 declares bInterval 3 and 4 and never reached this counter
     * (run-13e.md, Finding Y). *(Until a later review this read "every device
     * asked for bInterval = 1", which the X4 had already disproved.)*
     *
     * `partial` is expected to be roughly equal to `committed` and is not a
     * fault: usbport reads the configuration header first to learn
     * `wTotalLength` and then re-reads the whole descriptor.
     */
    XHCI_DBG_VALUE_CHANGED("descriptor replies folded", ext->DescRepliesFolded);
    XHCI_DBG_VALUE_CHANGED("descriptor configs committed",
                           ext->DescConfigsCommitted);
    XHCI_DBG_VALUE_CHANGED("descriptor configs partial",
                           ext->DescConfigsPartial);
    XHCI_DBG_VALUE_CHANGED("descriptor configs malformed",
                           ext->DescConfigsMalformed);
    XHCI_DBG_VALUE_CHANGED("descriptor configs for an inactive configuration",
                           ext->DescConfigsInactive);
    XHCI_DBG_VALUE_CHANGED("descriptor configs superseded",
                           ext->DescConfigsSuperseded);
    XHCI_DBG_VALUE_CHANGED("descriptor interface selections",
                           ext->DescInterfacesSelected);
    XHCI_DBG_VALUE_CHANGED("descriptor interface selections dropped",
                           ext->DescInterfacesDropped);
    /* One literal, deliberately: `gen-offsets.ps1` matches a single quoted
     * string before the comma, and a label wrapped across two lines drops the
     * field out of the offset table while SIZEOF still agrees - the one way
     * that derivation loses a counter silently. */
    XHCI_DBG_VALUE_CHANGED("descriptor selections off address",
                           ext->DescSelectionsOffAddress);
    XHCI_DBG_VALUE_CHANGED("descriptor replies with no mapping",
                           ext->DescRepliesUnmapped);
    XHCI_DBG_VALUE_CHANGED("descriptor actions with no device",
                           ext->DescRepliesOrphaned);
    XHCI_DBG_VALUE_CHANGED("descriptor iso endpoint declarations",
                           ext->DescIsoEntries);
    XHCI_DBG_VALUE_CHANGED("descriptor iso declarations dropped",
                           ext->DescIsoEntriesDropped);
    XHCI_DBG_VALUE_CHANGED("descriptor iso bInterval out of range",
                           ext->DescIsoBadInterval);
    XHCI_DBG_VALUE_CHANGED("descriptor iso declarations that did not parse",
                           ext->DescIsoBadDescriptor);
    XHCI_DBG_VALUE_CHANGED("iso intervals derived", ext->DescIntervalsDerived);
    XHCI_DBG_VALUE_CHANGED("iso intervals assumed", ext->DescIntervalsAssumed);
    XHCI_DBG_VALUE_CHANGED("iso intervals declared but unresolved",
                           ext->DescIntervalsUnresolved);
    XHCI_DBG_VALUE_CHANGED("iso intervals resolved after the endpoint opened",
                           ext->DescIntervalsStaleAfterSelect);
    XHCI_DBG_VALUE_CHANGED("iso intervals derived and unchanged",
                           ext->DescIntervalsAgreed);
    XHCI_DBG_VALUE_CHANGED("iso intervals refused", ext->DescIntervalsRefused);

    escalate = XhciControllerHealthPoll(ext);
    if (escalate) {
        XhciRequestControllerReset(ext);
    }

    /*
     * Task 13-R.1's owner for the recovery step ResetController names. **After
     * the escalation rather than before it**, so a poll that has just asked for
     * the reset arms nothing: the request that matters is the one
     * ResetController raises when it publishes the failure, and that callback
     * has not run yet - usbport queues it as a DPC. Arming from a pre-existing
     * request here would be arming against a controller that has not been
     * declared failed.
     *
     * It runs on every poll rather than only on a failed one because it is the
     * *retry* vehicle as well as the first arming; the predicate that decides is
     * inside it, under the lock, where the request and the latch are.
     */
    xhciArmRecovery(ext);

    /*
     * The root hub's half of the same poll (Phase 5 task 6), and it is a second
     * function rather than a branch inside the first because the two answer
     * different questions with different escalations - one asks whether the
     * controller is still alive and ends in a reset request, the other asks
     * whether any port is signalling resume with nothing timing it and ends in a
     * timer.
     *
     * It also drains the deferred work, so a change latched while
     * XHCI_EXT_FLAG_RH_IRQ was closed gets its announcement at the next poll
     * once usbport re-opens the gate, without needing another port event to
     * carry it.
     */
    XhciRootHubPoll(ext);

    /*
     * The device layer's third of the poll, and it exists for the one failure
     * `UsbPortRequestAsyncCallback` cannot report: a command submitted with
     * nothing scheduled to time it. It also drains the deferred work, which is
     * what closes the window in XhciSlotDeferredWork's re-entry guard - a
     * command another CPU queued while this one was pumping waits at most until
     * this poll rather than for ever.
     */
    XhciSlotPoll(ext);

    /*
     * The log's counters, and this block is here for a reason that has nothing
     * to do with the traced build's printing: `scripts\vm-matrix\gen-offsets.
     * ps1` derives the counter offset table **from these print sites**, so a
     * counter with no site here cannot be read out of a live guest at all - and
     * cannot be decoded out of a snapshot `.BIN` either, which is the channel
     * that matters on Windows 98.
     *
     * On an ordinary machine every one of these is zero for the life of the
     * driver and each prints once.
     *
     * How to read them, because no single one of them answers anything:
     *
     *   read = 1, verbosity = 0 the ladder is at its default: the read channel
     *                           is SHUT and the ring is empty ON PURPOSE. A
     *                           dump cannot be taken at all in this state, and
     *                           that is the whole of "off" since `0.0.0.6`.
     *   read = 1, verbosity = 1 the channel is engaged and the ring is still
     *                           off ON PURPOSE. The counters are the payload,
     *                           and that is a real report, not a failure.
     *   read = 0                the start path never reached the registry read
     *                           at all. NOT the "nothing was configured" case -
     *                           no INF, a hand-copied driver and a packet with
     *                           no registry service all set `read` to 1, since
     *                           it is set before the service is tested. (This
     *                           row named those three until the post-Phase 13 review rounds.)
     *   status = 0 with the value 0
     *                           the driver read a zero somebody set.
     *   status != 0             usbport refused that read, and THIS is the row
     *                           that says the value never arrived. It has one
     *                           failure code and will not say which failure, so
     *                           it separates "absent or unreadable" from "was
     *                           explicitly zero" and nothing else does.
     *   verbosity refused = 1   the value was outside 0-4 and the default was
     *                           applied instead - which SHUTS the channel,
     *                           because the default is off. `verbosity read` is
     *                           what was actually in the registry.
     *   verbosity >= 2, appends = 0
     *                           recording is on and nothing has produced a
     *                           record yet.
     *   debugview = 1, flushes = 0 with refusals-irql > 0
     *                           there is no PASSIVE flush context on this
     *                           target. That is 11-V.7's stop rule, measured.
     *   debugview = 1, emits = 0, refusals-state > 0
     *                           the flush ran and had nothing to hand over.
     *   dropped > 0             what a reader has is a window on the run rather
     *                           than the run.
     */
    XHCI_DBG_VALUE_CHANGED("log switch read", ext->Log.SwitchRead);
    XHCI_DBG_VALUE_CHANGED("log verbosity switch status",
                           ext->Log.SwitchStatusVerbosity);
    XHCI_DBG_VALUE_CHANGED("log DebugView switch status",
                           ext->Log.SwitchStatusDebugView);
    XHCI_DBG_VALUE_CHANGED("log verbosity applied", ext->Log.Verbosity);
    XHCI_DBG_VALUE_CHANGED("log verbosity read", ext->Log.VerbosityRead);
    XHCI_DBG_VALUE_CHANGED("log verbosity refused", ext->Log.VerbosityRefused);
    XHCI_DBG_VALUE_CHANGED("log enabled", ext->Log.Enabled);
    XHCI_DBG_VALUE_CHANGED("log records appended", ext->Log.Appends);
    XHCI_DBG_VALUE_CHANGED("log records suppressed", ext->Log.Suppressed);
    XHCI_DBG_VALUE_CHANGED("log records truncated", ext->Log.Truncated);
    XHCI_DBG_VALUE_CHANGED("log bytes dropped by the wrap",
                           ext->Log.BytesDropped);
    XHCI_DBG_VALUE_CHANGED("log flushes", ext->Log.Flushes);
    XHCI_DBG_VALUE_CHANGED("log bytes handed to the sink", ext->Log.FlushBytes);
    XHCI_DBG_VALUE_CHANGED("log flushes refused - above PASSIVE_LEVEL",
                           ext->Log.FlushesRefusedIrql);
    XHCI_DBG_VALUE_CHANGED("log flushes refused - off or empty",
                           ext->Log.FlushesRefusedState);
    XHCI_DBG_VALUE_CHANGED("log flush failures", ext->Log.FlushFailures);
    XHCI_DBG_VALUE_CHANGED("log DebugView sink enabled",
                           ext->Log.DebugViewEnabled);
    XHCI_DBG_VALUE_CHANGED("log DebugView emits", ext->Log.DebugViewEmits);
    XHCI_DBG_VALUE_CHANGED("log DebugView bytes", ext->Log.DebugViewBytes);
    XHCI_DBG_VALUE_CHANGED("log error records over budget",
                           ext->LogErrorsOverBudget);

    /*
     * **There is deliberately no periodic print of RhSpeedsSeen here, and the
     * reason is the one property every XHCI_DBG_VALUE_CHANGED site in this
     * function shares: its "last value" static is per *expansion*, so it is
     * shared by every controller this driver image binds, while the value it
     * watches is per controller.** Two xHCI controllers whose sets differ - one
     * with a Full Speed device, one without - alternate at this site on
     * successive 500 ms calls, each value differing from the one before it, and
     * spend the whole 32-print budget in about eight seconds. Being monotone
     * per controller does not help: the *site* sees an alternating sequence.
     *
     * So the speed evidence is carried entirely by the first-decode line in
     * xhciRhRefresh, which is strictly more durable than anything here could be
     * - it is gated on a monotone set rather than on a print budget, so it
     * cannot go quiet at all, and it carries StartEpoch to say which
     * controller and which start it came from.
     */
}

/*
 * Recovery after a fatal error. Every shipping usbehci.sys leaves this slot
 * NULL, which confirms where the field is but not whether usbport null-checks
 * it, so this driver supplies it.
 *
 * **IRQL: DISPATCH_LEVEL, with one of usbport's spin locks held - measured, not
 * assumed**, and that measurement is what this callback's shape is. The RESET
 * invalidation queues a DPC (`KeInsertQueueDpc`), and that DPC clears the
 * request flag, takes `KfAcquireSpinLock` on `FdoExtension+0x288` (NUSB) /
 * `+0x28C` (SP4), calls this slot, and releases it - NUSB at `00011AC4` /
 * `00011B36` / `00011B42`, SP4 at `00011B80` / `00011BF2` / `00011BFE`.
 * Extracts in `tools/{nusb,win2ksp4}-extracted/usbport-invalidate-disasm.txt`.
 *
 * **So this callback cannot reinitialize the controller, and the first version
 * of it was wrong to try.** `XhciInitController` waits: `XhciWaitForBits` calls
 * `UsbPortWait`, which is `KeDelayExecutionThread`, and the port-power step
 * sleeps 20 ms unconditionally when a port really transitioned. Sleeping at
 * DISPATCH_LEVEL inside somebody else's spin lock is a hang on Win98 and a
 * Driver Verifier bugcheck on Win2000. It was written against an assumed
 * PASSIVE_LEVEL, and the assumption was never checked.
 *
 * **What usbport does after this returns is nothing**, which is the other half
 * of the measurement: the DPC releases the lock and drops a busy reference, and
 * the callback returns `VOID`, so no usbport state machine is waiting for a
 * controller that works. That is what makes it legitimate for the miniport to
 * decline the work rather than do it badly.
 *
 * So the body is what is safe here and nothing else: mask the interrupt enables
 * (two register writes, no wait) so a wedged controller cannot storm the shared
 * line, mark the controller failed so every later callback refuses rather than
 * touching hardware in an unknown state, and record the call. Recovery is a
 * stop/start - `XhciCommandInit` and `XhciInitController` both run at
 * PASSIVE_LEVEL from `StartController` - and there is no service a miniport can
 * call to request one.
 *
 * The engine that escalates here therefore does **not** wait to be rescued: it
 * stays out of service permanently, which is the honest terminal state for a
 * command ring that will not stop. See `XhciCommandEvent`'s divergence path.
 */
static VOID NTAPI xhciResetController(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("ResetController", miniPortExtension, 0, 0);

    if (!xhciExtensionValid(ext)) {
        return;
    }

    ext->ResetControllerCalls++;

    /*
     * The stable driver-image lock excludes every DISPATCH-level path that
     * could re-arm after this mask. Mask before publishing failure: until both
     * enables are down, a level-triggered INTx can still preempt this callback,
     * and the ISR must acknowledge it rather than decline an asserted line.
     */
    XhciControllerLockAcquire(&oldIrql);
    if (!ext->ControllerFailed) {
        if ((ext->Flags & XHCI_EXT_FLAG_INITIALIZED) != 0) {
            XhciMaskInterrupts(ext);
        }
        ext->ControllerFailed = 1;
        /*
         * Task 11-V.7. The Locked spelling because the lock is already held -
         * this is one of the transactions design doc 05 section 2's rule 2
         * names, and the acquiring helper would take a non-recursive spin lock
         * twice on one CPU. The note is what puts the failure *in order* with
         * everything before it; the counter block at flush only says it
         * happened.
         */
        XhciLogNoteLocked(ext, "ctrl.failed.here", ext->ResetControllerCalls);
        /*
         * **Task 13-R.1, and this one line is the repair's hinge.** Until
         * batch 13-R the callback ended here, having named a recovery step that
         * had no owner - and the batch 13-R census of both shipping
         * usbport.sys builds established that no owner was going to appear:
         * StopController/StartController are reached only from a PnP or power
         * transition something outside usbport initiates. So the miniport takes
         * the step itself, and the *request* is all that can be raised here,
         * because here is inside one of usbport's spin locks.
         *
         * Raised under the same lock hold that publishes the failure, so a
         * health poll on another CPU sees both or neither. Inside the
         * !ControllerFailed transition, so a second RESET arriving while a
         * recovery is already owed does not double the attempts.
         */
        ext->RecoveryRequested = 1;
    }
    XhciControllerLockRelease(oldIrql);

    XHCI_DBG_TEXT("ResetController: controller marked failed - a miniport "
                  "cannot reinitialize from a DPC holding usbport's lock; "
                  "the in-place recovery is armed from the health poll");
}

/*
 * **Task 13-R.1: the recovery the latch asked for, in the one context it is
 * legal in.**
 *
 * usbport hands a miniport exactly one deferred-work tool and this is it. The
 * DPC that runs this acquires **no** spin lock before calling
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 7, confirmed in the NUSB
 * binary at `0002785E`), which is the difference between here and
 * ResetController, and is why the whole sequence can run here and none of it
 * can run there.
 *
 * The context is an XHCI_COMMAND_TIMEOUT carrying XHCI_CMD_PHASE_RECOVERY. It is
 * checked the way every other uncancellable callback in this driver is: both
 * signatures and the **epoch**, under the controller lock, before anything is
 * read or written - a callback armed by a previous start can arrive against an
 * extension usbport has zeroed and restarted, and nothing in it may be trusted
 * until the epoch says otherwise.
 *
 * RecoveryArmed is cleared here whatever the outcome, because the timer cannot
 * be cancelled and exactly one callback exists per arming: leaving it set would
 * be a recovery that never retries, and clearing it earlier would let the health
 * poll arm a second one against the first.
 *
 * IRQL: DISPATCH_LEVEL, no usbport lock held.
 */
static VOID NTAPI xhciRecoveryCallback(PVOID miniPortExtension, PVOID context)
{
    PXHCI_EXTENSION ext;
    PXHCI_COMMAND_TIMEOUT armed;
    KIRQL oldIrql;
    ULONG go;

    ext = (PXHCI_EXTENSION)miniPortExtension;
    armed = (PXHCI_COMMAND_TIMEOUT)context;

    if (ext == NULL || armed == NULL) {
        return;
    }

    go = 0;
    XhciControllerLockAcquire(&oldIrql);
    if (ext->Signature != XHCI_EXTENSION_SIGNATURE ||
        ext->TrailingSignature != XHCI_EXTENSION_TRAILING ||
        armed->Epoch == 0 || armed->Epoch != ext->StartEpoch) {
        /* Not this driver's extension, or not this start's callback. Counting it
         * would be a write into somebody else's structure. */
        XhciControllerLockRelease(oldIrql);
        return;
    }

    ext->RecoveryArmed = 0;
    if ((ext->Flags & XHCI_EXT_FLAG_SUSPENDED) != 0) {
        /*
         * **SUSPENDED, which neither clause below covers**: the epoch answers a
         * *completed restart* and `ControllerFailed` answers a *healthy*
         * controller, while a suspend leaves the latch and the request exactly
         * as it found them. Windows 98's usbport idle-suspends this controller
         * within about half a second of the last transfer, and a wedge is
         * exactly a bus with nothing moving on it - so one landing in the
         * latch-to-callback window is an ordinary interleaving, not a corner.
         * Recovering here would run, power the ports of, and re-enable
         * interrupts on a controller usbport believes is asleep.
         *
         * **The request goes back, and that is what separates this clause from
         * the two below.** They decline because there is nothing left to do;
         * this one declines because it *cannot act now*, and the work is still
         * owed. The arming consumed `RecoveryRequested`, so without this the
         * latch would be left with no request and no armed callback - and the
         * resume does not always clear it: a resume that succeeds through the
         * **restore** path returns without reinitialising, so `ControllerFailed`
         * survives it and nothing would ever ask again. Putting the request back
         * lets the first poll after the resume arm a fresh attempt.
         */
        ext->RecoveryStaleCallbacks++;
        if (armed->Phase == XHCI_CMD_PHASE_RECOVERY && ext->ControllerFailed) {
            ext->RecoveryRequested = 1;
        }
    } else if (armed->Phase != XHCI_CMD_PHASE_RECOVERY ||
               !ext->ControllerFailed) {
        /* A stop/start, a resume, or an earlier recovery got here first and the
         * controller is already back in service. Doing the work anyway would
         * reset a controller that is carrying traffic. */
        ext->RecoveryStaleCallbacks++;
    } else {
        go = 1;
    }
    XhciControllerLockRelease(oldIrql);

    if (!go) {
        return;
    }

    if (!XhciRecoverController(ext)) {
        /*
         * Ask for another one, up to the bound. The request is what the health
         * poll re-arms from, so the retries are spaced by its period rather than
         * issued back to back - which matters, because each attempt drives the
         * controller through a halt and an HCRST.
         */
        XhciControllerLockAcquire(&oldIrql);
        if (ext->RecoveryFailuresConsecutive < XHCI_RECOVERY_MAX_ATTEMPTS) {
            ext->RecoveryRequested = 1;
        }
        XhciControllerLockRelease(oldIrql);
    }
}

/*
 * Arm one recovery callback, and **only** from the health poll.
 *
 * The arming does not happen in ResetController, where the request is raised,
 * and that placement is the point rather than an inconvenience:
 * UsbPortRequestAsyncCallback reaches usbport's own timer machinery, and
 * ResetController runs inside usbport's reset-DPC spin lock - nesting two of
 * usbport's locks with no stated order between them is the shape of a deadlock
 * rather than of a race. The health poll is where this driver already calls
 * usbport services (UsbPortInvalidateController from its own escalation, and the
 * root hub's timers from XhciRootHubPoll), so it is an established context
 * rather than a new one.
 *
 * The request survives the latch because CheckController does: usbport drives it
 * from its own 500 ms timer, which is why HealthPolls kept climbing past 8,485
 * on the wedged E460 while InterruptCount sat frozen at 182.
 *
 * IRQL: DISPATCH_LEVEL, under usbport's MiniportSpinLock.
 */
static VOID xhciArmRecovery(PXHCI_EXTENSION ext)
{
    XHCI_COMMAND_TIMEOUT armed;
    KIRQL oldIrql;
    ULONG arm;

    if (XhciRegPacket.UsbPortRequestAsyncCallback == NULL) {
        return;
    }

    arm = 0;
    armed.Epoch = 0;
    armed.Generation = 0;
    armed.Phase = XHCI_CMD_PHASE_RECOVERY;
    armed.Attempt = 0;

    XhciControllerLockAcquire(&oldIrql);
    /* SUSPENDED excludes the arming as well as the callback, and for the reason
     * given at xhciRecoveryCallback: usbport gates its own 500 ms timer on
     * HC_SUSPEND, so this is the narrow window rather than the ordinary case,
     * but arming inside it would deliver a recovery into a suspended
     * controller. The request is left standing, and which way out the resume
     * takes decides whether it is used: a **reinitialising** resume clears
     * `ControllerFailed` itself, so the predicate below stops matching and the
     * request goes *inert* - nothing clears it either, because the only
     * assignment that does sits inside this predicate, so it stays set and would
     * be consumed by the first poll after some later failure re-latches the
     * controller; a **restoring** one leaves the latch set, and that is what
     * lets a later poll arm the recovery. The predicate needs the latch STILL
     * SET - clearing it is not what enables the arming, it is what makes it
     * unnecessary. */
    if (ext->RecoveryRequested && !ext->RecoveryArmed &&
        ext->ControllerFailed &&
        (ext->Flags & XHCI_EXT_FLAG_SUSPENDED) == 0 &&
        ext->RecoveryFailuresConsecutive < XHCI_RECOVERY_MAX_ATTEMPTS) {
        ext->RecoveryRequested = 0;
        ext->RecoveryArmed = 1;
        /* Captured under the lock, with the decision, and not re-read after it
         * is dropped - the rule xhciArmCommandTimer follows, for the same
         * reason: a restart landing in that window would stamp the new start's
         * epoch onto a callback belonging to the old one. */
        armed.Epoch = ext->StartEpoch;
        armed.Attempt = ext->RecoveryAttempts;
        arm = 1;
    }
    XhciControllerLockRelease(oldIrql);

    if (!arm) {
        return;
    }

    /*
     * The lock is released first, because this service takes usbport's timer
     * lock and this driver's lock must never be held across one of usbport's.
     * The return value is discarded for the reason recorded at
     * xhciArmCommandTimer: the service answers 0 on success and 0 on its own
     * pool-allocation failure, so there is nothing to branch on. A failure there
     * costs one attempt - RecoveryArmed stays set and no callback arrives -
     * which is the residual every armed callback in this driver carries, and the
     * attempt cap bounds it either way.
     */
    (VOID)XhciRegPacket.UsbPortRequestAsyncCallback(
        ext, XHCI_RECOVERY_DELAY_MS, &armed, sizeof(armed),
        xhciRecoveryCallback);
}

/*
 * usbport uses this to stamp endpoint state changes and to answer URB frame
 * queries, and it waits for the number to *advance* before confirming some
 * transitions. A constant would therefore be worse than useless. Phase 4
 * returns MFINDEX >> 3 with software rollover extension; until then a counter
 * that only ever increases keeps every such wait bounded.
 *
 * What usbport needs is advancement with *time*, not with calls - so the real
 * MFINDEX >> 3 will answer the same value to several calls inside one 1 ms
 * frame, and that is correct rather than a regression. The host suite's
 * test_registered_frame_number pins this placeholder's exact values, on purpose:
 * it must be rewritten against a model clock in the same change that reads the
 * register, not kept green by incrementing on top of it.
 * docs/contributing/design/03-host-unit-tests.md names the three vectors that replace
 * it, and which one of them is the proof that this callback advances at all.
 *
 * The frame axis is **not** an unlocked counter, and this comment said it was
 * until the second-reader review. `XhciFrameNumber` takes the controller lock around the
 * MFINDEX read and the publish, and the health poll's `XhciFrameSample` writes
 * the same axis with that lock already held - two writers, one lock
 * (task 9-A.1; docs/contributing/design/05-locking-model.md section 2).
 *
 * IRQL: DISPATCH_LEVEL (called under MiniportSpinLock).
 */
static ULONG NTAPI xhciGet32BitFrameNumber(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;
    if (!xhciExtensionValid(ext)) {
        return 0;
    }
    return XhciFrameNumber(ext);
}

/*
 * "Interrupt me at the next Start-of-Frame", and it stays a counter on purpose
 * (task 9-A.3). The ReactOS mirror has no call site; both shipping builds have
 * two, and reading them is what settled this rather than another boot.
 *
 * Both sites are usbport's endpoint state-change machine.
 * `USBPORT_SetEndpointState` writes the requested state to the endpoint, stamps
 * it with `Get32BitFrameNumber()`, queues it on a state-change list and then
 * asks for this callback; the walker that drains that list re-queues the head
 * endpoint and asks again whenever the frame number has not yet passed the
 * stamp. So the request means "the frame number needs to move on".
 *
 * It is NOT one per endpoint state change: usbport has *four* SetEndpointState
 * call sites per build and only the queueing one above ends here. The other
 * three call this driver directly and queue nothing.
 *
 * **Nothing waits on the callback - but something waits on what it
 * accelerates.** The call takes one argument, its return value is never read,
 * and the instruction after it is the spin-lock release in all four sites; that
 * much is fire and forget. What an earlier draft of this comment missed is that
 * usbport's endpoint removal path runs an **uncapped 1 ms poll loop at PASSIVE**
 * whose exit test is StateLast == StateNext, which only the walker can make
 * true, and whose body calls nothing but UsbPortWait. So a PASSIVE thread does
 * block until the state-change list drains.
 *
 * The stub is legal because the walker has a second driver that owes nothing to
 * this callback - the self-rearming 500 ms timer DPC - so the wait ends in at
 * most one tick per state change instead of never. That makes the timer
 * load-bearing rather than a convenience.
 *
 * One qualification is open and is recorded rather than smoothed over: that
 * timer is armed only on a *successful* StartController and there is a stop path
 * that clears its rearm flag and cancels it. Whether that path can run while
 * endpoints still change state has not been established, so "cannot wedge
 * anything" rests on the timer being up whenever endpoints exist - argued from
 * the lifecycle, not proven from the binaries. See
 * docs/usb-xhci-info/usbport-miniport-abi.md section 4. That is why neither of the two implementations Phase 4
 * contemplated - an MFINDEX-wrap event or a short `UsbPortRequestAsyncCallback`
 * - is worth its cost: both would buy latency the timer already bounds, and the
 * async callback would additionally be an unreportable failure (Phase 4 task 7:
 * it returns 0 on allocation failure as well as success).
 *
 * **What the drain really depends on is `Get32BitFrameNumber` advancing**, not
 * this callback: the walker's gate is a strictly-greater comparison against the
 * stamp, and an endpoint that fails it goes back on the *head* of the list and
 * aborts the whole pass - head-of-line blocking every other endpoint's state
 * change. `XhciFrameNumber` never repeats a value (its stall path counts calls),
 * which is what makes that unreachable here.
 *
 * The counter, not the trace, is the channel: XHCI_DBG_CB is budgeted per site,
 * and batch 9-V read the resulting handful of lines as "this callback has never
 * fired before" when in fact it is in every debug log this repository holds
 * from batch 6-V onward, on both targets.
 *
 * Full derivation, with the call sites and the timer, in
 * docs/usb-xhci-info/usbport-miniport-abi.md section 4.
 *
 * IRQL: DISPATCH_LEVEL, holding usbport's MiniportSpinLock and nothing else
 * (confirmed in both builds - the endpoint's own locks are released first).
 */
static VOID NTAPI xhciInterruptNextSOF(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;

    XHCI_DBG_CB("InterruptNextSOF", miniPortExtension, 0, 0);

    ext = (PXHCI_EXTENSION)miniPortExtension;
    if (!xhciExtensionValid(ext)) {
        return;
    }
    ext->InterruptNextSofRequests++;
}

/* Polling-mode sweep. Present because MiniPortFlags carries POLLING, matching
 * the shipping miniports. IRQL: DISPATCH_LEVEL (assumed). */
static VOID NTAPI xhciPollController(PVOID miniPortExtension)
{
    XHCI_DBG_CB("PollController", miniPortExtension, 0, 0);
}

/* Companion-controller handback: not applicable to xHCI, and left
 * unimplemented by the shipping EHCI miniport too. IRQL: any. */
static VOID NTAPI xhciTakePortControl(PVOID miniPortExtension)
{
    XHCI_DBG_CB("TakePortControl", miniPortExtension, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Endpoint callbacks                                                  */
/* ------------------------------------------------------------------ */

/*
 * Phase 6 batch B is where this family stopped being stubs. The bodies are in
 * src/xhci_slot.c, next to the device records they read and the lock that covers
 * them; what is here is the wrapper each one needs and nothing else - the same
 * split the root-hub family below uses.
 *
 * Two of them stay stubs on purpose, and it is a measured decision rather than
 * unfinished work: **neither shipping usbport.sys build ever calls
 * `CloseEndpoint` or `GetEndpointState`** (batch 6-0, established across both
 * whole images). There is no implementation to land against either target, so
 * they keep the shape a wrong packet layout would expose them through - a NULL
 * slot bugchecks with no evidence where a refusing stub produces a log line -
 * and they stay defensive against a third build that does call them.
 */

/*
 * usbport adopts MaxTransferSize as the endpoint's cap for bulk and interrupt,
 * so a zero would divide the transfer splitter by zero. The body decides both
 * fields; the reason HeaderBufferSize is 0 - this driver asks for no
 * per-endpoint common buffer at all - is spelled out there, because it is a
 * consequence of the reopen sequence rather than of this callback.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 */
static VOID NTAPI xhciQueryEndpointRequirements(
    PVOID miniPortExtension,
    PUSBPORT_ENDPOINT_PROPERTIES properties,
    PUSBPORT_ENDPOINT_REQUIREMENTS requirements)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    if (requirements == NULL) {
        return;
    }
    if (!xhciExtensionValid(ext)) {
        /*
         * An unrecognisable extension still has to leave a usable pair behind:
         * usbport does not check whether this callback filled the structure, and
         * a zero MaxTransferSize reaches the splitter's division before the open
         * that would have refused. The open then refuses, which is the failure
         * this driver wants.
         */
        requirements->HeaderBufferSize = 0;
        requirements->MaxTransferSize = XHCI_PAGE_SIZE;
        return;
    }
    XhciProbeEndpoint(ext, XHCI_PROBE_EVENT_QUERY, properties, NULL, 0);
    XhciSlotQueryEndpointRequirements(ext, properties, requirements);
}

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock. */
static MPSTATUS NTAPI xhciOpenEndpoint(PVOID miniPortExtension,
                                       PUSBPORT_ENDPOINT_PROPERTIES properties,
                                       PVOID endpointExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("OpenEndpoint", miniPortExtension, properties,
                endpointExtension);

    if (!xhciExtensionValid(ext) || endpointExtension == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    XhciProbeEndpoint(ext, XHCI_PROBE_EVENT_OPEN, properties, NULL, 0);
    return XhciSlotOpenEndpoint(ext, properties,
                                (PXHCI_ENDPOINT)endpointExtension);
}

/*
 * **The enumeration path does not call this, but the device-restore path does**,
 * and that is not the same finding as the two dead callbacks.
 * `USBPORT_ReopenPipe` is exercised on every enumeration and deliberately
 * bypasses this slot, driving `SetEndpointState(REMOVE)` ->
 * `QueryEndpointRequirements` -> `OpenEndpoint` instead (batch 6-0). But
 * `USBPORT_RestoreDevice` calls this slot directly for an endpoint whose
 * `ENDPOINT_FLAG_NUKE` is clear (SP4 `0x2770E` through interface `+0x40`, NUSB
 * `0x27086` through `+0x3C`; both are packet `0x2C`), following it with
 * `SetEndpointDataToggle` and `SetEndpointStatus(RUN)`. An earlier draft of this
 * comment said "neither shipping build calls this", which overstated batch 6-0's
 * result - `docs/usb-xhci-info/usbport-miniport-abi.md` had it right.
 *
 * It forwards to the open, which is what a restore means: usbport is
 * re-establishing an endpoint it still has properties for, and the open's own
 * "the DCI is already open" branch is exactly the right handling.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 */
static MPSTATUS NTAPI xhciReopenEndpoint(PVOID miniPortExtension,
                                         PUSBPORT_ENDPOINT_PROPERTIES properties,
                                         PVOID endpointExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("ReopenEndpoint", miniPortExtension, properties,
                endpointExtension);

    if (!xhciExtensionValid(ext) || endpointExtension == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    XhciProbeEndpoint(ext, XHCI_PROBE_EVENT_REOPEN, properties, NULL, 0);
    return XhciSlotOpenEndpoint(ext, properties,
                                (PXHCI_ENDPOINT)endpointExtension);
}

/*
 * The probe call is the point of this stub now. Batch 6-0 established across
 * both whole images that no shipping build calls this, and the driver stubs it
 * on that strength; `ProbeEpEvents[XHCI_PROBE_EVENT_CLOSE]` is what a
 * *release* build would show if that static read were wrong on a target,
 * which no trace line can (task 6-V.1).
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 */
static VOID NTAPI xhciCloseEndpoint(PVOID miniPortExtension,
                                    PVOID endpointExtension,
                                    BOOLEAN isDoDisablePeriodic)
{
    XHCI_DBG_CB("CloseEndpoint", miniPortExtension, endpointExtension,
                isDoDisablePeriodic);
    XhciProbeEndpoint((PXHCI_EXTENSION)miniPortExtension,
                      XHCI_PROBE_EVENT_CLOSE, NULL,
                      (const XHCI_ENDPOINT *)endpointExtension,
                      isDoDisablePeriodic);
}

/*
 * **Never called by either shipping build** (batch 6-0, both whole images), so
 * this is a permanent stub rather than one waiting for an implementation, and
 * ReactOS's 1000 x 1 ms poll around it has no counterpart in either binary
 * either. What the real post-open wait polls is usbport's *own* software
 * endpoint state, which advances on `Get32BitFrameNumber` - which is why that
 * callback became a Phase 6 deliverable.
 *
 * Answering ACTIVE keeps the stub safe against a build that does call it:
 * reporting anything else would hang an enumerating thread in a loop with no
 * cap and no error value in its contract.
 *
 * IRQL: DISPATCH_LEVEL.
 */
static ULONG NTAPI xhciGetEndpointState(PVOID miniPortExtension,
                                        PVOID endpointExtension)
{
    XHCI_DBG_CB("GetEndpointState", miniPortExtension, endpointExtension, 0);
    XhciProbeEndpoint((PXHCI_EXTENSION)miniPortExtension,
                      XHCI_PROBE_EVENT_GET_STATE, NULL,
                      (const XHCI_ENDPOINT *)endpointExtension, 0);
    return USBPORT_ENDPOINT_ACTIVE;
}

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock. */
static VOID NTAPI xhciSetEndpointState(PVOID miniPortExtension,
                                       PVOID endpointExtension,
                                       ULONG state)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("SetEndpointState", miniPortExtension, endpointExtension, state);

    if (!xhciExtensionValid(ext) || endpointExtension == NULL) {
        return;
    }
    XhciProbeEndpoint(ext, XHCI_PROBE_EVENT_SET_STATE, NULL,
                      (const XHCI_ENDPOINT *)endpointExtension, state);
    XhciSlotSetEndpointState(ext, (PXHCI_ENDPOINT)endpointExtension, state);
}

/* IRQL: DISPATCH_LEVEL. */
static VOID NTAPI xhciPollEndpoint(PVOID miniPortExtension,
                                   PVOID endpointExtension)
{
    XHCI_DBG_CB("PollEndpoint", miniPortExtension, endpointExtension, 0);
}

/*
 * The three status callbacks (task 7a-B.3). **All three are called by both
 * shipping builds** - one `GetEndpointStatus` site each, two each for the other
 * two - so unlike `CloseEndpoint` and `GetEndpointState` these are contract
 * rather than defensive stubs. The bodies are in src/xhci_slot.c next to the
 * endpoint state they read.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 */
static VOID NTAPI xhciSetEndpointDataToggle(PVOID miniPortExtension,
                                            PVOID endpointExtension,
                                            ULONG toggle)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("SetEndpointDataToggle", miniPortExtension, endpointExtension,
                toggle);

    if (!xhciExtensionValid(ext) || endpointExtension == NULL) {
        return;
    }
    XhciSlotSetEndpointDataToggle(ext, (PXHCI_ENDPOINT)endpointExtension,
                                  toggle);
}

/*
 * An unrecognisable extension answers RUN, which is the safe direction: usbport
 * caches this at `Endpoint+0x2C` and a HALT nothing will ever clear is a pipe
 * that stays dead. IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 */
static ULONG NTAPI xhciGetEndpointStatus(PVOID miniPortExtension,
                                         PVOID endpointExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("GetEndpointStatus", miniPortExtension, endpointExtension, 0);

    if (!xhciExtensionValid(ext) || endpointExtension == NULL) {
        return USBPORT_ENDPOINT_RUN;
    }
    return XhciSlotGetEndpointStatus(ext, (PXHCI_ENDPOINT)endpointExtension);
}

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock. */
static VOID NTAPI xhciSetEndpointStatus(PVOID miniPortExtension,
                                        PVOID endpointExtension,
                                        ULONG status)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("SetEndpointStatus", miniPortExtension, endpointExtension,
                status);

    if (!xhciExtensionValid(ext) || endpointExtension == NULL) {
        return;
    }
    XhciSlotSetEndpointStatus(ext, (PXHCI_ENDPOINT)endpointExtension, status);
}

/* Periodic-schedule rebalance from usbport's USB2 budgeter. Left as a no-op by
 * the shipping EHCI miniport too. IRQL: DISPATCH_LEVEL. */
static VOID NTAPI xhciRebalanceEndpoint(PVOID miniPortExtension,
                                        PUSBPORT_ENDPOINT_PROPERTIES properties,
                                        PVOID endpointExtension)
{
    XHCI_DBG_CB("RebalanceEndpoint", miniPortExtension, properties,
                endpointExtension);
}

/* ------------------------------------------------------------------ */
/* Transfer callbacks                                                  */
/* ------------------------------------------------------------------ */

/*
 * A nonzero return leaves the transfer **queued for retry** rather than failing
 * it, which is the behaviour a device whose command chain is still running
 * needs and an infinite loop for one that has failed - so the body never uses it
 * as a rejection. Failing a transfer means accepting it and completing it with
 * an error; see XhciSlotSubmitTransfer.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 */
static MPSTATUS NTAPI xhciSubmitTransfer(PVOID miniPortExtension,
                                         PVOID endpointExtension,
                                         PUSBPORT_TRANSFER_PARAMETERS parameters,
                                         PVOID transferExtension,
                                         PUSBPORT_SCATTER_GATHER_LIST sgList)
{
    PXHCI_EXTENSION ext;
    MPSTATUS status;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("SubmitTransfer", miniPortExtension, endpointExtension,
                parameters);

    if (!xhciExtensionValid(ext) || endpointExtension == NULL ||
        transferExtension == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    /*
     * Task 6-V.1, and it sits here rather than inside the device layer on
     * purpose: the probe's subject is the *surface* usbport calls, not the
     * decisions this driver makes about what arrives on it, and every path
     * below returns without reaching a common point where all of them could be
     * observed.
     */
    XhciProbeTransfer(ext, parameters, sgList);
    /*
     * **Nothing below may deliver a completion to usbport**, because usbport
     * writes to the transfer record after this callback returns success (see
     * XHCI_EXTENSION.SubmitDepth for the call site that does it and the fault
     * it produced on 2a). This bracket is what tells XhciSlotDeferredWork to
     * hold the completions back; it is here, in the wrapper, because this is
     * the single place usbport enters the submit path. A depth rather than a
     * flag for the SMP shape: two CPUs can be inside SubmitTransfer for two
     * endpoints at once, and a flag one of them clears on exit would drop the
     * other's hold. (An earlier reason - "usbport answers a completion with a
     * fresh SubmitTransfer, so this nests" - was refuted by the batch 7a-V
     * binary read: UsbPortCompleteTransfer re-enters no miniport slot.)
     */
    XhciSlotEnterSubmit(ext);
    status = XhciSlotSubmitTransfer(ext, (PXHCI_ENDPOINT)endpointExtension,
                                    parameters,
                                    (PXHCI_TRANSFER)transferExtension, sgList,
                                    NULL);
    XhciSlotLeaveSubmit(ext);
    return status;
}

/*
 * Task 9-A.1. The isochronous half of the same dispatch: usbport picks between
 * this slot and the one above on bit 5 of its own private transfer flags, five
 * arguments either way, and only the fifth differs - a
 * `USBPORT_ISO_TRANSFER` block instead of the scatter/gather list
 * (docs/usb-xhci-info/usbport-miniport-abi.md, "Isochronous transfers").
 *
 * **The fifth argument is never NULL at the real call site.** usbport's
 * allocator sets the pointer and the dispatch's flag bit in the same basic block
 * from the same condition, so the two cannot disagree (task 9-0.1) - and it is
 * checked anyway, because that is a statement about two disassembled builds
 * rather than a term of the ABI.
 *
 * The submit-depth bracket is the same one and for the same reason: usbport
 * writes to the transfer record after this callback returns success, so no
 * completion may be delivered from inside it (`XHCI_EXTENSION.SubmitDepth`, and
 * the batch 7a-V fault on 2a).
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 */
static MPSTATUS NTAPI xhciSubmitIsoTransfer(PVOID miniPortExtension,
                                            PVOID endpointExtension,
                                            PUSBPORT_TRANSFER_PARAMETERS parameters,
                                            PVOID transferExtension,
                                            PVOID isoParameters)
{
    PXHCI_EXTENSION ext;
    MPSTATUS status;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("SubmitIsoTransfer", miniPortExtension, endpointExtension,
                parameters);

    if (!xhciExtensionValid(ext) || endpointExtension == NULL ||
        transferExtension == NULL || isoParameters == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    /*
     * **The task 6-V.1 probe is not called here**, and that is a decision rather
     * than an omission. Its subject is the scatter/gather contract - element
     * ordering, offsets, the high address DWORD - and an isochronous request
     * carries no list at all: its fragments arrive inside the parameter block,
     * already split. Feeding it a NULL list to keep the call sites symmetric
     * would move its counters for a contract it is not observing.
     */
    XhciSlotEnterSubmit(ext);
    status = XhciSlotSubmitTransfer(ext, (PXHCI_ENDPOINT)endpointExtension,
                                    parameters,
                                    (PXHCI_TRANSFER)transferExtension, NULL,
                                    (const USBPORT_ISO_TRANSFER *)isoParameters);
    XhciSlotLeaveSubmit(ext);
    return status;
}

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock. */
static VOID NTAPI xhciAbortTransfer(PVOID miniPortExtension,
                                    PVOID endpointExtension,
                                    PVOID transferExtension,
                                    PULONG completedLength)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("AbortTransfer", miniPortExtension, endpointExtension,
                transferExtension);

    if (completedLength != NULL) {
        *completedLength = 0;
    }
    if (!xhciExtensionValid(ext) || endpointExtension == NULL ||
        transferExtension == NULL) {
        return;
    }
    XhciProbeEndpoint(ext, XHCI_PROBE_EVENT_ABORT, NULL,
                      (const XHCI_ENDPOINT *)endpointExtension, 0);
    /*
     * The minimum that keeps the completion path honest, not task 7a-B.2's
     * cancellation machine: detach the transfer so nothing can complete it a
     * second time after usbport has reclaimed its record, and report what it
     * moved. The ring is left alone - see XhciSlotAbortTransfer.
     */
    XhciSlotAbortTransfer(ext, (PXHCI_ENDPOINT)endpointExtension,
                          (PXHCI_TRANSFER)transferExtension, completedLength);
}

/* ------------------------------------------------------------------ */
/* Root-hub callbacks                                                  */
/* ------------------------------------------------------------------ */

/*
 * The root-hub callback family (roadmap Phase 5 task 1). Every body is in
 * src/xhci_rh.c, next to the port shadow it reads and the lock it takes; what
 * is here is the wrapper each one needs and nothing else.
 *
 * Two wrapper-level rules, both from the contract rather than from taste:
 *
 *   An unrecognisable extension is answered the way *that callback's* caller
 *   can survive. The two status queries report zeros and succeed, because the
 *   status-change scan treats any nonzero return as a hard error and abandons
 *   the whole scan; the feature operations refuse with MP_STATUS_NOT_SUPPORTED,
 *   never MP_STATUS_FAILURE, whose value usbport maps to "no changes" and which
 *   would leave an endpoint-0 request queued forever.
 *
 *   The twelve feature slots stay twelve separate functions even though ten of
 *   them now forward into two shared bodies. With twelve identical signatures
 *   in a row, a shifted packet offset is invisible unless each slot can name
 *   itself in the trace - which is the same reason they were separate in the
 *   Phase 3 spike, and it has not stopped being true now that they do work.
 *
 * IRQL: DISPATCH_LEVEL throughout.
 */
static VOID NTAPI xhciRhGetRootHubData(PVOID miniPortExtension, PVOID data)
{
    PXHCI_EXTENSION ext;
    PUSBPORT_ROOT_HUB_DATA hubData;

    ext = (PXHCI_EXTENSION)miniPortExtension;
    hubData = (PUSBPORT_ROOT_HUB_DATA)data;

    XHCI_DBG_CB("RH_GetRootHubData", miniPortExtension, data, 0);

    if (hubData == NULL) {
        return;
    }
    if (!xhciExtensionValid(ext)) {
        /*
         * The one refusal in this family that cannot simply return. usbport is
         * building the root hub descriptor out of this structure and does not
         * check whether the callback filled it, so leaving it untouched hands
         * the mask arithmetic whatever was on the stack - and a zero port count
         * is the value that asks for about 1 GB of nonpaged pool. One
         * disconnected port is the answer that keeps the descriptor buildable.
         */
        hubData->NumberOfPorts = 1;
        hubData->HubCharacteristics = 0;
        hubData->Padded1 = 0;
        hubData->PowerOnToPowerGood = XHCI_RH_POWER_ON_TO_POWER_GOOD;
        hubData->HubControlCurrent = 0;
        return;
    }

    XhciRhGetRootHubData(ext, hubData);
}

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock. */
static MPSTATUS NTAPI xhciRhGetStatus(PVOID miniPortExtension, PUSHORT status)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_GetStatus", miniPortExtension, status, 0);

    if (status == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    if (!xhciExtensionValid(ext)) {
        *status = 0;
        return MP_STATUS_SUCCESS;
    }
    return XhciRhGetStatus(ext, status);
}

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock. */
static MPSTATUS NTAPI xhciRhGetPortStatus(PVOID miniPortExtension,
                                          USHORT port,
                                          PUSBPORT_PORT_STATUS_AND_CHANGE status)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_GetPortStatus", miniPortExtension, port, status);

    if (status == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    if (!xhciExtensionValid(ext)) {
        status->PortStatus = 0;
        status->PortChange = 0;
        return MP_STATUS_SUCCESS;
    }
    return XhciRhGetPortStatus(ext, port, status);
}

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock. */
static MPSTATUS NTAPI xhciRhGetHubStatus(PVOID miniPortExtension,
                                         PUSBPORT_HUB_STATUS_AND_CHANGE status)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_GetHubStatus", miniPortExtension, status, 0);

    if (status == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    if (!xhciExtensionValid(ext)) {
        status->HubStatus = 0;
        status->HubChange = 0;
        return MP_STATUS_SUCCESS;
    }
    return XhciRhGetHubStatus(ext, status);
}

/*
 * SET_FEATURE(PORT_RESET) - the one root-hub operation the whole enumeration
 * path waits on, and the reason it took its own task: PORTSC.PR takes effect
 * asynchronously, this callback runs at DISPATCH_LEVEL and may not wait, and the
 * completion arrives as a PRC in a Port Status Change Event or not at all. The
 * body is XhciRhSetFeaturePortReset in src/xhci_rh.c, which writes PR, arms a
 * per-port generation, gets an uncancellable timer onto it, and returns.
 *
 * IRQL: DISPATCH_LEVEL.
 */
static MPSTATUS NTAPI xhciRhSetFeaturePortReset(PVOID miniPortExtension,
                                                USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_SetFeaturePortReset", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhSetFeaturePortReset(ext, port);
}

/* IRQL: DISPATCH_LEVEL. */
static MPSTATUS NTAPI xhciRhSetFeaturePortPower(PVOID miniPortExtension,
                                                USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_SetFeaturePortPower", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhSetFeaturePortPower(ext, port);
}

/* IRQL: DISPATCH_LEVEL. */
static MPSTATUS NTAPI xhciRhSetFeaturePortEnable(PVOID miniPortExtension,
                                                 USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_SetFeaturePortEnable", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhSetFeaturePortEnable(ext, port);
}

/* IRQL: DISPATCH_LEVEL. */
static MPSTATUS NTAPI xhciRhSetFeaturePortSuspend(PVOID miniPortExtension,
                                                  USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_SetFeaturePortSuspend", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhSetFeaturePortSuspend(ext, port);
}

/* IRQL: DISPATCH_LEVEL. */
static MPSTATUS NTAPI xhciRhClearFeaturePortEnable(PVOID miniPortExtension,
                                                   USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_ClearFeaturePortEnable", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhClearFeaturePortEnable(ext, port);
}

/* IRQL: DISPATCH_LEVEL. */
static MPSTATUS NTAPI xhciRhClearFeaturePortPower(PVOID miniPortExtension,
                                                  USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_ClearFeaturePortPower", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhClearFeaturePortPower(ext, port);
}

/* IRQL: DISPATCH_LEVEL. */
static MPSTATUS NTAPI xhciRhClearFeaturePortSuspend(PVOID miniPortExtension,
                                                    USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_ClearFeaturePortSuspend", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhClearFeaturePortSuspend(ext, port);
}

/* IRQL: DISPATCH_LEVEL. */
static MPSTATUS NTAPI xhciRhClearFeaturePortEnableChange(PVOID miniPortExtension,
                                                         USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_ClearFeaturePortEnableChange", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhClearFeaturePortChange(ext, port, XHCI_HUB_C_PORT_ENABLE);
}

/* IRQL: DISPATCH_LEVEL. */
static MPSTATUS NTAPI xhciRhClearFeaturePortConnectChange(PVOID miniPortExtension,
                                                          USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_ClearFeaturePortConnectChange", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhClearFeaturePortChange(ext, port, XHCI_HUB_C_PORT_CONNECTION);
}

/* IRQL: DISPATCH_LEVEL. */
static MPSTATUS NTAPI xhciRhClearFeaturePortResetChange(PVOID miniPortExtension,
                                                        USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_ClearFeaturePortResetChange", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhClearFeaturePortChange(ext, port, XHCI_HUB_C_PORT_RESET);
}

/* IRQL: DISPATCH_LEVEL. */
static MPSTATUS NTAPI xhciRhClearFeaturePortSuspendChange(PVOID miniPortExtension,
                                                          USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_ClearFeaturePortSuspendChange", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhClearFeaturePortChange(ext, port, XHCI_HUB_C_PORT_SUSPEND);
}

/*
 * The one feature callback Win2000 reaches with `Port = 0`, from its
 * hub-directed path (`0x207FA`; NUSB has no such site). The shared body
 * tolerates zero rather than indexing with it - see XhciRhClearFeaturePortChange.
 * IRQL: DISPATCH_LEVEL.
 */
static MPSTATUS NTAPI xhciRhClearFeaturePortOvercurrentChange(
    PVOID miniPortExtension, USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_ClearFeaturePortOvercurrentChange", miniPortExtension, port,
                0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhClearFeaturePortChange(ext, port,
                                        XHCI_HUB_C_PORT_OVER_CURRENT);
}

/* IRQL: DISPATCH_LEVEL. */
static VOID NTAPI xhciRhDisableIrq(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_DisableIrq", miniPortExtension, 0, 0);

    if (xhciExtensionValid(ext)) {
        XhciRhDisableIrq(ext);
    }
}

/* IRQL: DISPATCH_LEVEL. */
static VOID NTAPI xhciRhEnableIrq(PVOID miniPortExtension)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_EnableIrq", miniPortExtension, 0, 0);

    if (xhciExtensionValid(ext)) {
        XhciRhEnableIrq(ext);
    }
}

/*
 * Called once per root port at root-hub start, and only when the interface
 * version is >= 200 - which makes it the one callback whose arrival proves the
 * long 316-byte packet was accepted, not merely that registration returned
 * success. The body is XhciRhChirpRootPort; there is no xHCI equivalent of the
 * EHCI handshake, so it succeeds without bus action.
 *
 * IRQL: DISPATCH_LEVEL.
 */
static MPSTATUS NTAPI xhciRhChirpRootPort(PVOID miniPortExtension, USHORT port)
{
    PXHCI_EXTENSION ext;

    ext = (PXHCI_EXTENSION)miniPortExtension;

    XHCI_DBG_CB("RH_ChirpRootPort", miniPortExtension, port, 0);

    if (!xhciExtensionValid(ext)) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    return XhciRhChirpRootPort(ext, port);
}

/* ------------------------------------------------------------------ */
/* Debug single-packet path                                            */
/* ------------------------------------------------------------------ */

/*
 * usbport's USBUSER debug transfer path. Refused without touching the output
 * parameters: their contract is not pinned by anything this project has read,
 * and writing an invented status into a caller's buffer is a worse failure than
 * declining the operation.
 *
 * IRQL: PASSIVE_LEVEL (assumed).
 */
static MPSTATUS NTAPI xhciStartSendOnePacket(PVOID miniPortExtension,
                                             PVOID controlPacket,
                                             PVOID buffer,
                                             PULONG bufferLength,
                                             PVOID bufferVA,
                                             PVOID bufferPA,
                                             ULONG transferType,
                                             XHCI_USBD_STATUS *usbdStatus)
{
    XHCI_DBG_CB("StartSendOnePacket", miniPortExtension, controlPacket,
                transferType);
    return MP_STATUS_NOT_SUPPORTED;
}

/* IRQL: PASSIVE_LEVEL (assumed). */
static MPSTATUS NTAPI xhciEndSendOnePacket(PVOID miniPortExtension,
                                           PVOID controlPacket,
                                           PVOID buffer,
                                           PULONG bufferLength,
                                           PVOID bufferVA,
                                           PVOID bufferPA,
                                           ULONG transferType,
                                           XHCI_USBD_STATUS *usbdStatus)
{
    XHCI_DBG_CB("EndSendOnePacket", miniPortExtension, controlPacket,
                transferType);
    return MP_STATUS_NOT_SUPPORTED;
}

/*
 * Copy bytes, one at a time, because RtlCopyMemory/memcpy resolve to a
 * compiler intrinsic or an ntoskrnl import depending on build flags and this
 * driver decides its import list deliberately rather than by build-flag
 * accident - the same reason xhciZeroPacket exists.
 *
 * A byte loop over a full window is up to 65,488 iterations with the
 * controller lock held, so on the order of 100 us at DISPATCH. That is
 * deliberate and it is the right trade here: the instrument runs on a wedged,
 * idle machine, it is not in any hot path, and a word-copy fast path would buy
 * microseconds in exchange for an alignment argument that has to be right. The
 * ISR does not take this lock (see the import allowlist's spin-lock block), so
 * what the hold delays is the command engine and the DPC, not interrupts.
 *
 * IRQL: any.
 */
static VOID xhciSnapshotCopy(UCHAR *dst, const UCHAR *src, ULONG bytes)
{
    ULONG i;

    for (i = 0; i < bytes; i++) {
        dst[i] = src[i];
    }
}

/*
 * What kind of binary produced a window.
 *
 * This says checked-vs-free and diagnostic-or-not; it cannot say WHICH checked
 * build, because `DBG` is set for both `debug` and `qemu`. `Flavour` in the
 * header is what answers that, out of XHCI_FLAVOUR_CODE.
 *
 * IRQL: any.
 */
static ULONG xhciSnapshotBuildFlags(VOID)
{
    ULONG flags;

    flags = 0;
#if DBG
    flags |= XHCI_SNAPSHOT_B_DEBUG;
#endif
#ifdef XHCI_DIAGNOSTIC_BUILD
    flags |= XHCI_SNAPSHOT_B_DIAGNOSTIC;
#endif
    return flags;
}

/*
 * usbport's vendor escape, and **this driver's only way to get evidence off a
 * Windows 98 machine**. In every flavour since task 13-L.2, and answering
 * nothing until `XhciLogVerbosity` is raised off 0. See the block in src/xhci.h
 * for the whole contract, the wire format, and why a route through somebody
 * else's driver object is the only one Option A leaves open.
 *
 * IRQL: PASSIVE_LEVEL - established by disassembly rather than assumed, unlike
 * the two SendOnePacket stubs above: usbport releases the device lock it took
 * to test the removed flag before it dispatches the USBUSER request, on both
 * exits (NUSB 0001138F acquire, 000113A0 / 000113F6 release).
 */
static MPSTATUS NTAPI xhciPassThru(PVOID miniPortExtension,
                                   PVOID serviceGuid,
                                   ULONG parameterLength,
                                   PVOID parameters)
{
    PXHCI_EXTENSION ext;
    const ULONG *guid;
    const ULONG *request;
    XHCI_SNAPSHOT_HEADER *header;
    UCHAR *payload;
    ULONG *portscOut;
    ULONG requestSignature;
    ULONG requestRegion;
    ULONG requestOffset;
    ULONG capacity;
    ULONG regionBytes;
    ULONG available;
    ULONG copied;
    ULONG ports;
    ULONG first;
    ULONG slots;
    ULONG i;
    KIRQL oldIrql;

    XHCI_DBG_CB("PassThru", miniPortExtension, serviceGuid, parameterLength);

    /*
     * **Return exactly MP_STATUS_NOT_SUPPORTED for anything that is not ours,
     * and never any other nonzero value.** usbport's own PassThru call site -
     * its root-hub port-status probe - retries through RH_GetPortStatus only
     * when the return is exactly 6 (NUSB 00028603). A miniport answering
     * MP_STATUS_FAILURE there would suppress that fallback silently and leave
     * usbport reporting a zeroed port status. That site is reachable only
     * through a test-mode USBUSER request so it cannot fire in ordinary use,
     * but the rule costs nothing and the failure would be invisible.
     */
    if (serviceGuid == NULL || parameters == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    guid = (const ULONG *)serviceGuid;
    if (guid[0] != XHCI_SNAPSHOT_GUID_0 || guid[1] != XHCI_SNAPSHOT_GUID_1 ||
        guid[2] != XHCI_SNAPSHOT_GUID_2 || guid[3] != XHCI_SNAPSHOT_GUID_3) {
        return MP_STATUS_NOT_SUPPORTED;
    }

    /*
     * **The door, and it is shut by default** (task 13-L.2, amended
     * the merge). `XhciLogVerbosity` defaults to 0 on every machine, and rung
     * 0 of the ladder IS the shut door: this returns the same thing a binary
     * built without any of this would return - rule 1's exactly
     * `MP_STATUS_NOT_SUPPORTED` - **having written nothing into the caller's
     * block.** The code being present is not the same as the channel being
     * open, which is the same polarity rule as the port-0xE9 mirror's.
     *
     * *(The gate was `Log.SnapshotEnabled`, a separate `XhciLogSnapshot`
     * value, until the merge. It was a pure consent bit with no width, and
     * consent nests inside depth, so it became rung 0 of the one value. The
     * behaviour at this line is unchanged to the caller.)*
     *
     * Do not invent a "disabled" status code for this. Rule 1's whole content
     * is that 6 is the only honest nonzero answer at this slot, because
     * usbport's own root-hub port-status probe retries through
     * `RH_GetPortStatus` only when the return is exactly 6.
     *
     * **An extension this driver does not recognise falls through instead**, to
     * the `XHCI_SNAPSHOT_S_BAD_EXTENSION` path below, and that is deliberate: a
     * caller that has got this far is holding a GUID nothing else knows, so it
     * is this project's own tool, and telling it "your driver's extension is
     * corrupt" is worth more than telling it "no such channel". A driver in
     * that state cannot say whether its switch was set either way.
     */
    ext = (PXHCI_EXTENSION)miniPortExtension;
    if (xhciExtensionValid(ext) &&
        ext->Log.Verbosity == XHCI_LOG_VERBOSITY_OFF) {
        return MP_STATUS_NOT_SUPPORTED;
    }

    /*
     * The GUID is ours, so from here on every refusal is reported in the
     * header rather than through the return - usbport collapses every nonzero
     * MPSTATUS to one UsbUserStatusCode, so a status returned that way is
     * indistinguishable from usbport's own errors. The one exception is a
     * block too small to hold a header, where there is nowhere to write.
     */
    if (parameterLength < sizeof(XHCI_SNAPSHOT_HEADER)) {
        return MP_STATUS_FAILURE;
    }

    /* Read the request out before writing a byte of the reply: the request and
     * the header overlay each other in the caller's block. */
    request = (const ULONG *)parameters;
    requestSignature = request[0];
    requestRegion = request[1];
    requestOffset = request[2];

    header = (XHCI_SNAPSHOT_HEADER *)parameters;
    payload = (UCHAR *)parameters + sizeof(XHCI_SNAPSHOT_HEADER);
    capacity = parameterLength - sizeof(XHCI_SNAPSHOT_HEADER);

    /*
     * Fill every field before any early return, so a refused window still
     * comes back with a header that is true about what it can be true about -
     * ExtensionBytes above all, because that is the key the host-side offset
     * table is matched against and a dump decoded against the wrong table is a
     * wrong reading rather than a failed one.
     */
    header->Signature = XHCI_SNAPSHOT_SIGNATURE;
    header->SchemaVersion = XHCI_SNAPSHOT_SCHEMA;
    header->HeaderBytes = sizeof(XHCI_SNAPSHOT_HEADER);
    header->Status = 0;
    header->Region = requestRegion;
    header->Offset = requestOffset;
    header->RegionBytes = 0;
    header->PayloadBytes = 0;
    header->ExtensionBytes = sizeof(XHCI_EXTENSION);
    header->PortCount = 0;
    header->TearDetector = 0;
    header->BuildFlags = xhciSnapshotBuildFlags();

    /*
     * Schema 2's block, and it is filled here with the rest rather than later:
     * these are what `XHCISNAP`'s plain-text companion prints with no offset
     * table, so a window that comes back refused must still carry them. A
     * refused window is exactly when a reader most needs to know which build
     * and which tier answered.
     *
     * The ring's location is derived from the structs rather than written
     * down, so it cannot drift from the layout the payload is a copy of.
     */
    header->Flavour = XHCI_FLAVOUR_CODE;
    header->VerbosityRead = 0;
    header->VerbosityApplied = 0;
    header->SwitchStatusVerbosity = 0;
    header->SwitchStatusDebugView = 0;
    header->SwitchRead = 0;
    header->RingOffset = XHCI_FIELD_OFFSET(XHCI_EXTENSION, Log) +
                         XHCI_FIELD_OFFSET(XHCI_LOG, Ring);
    header->RingBytes = XHCI_LOG_RING_BYTES;
    header->RingHead = 0;
    header->RingUsed = 0;

    if (requestSignature != XHCI_SNAPSHOT_REQUEST_SIGNATURE) {
        header->Status |= XHCI_SNAPSHOT_S_BAD_REQUEST;
        return MP_STATUS_SUCCESS;
    }

    if (!xhciExtensionValid(ext)) {
        header->Status |= XHCI_SNAPSHOT_S_BAD_EXTENSION;
        return MP_STATUS_SUCCESS;
    }

    XhciControllerLockAcquire(&oldIrql);

    /*
     * Read inside the lock, with the payload, so the header describes the same
     * moment the bytes came from. `RingHead` and `RingUsed` above all: a
     * companion that unwrapped the ring with a head from before the copy would
     * print it rotated.
     */
    header->VerbosityRead = ext->Log.VerbosityRead;
    header->VerbosityApplied = ext->Log.Verbosity;
    header->SwitchStatusVerbosity = ext->Log.SwitchStatusVerbosity;
    header->SwitchStatusDebugView = ext->Log.SwitchStatusDebugView;
    header->SwitchRead = ext->Log.SwitchRead;
    header->RingHead = ext->Log.Head;
    header->RingUsed = ext->Log.Used;

    /*
     * **The tear detector, and it is a SUM of four counters rather than one.**
     * Taken inside the lock so it belongs to this window and not to the moment
     * before it.
     *
     * It was `CheckCallbacks` alone, and that was too narrow: usbport's health
     * check is not the only thing that mutates this extension between two
     * windows. The interrupt DPC advances `DpcCount` without going near it, and
     * every producer that reaches the note ring advances `Log.Appends` - so a
     * dump could be genuinely mixed and still come back with two equal
     * detectors, which the tool would report as coherent. (Caught by a Codex
     * review of commit `efa86a3`, and widened again after the next round.)
     *
     * **`Log.Suppressed` is in the sum for a reason the first repair missed**:
     * below the recording rung - which since the snapshot-value merge is the lowest
     * level a dump can be taken at at all, so it is the level the cheapest
     * captures use - a producer call bumps `Suppressed` and NOT `Appends`. A
     * root-hub reset between two windows would then have mutated the extension
     * while leaving a three-term detector unmoved, which is the same hole one
     * term wider. Four terms cover every producer call whether or not it
     * recorded.
     *
     * All four are monotonically increasing, so their sum is too: it cannot
     * return to an earlier value by two of them moving in opposite directions,
     * which is the failure a checksum would have. A single ULONG is what the
     * wire format has room for and what a reader can compare by eye.
     *
     * **It is a detector and not a lock, and it does not become one by being
     * wider.** `CheckCallbacks` is incremented outside the controller lock, so
     * holding that lock here does not serialise it - and there is still no
     * fifth counter behind every field in the extension. What the sum
     * guarantees is the useful direction: **unequal means the dump IS torn**.
     * Equal means none of the four moved, which is strong evidence of
     * coherence and not a proof of it.
     */
    header->TearDetector = ext->CheckCallbacks + ext->DpcCount +
                           ext->Log.Appends + ext->Log.Suppressed;

    /*
     * Filled for **both** regions, not just PORTSC. It is a property of the
     * controller rather than of the region asked for, and a window that left it
     * at 0 made the header only conditionally true - which cost the host tool a
     * defect, since it read the port count out of whichever window happened to
     * be last.
     */
    if (ext->HcInfoStatus == XHCI_HC_OK) {
        header->PortCount = ext->HcInfo.MaxPorts;
    }

    if (requestRegion == XHCI_SNAPSHOT_REGION_EXTENSION) {
        regionBytes = sizeof(XHCI_EXTENSION);
        header->RegionBytes = regionBytes;
        if (requestOffset >= regionBytes) {
            header->Status |= XHCI_SNAPSHOT_S_PAST_END;
        } else {
            available = regionBytes - requestOffset;
            copied = (available > capacity) ? capacity : available;
            xhciSnapshotCopy(payload,
                             (const UCHAR *)ext + requestOffset, copied);
            header->PayloadBytes = copied;
            if (copied < available) {
                header->Status |= XHCI_SNAPSHOT_S_TRUNCATED;
            }
        }
    } else if (requestRegion == XHCI_SNAPSHOT_REGION_PORTSC) {
        /*
         * **The documented observation-mode exception**: this is the one PORTSC
         * read in the driver that does NOT fold the value into the port shadow
         * and does NOT acknowledge the change bits. Every other read in
         * src/xhci_rh.c must, because a read that drops a change bit silences
         * the port (lessons.md, "hot-plug *operations* are not hot-plug
         * *events*"). An instrument that acknowledged
         * what it came to measure would destroy the evidence, and the whole
         * point of Finding Q is that nobody has ever read this register in the
         * wedged state. XhciReadPortsc is a bare read (src/xhci_pci.c).
         */
        if (ext->HcInfoStatus != XHCI_HC_OK) {
            header->Status |= XHCI_SNAPSHOT_S_NO_MMIO;
        } else if ((requestOffset & 3UL) != 0) {
            /* A PORTSC window is an array of ULONGs; a byte offset that does
             * not land on one has no honest answer, so refuse rather than
             * round and leave the caller to reassemble a shifted array. */
            header->Status |= XHCI_SNAPSHOT_S_BAD_REQUEST;
        } else {
            ports = ext->HcInfo.MaxPorts;
            regionBytes = ports * (ULONG)sizeof(ULONG);
            header->RegionBytes = regionBytes;
            if (requestOffset >= regionBytes) {
                header->Status |= XHCI_SNAPSHOT_S_PAST_END;
            } else {
                first = requestOffset / (ULONG)sizeof(ULONG);
                available = ports - first;
                slots = capacity / (ULONG)sizeof(ULONG);
                copied = (available > slots) ? slots : available;
                portscOut = (ULONG *)payload;
                for (i = 0; i < copied; i++) {
                    portscOut[i] = XhciReadPortsc(ext, first + i + 1UL);
                }
                header->PayloadBytes = copied * (ULONG)sizeof(ULONG);
                if (copied < available) {
                    header->Status |= XHCI_SNAPSHOT_S_TRUNCATED;
                }
            }
        }
    } else {
        header->Status |= XHCI_SNAPSHOT_S_BAD_REGION;
    }

    XhciControllerLockRelease(oldIrql);
    return MP_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Packet fill and registration                                        */
/* ------------------------------------------------------------------ */

/*
 * Filled by explicit assignment rather than a positional initializer, on
 * purpose (docs/usb-xhci-info/win98-wdm.md, "MSVC 6.0 / C89 Language Pitfalls"): with 51
 * same-shaped function pointers in a row - twelve of the root-hub ones sharing
 * a single signature - a positional list hides an off-by-one that this form
 * makes impossible.
 *
 * IRQL: PASSIVE_LEVEL.
 */
static VOID xhciFillPacket(VOID)
{
    xhciZeroPacket();

    XhciRegPacket.MiniPortVersion = XHCI_MINIPORT_VERSION;
    XhciRegPacket.MiniPortFlags = XHCI_MINIPORT_FLAGS;
    XhciRegPacket.MiniPortBusBandwidth = TOTAL_USB20_BUS_BANDWIDTH;
    XhciRegPacket.MiniPortExtensionSize = sizeof(XHCI_EXTENSION);
    XhciRegPacket.MiniPortEndpointSize = sizeof(XHCI_ENDPOINT);
    XhciRegPacket.MiniPortTransferSize = sizeof(XHCI_TRANSFER);
    XhciRegPacket.MiniPortResourcesSize = XHCI_DECLARED_RESOURCES_SIZE;

    XhciRegPacket.Reserved1 = XHCI_CANARY_1;
    XhciRegPacket.Reserved2 = XHCI_CANARY_2;
    XhciRegPacket.Reserved3 = XHCI_CANARY_3;
    XhciRegPacket.Reserved4 = XHCI_CANARY_4;
    XhciRegPacket.Reserved5 = XHCI_CANARY_5;

    XhciRegPacket.OpenEndpoint = xhciOpenEndpoint;
    XhciRegPacket.ReopenEndpoint = xhciReopenEndpoint;
    XhciRegPacket.QueryEndpointRequirements = xhciQueryEndpointRequirements;
    XhciRegPacket.CloseEndpoint = xhciCloseEndpoint;
    XhciRegPacket.StartController = xhciStartController;
    XhciRegPacket.StopController = xhciStopController;
    XhciRegPacket.SuspendController = xhciSuspendController;
    XhciRegPacket.ResumeController = xhciResumeController;
    XhciRegPacket.InterruptService = xhciInterruptService;
    XhciRegPacket.InterruptDpc = xhciInterruptDpc;
    XhciRegPacket.SubmitTransfer = xhciSubmitTransfer;
    XhciRegPacket.SubmitIsoTransfer = xhciSubmitIsoTransfer;
    XhciRegPacket.AbortTransfer = xhciAbortTransfer;
    XhciRegPacket.GetEndpointState = xhciGetEndpointState;
    XhciRegPacket.SetEndpointState = xhciSetEndpointState;
    XhciRegPacket.PollEndpoint = xhciPollEndpoint;
    XhciRegPacket.CheckController = xhciCheckController;
    XhciRegPacket.Get32BitFrameNumber = xhciGet32BitFrameNumber;
    XhciRegPacket.InterruptNextSOF = xhciInterruptNextSOF;
    XhciRegPacket.EnableInterrupts = xhciEnableInterrupts;
    XhciRegPacket.DisableInterrupts = xhciDisableInterrupts;
    XhciRegPacket.PollController = xhciPollController;
    XhciRegPacket.SetEndpointDataToggle = xhciSetEndpointDataToggle;
    XhciRegPacket.GetEndpointStatus = xhciGetEndpointStatus;
    XhciRegPacket.SetEndpointStatus = xhciSetEndpointStatus;
    XhciRegPacket.ResetController = xhciResetController;

    XhciRegPacket.RH_GetRootHubData = xhciRhGetRootHubData;
    XhciRegPacket.RH_GetStatus = xhciRhGetStatus;
    XhciRegPacket.RH_GetPortStatus = xhciRhGetPortStatus;
    XhciRegPacket.RH_GetHubStatus = xhciRhGetHubStatus;
    XhciRegPacket.RH_SetFeaturePortReset = xhciRhSetFeaturePortReset;
    XhciRegPacket.RH_SetFeaturePortPower = xhciRhSetFeaturePortPower;
    XhciRegPacket.RH_SetFeaturePortEnable = xhciRhSetFeaturePortEnable;
    XhciRegPacket.RH_SetFeaturePortSuspend = xhciRhSetFeaturePortSuspend;
    XhciRegPacket.RH_ClearFeaturePortEnable = xhciRhClearFeaturePortEnable;
    XhciRegPacket.RH_ClearFeaturePortPower = xhciRhClearFeaturePortPower;
    XhciRegPacket.RH_ClearFeaturePortSuspend = xhciRhClearFeaturePortSuspend;
    XhciRegPacket.RH_ClearFeaturePortEnableChange =
        xhciRhClearFeaturePortEnableChange;
    XhciRegPacket.RH_ClearFeaturePortConnectChange =
        xhciRhClearFeaturePortConnectChange;
    XhciRegPacket.RH_ClearFeaturePortResetChange =
        xhciRhClearFeaturePortResetChange;
    XhciRegPacket.RH_ClearFeaturePortSuspendChange =
        xhciRhClearFeaturePortSuspendChange;
    XhciRegPacket.RH_ClearFeaturePortOvercurrentChange =
        xhciRhClearFeaturePortOvercurrentChange;
    XhciRegPacket.RH_DisableIrq = xhciRhDisableIrq;
    XhciRegPacket.RH_EnableIrq = xhciRhEnableIrq;

    XhciRegPacket.StartSendOnePacket = xhciStartSendOnePacket;
    XhciRegPacket.EndSendOnePacket = xhciEndSendOnePacket;
    XhciRegPacket.PassThru = xhciPassThru;

    XhciRegPacket.RebalanceEndpoint = xhciRebalanceEndpoint;
    XhciRegPacket.FlushInterrupts = xhciFlushInterrupts;
    XhciRegPacket.RH_ChirpRootPort = xhciRhChirpRootPort;
    XhciRegPacket.TakePortControl = xhciTakePortControl;
}

/*
 * Check what registration did to the packet, and log the evidence.
 *
 * Returns 1 if everything is as the ABI record says it must be. A 0 is
 * deliberately *not* fatal - see the call site.
 *
 * IRQL: PASSIVE_LEVEL.
 */
static ULONG xhciVerifyPacketAfterRegistration(VOID)
{
    ULONG *word;
    ULONG serviceIndex;
    ULONG servicesPresent;
    ULONG ok;

    ok = 1;

    if (XhciRegPacket.Reserved1 != XHCI_CANARY_1 ||
        XhciRegPacket.Reserved2 != XHCI_CANARY_2 ||
        XhciRegPacket.Reserved3 != XHCI_CANARY_3 ||
        XhciRegPacket.Reserved4 != XHCI_CANARY_4 ||
        XhciRegPacket.Reserved5 != XHCI_CANARY_5) {
        XHCI_DBG_TEXT("ABI-SUSPECT: a reserved-field canary was overwritten");
        XHCI_DBG_VALUE("Reserved1", XhciRegPacket.Reserved1);
        XHCI_DBG_VALUE("Reserved2", XhciRegPacket.Reserved2);
        XHCI_DBG_VALUE("Reserved3", XhciRegPacket.Reserved3);
        XHCI_DBG_VALUE("Reserved4", XhciRegPacket.Reserved4);
        XHCI_DBG_VALUE("Reserved5", XhciRegPacket.Reserved5);
        ok = 0;
    }

    /*
     * The service block is 16 consecutive pointers at 0xE4-0x120. All 16 being
     * non-NULL is the positive proof that the packet's in/out boundary sits
     * where the layout says: one shifted field and either a service lands in a
     * callback slot or a slot stays NULL.
     */
    word = (ULONG *)&XhciRegPacket.UsbPortDbgPrint;
    servicesPresent = 0;
    for (serviceIndex = 0; serviceIndex < 16; serviceIndex++) {
        if (word[serviceIndex] != 0) {
            servicesPresent++;
        }
    }
    XHCI_DBG_VALUE("usbport services written", servicesPresent);
    XHCI_DBG_WORDS("services", word, 16);
    if (servicesPresent != 16) {
        XHCI_DBG_TEXT("ABI-SUSPECT: usbport did not fill all 16 services");
        ok = 0;
    }

    /* Our own slots must survive registration untouched. */
    if (XhciRegPacket.StartController != xhciStartController ||
        XhciRegPacket.RH_GetRootHubData != xhciRhGetRootHubData ||
        XhciRegPacket.TakePortControl != xhciTakePortControl) {
        XHCI_DBG_TEXT("ABI-SUSPECT: a miniport callback slot was overwritten");
        ok = 0;
    }

    return ok;
}

/*
 * The host suite's way in (docs/contributing/design/03-host-unit-tests.md).
 *
 * usbport reaches this file only through the registration packet, so a test
 * that calls the bodies in src/xhci_evt.c and src/xhci_init.c directly is
 * testing everything *except* the wrappers - and the wrappers are where
 * xhciExtensionValid runs. That gap was real: removing all four signature
 * checks from the bodies left the whole suite green, and the trailing-signature
 * half of the guard had never been executed by a test at all.
 *
 * DriverEntry stays out of the host build (it needs the DDK types and the two
 * usbport imports); the packet fill does not, so exposing it lets a test drive
 * **the exact function pointers usbport is given**. Nothing here is compiled
 * into a shipping image - XHCI_HOST_TEST is set only by test/run-host-tests.cmd.
 */
#ifdef XHCI_HOST_TEST

VOID XhciFillPacketForTest(VOID)
{
    xhciFillPacket();
}

#else

/*
 * DriverEntry - the only entry point this driver has.
 *
 * On success usbport owns the driver object: it installs its own AddDevice and
 * MajorFunction handlers, so no IRP ever reaches this module and there is
 * nothing else for DriverEntry to set up.
 *
 * IRQL: PASSIVE_LEVEL.
 */
NTSTATUS NTAPI DriverEntry(IN PDRIVER_OBJECT DriverObject,
                           IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    ULONG hciMn;
#ifdef XHCI_DBG_TRACE
    ULONG espBefore = 0;
    ULONG espAfter = 0;
#endif

#ifdef XHCI_EMITS_DIAGNOSTIC_MARKER
    /* The read is what keeps the marker in the image; it must track the same
     * condition that defines the marker, or a diagnostic build emits a string
     * the linker is free to drop and the packager's gate goes blind. */
    if (XhciProbeBuildMarker[0] != 'X') {
        return STATUS_UNSUCCESSFUL;
    }
#endif

#ifdef XHCI_EMITS_FAILSTART_MARKER
    /* Same rule, own condition: task 12.3's artifact is recognised by this
     * string, so it has to survive the linker for exactly as long as the
     * behaviour it names does. */
    if (XhciFailStartMarker[0] != 'X') {
        return STATUS_UNSUCCESSFUL;
    }
#endif

    /* Same rule again, and unconditional: every image carries exactly one
     * flavour marker, so this read is not guarded on anything. A binary whose
     * marker the linker dropped could not be identified from the file, which
     * is the whole of what task 13-L.1 added it for. */
    if (XhciFlavourMarker[0] != 'X') {
        return STATUS_UNSUCCESSFUL;
    }

    XHCI_DBG_TEXT("DriverEntry (built " __DATE__ " " __TIME__ ")");

    /*
     * The controller lock is created once for the life of the driver. It is
     * deliberately not in the miniport extension: usbport zeroes that before
     * every StartController, and a lock re-created underneath an uncancellable
     * timer callback is a race no epoch check can close (src/xhci_cmd.c).
     */
    XhciControllerGlobalInit();

    /*
     * The cheapest possible proof that the USBPORT.SYS import resolved to a
     * real usbport build, and the same probe the shipping miniports do first.
     * Each lineage returns its own constant; both known values are accepted so
     * one binary can also load on XP, and anything else is refused before the
     * driver object is handed over.
     */
    hciMn = USBPORT_GetHciMn();
    XHCI_DBG_VALUE("USBPORT_GetHciMn", hciMn);
    if (hciMn != USBPORT_HCI_MN_W2K && hciMn != USBPORT_HCI_MN_XP) {
        XHCI_DBG_TEXT("unknown usbport lineage - refusing to register");
        return STATUS_UNSUCCESSFUL;
    }

    xhciFillPacket();

    XHCI_DBG_VALUE("packet size", sizeof(USBPORT_REGISTRATION_PACKET));
    XHCI_DBG_VALUE("MiniPortExtensionSize", sizeof(XHCI_EXTENSION));
    XHCI_DBG_VALUE("MiniPortEndpointSize", sizeof(XHCI_ENDPOINT));
    XHCI_DBG_VALUE("MiniPortTransferSize", sizeof(XHCI_TRANSFER));
    XHCI_DBG_VALUE("MiniPortResourcesSize", XHCI_DECLARED_RESOURCES_SIZE);
    XHCI_DBG_VALUE("common buffer usbport will request",
                   XhciCommonBufferAllocationBytes(XHCI_DECLARED_RESOURCES_SIZE));

    /*
     * The image base and length, so a fault address in a later bugcheck can be
     * attributed to a module instead of guessed at. Win98's fatal-exception
     * screen prints a bare 0028:XXXXXXXX with no module name whenever the
     * address is outside a registered VxD's range, which made the task 8
     * disable bugcheck unattributable until this line existed.
     */
    XHCI_DBG_VALUE("image base", (ULONG)DriverObject->DriverStart);
    XHCI_DBG_VALUE("image size", (ULONG)DriverObject->DriverSize);

    /* What the shipping usbehci.sys does immediately before registering:
     * usbport saves this pointer and installs its own unload handler. */
    DriverObject->DriverUnload = NULL;

#ifdef XHCI_DBG_TRACE
    __asm mov espBefore, esp
#endif

    status = USBPORT_RegisterUSBPortDriver(DriverObject,
                                           USB20_MINIPORT_INTERFACE_VERSION,
                                           &XhciRegPacket);

#ifdef XHCI_DBG_TRACE
    __asm mov espAfter, esp

    /*
     * A stack-pointer delta across the call means the export was reached with
     * the wrong calling convention - the failure the stdcall-stub import
     * library exists to prevent, and one that otherwise corrupts state far from
     * its cause. Worth three instructions in a build that can report them.
     *
     * **Guarded on XHCI_DBG_TRACE and not on DBG**, which is src/xhci_dbg.h's
     * own rule for anything touching this channel and which this site broke
     * until the post-Phase 13 review rounds. `debug` and `qemu` are both checked builds, so `DBG` is
     * set in both - but XHCI_DBG_VALUE compiles to nothing without
     * XHCI_DBG_LIVE, so under `DBG` alone the shipping `debug` build spent the
     * three instructions and the compare on a report it could not make. Same
     * class as the two src/xhci_probe.c sites the first cut of the split left
     * behind, from the harmless end.
     */
    if (espBefore != espAfter) {
        XHCI_DBG_VALUE("STACK DELTA across registration", espBefore - espAfter);
    }
#endif

    XHCI_DBG_VALUE("USBPORT_RegisterUSBPortDriver status", status);

    if (!NT_SUCCESS(status)) {
        /*
         * Not a no-op failure: in all three shipping builds the version-reject
         * branch sits *after* the driver-object takeover, so MajorFunction and
         * AddDevice are already replaced and the interface allocation is
         * leaked. Nothing can be undone from here - just say so in the log so
         * the next symptom is not misread.
         */
        XHCI_DBG_TEXT("registration failed - driver object is already hijacked");
        return status;
    }

    if (!xhciVerifyPacketAfterRegistration()) {
        /*
         * Deliberately still returns success. usbport has already linked this
         * driver object into its global miniport list; failing DriverEntry now
         * would unload the image out from under those pointers and turn a
         * diagnosable ABI mismatch into an unrelated crash later. The log says
         * ABI-SUSPECT, the phase does not close on it, and the driver stays
         * loaded so the rest of the callback sequence can be observed.
         */
        XHCI_DBG_TEXT("continuing despite ABI-SUSPECT - see lines above");
    }

    XHCI_DBG_TEXT("registered with usbport.sys");
    return STATUS_SUCCESS;
}

#endif /* XHCI_HOST_TEST */
