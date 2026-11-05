/*
 * pfw_ids.c - the host interfaces (NHIs) XHCIPFW.VXD's interrupt-line pass
 * selects by id (design record 32 section 12.10 step 2, task 46.11): the
 * vendor and device ids of xhcicm.inf's 98 SE and ME models (its
 * [CmModels] Cm.Dev rows) as (device << 16) | vendor; the INF's class row,
 * PCI\CC_0C0340, is pfw_scan.h's PFW_CLASS_USB4_NHI. test\test_pfw.c reads
 * the INF and holds the two equal.
 *
 * Data in an object of its own, as pfw_data.c's: the 98 DDK's LINK 5.12
 * mislinks a reference from an object's code back to data it defined
 * (design record 30 section 6.1); the host suite links this file too.
 */

#include "pfw_scan.h"

const PFW_U32 PfwNhiIds[] = {
    0x15BF8086UL, 0x156A8086UL, 0x156C8086UL, 0x15758086UL,
    0x15778086UL, 0x15D28086UL, 0x15D98086UL, 0x15E88086UL,
    0x15EB8086UL, 0x11348086UL, 0x11378086UL, 0x8A178086UL,
    0x8A0D8086UL, 0x9A1B8086UL, 0x9A1D8086UL, 0x9A1F8086UL,
    0x9A218086UL, 0x463E8086UL, 0x466D8086UL, 0xA73E8086UL,
    0xA76D8086UL, 0x7EB28086UL, 0x7EC28086UL, 0x7EC38086UL,
    0xA8338086UL, 0xA8348086UL, 0xE3338086UL, 0xE3348086UL,
    0xE4338086UL, 0xE4348086UL, 0x4D338086UL, 0x57818086UL,
    0x57848086UL, 0x16681022UL, 0x16691022UL, 0x162E1022UL,
    0x162F1022UL, 0x15C41022UL, 0x15C51022UL, 0x151C1022UL,
    0x151D1022UL, 0x158D1022UL, 0x158E1022UL, 0x11201022UL,
    0x11211022UL, 0x113B1022UL, 0x113C1022UL, 0x24251B21UL
};

const PFW_U32 PfwNhiIdCount = sizeof(PfwNhiIds) / sizeof(PfwNhiIds[0]);
