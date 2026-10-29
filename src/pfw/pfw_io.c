/*
 * pfw_io.c - XHCIPFW.VXD's configuration accesses: PCI configuration
 * mechanism #1, the address dword to port CF8h and the data dword at port
 * CFCh (design record 32 sections 4.1 and 5.1). Called at
 * Sys_Critical_Init only, in ring 0 with interrupts disabled, so the pair
 * is never split by another user of the ports. The only object of the
 * release image with port instructions; src\pfw\build.cmd refuses one in
 * any other.
 */

#include <conio.h>
#include "pfw_scan.h"

PFW_U32 PfwCfgAddr(PFW_U32 bus, PFW_U32 devfn, PFW_U32 off)
{
    return 0x80000000UL | ((bus & 0xFFUL) << 16) | ((devfn & 0xFFUL) << 8) |
           (off & 0xFCUL);
}

PFW_U32 PfwCfgRead(PFW_U32 bus, PFW_U32 devfn, PFW_U32 off)
{
    outpd(0xCF8, PfwCfgAddr(bus, devfn, off));
    return inpd(0xCFC);
}

void PfwCfgWrite(PFW_U32 bus, PFW_U32 devfn, PFW_U32 off, PFW_U32 val)
{
    outpd(0xCF8, PfwCfgAddr(bus, devfn, off));
    outpd(0xCFC, val);
}
