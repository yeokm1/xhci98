/*
 * pfw.c - XHCIPFW.VXD, the Windows 98 SE and ME static VxD that closes
 * every PCI-to-PCI bridge's prefetchable window lying wholly above 4 GB
 * before the configuration manager and the PCI enumerator read the
 * bridges (roadmap-usb4.md task 44.49, design record 32), and logs any
 * xHCI controller whose register BAR lies above 4 GB.
 *
 * Sys_Critical_Init: the step (pfw_scan.c's PfwRun), interrupts disabled,
 * no VMM service. Device_Init: its result written once to the registry
 * (section 6). Every other message: nothing. The VxD returns success to
 * VMM in every case (section 5.1). No file-scope static anywhere (the 98
 * DDK's LINK 5.12 mislinks Watcom's static-symbol records, design record
 * 30 section 6.1); the data lives in pfw_data.c.
 */

#include "pfw.h"
#ifdef PFW_DEBUGCON
#include <conio.h>
#endif

/* VMM's registry services, C-callable (pfw_vmm.h): the int 20h call with
 * the service's dword, which VMM patches into a direct call on first use.
 * C cannot emit a dynalink, so these are byte sequences; the parameters
 * are ordinary C ones, removed by the caller. */
PFW_U32 PfwRegCreateKey(PFW_U32 hkey, const char *sub, PFW_U32 *result);
#pragma aux PfwRegCreateKey = 0xcd 0x20 0x4a 0x01 0x01 0x00 \
        parm caller [] value [eax] modify [eax ecx edx];

PFW_U32 PfwRegSetValueEx(PFW_U32 hkey, const char *name, PFW_U32 reserved,
                         PFW_U32 type, const void *data, PFW_U32 bytes);
#pragma aux PfwRegSetValueEx = 0xcd 0x20 0x52 0x01 0x01 0x00 \
        parm caller [] value [eax] modify [eax ecx edx];

PFW_U32 PfwRegCloseKey(PFW_U32 hkey);
#pragma aux PfwRegCloseKey = 0xcd 0x20 0x49 0x01 0x01 0x00 \
        parm caller [] value [eax] modify [eax ecx edx];

#ifdef PFW_DEBUGCON
/* The qemu build's lines to QEMU's debug console (section 6). */
void PfwPut(const char *s)
{
    while (*s) {
        outp(0xE9, *s++);
    }
}

void PfwHex(PFW_U32 v)
{
    const char *d = "0123456789abcdef";
    int i;

    for (i = 28; i >= 0; i -= 4) {
        outp(0xE9, d[(v >> i) & 0xF]);
    }
}

void PfwLine(const char *what, PFW_U32 a, PFW_U32 b, PFW_U32 c)
{
    PfwPut("xhcipfw ");
    PfwPut(what);
    PfwPut(" ");
    PfwHex(a);
    PfwPut(" ");
    PfwHex(b);
    PfwPut(" ");
    PfwHex(c);
    PfwPut("\r\n");
}

void PfwTrace(void)
{
    PFW_U32 i;
    PFW_BRIDGE_REC *b;
    PFW_BAR_REC *m;
    PFW_XHCI_REC *x;

    PfwLine("scan result/functions/subjects", PfwRes.Result,
            PfwRes.Functions, PfwRes.NSubj);
    for (i = 0; i < PfwRes.NBridges; i++) {
        b = &PfwRes.Bridges[i];
        PfwLine("bridge bus/devfn/action", b->Bus, b->DevFn, b->Action);
        PfwLine(" before 24/28/2c", b->Before[0], b->Before[1], b->Before[2]);
        PfwLine(" after 24/28/2c", b->After[0], b->After[1], b->After[2]);
    }
    for (i = 0; i < PfwRes.NBars; i++) {
        m = &PfwRes.Bars[i];
        PfwLine("bar bus/devfn/offset", m->Bus, m->DevFn, m->Offset);
        PfwLine(" bar upper", m->Upper, 0, 0);
    }
    for (i = 0; i < PfwRes.NXhci; i++) {
        x = &PfwRes.Xhci[i];
        PfwLine("xhci bus/devfn/offset", x->Bus, x->DevFn, x->Offset);
        PfwLine(" xhci id/hi/lo", x->Id, x->Hi, x->Lo);
    }
    PfwLine("closed/refused/parked", PfwRes.Closed, PfwRes.Refused,
            PfwRes.Parked);
    PfwLine("across/barsinside/xhcihigh", PfwRes.Across, PfwRes.BarsInside,
            PfwRes.XhciHigh);
}
#define PFW_TRACE()             PfwTrace()
#define PFW_LINE(w, a, b, c)    PfwLine((w), (PFW_U32)(a), (PFW_U32)(b), (PFW_U32)(c))
#else
#define PFW_TRACE()             ((void)0)
#define PFW_LINE(w, a, b, c)    ((void)0)
#endif

