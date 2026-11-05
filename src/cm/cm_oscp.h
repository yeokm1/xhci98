/*
 * cm_oscp.h - the ASM4242 handover's known-board route on NT, its pure
 * half (roadmap-usb4.md task 46.12; design record 32 sections 12.8.15.11
 * and 12.8.15.13): the SMBIOS identity decoded from the system's raw
 * SMBIOS tables (as WMI's MSSmBios_RawSMBiosTables block or the
 * firmware table interface gives them), the match against the profiles
 * of boards read on a bench, and the boot guard's decisions. Compiled
 * into xhcicm.sys and into the installer (inst_core.c); host-tested in
 * test\test_cm_oscp.c.
 *
 * DDK-free. C89, no allocation, no lock. IRQL: any.
 */

#ifndef CM_OSCP_H
#define CM_OSCP_H

#include "../xhci_compat.h"

/* Reasons, the first refusal (logged as they are). */
#define CM_OSCP_OK              0UL
#define CM_OSCP_WNODE_SHORT     1UL   /* a node's header or size outside   */
#define CM_OSCP_WNODE_LINK      2UL   /* a next offset backward or outside */
#define CM_OSCP_WNODE_COUNT     3UL   /* not exactly one instance          */
#define CM_OSCP_WNODE_DATA      4UL   /* the instance's data outside       */
#define CM_OSCP_RAW_LENGTH      5UL   /* the raw block's length outside    */
#define CM_OSCP_SM_STRUCT       6UL   /* a structure outside the tables    */
#define CM_OSCP_SM_FIELD        7UL   /* a field read past its length      */
#define CM_OSCP_SM_STRINGS      8UL   /* a string set not ended inside     */
#define CM_OSCP_SM_INDEX        9UL   /* a string index 0 or past the set  */
#define CM_OSCP_SM_LONG         10UL  /* a string longer than 64 bytes     */
#define CM_OSCP_SM_TWICE        11UL  /* type 0 or type 2 twice            */
#define CM_OSCP_SM_MISSING      12UL  /* type 0 or type 2 absent           */

/* The identity's SMBIOS part: each string as CmOscpHash gives it. */
typedef struct _CM_OSCP_SMBIOS {
    ULONG BiosVendor;       /* type 0, offset 04h: matched              */
    ULONG BiosVersion;      /* type 0, offset 05h: logged only          */
    ULONG BiosDate;         /* type 0, offset 08h: logged only          */
    ULONG BoardMaker;       /* type 2, offset 04h: matched              */
    ULONG BoardProduct;     /* type 2, offset 05h: matched              */
} CM_OSCP_SMBIOS;

/* The identity's PCI part: vendor in bits 15:0, device in 31:16. */
typedef struct _CM_OSCP_PCI {
    ULONG RootId;           /* the root port above the switch           */
    ULONG UspId;            /* the switch's upstream port               */
    ULONG DspId;            /* the downstream port above the NHI        */
    ULONG NhiId;            /* the NHI                                  */
    ULONG NhiSubsystem;     /* the NHI's dword 2Ch                      */
} CM_OSCP_PCI;

typedef struct _CM_OSCP_PROFILE {
    CM_OSCP_PCI Pci;
    ULONG BiosVendor;
    ULONG BoardMaker;
    ULONG BoardProduct;
    ULONG Data;             /* the handover's data byte                 */
} CM_OSCP_PROFILE;

/* FNV-1a, 32 bits, of `n` bytes with leading and trailing spaces left
 * out (case kept). */
ULONG CmOscpHash(const UCHAR *s, ULONG n);

/*
 * The raw SMBIOS tables inside a WNODE_ALL_DATA chain of the
 * MSSmBios_RawSMBiosTables block (`buf`, `len` bytes, as
 * IoWMIQueryAllData returned it): exactly one instance in the whole
 * chain; its data the block's four bytes, a 32-bit length and that many
 * table bytes. *tables and *tlen on CM_OSCP_OK.
 */
ULONG CmOscpWmiRaw(const UCHAR *buf, ULONG len, const UCHAR **tables,
                   PULONG tlen);

/*
 * The identity in the raw SMBIOS tables (`t`, `len` bytes, the structures
 * alone): type 0 and type 2 each exactly once, every field read inside
 * its structure's formatted length, every string index inside its set,
 * every string at most 64 bytes.
 */
ULONG CmOscpSmbios(const UCHAR *t, ULONG len, CM_OSCP_SMBIOS *out);

