/*
 * pfw.h - XHCIPFW.VXD, shared by pfw.c (the control procedure and the
 * log), pfw_io.c (the configuration ports) and pfw_data.c (the data);
 * design record 32.
 */

#ifndef PFW_H
#define PFW_H

#include "pfw_vmm.h"
#include "pfw_scan.h"

void __declspec(naked) PfwControl(void);

extern PFW_DDB XHCIPFW_DDB;
extern PFW_RESULT PfwRes;
extern unsigned long PfwLogRc;      /* the registry write's outcome */
extern const char PfwStamp[];

#endif