void PfwDword(PFW_U32 key, const char *name, PFW_U32 v)
{
    PFW_U32 rc;

    rc = PfwRegSetValueEx(key, name, 0, PFW_REG_DWORD, &v, 4);
    if (rc != 0 && PfwLogRc == 0) {
        PfwLogRc = rc;
    }
}

void PfwBinary(PFW_U32 key, const char *name, const void *data, PFW_U32 bytes)
{
    PFW_U32 rc;

    rc = PfwRegSetValueEx(key, name, 0, PFW_REG_BINARY, data, bytes);
    if (rc != 0 && PfwLogRc == 0) {
        PfwLogRc = rc;
    }
}

/* Device_Init: the result as section 6's values, under the key the
 * installer registers the VxD in. */
void PfwLog(void)
{
    PFW_U32 key;
    PFW_U32 rc;
    PFW_U32 n;

    key = 0;
    rc = PfwRegCreateKey(PFW_HKEY_LOCAL_MACHINE,
                         "System\\CurrentControlSet\\Services\\VxD\\XHCIPFW",
                         &key);
    if (rc != 0) {
        PfwLogRc = rc;
        PFW_LINE("log create rc", rc, 0, 0);
        return;
    }
    PfwDword(key, "XhciPfwResult", PfwRes.Result);
    PfwDword(key, "XhciPfwClosed", PfwRes.Closed);
    PfwDword(key, "XhciPfwRefused", PfwRes.Refused);
    PfwDword(key, "XhciPfwParked", PfwRes.Parked);
    PfwDword(key, "XhciPfwAcross", PfwRes.Across);
    PfwDword(key, "XhciPfwBarsInside", PfwRes.BarsInside);
    PfwDword(key, "XhciPfwFunctions", PfwRes.Functions);
    PfwDword(key, "XhciPfwXhciHigh", PfwRes.XhciHigh);
    PfwBinary(key, "XhciPfwBridges", PfwRes.Bridges,
              PfwRes.NBridges * sizeof(PFW_BRIDGE_REC));
    PfwBinary(key, "XhciPfwBars", PfwRes.Bars,
              PfwRes.NBars * sizeof(PFW_BAR_REC));
    PfwBinary(key, "XhciPfwXhci", PfwRes.Xhci,
              PfwRes.NXhci * sizeof(PFW_XHCI_REC));
    for (n = 0; PfwStamp[n] != 0; n++) {
        ;
    }
    rc = PfwRegSetValueEx(key, "XhciPfwVersion", 0, PFW_REG_SZ, PfwStamp,
                          n + 1);
    if (rc != 0 && PfwLogRc == 0) {
        PfwLogRc = rc;
    }
    PfwRegCloseKey(key);
    PFW_LINE("log rc/result/closed", PfwLogRc, PfwRes.Result, PfwRes.Closed);
}

/* VMM passes the message in EAX; the C result 0 is success. */
int PfwCtrl(PFW_U32 msg);
#pragma aux PfwCtrl parm [eax] value [eax];
int PfwCtrl(PFW_U32 msg)
{
    if (msg == PFW_SYS_CRITICAL_INIT) {
        PFW_LINE("sys_critical_init order", PFW_INIT_ORDER, 0, 0);
        PfwRun(&PfwRes);
        PFW_TRACE();
    } else if (msg == PFW_DEVICE_INIT) {
        PFW_LINE("device_init", 0, 0, 0);
        PfwLog();
    }
    return 0;
}

void __declspec(naked) PfwControl(void)
{
    __asm {
        call PfwCtrl    ; the C handler: VMM's message in EAX, the result back in EAX
        neg  eax        ; carry = (EAX != 0), VMM's failure flag: C cannot set a flag
        ret             ; a naked function gets no epilogue from the compiler
    }
}