/* The profiles read on a bench (cm_oscp.c); the qemu flavour adds the
 * emulator's. */
extern const CM_OSCP_PROFILE CmOscpProfiles[];
extern const ULONG CmOscpProfileCount;

/* The index + 1 of the profile both parts equal, or 0 (unknown); the
 * profile's data byte in *data. */
ULONG CmOscpMatch(const CM_OSCP_PCI *pci, const CM_OSCP_SMBIOS *sm,
                  PULONG data);

/* ---- the boot guard (record 32 section 12.8.15.13) --------------------- */

/* The marker as read at a boot's first start. */
#define CM_OSCG_M_ABSENT        0UL
#define CM_OSCG_M_PRESENT       1UL
#define CM_OSCG_M_UNREADABLE    2UL

/* What the first start does. */
#define CM_OSCG_GO              0UL   /* may send: write the marker first */
#define CM_OSCG_SKIP_CLEAR      1UL   /* skip this boot, delete the marker */
#define CM_OSCG_SKIP            2UL   /* skip this boot, touch nothing     */

ULONG CmOscgAtBoot(ULONG marker);

/* What ends an armed send. */
#define CM_OSCG_E_ROUTER        1UL   /* the router answered               */
#define CM_OSCG_E_INERT         2UL   /* the USB4 start ended inert        */
#define CM_OSCG_E_SHUTDOWN      3UL   /* an orderly system shutdown        */
#define CM_OSCG_E_RESTART       4UL   /* a disable and enable in the boot  */

/* TRUE when the event clears the marker. */
BOOLEAN CmOscgClears(ULONG event);

/* After a clear's delete and flush: what follows. */
#define CM_OSCG_C_DONE          0UL   /* cleared                          */
#define CM_OSCG_C_KEPT          1UL   /* the delete failed: still there   */
#define CM_OSCG_C_LATCH         2UL   /* deleted, the flush failed: latch,
                                         write it back                    */

ULONG CmOscgCleared(BOOLEAN deleted, BOOLEAN flushed);

/* Whether a send may go on: never while the boot's latch is set (a clear
 * whose persistence is unknown, a skip), and only once the marker was
 * written and its key flushed. */
BOOLEAN CmOscgMaySend(BOOLEAN latched, BOOLEAN written, BOOLEAN flushed);

/* ---- the mailbox's members (record 32 section 12.8.15.7) --------------- */

#define CM_OSCP_R_NONE          0UL
#define CM_OSCP_R_USP           1UL   /* the switch's upstream port        */
#define CM_OSCP_R_DSP           2UL   /* one of its four downstream ports  */
#define CM_OSCP_R_XHCI          3UL   /* the switch's xHCI                 */

#define CM_OSCP_ID_USP          0x24211B21UL
#define CM_OSCP_ID_DSP          0x24231B21UL
#define CM_OSCP_ID_XHCI         0x24261B21UL
#define CM_OSCP_ID_NHI          0x24251B21UL

/*
 * The member role of a PDO this driver's AddDevice is called for, from
 * its ids (vendor, device), whether its class is a bridge's, whether the
 * PCI driver's FDO is already above it (an upper filter's call on a
 * bridge) and whether any device is (an upper filter's call on a
 * function another driver owns): the upstream port as a bridge's upper
 * filter, the xHCI as its function driver's upper filter; a downstream
 * port's member is the bridge filter's (CM_OSCP_R_DSP for its ids alone).
 */
ULONG CmOscpMemberRole(ULONG vendor, ULONG device, BOOLEAN bridge,
                       BOOLEAN pciAbove, BOOLEAN anyAbove);

/* What the exchange needs of a member, a snapshot under the list's lock. */
typedef struct _CM_OSCP_MEMBER {
    ULONG Role;
    ULONG Bdf;
    ULONG Located;
    ULONG Started;
    ULONG Closed;
    ULONG Gone;
} CM_OSCP_MEMBER;

/*
 * Coverage: each of the `n` functions wanted (wantBdf[i], wantRole[i])
 * has a member of that role at that location, started, neither closed
 * nor gone. TRUE, or FALSE with *missing the first one that has none.
 */
BOOLEAN CmOscpCovered(const ULONG *wantBdf, const ULONG *wantRole, ULONG n,
                      const CM_OSCP_MEMBER *have, ULONG haveCount,
                      PULONG missing);

