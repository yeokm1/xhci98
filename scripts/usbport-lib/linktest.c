/*
 * Link-time proof that the generated usbport.lib is usable by a miniport
 * that prototypes the usbport exports the way xhci98.sys will: NTAPI
 * (__stdcall), so the references are _USBPORT_GetHciMn@0 and
 * _USBPORT_RegisterUSBPortDriver@12.
 *
 * This is the check that catches the documented failure mode - a lib built
 * with plain `lib /def:` links only against cdecl-style _Name symbols and
 * fails here with LNK2001, rather than at the far end of a VM deploy.
 *
 * Run by scripts\make-usbport-lib.cmd after building the lib; the resulting
 * image is inspected for its USBPORT.SYS import descriptor and then deleted.
 * It is not xhci98.sys and shares no code with it.
 *
 * No DDK headers on purpose (see usbport-stub.c).
 */

typedef unsigned long ULONG;
typedef long NTSTATUS;

ULONG __stdcall USBPORT_GetHciMn(void);

NTSTATUS __stdcall USBPORT_RegisterUSBPortDriver(void *DriverObject,
                                                 ULONG Version,
                                                 void *RegistrationPacket);

NTSTATUS __stdcall
DriverEntry(void *DriverObject, void *RegistryPath)
{
    static char Packet[316];

    (void)RegistryPath;

    if (USBPORT_GetHciMn() == 0)
    {
        return (NTSTATUS)0xC0000001;
    }

    return USBPORT_RegisterUSBPortDriver(DriverObject, 200, Packet);
}
