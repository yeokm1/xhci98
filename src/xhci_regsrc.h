/*
 * xhci_regsrc.h - where a registry value of the two drivers is taken from
 * (roadmap-usb4.md task 42-A.17; design record 13 section 5.5, design
 * record 21 section 8).
 *
 * From 3.1.0.0 every Xhci* value of xhci98.sys and every XhciCm* value of
 * xhcicm.sys is read first from the machine-wide key
 * HKLM\System\CurrentControlSet\Services\USB and then, when the value is
 * not there, from the controller's or the host interface's driver key as
 * before. A value present machine-wide wins whatever its type: one of
 * another type there is present and unreadable, and its default stands, as
 * the same value in the driver key would give (owner, 2026-10-18: the INFs
 * write every per-controller value, so the other precedence would leave
 * the machine-wide key dead; a tester who wants one controller different
 * deletes the machine-wide value). The OS glue reads each key (hcd_ctl.c
 * hcdReadDword, cm_reg.c cmQuery); this file only decides.
 *
 * DDK-free: part of the pure core, linked into both drivers. C89.
 * IRQL: any.
 */

#ifndef XHCI_REGSRC_H
#define XHCI_REGSRC_H

#include "xhci_compat.h"

/* One key's reading of one value. */
#define XHCI_REG_ABSENT         0UL     /* no such value, or no such key   */
#define XHCI_REG_TAKEN          1UL     /* there, of the type the reader
                                         * takes (a 4-byte REG_DWORD, or a
                                         * REG_SZ for the one string)      */
#define XHCI_REG_OTHER          2UL     /* there, of another type          */

/* Which key's reading stands. */
#define XHCI_REG_FROM_NONE      0UL     /* neither has it: the default     */
#define XHCI_REG_FROM_MACHINE   1UL     /* Services\USB                    */
#define XHCI_REG_FROM_DEVICE    2UL     /* the driver (software) key       */

/* A reading from what the query routine saw: `taken` when it accepted the
 * value, else `seen` when it was called with a value at all, else absent. */
ULONG XhciRegState(ULONG seen, ULONG taken);

/* The machine-wide reading when it is not absent, else the driver key's
 * when that is not absent, else none. A state outside the three is read as
 * absent. */
ULONG XhciRegPick(ULONG machineState, ULONG deviceState);

/* The state that stands: the picked key's, or XHCI_REG_ABSENT. */
ULONG XhciRegPickedState(ULONG machineState, ULONG deviceState);

#endif /* XHCI_REGSRC_H */