/* The S4 obligation (record 32 section 12.8.13.6, Q16 (a)) after a system
 * SET_POWER to `state` (SYSTEM_POWER_STATE's numbering: 1 working, 2 to 4
 * sleeping, 5 hibernate, 6 shutdown) completed successfully: owed after a
 * hibernate request, not after any other sleep, unchanged otherwise. */
ULONG CmOscpOwed(ULONG owed, ULONG state);

/* ---- the read path, B, C and D (record 32 section 12.8.15.17) -------- */

/* A range of the memory the system manages, as the range routines give
 * it: 64-bit base and length in bytes, each as two 32-bit halves. */
typedef struct _CM_OSCP_RANGE {
    ULONG BaseLo;
    ULONG BaseHi;
    ULONG BytesLo;
    ULONG BytesHi;
} CM_OSCP_RANGE;

#define CM_OSCP_RANGES_MAX      256UL

/*
 * The page refusal: TRUE when any byte of the pages that `bytes` at
 * `phys` span (from the first byte's page start to the last byte's page
 * end) lies in one of the `n` ranges (the caller stopped at the zero pair).
 * A range whose base is at or above 4 GB is ignored; one ending at or
 * above 4 GB is taken to 4 GB. A read of 0 bytes, or one past FFFFFFFFh,
 * is TRUE (refused). No 64-bit arithmetic.
 */
BOOLEAN CmOscpRangeHit(const CM_OSCP_RANGE *r, ULONG n, ULONG phys,
                       ULONG bytes);

/* The routes and the outcome of a step. */
#define CM_OSCP_RT_NONE         0UL
#define CM_OSCP_RT_B            1UL
#define CM_OSCP_RT_C            2UL
#define CM_OSCP_RT_D            3UL

#define CM_OSCP_S_OBTAINED      0UL
#define CM_OSCP_S_ABSENT        1UL
#define CM_OSCP_S_REFUSED       2UL

/* What the route's choice reads of the system. */
#define CM_OSCP_HAVE_EX2        0x01UL  /* MmGetPhysicalMemoryRangesEx2 */
#define CM_OSCP_HAVE_RANGES     0x02UL  /* MmGetPhysicalMemoryRanges    */
#define CM_OSCP_HAVE_FWENV      0x04UL  /* ExGetFirmwareEnvironmentVariable */
#define CM_OSCP_HAVE_QUERY      0x08UL  /* ZwQuerySystemInformation     */
#define CM_OSCP_SYS_130         0x10UL  /* IoIsWdmVersionAvailable(1, 0x30) */
#define CM_OSCP_SYS_X86         0x20UL  /* the x86 image                 */

/*
 * The first route of an attempt: B when Ex2 is there (its boot test and
 * scan then decide whether C follows), C on WDM 1.30 and later without
 * Ex2, D on the x86 image below WDM 1.30; CM_OSCP_RT_NONE when none
 * applies. *need the CM_OSCP_HAVE_* bits the route requires (a bit
 * missing from `have` is that route's refusal, never another route).
 */
ULONG CmOscpReadRoute(ULONG have, PULONG need);

/*
 * After B's step: `boot` CM_OSCP_S_OBTAINED a BIOS boot, ABSENT a UEFI
 * boot; `scan` (BIOS boot only) OBTAINED a root pointer, ABSENT none in
 * admitted memory, REFUSED a page refused. Returns CM_OSCP_RT_B (B
 * serves), CM_OSCP_RT_C (C serves), or CM_OSCP_RT_NONE (refused).
 */
ULONG CmOscpAfterB(ULONG boot, ULONG scan);

/*
 * Class 76's answer for one table (C): `ok` the status a success,
 * `notFound` the status the "not found" one, `returned` the
 * TableBufferLength it gave, `cap` the payload bound asked with, `table`
 * the payload (at least min(returned, cap) bytes readable). Returns
 * CM_OSCP_S_OBTAINED (its length 36 to cap, equal to the header's Length,
 * the bytes summing to zero), CM_OSCP_S_ABSENT (not found), else
 * CM_OSCP_S_REFUSED.
 */
ULONG CmOscpFwTable(BOOLEAN ok, BOOLEAN notFound, ULONG returned, ULONG cap,
                    const UCHAR *table);

/*
 * The system gate (12.8.15.17): `haveVer` RtlGetVersion found, `build`
 * its build; `nt5` IoIsWdmVersionAvailable(1, 0x20). TRUE when the
 * handover may go on: a build below 22000, or no RtlGetVersion on
 * Windows 2000 alone.
 */
