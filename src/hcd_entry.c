/*
 * hcd_entry.c - DriverEntry of xhci98.sys, the successor host controller
 * driver (design record 13; roadmap task 25.8).
 *
 * This is the build scaffold and nothing more. It registers no dispatch
 * routine, no AddDevice and no Unload, touches no hardware and imports
 * nothing a later phase has not yet evidenced; Phase 26 task 26-A.2 replaces
 * it with the FDO. What it proves is the build path: that the pure core
 * compiles and links into the HCD with no usbport.sys beneath it, in all three
 * flavours and both architectures, and that the gates run over the result
 * (scripts\build-driver.cmd).
 *
 * Two strings are carried in the image, each read once below so the linker
 * keeps it:
 *
 *   - the flavour marker, XHCI98_FLAVOUR_*, which scripts\check-flavour-marker.ps1
 *     and make-package.ps1 read out of the bytes.
 *   - XHCI98_SCAFFOLD_DO_NOT_STAGE, which scripts\package\make-package.ps1
 *     refuses to stage and which is the import gate's one licence for an
 *     image with no imports. Nothing here installs on a guest - that is
 *     26-V.0's, after 26-A.1 removes this marker - and a package holding this
 *     image would install a driver that does nothing at all.
 *
 * IRQL: DriverEntry runs at PASSIVE_LEVEL.
 */

#include "xhci_compat.h"
#include "xhci_version.h"

#if defined(XHCI_FLAVOUR_QEMU)
#define HCD_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#elif defined(XHCI_FLAVOUR_DEBUG)
#define HCD_FLAVOUR_NAME "XHCI98_FLAVOUR_DEBUG"
#elif defined(XHCI_FLAVOUR_RELEASE)
#define HCD_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#else
#error "no build flavour was defined. src/sources derives XHCI_FLAVOUR_RELEASE, _DEBUG or _QEMU from BUILD_ALT_DIR; build through scripts\build-driver.cmd."
#endif

static volatile const char HcdFlavourMarker[] = HCD_FLAVOUR_NAME;

static volatile const char HcdScaffoldMarker[] =
    "XHCI98_SCAFFOLD_DO_NOT_STAGE";

static volatile const char HcdVersionString[] = "xhci98 " XHCI_VER_STR;

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);

NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    if (HcdFlavourMarker[0] != 'X' || HcdScaffoldMarker[0] != 'X' ||
        HcdVersionString[0] != 'x') {
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}
