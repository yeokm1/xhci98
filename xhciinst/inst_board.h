/*
 * inst_board.h - an ASM4242 board's identity as Setup reads it, for the
 * USB4 handover on 2000 and later (roadmap-usb4.md task 46.12 on NT;
 * design record 32 section 12.8.15.12): the four functions' PCI ids and
 * the raw SMBIOS tables, matched by the driver's own pure CmOscpSmbios
 * and CmOscpMatch (src\cm\cm_oscp.c), so Setup's classification is
 * xhcicm.sys's. Linked into XHCIINST, XHCIWIZ and their x64 builds, and
 * into test\test_inst.c. Plain C types alone, so the engine includes it
 * beside windows.h. C89.
 */

#ifndef INST_BOARD_H
#define INST_BOARD_H

#define INST_BOARD_NONE     0   /* no ASM4242 USB4 host, or 98 SE and ME  */
#define INST_BOARD_KNOWN    1   /* a board read on a bench                */
#define INST_BOARD_UNKNOWN  2   /* read whole, not recognised             */
#define INST_BOARD_UNREAD   3   /* Setup could not check it: its parents'
                                   ids or the SMBIOS tables not read (no
                                   routine, a failure, WMI's timeout)     */
#define INST_BOARD_PENDING  4   /* WMI asked, its answer still to come    */
#define INST_BOARD_HELPER   6   /* known, but the driver cannot send: on
                                   32-bit NT its routines come from the
                                   xHCI98 USB drivers' helper, xhcihlpr,
                                   which is not there after the run       */
#define INST_BOARD_LATER    5   /* WMI not asked yet: the window asks at
                                   the Thunderbolt and USB4 page's Next   */

/* The reasons beside CmOscpSmbios's (CM_OSCP_*, below 100), logged. */
#define INST_BOARD_W_OK         0UL
#define INST_BOARD_W_MATCH      100UL   /* read whole, no profile equal   */
#define INST_BOARD_W_ID         101UL   /* a function's id not a PCI id   */
#define INST_BOARD_W_READ       102UL   /* the tables could not be read   */
#define INST_BOARD_W_ROUTE      103UL   /* no routine for them            */
#define INST_BOARD_W_LENGTH     104UL   /* the tables' length outside     */

/* What the tables' read gave: route 1 read (rsmb, rsmbLen: the firmware
 * table routine's whole answer, or WMI's bytes in the same form: four
 * bytes, a 32-bit length and the tables), 0 the read failed, -1 no route
 * on this Windows. */
#define INST_BOARD_R_READ       1
#define INST_BOARD_R_FAILED     0
#define INST_BOARD_R_NONE       (-1)
#define INST_BOARD_R_PENDING    2       /* asked, not answered yet        */
#define INST_BOARD_R_LATER      3       /* to be asked at Next            */

/*
 * The class of the board under the NHI whose instance id is nhiId, its
 * parents' instance ids (the downstream port above it, the upstream port,
 * the root port; NULL where one could not be read) and the tables as
 * route says. *why the reason (INST_BOARD_W_* or CM_OSCP_*), *profile the
 * profile's number (1 on) when known, else 0.
 */
int inst_board_class(const char *rootId, const char *uspId,
                     const char *dspId, const char *nhiId, int route,
                     const unsigned char *rsmb, unsigned long rsmbLen,
                     unsigned long *why, unsigned long *profile);

/* Of two classes the one the boards together say: unknown, then being
 * checked, then to be checked, then not checked, then known, then
 * none. */
int inst_board_worse(int a, int b);

/* 1 when the SMBIOS tables are to be asked for (the owner's decision of
 * 2026-11-04): on 2000 and later, the Thunderbolt support in the run
 * (tbIn: its box ticked, or /thunderbolt) and an ASM4242 host listed (its
 * NHI or its xHCI); never otherwise, so an unticked box asks nothing. */
int inst_board_query(int nt, int tbIn, int asmHost);

/* 1 when Setup reads the identity on this system: the installer's line
 * follows what the driver will do (record 32 section 12.8.15.12). On
 * Windows 2000 (NT 5.0) the driver's route E has no WMI routines and
 * reads no identity, so it never sends by E: Setup reads nothing and says
 * it could not check. 98 SE and ME: never. */
int inst_board_reads(int nt, unsigned long major, unsigned long minor);

/* 1 when the driver can send the handover to a known board after the run
 * (record 32 sections 12.8.15.12 and 12.8.15.15): on 64-bit Windows
 * always; on 32-bit NT only through the helper xhcihlpr.sys, which both
 * the xHCI98 USB drivers (xhci98.inf) and the Thunderbolt part
 * (xhcicm.inf, design record 18 section 15.15) install: the run installs
 * a controller (usbInstalls) or an NHI (cmInstalls) with them, or the
 * helper is in place now (helperNow) at this package's version or later
 * (helperCmp >= 0, inst_ver_cmp's answer), the first with the routine
 * service. */
int inst_board_sendable(int x64, int usbInstalls, int cmInstalls,
                        int helperNow, int helperCmp);

/* The first helper with the routine service (channel minor 1, record 32
 * section 12.8.15.15): 3.4.1.0. The helper of 3.4.0.0 answers minor 0
 * alone and carries the same version stamp as this tree until the
 * 3.4.1.0 bump, so a helper in place is measured against this, never
 * against the package's own version: 0 when the version is at or above
 * it (inst_board_sendable's helperCmp), -1 below. */
#define INST_HLPR_ROUTINE_V1    3UL
#define INST_HLPR_ROUTINE_V2    4UL
#define INST_HLPR_ROUTINE_V3    1UL
#define INST_HLPR_ROUTINE_V4    0UL
int inst_board_hlpr_cmp(unsigned long v1, unsigned long v2, unsigned long v3,
                        unsigned long v4);

/* 1 when the run installs a device with its INF, so the helper that INF
 * carries is in place after the run (inst_board_sendable's usbInstalls
 * and cmInstalls): present, not refused by its group, to be installed,
 * and its INF not held back by the staging (a stored build the run may
 * not replace, or a store that could not be read). An older helper on the
 * disk is then replaced, a newer one kept (COPYFLG_NO_VERSION_DIALOG). */
int inst_board_run_installs(int present, int refused, int runs, int held);

/* 1 when the board's line is shown: the Thunderbolt support in the run
 * and an ASM4242 NHI found. */
int inst_board_shown(int tbIn, int nhiCount);

/* 1 when an instance or hardware id names the ASM4242's NHI,
 * PCI\VEN_1B21&DEV_2425 alone or followed by "&" or "\". */
int inst_board_nhi(const char *id);

#endif