BOOLEAN CmOscpSystem(BOOLEAN haveVer, ULONG build, BOOLEAN nt5);

/* ---- the board decided once a start (record 32 section 12.8.15.16) ---- */

/* What a start keeps of its board's decision. */
#define CM_OSCP_K_NONE          0UL   /* nothing decided this start      */
#define CM_OSCP_K_UNKNOWN       1UL   /* E's match: the board unknown    */
#define CM_OSCP_K_SEND          2UL   /* a decision to send, Tier, Data  */

/* The key's test this try. */
#define CM_OSCP_F_UNTESTED      0UL
#define CM_OSCP_F_OK            1UL
#define CM_OSCP_F_FAILED        2UL

/* The next step of the sequence (CmOscpBoardNext). */
#define CM_OSCP_B_MATCH         0UL   /* E's match                       */
#define CM_OSCP_B_UNKNOWN       1UL   /* done: unknown, reads 0          */
#define CM_OSCP_B_LATCHED       2UL   /* refused: the boot's latch, 10Fh */
#define CM_OSCP_B_PROBE         3UL   /* the mailbox probe               */
#define CM_OSCP_B_FLUSH         4UL   /* the key's test                  */
#define CM_OSCP_B_DEFER         5UL   /* defer: the key not flushable    */
#define CM_OSCP_B_READ          6UL   /* the read path                   */
#define CM_OSCP_B_SEND          7UL   /* the send                        */
#define CM_OSCP_B_ECAM          8UL   /* the ECAM base found (12.8.15.18) */
#define CM_OSCP_B_NOECAM        9UL   /* refused: ECAM needs reads 1, 113h */

/*
 * The mailbox probe's outcome from what it read, under its own hold:
 * `held` the hold had within its wait, `usable` the upstream port's
 * member started with its interface held, `aborted` a surprise removal
 * or the NHI's revocation during it, `route` the port's id and secondary
 * bus as found by bus, `d0` the port in D0, `aliased` CmOscpAliased's
 * answer, `mbRead` the command dword at 420h read with its whole length,
 * `mb` its value. Returns 0 (passed), CM_OSCP_P_DEFER with *why the
 * reason (10Eh, 10Ah, 10Bh), or the refusal (10Bh, 104h, 112h, 109h;
 * *why the same).
 */
#define CM_OSCP_P_DEFER         1UL

ULONG CmOscpProbe(BOOLEAN held, BOOLEAN usable, BOOLEAN aborted,
                  BOOLEAN route, BOOLEAN d0, BOOLEAN aliased, BOOLEAN mbRead,
                  ULONG mb, PULONG why);

/*
 * TRUE when the route seems to drop the offset's high bits, so that an
 * access to the extended configuration space lands in the first 256 bytes
 * (Windows 2000's, read on the emulator: 420h read the bridge's dword 20h):
 * dword 100h equal to dword 0, or dword 420h equal to dword 20h, as the
 * upstream port's own interface read them. A coincidence on a route that
 * does reach the extended space only refuses (no write is ever made).
 */
BOOLEAN CmOscpAliased(ULONG d000, ULONG d100, ULONG d020, ULONG d420);

/*
 * The system gate on the extended configuration space (12.8.15.16): TRUE
 * when the bus interface may be asked for an offset of 100h or more at
 * all. `haveVer` RtlGetVersion found: FALSE on Windows 2000, whose route
 * was read aliasing such an offset into the first 256 bytes (the
 * emulator, 2026-11-04), whatever a probe would read there.
 */
BOOLEAN CmOscpExtAllowed(BOOLEAN haveVer);

/* ---- ECAM on 2000, 32-bit XP and XP x64 (record 32 section 12.8.15.18) -- */

/*
 * The systems whose mailbox is reached through ECAM alone: `haveVer`
 * RtlGetVersion found (none: Windows 2000, the system gate having
 * admitted only it), `major` and `minor` its version, `amd64` the amd64
 * image. TRUE for 2000, for 5.1 on x86 (32-bit XP) and for 5.2 on amd64
 * (XP x64, and Server 2003 x64, the same system); FALSE for 5.2 on x86
 * and for every later system.
 */
BOOLEAN CmOscpEcamSystem(BOOLEAN haveVer, ULONG major, ULONG minor,
                         BOOLEAN amd64);

/*
 * The offsets the ECAM page may be accessed at: TRUE for a write at
 * 420h or 430h alone, a read at those and at 0, 08h, 18h, 20h and 100h
 * (the check against the bus interface and the alias test).
 */
