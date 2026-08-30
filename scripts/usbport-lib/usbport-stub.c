/*
 * Stub bodies used only to generate an import library for USBPORT.SYS.
 * They are never compiled into xhci98.sys and never execute - the linker
 * only needs their __stdcall signatures to emit the decorated symbols.
 *
 * Why a stub DLL rather than `lib /def:`: usbport.sys exports these as
 * __stdcall functions under *undecorated* names. `lib /def:` with plain
 * names emits cdecl-style _USBPORT_GetHciMn symbols, which cannot resolve
 * the _USBPORT_GetHciMn@0 / _USBPORT_RegisterUSBPortDriver@12 references a
 * correctly NTAPI-prototyped miniport generates (verified), and
 * re-declaring them cdecl to force the link would corrupt the stack at the
 * 3-argument registration call. See docs/contributing/build-and-test.md "Build Files".
 *
 * Built by scripts\make-usbport-lib.cmd; do not build by hand.
 *
 * No DDK headers on purpose: the import library must be buildable with
 * nothing but MSVC 6.0. Signatures per docs/usb-xhci-info/usbport-miniport-abi.md
 * section 3 (ULONG return / NTSTATUS return, both __stdcall).
 */

typedef unsigned long ULONG;

ULONG __stdcall
USBPORT_GetHciMn(void)
{
    return 0;
}

long __stdcall
USBPORT_RegisterUSBPortDriver(void *DriverObject,
                              ULONG Version,
                              void *RegistrationPacket)
{
    (void)DriverObject;
    (void)Version;
    (void)RegistrationPacket;
    return 0;
}