BOOLEAN CmOscpEcamOffset(ULONG offset, BOOLEAN write);

/* The ECAM page's check, in the order made (CmOscpEcamCheck's answer). */
#define CM_OSCP_E_OK            0UL
#define CM_OSCP_E_ID            1UL   /* ECAM's dword 0 not 1B21:2421      */
#define CM_OSCP_E_DIFFERS       2UL   /* a dword unequal to the route's    */
#define CM_OSCP_E_CLASS         3UL   /* dword 08h's class not 0604h       */
#define CM_OSCP_E_BUS           4UL   /* dword 18h's secondary bus          */
#define CM_OSCP_E_ALIAS         5UL   /* the alias test on the page         */

/*
 * The ECAM page is the upstream port the route reads: `route` and
 * `ecam` the dwords at 0, 08h, 18h and 20h through the bus interface
 * and through the page, `sec` the secondary bus found by bus, `d100` and
 * `d420` the page's dwords 100h and 420h. CM_OSCP_E_OK or the first test
 * that failed (the refusal 115h).
 */
ULONG CmOscpEcamCheck(const ULONG *route, const ULONG *ecam, ULONG sec,
                      ULONG d100, ULONG d420);

/*
 * CmOscpBoardNext with ECAM: `ecam` the system's mailbox is reached
 * through ECAM alone (CmOscpEcamSystem), `ecamKept` the base found this
 * start. On such a system a board decided to send with `reads` 0 is
 * CM_OSCP_B_NOECAM; with `reads` 1, the key's test (CM_OSCP_B_FLUSH,
 * then CM_OSCP_B_DEFER when it failed) comes after the latch and before
 * any firmware read, then CM_OSCP_B_ECAM until the base is kept, then the
 * probe. With `ecam` FALSE the
 * answer is CmOscpBoardNext's.
 */
ULONG CmOscpBoardNextEx(ULONG kept, BOOLEAN reads, BOOLEAN latched,
                        BOOLEAN ecam, BOOLEAN ecamKept, BOOLEAN probed,
                        ULONG flush);

/*
 * CmOscpProbe with the ECAM page's outcome: `ecamWhy` 0 where no page is
 * used or it passed, else its refusal (114h the page not had, 115h not
 * the port), judged after the port's D0 and before the mailbox's read.
 */
ULONG CmOscpProbeEx(BOOLEAN held, BOOLEAN usable, BOOLEAN aborted,
                    BOOLEAN route, BOOLEAN d0, ULONG ecamWhy,
                    BOOLEAN aliased, BOOLEAN mbRead, ULONG mb, PULONG why);

/* The decision a start keeps, and the host it was made for. */
typedef struct _CM_OSCP_KEPT {
    ULONG State;            /* CM_OSCP_K_*                               */
    ULONG Probed;           /* the mailbox probe passed this start       */
    ULONG NhiBdf;
    ULONG DspBdf;
    ULONG UspBdf;
    ULONG RootBdf;
    CM_OSCP_PCI Pci;
    ULONG Tier;             /* CM_OSCP_K_SEND: the status record's tier  */
    ULONG Data;             /* CM_OSCP_K_SEND: the command's data byte   */
    ULONG Ecam;             /* the ECAM base found this start (12.8.15.18) */
    ULONG EcamBase;         /* ... MCFG's base of bus 0                  */
    ULONG EcamEnd;          /* ... and its last bus                      */
} CM_OSCP_KEPT;

/*
 * The step after what is kept (`kept` CM_OSCP_K_*), `reads`
 * XhciCmAsm4242Reads 1, `latched` the boot's latch set, `probed` the
 * mailbox probe passed this start, `flush` this try's key test
 * (CM_OSCP_F_*): a CM_OSCP_B_*. The caller runs the step, records its
 * outcome and asks again; a probe's or a read's refusal ends the
 * sequence in the caller.
 */
ULONG CmOscpBoardNext(ULONG kept, BOOLEAN reads, BOOLEAN latched,
                      BOOLEAN probed, ULONG flush);

/*
 * TRUE when `k` may be used for the host found by bus now (`now`'s four
 * addresses and Pci): nothing kept yet, or every address and id equal.
 * FALSE (the start's refusal, 111h) when something was kept for another.
 */
BOOLEAN CmOscpKept(const CM_OSCP_KEPT *k, const CM_OSCP_KEPT *now);

#endif /* CM_OSCP_H */
