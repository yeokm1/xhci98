/*
 * xhcitr.c - XHCITR.EXE (32-bit) and XHCITR64.EXE (x64): the USB and
 * Thunderbolt tree with eject and tray icon (roadmap-usb4.md tasks 41-A.3
 * to 41-A.5; design record 25).
 *
 * WHAT IT DOES. One window laid out like a USB viewer: every USB host
 * controller Windows reports, its hubs, ports and devices with their
 * descriptors (section 5), and every Thunderbolt 3 / USB4 host interface
 * xhcicm.sys drives with its ports, routers, tunnels and the Windows
 * devices behind each tunnel (section 3). A unit behind a tunnel can be
 * stopped from the window or the notification-area icon, under record
 * 24's rules and the transaction of section 6.2. The icon polls
 * xhcicm.sys's query device once a second and says what changed (section
 * 7).
 *
 * RIGHTS. It runs as the invoker (xhcitr.manifest). Every device it opens
 * is opened with no access rights, and every IOCTL it sends is
 * FILE_ANY_ACCESS; the registry is read only. An eject Windows refuses for
 * want of rights is offered elevated on Vista and 7 (section 6.4).
 *
 * TARGETS AND BUILDS. ANSI C89, built with the in-repo MSVC 6.0 as
 * XHCITR.EXE (/SUBSYSTEM:WINDOWS,4.0) and with WDK 7.1's amd64 compiler as
 * XHCITR64.EXE (build.cmd). The import table names KERNEL32, USER32,
 * GDI32, ADVAPI32, SHELL32, COMCTL32 and COMDLG32 alone (check-imports.ps1);
 * every SetupAPI and configuration-manager routine, the security routines
 * and shell32's version are looked up at run time, so the one image starts
 * on Windows 98 SE. The pure half is tr_core.c (host suite test\test_tr.c);
 * record 24's unit rules and eject texts are xhciinst\inst_core.c's.
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#ifdef _WIN64
#define _WIN32_IE 0x0500
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <setupapi.h>
#include <dbt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/xhci_version.h"
#include "../xhciinst/inst_core.h"
#include "../src/cm/cm_status.h"
#include "../src/cm/cm_query.h"
#include "tr_core.h"
#include "tr_res.h"

#define XHCITR_VERSION XHCI_VER_STR

#ifdef _WIN64
#define TR_MY_NAME          "XHCITR64.EXE"
typedef NOTIFYICONDATAA TR_NID;
#define TR_NID_V1           NOTIFYICONDATAA_V1_SIZE
#define TR_NID_V2           NOTIFYICONDATAA_V2_SIZE
#define TR_NID_VERSION(n)   ((n).uVersion)
#else
#define TR_MY_NAME          "XHCITR.EXE"
typedef TR_NIDA TR_NID;
#define TR_NID_V1           TR_NIDA_V1_BYTES
#define TR_NID_V2           TR_NIDA_V2_BYTES
#define TR_NID_VERSION(n)   ((n).uTimeoutOrVersion)
#endif

/* ---- numbers not in MSVC 6.0's SDK headers -------------------------------- */

#ifndef NIF_INFO
#define NIF_INFO            0x00000010
#endif
#ifndef NIM_SETVERSION
#define NIM_SETVERSION      0x00000004
#endif
#ifndef NIIF_INFO
#define NIIF_INFO           0x00000001
#define NIIF_WARNING        0x00000002
#endif
#ifndef DIGCF_DEVICEINTERFACE
#define DIGCF_DEVICEINTERFACE 0x00000010
#endif

#define TR_CR_SUCCESS               0x00000000UL
#define TR_CR_NO_SUCH_DEVINST       0x0000000DUL
#define TR_CR_NO_SUCH_VALUE         0x00000025UL
#define TR_CM_DRP_DEVICEDESC        0x00000001UL
#define TR_CM_DRP_HARDWAREID        0x00000002UL
#define TR_CM_DRP_COMPATIBLEIDS     0x00000003UL
#define TR_CM_DRP_SERVICE           0x00000005UL
#define TR_CM_DRP_CLASS             0x00000008UL
#define TR_CM_DRP_DRIVER            0x0000000AUL
#define TR_CM_DRP_FRIENDLYNAME      0x0000000DUL
#define TR_IOCTL_STORAGE_GET_DEVICE_NUMBER 0x002D1080UL
#define TR_CM_DRP_CAPABILITIES      0x00000010UL
#define TR_CM_DRP_BUSNUMBER         0x00000016UL
#define TR_CM_DRP_ADDRESS           0x0000001DUL
#define TR_CM_DEVCAP_REMOVABLE      0x00000004UL
#define TR_CM_DEVCAP_SURPRISEOK     0x00000080UL
#define TR_DN_REMOVABLE             0x00004000UL
#define TR_UI_NOT_OK                0x00000001UL
#define TR_LIVE_ACTIVE              1UL     /* CM_LIVE_ACTIVE (cm_live.h) */

#define TR_MSZ          1024
#define TR_MAX_DEVS     3072
#define TR_MAX_TNODES   4096
#define TR_MAX_CTRL     64
#define TR_MAX_UPORTS   1024
#define TR_NAME_LEN     320
#define TR_DETAIL_BYTES 65536
#define TR_EJ_DEVS      64
#define TR_PROP_SCRATCH 8192

#define WM_TRAYICON     (WM_APP + 1)
#define TIMER_POLL      1
#define TIMER_CHANGE    2
#define TIMER_CLICK     3

/* GUID_DEVINTERFACE_DISK */
static const GUID trGuidDisk = {
    0x53F56307L, 0xB6BF, 0x11D0, { 0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B }
};

/* GUID_DEVINTERFACE_USB_HOST_CONTROLLER */
static const GUID trGuidHc = {
    0x3ABF6F2DL, 0x71C4, 0x462A, { 0x8A, 0x92, 0x1E, 0x68, 0x61, 0xE6, 0xAF, 0x27 }
};

/* ---- the routines, looked up at run time ---------------------------------- */

typedef DWORD (WINAPI *PFN_CM_LOCATE)(DWORD *, char *, ULONG);
typedef DWORD (WINAPI *PFN_CM_STATUS)(ULONG *, ULONG *, DWORD, ULONG);
typedef DWORD (WINAPI *PFN_CM_REL)(DWORD *, DWORD, ULONG);
typedef DWORD (WINAPI *PFN_CM_ID)(DWORD, char *, ULONG, ULONG);
typedef DWORD (WINAPI *PFN_CM_PROP)(DWORD, ULONG, ULONG *, void *, ULONG *,
                                    ULONG);
typedef DWORD (WINAPI *PFN_CM_QREMOVE)(DWORD, ULONG);
typedef DWORD (WINAPI *PFN_CM_EJECT)(DWORD, PULONG, LPSTR, ULONG, ULONG);
typedef DWORD (WINAPI *PFN_CM_QANDR_W)(DWORD, PULONG, LPWSTR, ULONG, ULONG);
typedef DWORD (WINAPI *PFN_CM_LISTSIZE)(PULONG, PCSTR, ULONG);
/* LOG_CONF and RES_DES are DWORD_PTR: 8 bytes on x64. */
#ifdef _WIN64
typedef unsigned __int64 TR_CMH;
#else
typedef unsigned long TR_CMH;
#endif
typedef DWORD (WINAPI *PFN_CM_FIRSTLC)(TR_CMH *, DWORD, ULONG);
typedef DWORD (WINAPI *PFN_CM_NEXTRD)(TR_CMH *, TR_CMH, ULONG, ULONG *, ULONG);
typedef DWORD (WINAPI *PFN_CM_RDSIZE)(ULONG *, TR_CMH, ULONG);
typedef DWORD (WINAPI *PFN_CM_RDDATA)(TR_CMH, PVOID, ULONG, ULONG);
typedef DWORD (WINAPI *PFN_CM_FREEH)(TR_CMH);
typedef DWORD (WINAPI *PFN_CM_LIST)(PCSTR, PCHAR, ULONG, ULONG);
typedef HDEVINFO (WINAPI *PFN_GETCLASSDEVS)(const GUID *, PCSTR, HWND, DWORD);
typedef BOOL (WINAPI *PFN_ENUMIFACE)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *,
                                     DWORD, PSP_DEVICE_INTERFACE_DATA);
typedef BOOL (WINAPI *PFN_IFACEDETAIL)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA,
                                       PSP_DEVICE_INTERFACE_DETAIL_DATA_A,
                                       DWORD, PDWORD, PSP_DEVINFO_DATA);
typedef BOOL (WINAPI *PFN_DESTROY)(HDEVINFO);

static struct {
    HMODULE SetupApi;
    HMODULE CfgMgr;
    PFN_CM_LOCATE Locate;
    PFN_CM_STATUS Status;
    PFN_CM_REL Child;
    PFN_CM_REL Sibling;
    PFN_CM_REL Parent;
    PFN_CM_ID DeviceId;
    PFN_CM_PROP Property;
    PFN_CM_QREMOVE QueryRemove;
    PFN_CM_QREMOVE Remove;
    PFN_CM_EJECT Eject;
    PFN_CM_QANDR_W QueryAndRemove;
    PFN_CM_LISTSIZE ListSize;   /* NT: a devnode's removal relations       */
    PFN_CM_LIST List;
    PFN_CM_FIRSTLC FirstLogConf;    /* the resources (both families)     */
    PFN_CM_NEXTRD NextResDes;
    PFN_CM_RDSIZE ResDesSize;
    PFN_CM_RDDATA ResDesData;
    PFN_CM_FREEH FreeLogConf;
    PFN_CM_FREEH FreeResDes;
    PFN_GETCLASSDEVS GetClassDevs;
    PFN_ENUMIFACE EnumIface;
    PFN_IFACEDETAIL IfaceDetail;
    PFN_DESTROY Destroy;
} api;

/* ---- the model ----------------------------------------------------------- */

typedef struct _DNODE {
    DWORD Dn;
    int Parent;
    char Id[INST_ID_CHARS];
    char Desc[128];
    char Service[64];
    char Driver[128];
    char Hw[TR_MSZ];
    DWORD HwBytes;
    char Compat[TR_MSZ];
    DWORD CompatBytes;
    ULONG Status;
    ULONG Problem;
    ULONG Caps;
    int HasCaps;
    ULONG Bus;
    ULONG Addr;
    int HasLoc;
    int IsNhi;
    int IsUsbHc;
    int Candidate;
    int PortNhi;            /* a tunnel port of this NHI, by location, -1 */
    int PortIdx;
    int Verified;           /* ... of an NHI whose query answered          */
    int Prot;
    int UnitOf;
    int Ejectable;          /* a unit, not blocked                          */
    int IsDisk;             /* setup class DiskDrive                        */
    char Friendly[128];     /* a disk's friendly name, where it has one     */
    char Letters[TR_LETTERS_BYTES]; /* a disk's drive letters, where found  */
    char Cls[32];           /* the setup class                              */
    int ReadOk;             /* every property read whole                    */
    int Incomplete;         /* its children were not all read (the walk)    */
    int IsVolume;           /* setup class Volume                           */
    int IsHid;              /* an input device (tr_node_is_hid)             */
    int IsHubId;            /* its ids say a hub                            */
    int UPort;              /* the USB port it is the device of, or -1      */
    int UsbRule;            /* TR_UE_* for that device (section 6.5)        */
    const char *UsbWhy;
    int UsbOf;              /* the USB device whose set holds it, or -1     */
} DNODE;

typedef struct _WALK {
    DNODE *N;
    int Count;
    int Bad;
} WALK;

typedef struct _NHI {
    char Id[INST_ID_CHARS];
    int Node;
    char QName[24];
    int NameUnique;         /* no other NHI of the reading has QName       */
    int ByName;             /* the answer had no instance ID (98 SE, ME)   */
    unsigned char *QBuf;
    TR_QUERY Q;
    int HaveQ;              /* the query answered and names this NHI       */
    unsigned char Rec[CM_ST_MAX_BYTES];
    int HaveRec;            /* the registry record, read only              */
    const unsigned char *R; /* the record shown                            */
    char Why[160];
} NHI;

typedef struct _UCTRL {
    char Path[TR_NAME_LEN];
    char Hcd[16];
    char DrvKey[128];
    int Dn;
    char RootHub[TR_NAME_LEN];
    unsigned char Node[TR_NI_BYTES];
    int HaveNode;
    int Ours;               /* xhci98 drives it                            */
    int RootErr;            /* its root hub: 1 not opened, 2 no answer     */
    char Behind[TR_NAME_CHARS + 16];
} UCTRL;

typedef struct _UPORT {
    int Ctrl;
    int ParentUp;               /* the hub's own port, -1 for the root hub  */
    int HubErr;                 /* a hub: 1 not opened, 2 no node answer    */
    char Hub[TR_NAME_LEN];      /* the hub to ask about this port           */
    char Path[64];
    TR_CONN Conn;
    int HaveConn;
    int Ex;
    int HaveV2;
    TR_U32 V2Proto;
    TR_U32 V2Flags;
    char DrvKey[128];
    int Dn;
    unsigned char *Cfg;
    DWORD CfgN;
    int CfgTried;
    char HubName[TR_NAME_LEN];
    unsigned char HubNode[TR_NI_BYTES];
    int HaveHubNode;
    int Fetched;                /* strings and BOS asked                    */
    unsigned char *Bos;
    DWORD BosN;
    char Str[3][128];
    unsigned char StrRaw[3][256];
    DWORD StrN[3];
    int HaveStr[3];
} UPORT;

#define K_ROOT      0
#define K_TBROOT    1
#define K_NHI       2
#define K_PORT      3
#define K_ROUTER    4
#define K_TUNNEL    5
#define K_DEV       6
#define K_USBROOT   7
#define K_CTRL      8
#define K_ROOTHUB   9
#define K_UPORT     10
#define K_NOTE      11

typedef struct _TNODE {
    int Parent;
    int Kind;
    int A;
    int B;
    int C;
    int Dn;
    char Label[200];
    char Key[280];
    HTREEITEM Item;
} TNODE;

static HINSTANCE hInst;
static int envNt;
static DWORD envMajor;
static DWORD envMinor;
static TR_OPTS opts;

static WALK gw;
static NHI nhi[TR_SNAP_NHI];
static int nhiCount;
static UCTRL *uc;
static int ucCount;
static UPORT *up;
static int upCount;
static TNODE *tn;
static int tnCount;
static int usbDevices;
static DWORD hcdOpenError;      /* why \\.\HCD0 did not open, 0 when it did */
static int hcdNameRefused;      /* controllers that did not name their key */
static int tbDevices;
static DWORD viewFlags = TR_V_DEFAULT;
static char detail[TR_DETAIL_BYTES];

static HWND hTray;
static HWND hMain;
static HWND hTree;
static HWND hEdit;
static HWND hStatus;
static HFONT hFixed;
static HACCEL hAccel;
static HICON hIconBig;
static HICON hIconSmall;
static int splitX = 300;     /* as the user set it, stored             */
static int drawnSplit = 300; /* as drawn: splitX clamped to the window  */
static int dragging;
static UINT msgTaskbarCreated;
static UINT msgShow;
static int shellV5;
static int iconAdded;
static int trayNoClick;     /* the button-up after a double click */
static int trayV3;          /* NIM_SETVERSION 3 took: WM_CONTEXTMENU comes */
static TR_SNAP snapOld;
/* The poll's last answer per NHI, kept apart from the model the tree and
 * the details were built from (they change only with a refresh). */
static unsigned char *pollBuf[TR_SNAP_NHI];
static TR_QUERY pollQ[TR_SNAP_NHI];
static char pollId[TR_SNAP_NHI][INST_ID_CHARS];
static int viewStale;       /* the poll saw a change the tree does not show */
static int pollCount;
static char lastEvent[128];
static char trayUnitIds[64][INST_ID_CHARS];
static int trayUnitCount;
static int quitting;
static int firstBuild = 1;
static int reportMode;     /* a report decodes every descriptor */
/* Device-change broadcasts counted by the watcher thread (record 25
 * section 6.5), and the count read just before the model was. */
static volatile LONG chgCount;
static LONG modelGen;
static int watchOk;

/* ======================================================================== */
/* Small helpers                                                             */
/* ======================================================================== */

static const char *ej_why_not(int u);
static void usb_label(int d, char *out, size_t cap);
static const char *usb_suffix(int d);
static int usb_listed(int d);
static const char *usb_why(int d);
static void details_usb_eject(TR_TEXT *t, int d);
static void usb_derive(void);
static void say(const char *text, int warn);

static FARPROC proc(HMODULE module, const char *name)
{
    return module != NULL ? GetProcAddress(module, name) : NULL;
}

static void load_routines(void)
{
    api.SetupApi = LoadLibraryA("setupapi.dll");
    api.CfgMgr = LoadLibraryA("cfgmgr32.dll");
    api.Locate = (PFN_CM_LOCATE)proc(api.CfgMgr, "CM_Locate_DevNodeA");
    api.Status = (PFN_CM_STATUS)proc(api.CfgMgr, "CM_Get_DevNode_Status");
    api.Child = (PFN_CM_REL)proc(api.CfgMgr, "CM_Get_Child");
    api.Sibling = (PFN_CM_REL)proc(api.CfgMgr, "CM_Get_Sibling");
    api.Parent = (PFN_CM_REL)proc(api.CfgMgr, "CM_Get_Parent");
    api.DeviceId = (PFN_CM_ID)proc(api.CfgMgr, "CM_Get_Device_IDA");
    api.Property = (PFN_CM_PROP)proc(api.CfgMgr, "CM_Get_DevNode_Registry_PropertyA");
    api.QueryRemove = (PFN_CM_QREMOVE)proc(api.CfgMgr, "CM_Query_Remove_SubTree");
    api.Remove = (PFN_CM_QREMOVE)proc(api.CfgMgr, "CM_Remove_SubTree");
    api.FirstLogConf = (PFN_CM_FIRSTLC)proc(api.CfgMgr, "CM_Get_First_Log_Conf");
    api.NextResDes = (PFN_CM_NEXTRD)proc(api.CfgMgr, "CM_Get_Next_Res_Des");
    api.ResDesSize = (PFN_CM_RDSIZE)proc(api.CfgMgr, "CM_Get_Res_Des_Data_Size");
    api.ResDesData = (PFN_CM_RDDATA)proc(api.CfgMgr, "CM_Get_Res_Des_Data");
    api.FreeLogConf = (PFN_CM_FREEH)proc(api.CfgMgr, "CM_Free_Log_Conf_Handle");
    api.FreeResDes = (PFN_CM_FREEH)proc(api.CfgMgr, "CM_Free_Res_Des_Handle");
    api.Eject = (PFN_CM_EJECT)proc(api.CfgMgr, "CM_Request_Device_EjectA");
    if (api.Eject == NULL) {
        /* Windows 2000 exports it from setupapi.dll alone (record 24
         * section 7.2). */
        api.Eject = (PFN_CM_EJECT)proc(api.SetupApi, "CM_Request_Device_EjectA");
    }
    if (envNt) {
        /* The W form: Windows 2000's A form answers
         * CR_CALL_NOT_IMPLEMENTED and does nothing else (record 24
         * section 7.2). */
        api.QueryAndRemove = (PFN_CM_QANDR_W)proc(api.SetupApi,
                                                  "CM_Query_And_Remove_SubTreeW");
        if (api.QueryAndRemove == NULL) {
            api.QueryAndRemove = (PFN_CM_QANDR_W)proc(api.CfgMgr,
                                                      "CM_Query_And_Remove_SubTreeW");
        }
        api.ListSize = (PFN_CM_LISTSIZE)proc(api.CfgMgr, "CM_Get_Device_ID_List_SizeA");
        api.List = (PFN_CM_LIST)proc(api.CfgMgr, "CM_Get_Device_ID_ListA");
        if (api.ListSize == NULL || api.List == NULL) {
            api.ListSize = (PFN_CM_LISTSIZE)proc(api.SetupApi,
                                                 "CM_Get_Device_ID_List_SizeA");
            api.List = (PFN_CM_LIST)proc(api.SetupApi, "CM_Get_Device_ID_ListA");
        }
        api.GetClassDevs = (PFN_GETCLASSDEVS)proc(api.SetupApi, "SetupDiGetClassDevsA");
        api.EnumIface = (PFN_ENUMIFACE)proc(api.SetupApi, "SetupDiEnumDeviceInterfaces");
        api.IfaceDetail = (PFN_IFACEDETAIL)proc(api.SetupApi,
                                                "SetupDiGetDeviceInterfaceDetailA");
        api.Destroy = (PFN_DESTROY)proc(api.SetupApi, "SetupDiDestroyDeviceInfoList");
    }
}

static void join(char *out, size_t cap, const char *a, const char *b)
{
    tr_sprintf(out, cap, "%s\\%s", a, b);
}

/* A string value: 1 read, 0 absent, -1 unreadable. */
static int reg_text(HKEY rootKey, const char *path, const char *name,
                    char *outText, DWORD cap)
{
    HKEY k;
    DWORD t;
    DWORD n = cap - 1;
    LONG r;

    outText[0] = '\0';
    if (RegOpenKeyExA(rootKey, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return -1;
    }
    r = RegQueryValueExA(k, name, NULL, &t, (LPBYTE)outText, &n);
    RegCloseKey(k);
    if (r == ERROR_FILE_NOT_FOUND) {
        outText[0] = '\0';
        return 0;
    }
    if (r != ERROR_SUCCESS || (t != REG_SZ && t != REG_EXPAND_SZ)) {
        outText[0] = '\0';
        return -1;
    }
    outText[n < cap ? n : cap - 1] = '\0';
    return 1;
}

static const char *first_hw(const DNODE *d)
{
    return d->Hw[0] ? d->Hw : d->Id;
}

static int dn_started(const DNODE *d)
{
    return (d->Status & INST_DN_STARTED) != 0 && d->Problem == 0;
}

static const char *dn_name(const DNODE *d)
{
    return d->Desc[0] ? d->Desc : first_hw(d);
}

/* The driver (software) key of a devnode, relative to HKLM. */
static void driver_key_path(const DNODE *d, char *path, size_t cap)
{
    join(path, cap, envNt ? "SYSTEM\\CurrentControlSet\\Control\\Class"
                          : "System\\CurrentControlSet\\Services\\Class",
         d->Driver);
}

/* ======================================================================== */
/* The device tree (record 24 section 4.1, adapted from xhciinst.c)          */
/* ======================================================================== */

/* A property into buf (cap bytes, two NULs kept). The configuration
 * manager writes into a scratch buffer larger than any field and the
 * result is copied when it fits cap, as the routine answered before: the
 * routine's honouring of the length it is given is not read on 98 SE and
 * ME, and a heap corruption was seen there (T.7 leg). */
static DWORD cm_prop(DWORD dn, ULONG prop, char *buf, DWORD cap, DWORD *bytes)
{
    static char scratch[TR_PROP_SCRATCH];
    ULONG type;
    ULONG n = TR_PROP_SCRATCH - 64;
    DWORD cr = 0xFFFFFFFFUL;

    memset(buf, 0, cap);
    memset(scratch, 0, sizeof(scratch));
    if (api.Property != NULL && cap >= 2) {
        cr = api.Property(dn, prop, &type, scratch, &n, 0);
    }
    if (cr != TR_CR_SUCCESS || n > TR_PROP_SCRATCH - 64) {
        n = 0;
    } else {
        if (n > cap - 2) {
            cr = 0x0000001AUL;      /* CR_BUFFER_SMALL, as before */
            n = 0;
        } else {
            memcpy(buf, scratch, n);
        }
    }
    if (bytes != NULL) {
        *bytes = n;
    }
    return cr;
}

static DWORD dword_prop(DWORD dn, ULONG prop, ULONG *value)
{
    ULONG type;
    ULONG n = sizeof(ULONG);

    if (api.Property == NULL) {
        return 0xFFFFFFFFUL;
    }
    return api.Property(dn, prop, &type, value, &n, 0);
}

static int prop_ok(DWORD cr)
{
    return cr == TR_CR_SUCCESS || cr == TR_CR_NO_SUCH_VALUE;
}

/* 98 and ME: a value of HKLM\Enum\<id>, the authority for the id lists
 * (the configuration manager's answer stops at the first id there). */
static int prop9x(DWORD dn, const char *id, ULONG prop, const char *name,
                  char *buf, DWORD cap)
{
    char path[INST_ID_CHARS + 16];
    char tmp[TR_MSZ];
    int r;

    /* 0xFFFF names a value the registry alone holds: no call for it. */
    if (prop != 0xFFFFUL) {
        cm_prop(dn, prop, buf, cap, NULL);
    } else {
        memset(buf, 0, cap);
    }
    tr_sprintf(path, sizeof(path), "Enum\\%s", id);
    r = reg_text(HKEY_LOCAL_MACHINE, path, name, tmp, cap - 2 < sizeof(tmp)
                                                       ? cap - 2 : sizeof(tmp));
    if (r > 0) {
        memset(buf, 0, cap);
        tr_copy(buf, tmp, cap - 2);
    }
    return r >= 0 || buf[0] != '\0';
}

static int dn_read(DNODE *t)
{
    ULONG status;
    ULONG problem;
    DWORD bytes;
    int ok = 1;

    if (api.DeviceId(t->Dn, t->Id, sizeof(t->Id), 0) != TR_CR_SUCCESS) {
        t->Id[0] = '\0';
        return 0;
    }
    if (envNt) {
        if (!prop_ok(cm_prop(t->Dn, TR_CM_DRP_HARDWAREID, t->Hw, sizeof(t->Hw),
                             &bytes))) {
            ok = 0;
        }
        t->HwBytes = bytes;
        if (!prop_ok(cm_prop(t->Dn, TR_CM_DRP_COMPATIBLEIDS, t->Compat,
                             sizeof(t->Compat), &bytes))) {
            ok = 0;
        }
        t->CompatBytes = bytes;
        if (!prop_ok(cm_prop(t->Dn, TR_CM_DRP_DRIVER, t->Driver,
                             sizeof(t->Driver), NULL)) ||
            !prop_ok(cm_prop(t->Dn, TR_CM_DRP_SERVICE, t->Service,
                             sizeof(t->Service), NULL))) {
            ok = 0;
        }
        cm_prop(t->Dn, TR_CM_DRP_DEVICEDESC, t->Desc, sizeof(t->Desc), NULL);
        {
            /* The class decides what is an input device or a disk
             * (record 25 section 6.5): a failed read is a node not read. */
            if (!prop_ok(cm_prop(t->Dn, TR_CM_DRP_CLASS, t->Cls, sizeof(t->Cls),
                                 NULL))) {
                ok = 0;
            }
            t->IsDisk = tr_eq(t->Cls, "DiskDrive");
            t->IsVolume = tr_eq(t->Cls, "Volume");
            if (t->IsDisk) {
                cm_prop(t->Dn, TR_CM_DRP_FRIENDLYNAME, t->Friendly,
                        sizeof(t->Friendly), NULL);
            }
        }
        t->HasCaps = dword_prop(t->Dn, TR_CM_DRP_CAPABILITIES, &t->Caps) ==
                     TR_CR_SUCCESS;
        t->HasLoc = dword_prop(t->Dn, TR_CM_DRP_BUSNUMBER, &t->Bus) ==
                        TR_CR_SUCCESS &&
                    dword_prop(t->Dn, TR_CM_DRP_ADDRESS, &t->Addr) ==
                        TR_CR_SUCCESS;
    } else {
        char path[300];
        char text[64];
        if (!prop9x(t->Dn, t->Id, TR_CM_DRP_HARDWAREID, "HardwareID", t->Hw,
                    sizeof(t->Hw)) ||
            !prop9x(t->Dn, t->Id, TR_CM_DRP_COMPATIBLEIDS, "CompatibleIDs",
                    t->Compat, sizeof(t->Compat)) ||
            !prop9x(t->Dn, t->Id, TR_CM_DRP_DRIVER, "Driver", t->Driver,
                    sizeof(t->Driver))) {
            ok = 0;
        }
        prop9x(t->Dn, t->Id, TR_CM_DRP_DEVICEDESC, "DeviceDesc", t->Desc,
               sizeof(t->Desc));
        {
            char letters[32];
            if (!prop9x(t->Dn, t->Id, TR_CM_DRP_CLASS, "Class", t->Cls,
                        sizeof(t->Cls))) {
                ok = 0;
            }
            t->IsDisk = tr_eq(t->Cls, "DiskDrive");
            if (t->IsDisk) {
                /* The 9x storage stack records the letters it gave the
                 * disk in its Enum key. */
                prop9x(t->Dn, t->Id, TR_CM_DRP_FRIENDLYNAME, "FriendlyName",
                       t->Friendly, sizeof(t->Friendly));
                letters[0] = '\0';
                if (prop9x(t->Dn, t->Id, 0xFFFFUL, "CurrentDriveLetterAssignment",
                           letters, sizeof(letters))) {
                    tr_letters_add(t->Letters, letters);
                }
            }
        }
        t->HwBytes = (DWORD)inst_ids_split(t->Hw, sizeof(t->Hw));
        t->CompatBytes = (DWORD)inst_ids_split(t->Compat, sizeof(t->Compat));
        if (t->Driver[0] != '\0') {
            /* 98 and ME bind a WDM driver by the driver key's NTMPDriver;
             * an IOS miniport is named by PortDriver, a disk by DevLoader. */
            driver_key_path(t, path, sizeof(path));
            if (reg_text(HKEY_LOCAL_MACHINE, path, "NTMPDriver", text,
                         sizeof(text)) > 0) {
                char *dot = strchr(text, '.');
                if (dot != NULL) {
                    *dot = '\0';
                }
                tr_copy(t->Service, text, sizeof(t->Service));
            } else if (reg_text(HKEY_LOCAL_MACHINE, path, "PortDriver", text,
                                sizeof(text)) > 0 ||
                       reg_text(HKEY_LOCAL_MACHINE, path, "DevLoader", text,
                                sizeof(text)) > 0) {
                tr_copy(t->Service, text, sizeof(t->Service));
            }
        }
    }
    tr_tidy_name(t->Desc);
    tr_tidy_name(t->Friendly);
    if (api.Status(&status, &problem, t->Dn, 0) == TR_CR_SUCCESS) {
        t->Status = status;
        t->Problem = (status & INST_DN_HAS_PROBLEM) ? problem : 0;
    } else {
        ok = 0;
    }
    t->IsHid = tr_node_is_hid(t->Id, t->Hw, t->HwBytes, t->Compat, t->CompatBytes,
                              t->Service, t->Cls);
    t->IsHubId = tr_node_is_hub(t->Hw, t->HwBytes, t->Compat, t->CompatBytes);
    t->ReadOk = ok;
    return ok;
}

static void walk_from(WALK *w, DWORD dn, int parent, int depth)
{
    DWORD child;
    DWORD sib;
    DWORD cr;
    int me;

    for (;;) {
        DNODE *t;
        if (w->Count >= TR_MAX_DEVS) {
            w->Bad++;
            if (parent >= 0) {
                w->N[parent].Incomplete = 1;
            }
            return;
        }
        me = w->Count++;
        t = &w->N[me];
        memset(t, 0, sizeof(*t));
        t->Dn = dn;
        t->Parent = parent;
        t->UnitOf = -1;
        t->PortNhi = -1;
        t->UPort = -1;
        t->UsbOf = -1;
        if (!dn_read(t)) {
            /* The tree's root has no registry entry of its own on NT. */
            if (parent >= 0 || t->Id[0] == '\0') {
                w->Bad++;
            }
        }
        cr = api.Child(&child, dn, 0);
        if (cr == TR_CR_SUCCESS) {
            if (depth >= 32) {
                w->Bad++;
                w->N[me].Incomplete = 1;
            } else {
                walk_from(w, child, me, depth + 1);
            }
        } else if (cr != TR_CR_NO_SUCH_DEVINST) {
            w->Bad++;
            w->N[me].Incomplete = 1;
        }
        cr = api.Sibling(&sib, dn, 0);
        if (cr != TR_CR_SUCCESS) {
            if (cr != TR_CR_NO_SUCH_DEVINST) {
                w->Bad++;
                if (parent >= 0) {
                    w->N[parent].Incomplete = 1;
                }
            }
            return;
        }
        dn = sib;
    }
}

/* The whole tree into w; 0 when the routines are missing. */
static int walk(WALK *w)
{
    DWORD root;
    int i;

    w->Count = 0;
    w->Bad = 0;
    if (w->N == NULL) {
        w->N = (DNODE *)calloc(TR_MAX_DEVS, sizeof(DNODE));
        if (w->N == NULL) {
            return 0;
        }
    }
    if (api.Locate == NULL || api.Child == NULL || api.Sibling == NULL ||
        api.DeviceId == NULL || api.Status == NULL ||
        api.Locate(&root, NULL, 0) != TR_CR_SUCCESS) {
        return 0;
    }
    walk_from(w, root, -1, 0);
    for (i = 0; i < w->Count; i++) {
        DNODE *t = &w->N[i];
        t->IsNhi = inst_tb_is_nhi(t->Hw, t->HwBytes, t->Compat, t->CompatBytes,
                                  t->Service);
        t->IsUsbHc = inst_tb_is_usbhc(t->Hw, t->HwBytes, t->Compat,
                                      t->CompatBytes, t->Service);
        t->Candidate = inst_tb_candidate(t->Hw, t->HwBytes);
    }
    return 1;
}

static int walk_find_id(const WALK *w, const char *id)
{
    int i;

    for (i = 0; i < w->Count; i++) {
        if (tr_eq(w->N[i].Id, id)) {
            return i;
        }
    }
    return -1;
}

static int walk_find_driver(const WALK *w, const char *key)
{
    int i;

    if (key[0] == '\0') {
        return -1;
    }
    for (i = 0; i < w->Count; i++) {
        if (tr_eq(w->N[i].Driver, key)) {
            return i;
        }
    }
    return -1;
}

/* ======================================================================== */
/* xhcicm.sys's query and the registry record (record 25 section 4)          */
/* ======================================================================== */

static HANDLE open_device(const char *name)
{
    char path[TR_NAME_LEN + 8];
    HANDLE h;

    if (name[0] == '\\') {
        tr_copy(path, name, sizeof(path));
    } else {
        tr_sprintf(path, sizeof(path), "\\\\.\\%s", name);
    }
    h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE && !envNt) {
        /* 98 SE and ME: a WDM device name may want an access mask; the
         * IOCTLs stay FILE_ANY_ACCESS and nothing is written. */
        h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
    }
    return h;
}

/* The query of one NHI into *buf (allocated, the caller frees), parsed
 * into q, and only when the answer names the NHI the caller expects.
 * 1 when it did. */
static int query_read(const char *qname, const char *expectId,
                      unsigned char **buf, TR_QUERY *q, int nameUnique,
                      int *byName)
{
    HANDLE h;
    DWORD size = CM_Q_ASK_BYTES;
    DWORD got = 0;
    int tries;
    int r = 0;

    *buf = NULL;
    if (qname[0] == '\0') {
        return 0;
    }
    h = open_device(qname);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    for (tries = 0; tries < 3; tries++) {
        unsigned char *b = (unsigned char *)realloc(*buf, size);
        if (b == NULL) {
            break;
        }
        *buf = b;
        got = 0;
        if (!DeviceIoControl(h, CM_Q_IOCTL, NULL, 0, b, size, &got, NULL) ||
            got > size) {
            break;
        }
        r = tr_query_parse(b, got, q);
        if (r == -1 && q->Bytes > size && q->Bytes <= CM_Q_MAX_BYTES) {
            size = q->Bytes;
            r = 0;
            continue;
        }
        break;
    }
    CloseHandle(h);
    if (r == 1 && (expectId == NULL ||
                   !tr_query_matches(q, expectId, nameUnique, byName))) {
        r = 0;
    }
    if (r != 1) {
        free(*buf);
        *buf = NULL;
        memset(q, 0, sizeof(*q));
        return 0;
    }
    return 1;
}

static void nhi_hw_key(const char *id, char *path, size_t cap)
{
    char base[INST_ID_CHARS + 64];

    if (envNt) {
        join(base, sizeof(base), "SYSTEM\\CurrentControlSet\\Enum", id);
        join(path, cap, base, "Device Parameters");
    } else {
        join(path, cap, "Enum", id);
    }
}

static void nhi_free(void)
{
    int k;

    for (k = 0; k < nhiCount; k++) {
        free(nhi[k].QBuf);
        nhi[k].QBuf = NULL;
    }
    nhiCount = 0;
}

/* Each NHI's state: its query when the mapping holds, else the registry
 * record, shown only (record 25 section 4.2). */
static void nhi_read(NHI *h)
{
    char path[INST_ID_CHARS + 96];
    HKEY k;
    DWORD type;
    DWORD n = sizeof(h->Rec);

    free(h->QBuf);
    h->QBuf = NULL;
    h->HaveQ = 0;
    h->HaveRec = 0;
    h->R = NULL;
    h->Why[0] = '\0';
    nhi_hw_key(h->Id, path, sizeof(path));
    if (reg_text(HKEY_LOCAL_MACHINE, path, CM_Q_NAME_VALUE, h->QName,
                 sizeof(h->QName)) <= 0) {
        h->QName[0] = '\0';
    }
    if (h->QName[0] != '\0' &&
        query_read(h->QName, h->Id, &h->QBuf, &h->Q, h->NameUnique, &h->ByName)) {
        h->HaveQ = 1;
        h->R = h->Q.Rec;
        return;
    }
    tr_copy(h->Why, h->QName[0] ? "its query device did not answer for it"
                                : "the driver has no query device for it",
            sizeof(h->Why));
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k) ==
        ERROR_SUCCESS) {
        if (RegQueryValueExA(k, "XhciCmStatus", NULL, &type, h->Rec, &n) ==
                ERROR_SUCCESS &&
            type == REG_BINARY && inst_st_check(h->Rec, n)) {
            h->HaveRec = 1;
            h->R = h->Rec;
        }
        RegCloseKey(k);
    }
}

/* 1 when no NHI devnode of the reading but nhiId carries the query-device
 * name qname: the condition for matching an answer without an instance ID
 * (tr_query_matches). */
static int name_unique(const WALK *w, const char *nhiId, const char *qname)
{
    int i;

    if (qname[0] == '\0') {
        return 0;
    }
    for (i = 0; i < w->Count; i++) {
        char path[INST_ID_CHARS + 96];
        char other[24];
        if (!tr_eq(w->N[i].Service, "xhcicm") || !inst_starts(w->N[i].Id, "PCI\\") ||
            tr_eq(w->N[i].Id, nhiId)) {
            continue;
        }
        nhi_hw_key(w->N[i].Id, path, sizeof(path));
        if (reg_text(HKEY_LOCAL_MACHINE, path, CM_Q_NAME_VALUE, other,
                     sizeof(other)) > 0 &&
            tr_eq(other, qname)) {
            return 0;
        }
    }
    return 1;
}

static void nhi_find(const WALK *w)
{
    int i;

    nhi_free();
    for (i = 0; i < w->Count && nhiCount < TR_SNAP_NHI; i++) {
        if (tr_eq(w->N[i].Service, "xhcicm") && inst_starts(w->N[i].Id, "PCI\\")) {
            NHI *h = &nhi[nhiCount++];
            char path[INST_ID_CHARS + 96];
            memset(h, 0, sizeof(*h));
            h->Node = i;
            tr_copy(h->Id, w->N[i].Id, sizeof(h->Id));
            nhi_hw_key(h->Id, path, sizeof(path));
            if (reg_text(HKEY_LOCAL_MACHINE, path, CM_Q_NAME_VALUE, h->QName,
                         sizeof(h->QName)) <= 0) {
                h->QName[0] = '\0';
            }
            h->NameUnique = name_unique(w, h->Id, h->QName);
            nhi_read(h);
        }
    }
}

/* The tunnel ports by location, the protection and the units (record 24
 * section 4; xhciinst.c's tb_derive). Only an NHI whose query answered
 * verifies a port; the registry record places the devices for display. */
static int derive(WALK *w, NHI *list, int count)
{
    int *buf;
    int *parent;
    int *isNhi;
    int *isHc;
    int *verified;
    int *prot;
    int *units;
    int i;
    int k;
    TR_U32 j;

    for (i = 0; i < w->Count; i++) {
        w->N[i].PortNhi = -1;
        w->N[i].Verified = 0;
    }
    for (k = 0; k < count; k++) {
        const unsigned char *rec = list[k].R;
        TR_U32 ports;
        TR_U32 active;
        if (rec == NULL) {
            continue;
        }
        ports = inst_get32(rec, CM_ST_O_PORT_COUNT);
        active = inst_get32(rec, CM_ST_O_PORT_ACTIVE);
        for (j = 0; j < ports && j < CM_ST_MAX_PORTS; j++) {
            TR_U32 bdf = inst_get32(rec, CM_ST_O_PORTS + 4 * j);
            const unsigned char *path = rec + CM_ST_O_PATHS + CM_ST_PATH_BYTES * j;
            for (i = 0; i < w->Count; i++) {
                int match;
                if (!inst_starts(w->N[i].Id, "PCI\\")) {
                    continue;
                }
                if (envNt) {
                    match = w->N[i].HasLoc &&
                            w->N[i].Bus == CM_ST_BDF_BUS(bdf) &&
                            w->N[i].Addr == ((CM_ST_BDF_DEV(bdf) << 16) |
                                             CM_ST_BDF_FN(bdf));
                } else {
                    match = inst_tb_loc_match(w->N[i].Id, bdf, path + 1,
                                              (int)path[0]);
                }
                if (match) {
                    w->N[i].PortNhi = k;
                    w->N[i].PortIdx = (int)j;
                    /* Display units come from any record; only a query
                     * makes them ejectable (record 25 section 7.1). */
                    w->N[i].Verified = (active & (1UL << j)) != 0;
                }
            }
        }
    }
    buf = (int *)calloc((size_t)(w->Count + 1) * 6, sizeof(int));
    if (buf == NULL) {
        return 0;
    }
    parent = buf;
    isNhi = parent + w->Count + 1;
    isHc = isNhi + w->Count + 1;
    verified = isHc + w->Count + 1;
    prot = verified + w->Count + 1;
    units = prot + w->Count + 1;
    for (i = 0; i < w->Count; i++) {
        parent[i] = w->N[i].Parent;
        isNhi[i] = w->N[i].IsNhi;
        isHc[i] = w->N[i].IsUsbHc;
        verified[i] = w->N[i].Verified;
    }
    inst_tb_protect(parent, isNhi, isHc, w->Count, prot);
    inst_tb_units(parent, verified, w->Count, units);
    for (i = 0; i < w->Count; i++) {
        w->N[i].Prot = prot[i];
        w->N[i].UnitOf = units[i];
        w->N[i].Ejectable = units[i] == i &&
                            !inst_tb_unit_blocked(units, prot, w->Count, i) &&
                            w->N[i].Parent >= 0 &&
                            w->N[w->N[i].Parent].PortNhi >= 0 &&
                            list[w->N[w->N[i].Parent].PortNhi].HaveQ;
    }
    free(buf);
    return 1;
}

/* The NHI a port node belongs to, and the record word of its bus,
 * device and function. */
static TR_U32 port_bdf(const WALK *w, const NHI *list, int port)
{
    const DNODE *p = &w->N[port];

    if (p->PortNhi < 0 || list[p->PortNhi].R == NULL) {
        return CM_ST_UNKNOWN;
    }
    return inst_get32(list[p->PortNhi].R, CM_ST_O_PORTS + 4 * p->PortIdx);
}

/* ======================================================================== */
/* What a unit is called: its disks and their letters (owner, 2026-10-16)    */
/* ======================================================================== */

typedef struct _TR_DEVNUM {
    DWORD DeviceType;
    DWORD DeviceNumber;
    DWORD PartitionNumber;
} TR_DEVNUM;

static int dev_number(const char *path, TR_DEVNUM *n)
{
    HANDLE h = open_device(path);
    DWORD got = 0;
    int ok;

    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    memset(n, 0, sizeof(*n));
    ok = DeviceIoControl(h, TR_IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, n,
                         sizeof(*n), &got, NULL) && got >= 8;
    CloseHandle(h);
    return ok;
}

/* NT: each disk inside a unit gets the letters of the volumes on it, by
 * the storage device number of the disk's interface and of each \\.\X:.
 * Read only, every device opened with no access rights; whatever cannot be
 * read just leaves a letter out. */
static void disk_letters_nt(WALK *w)
{
    TR_DEVNUM vol[26];
    int have[26];
    DWORD drives;
    int i;
    int any = 0;

    for (i = 0; i < w->Count; i++) {
        if (w->N[i].IsDisk && (w->N[i].UnitOf >= 0 || w->N[i].UsbOf >= 0)) {
            any = 1;
        }
    }
    if (!any || api.GetClassDevs == NULL || api.EnumIface == NULL ||
        api.IfaceDetail == NULL || api.Destroy == NULL) {
        return;
    }
    drives = GetLogicalDrives();
    for (i = 2; i < 26; i++) {      /* never the floppy letters */
        char root[4];
        char dev[8];
        UINT type;
        have[i] = 0;
        if (!(drives & (1UL << i))) {
            continue;
        }
        tr_sprintf(root, sizeof(root), "%c:\\", 'A' + i);
        type = GetDriveTypeA(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) {
            continue;
        }
        tr_sprintf(dev, sizeof(dev), "%c:", 'A' + i);
        have[i] = dev_number(dev, &vol[i]);
    }
    have[0] = have[1] = 0;
    for (i = 0; i < w->Count; i++) {
        HDEVINFO set;
        SP_DEVICE_INTERFACE_DATA ifd;
        union {
            SP_DEVICE_INTERFACE_DETAIL_DATA_A d;
            char raw[TR_NAME_LEN + 16];
        } det;
        DWORD need = 0;
        TR_DEVNUM dn;
        int l;
        if (!w->N[i].IsDisk || (w->N[i].UnitOf < 0 && w->N[i].UsbOf < 0)) {
            continue;
        }
        set = api.GetClassDevs(&trGuidDisk, w->N[i].Id, NULL,
                               DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) {
            continue;
        }
        memset(&ifd, 0, sizeof(ifd));
        ifd.cbSize = sizeof(ifd);
        memset(&det, 0, sizeof(det));
        det.d.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (api.EnumIface(set, NULL, &trGuidDisk, 0, &ifd) &&
            api.IfaceDetail(set, &ifd, &det.d, sizeof(det), &need, NULL) &&
            dev_number(det.d.DevicePath, &dn)) {
            char add[2];
            for (l = 2; l < 26; l++) {
                if (have[l] && vol[l].DeviceType == dn.DeviceType &&
                    vol[l].DeviceNumber == dn.DeviceNumber) {
                    add[0] = (char)('A' + l);
                    add[1] = '\0';
                    tr_letters_add(w->N[i].Letters, add);
                }
            }
        }
        api.Destroy(set);
    }
}

/* What the user sees for unit u: its disks and their letters, else its
 * own name (tr_unit_label). */
static void unit_label(int u, char *out, size_t cap)
{
    char names[512];
    char letters[TR_LETTERS_BYTES];
    TR_TEXT t;
    int i;

    tr_text_init(&t, names, sizeof(names));
    letters[0] = '\0';
    for (i = u; i < gw.Count; i++) {
        if (gw.N[i].UnitOf == u && gw.N[i].IsDisk) {
            tr_printf(&t, "%s%s", t.Len ? ", " : "",
                      gw.N[i].Friendly[0] ? gw.N[i].Friendly : dn_name(&gw.N[i]));
            tr_letters_add(letters, gw.N[i].Letters);
        }
    }
    tr_unit_label(dn_name(&gw.N[u]), names, letters, out, cap);
}

/* ======================================================================== */
/* The USB branch (record 25 section 5)                                      */
/* ======================================================================== */

static int usb_ioctl(HANDLE h, DWORD code, void *in, DWORD inN, void *out,
                     DWORD outN, DWORD *got)
{
    DWORD n = 0;
    BOOL ok = DeviceIoControl(h, code, in, inN, out, outN, &n, NULL);

    if (got != NULL) {
        *got = n;
    }
    return ok && n <= outN;
}

/* A name answer (driver key, root hub, connection name). */
static int usb_name(HANDLE h, DWORD code, TR_U32 port, char *out, size_t cap)
{
    unsigned char buf[1024];
    DWORD got;
    size_t off = port != 0 ? 8 : 4;

    memset(buf, 0, sizeof(buf));
    buf[0] = (unsigned char)(port & 0xFF);
    buf[1] = (unsigned char)((port >> 8) & 0xFF);
    out[0] = '\0';
    if (!usb_ioctl(h, code, buf, port != 0 ? (DWORD)sizeof(buf) : 0, buf,
                   sizeof(buf), &got)) {
        return 0;
    }
    return tr_wide_name(buf, got, off, out, cap);
}

/* A descriptor through the hub; returns the bytes read (0 none) into a
 * new buffer. */
static DWORD usb_desc(const char *hub, TR_U32 port, unsigned int type,
                      unsigned int index, unsigned int lang, unsigned int len,
                      unsigned char **outBuf)
{
    HANDLE h;
    unsigned char *b;
    TR_USB_DESC_REQ rq;
    DWORD got = 0;
    DWORD n = 0;

    *outBuf = NULL;
    h = open_device(hub);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    b = (unsigned char *)calloc(1, TR_DR_BYTES + len);
    if (b != NULL) {
        memset(&rq, 0, sizeof(rq));
        rq.ConnectionIndex = port;
        rq.bmRequest = 0x80;
        rq.bRequest = 6;
        rq.wValue = (unsigned short)((type << 8) | index);
        rq.wIndex = (unsigned short)lang;
        rq.wLength = (unsigned short)len;
        memcpy(b, &rq, sizeof(rq));
        if (usb_ioctl(h, TR_IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, b,
                      TR_DR_BYTES + len, b, TR_DR_BYTES + len, &got) &&
            got > TR_DR_BYTES) {
            n = got - TR_DR_BYTES;
            memmove(b, b + TR_DR_BYTES, n);
            *outBuf = b;
        } else {
            free(b);
        }
    }
    CloseHandle(h);
    return n;
}

/* The configuration descriptor, whole. */
static void usb_config(UPORT *p)
{
    unsigned char *b;
    DWORD n;
    unsigned int total;

    p->CfgTried = 1;
    n = usb_desc(p->Hub, p->Conn.Port, 2, 0, 0, 9, &b);
    if (n < 9) {
        free(b);
        return;
    }
    total = (unsigned int)b[2] | ((unsigned int)b[3] << 8);
    free(b);
    if (total < 9 || total > 4096) {
        return;
    }
    p->CfgN = usb_desc(p->Hub, p->Conn.Port, 2, 0, 0, total, &p->Cfg);
}

/* The configuration, string and BOS descriptors: only for the selected
 * node with Descriptors on, and for a report (record 25 section 3.4): a
 * refresh reads the connection information alone, since Windows' own
 * stacks may ask the device for them. */
static void usb_fetch(UPORT *p)
{
    unsigned char *b;
    DWORD n;
    unsigned int lang = 0x0409;
    int i;

    if (p->Fetched || !p->HaveConn || p->Conn.Status != TR_CS_CONNECTED) {
        return;
    }
    p->Fetched = 1;
    if (!p->CfgTried) {
        usb_config(p);
    }
    n = usb_desc(p->Hub, p->Conn.Port, 3, 0, 0, 255, &b);
    if (n >= 4) {
        lang = (unsigned int)b[2] | ((unsigned int)b[3] << 8);
    }
    free(b);
    for (i = 0; i < 3; i++) {
        unsigned int idx = p->Conn.Dev[14 + i];
        if (idx == 0) {
            continue;
        }
        n = usb_desc(p->Hub, p->Conn.Port, 3, idx, lang, 255, &b);
        p->HaveStr[i] = tr_dec_string(b, n, p->Str[i], sizeof(p->Str[i]));
        if (b != NULL && n <= sizeof(p->StrRaw[i])) {
            memcpy(p->StrRaw[i], b, n);
            p->StrN[i] = n;
        }
        free(b);
    }
    if (((unsigned int)p->Conn.Dev[2] | ((unsigned int)p->Conn.Dev[3] << 8)) >=
        0x0201) {
        n = usb_desc(p->Hub, p->Conn.Port, 0x0F, 0, 0, 5, &b);
        if (n >= 5) {
            unsigned int total = (unsigned int)b[2] | ((unsigned int)b[3] << 8);
            free(b);
            if (total >= 5 && total <= 4096) {
                p->BosN = usb_desc(p->Hub, p->Conn.Port, 0x0F, 0, 0, total,
                                   &p->Bos);
            }
        } else {
            free(b);
        }
    }
}

static int add_tnode(int parent, int kind, int a, int b, int c, int dn,
                     const char *label, const char *key)
{
    TNODE *t;

    if (tnCount >= TR_MAX_TNODES) {
        return -1;
    }
    t = &tn[tnCount];
    memset(t, 0, sizeof(*t));
    t->Parent = parent;
    t->Kind = kind;
    t->A = a;
    t->B = b;
    t->C = c;
    t->Dn = dn;
    tr_copy(t->Label, label, sizeof(t->Label));
    tr_copy(t->Key, key, sizeof(t->Key));
    return tnCount++;
}

static const char *speed_short(int speed)
{
    switch (speed) {
    case TR_SPEED_LOW:
        return "Low Speed";
    case TR_SPEED_FULL:
        return "Full Speed";
    case TR_SPEED_HIGH:
        return "High Speed";
    case TR_SPEED_SUPER:
        return "SuperSpeed";
    case TR_SPEED_SUPERPLUS:
        return "SuperSpeedPlus";
    default:
        return "speed unknown";
    }
}

static int uport_speed(const UPORT *p)
{
    return tr_speed(p->Conn.Speed, p->HaveV2, p->V2Flags);
}

/* Every port of one hub into up[] (record 25 section 5): the scan runs on
 * every refresh, the tray's too, whatever the View check boxes show, so
 * that the USB devices of section 6.5 are the same in the window, the tray
 * and /eject. The tree is built from up[] afterwards (usb_add_ports). */
static void usb_scan_hub(int ctrl, const char *hubName, const char *path,
                         int depth, int parentUp)
{
    HANDLE h;
    unsigned char ni[TR_NI_BYTES];
    DWORD got = 0;
    unsigned int ports = 0;
    int bus;
    unsigned int i;

    h = open_device(hubName);
    if (h == INVALID_HANDLE_VALUE) {
        if (parentUp < 0) {
            uc[ctrl].RootErr = 1;
        } else {
            up[parentUp].HubErr = 1;
        }
        return;
    }
    memset(ni, 0, sizeof(ni));
    if (!usb_ioctl(h, TR_IOCTL_USB_GET_NODE_INFORMATION, ni, sizeof(ni), ni,
                   sizeof(ni), &got) ||
        !tr_node_parse(ni, got, &ports, &bus)) {
        CloseHandle(h);
        if (parentUp < 0) {
            uc[ctrl].RootErr = 2;
        } else {
            up[parentUp].HubErr = 2;
        }
        return;
    }
    if (depth == 0) {
        memcpy(uc[ctrl].Node, ni, sizeof(ni));
        uc[ctrl].HaveNode = 1;
    }
    for (i = 1; i <= ports && i <= 255; i++) {
        unsigned char buf[TR_CI_BYTES + TR_MAX_PIPES * TR_CI_PIPE_BYTES];
        TR_USB_CONN_V2 v2;
        UPORT *p;
        int me;

        if (upCount >= TR_MAX_UPORTS) {
            break;
        }
        me = upCount++;
        p = &up[me];
        memset(p, 0, sizeof(*p));
        p->Ctrl = ctrl;
        p->Dn = -1;
        p->ParentUp = parentUp;
        tr_copy(p->Hub, hubName, sizeof(p->Hub));
        tr_port_path(p->Path, sizeof(p->Path), path, i);
        memset(buf, 0, sizeof(buf));
        buf[0] = (unsigned char)i;
        if (usb_ioctl(h, TR_IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, buf,
                      sizeof(buf), buf, sizeof(buf), &got) &&
            tr_conn_parse(buf, got, 1, &p->Conn)) {
            p->Ex = 1;
            p->HaveConn = 1;
        } else {
            /* Refused, or a short answer: the original request. */
            memset(buf, 0, sizeof(buf));
            buf[0] = (unsigned char)i;
            if (usb_ioctl(h, TR_IOCTL_USB_GET_NODE_CONNECTION_INFORMATION, buf,
                          sizeof(buf), buf, sizeof(buf), &got)) {
                p->HaveConn = tr_conn_parse(buf, got, 0, &p->Conn);
            }
        }
        p->Conn.Port = i;
        memset(&v2, 0, sizeof(v2));
        v2.ConnectionIndex = i;
        v2.Length = sizeof(v2);
        v2.SupportedUsbProtocols = 0x07UL;
        if (p->Ex && usb_ioctl(h, TR_IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2,
                               &v2, sizeof(v2), &v2, sizeof(v2), &got) &&
            got >= TR_V2_BYTES) {
            p->HaveV2 = 1;
            p->V2Proto = v2.SupportedUsbProtocols;
            p->V2Flags = v2.Flags;
        }
        if (!p->HaveConn || p->Conn.Status == TR_CS_NONE) {
            continue;
        }
        usb_name(h, TR_IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME, i,
                 p->DrvKey, sizeof(p->DrvKey));
        p->Dn = walk_find_driver(&gw, p->DrvKey);
        if (p->Conn.Status == TR_CS_CONNECTED) {
            usbDevices++;
        }
        if (p->Conn.IsHub && p->Conn.Status == TR_CS_CONNECTED && depth < 6 &&
            usb_name(h, TR_IOCTL_USB_GET_NODE_CONNECTION_NAME, i, p->HubName,
                     sizeof(p->HubName))) {
            HANDLE hh = open_device(p->HubName);
            if (hh != INVALID_HANDLE_VALUE) {
                memset(p->HubNode, 0, sizeof(p->HubNode));
                p->HaveHubNode = usb_ioctl(hh, TR_IOCTL_USB_GET_NODE_INFORMATION,
                                           p->HubNode, sizeof(p->HubNode),
                                           p->HubNode, sizeof(p->HubNode), &got) &&
                                 got >= TR_NI_BYTES;
                CloseHandle(hh);
            }
            usb_scan_hub(ctrl, p->HubName, p->Path, depth + 1, me);
        }
    }
    CloseHandle(h);
}

/* The tree's nodes for the ports of one hub (parentUp -1: the root hub's). */
static void usb_add_ports(int parentT, int ctrl, int parentUp)
{
    int k;

    for (k = 0; k < upCount; k++) {
        UPORT *p = &up[k];
        char label[200];
        char key[280];
        int dn = p->Dn;
        int t;

        if (p->Ctrl != ctrl || p->ParentUp != parentUp) {
            continue;
        }
        tr_key(key, sizeof(key), "U", uc[ctrl].DrvKey, p->Path);
        if (!p->HaveConn || p->Conn.Status == TR_CS_NONE) {
            if (viewFlags & TR_V_EMPTY) {
                tr_sprintf(label, sizeof(label), "[Port %lu%s] (empty)",
                           (unsigned long)p->Conn.Port,
                           tr_port_tag(p->HaveV2, p->V2Proto));
                add_tnode(parentT, K_UPORT, k, 0, 0, -1, label, key);
            }
            continue;
        }
        if (p->Conn.Status != TR_CS_CONNECTED) {
            tr_sprintf(label, sizeof(label), "[Port %lu%s] %s",
                       (unsigned long)p->Conn.Port,
                       tr_port_tag(p->HaveV2, p->V2Proto),
                       tr_conn_status_text(p->Conn.Status));
        } else {
            char name[160];
            tr_sprintf(name, sizeof(name), "USB device %04X:%04X",
                       (unsigned int)p->Conn.Dev[8] | ((unsigned int)p->Conn.Dev[9] << 8),
                       (unsigned int)p->Conn.Dev[10] | ((unsigned int)p->Conn.Dev[11] << 8));
            if (dn >= 0) {
                usb_label(dn, name, sizeof(name));
            }
            tr_sprintf(label, sizeof(label), "[Port %lu%s] %s  %s%s",
                       (unsigned long)p->Conn.Port,
                       tr_port_tag(p->HaveV2, p->V2Proto), name,
                       speed_short(uport_speed(p)), usb_suffix(dn));
        }
        t = add_tnode(parentT, K_UPORT, k, 0, 0, dn, label, key);
        if (t >= 0 && p->Conn.IsHub && p->Conn.Status == TR_CS_CONNECTED) {
            if (p->HubErr == 1) {
                add_tnode(t, K_NOTE, 0, 0, 0, -1, "(the hub could not be opened)",
                          "N:hub");
            } else if (p->HubErr == 2) {
                add_tnode(t, K_NOTE, 0, 0, 0, -1,
                          "(the hub did not report its ports)", "N:hub");
            } else {
                usb_add_ports(t, ctrl, k);
            }
        }
    }
}

static int ctrl_add(const char *path, const char *hcd)
{
    HANDLE h;
    char key[128];
    int i;
    UCTRL *c;

    if (ucCount >= TR_MAX_CTRL) {
        return -1;
    }
    h = open_device(path);
    if (h == INVALID_HANDLE_VALUE) {
        if (hcd != NULL && strcmp(hcd, "HCD0") == 0) {
            hcdOpenError = GetLastError();
        }
        return -1;
    }
    if (!usb_name(h, TR_IOCTL_GET_HCD_DRIVERKEY_NAME, 0, key, sizeof(key))) {
        key[0] = '\0';
        hcdNameRefused++;
    }
    for (i = 0; i < ucCount; i++) {
        if (key[0] != '\0' && tr_eq(uc[i].DrvKey, key)) {
            if (hcd != NULL && uc[i].Hcd[0] == '\0') {
                tr_copy(uc[i].Hcd, hcd, sizeof(uc[i].Hcd));
            }
            CloseHandle(h);
            return -1;
        }
    }
    c = &uc[ucCount];
    memset(c, 0, sizeof(*c));
    tr_copy(c->Path, path, sizeof(c->Path));
    if (hcd != NULL) {
        tr_copy(c->Hcd, hcd, sizeof(c->Hcd));
    }
    tr_copy(c->DrvKey, key, sizeof(c->DrvKey));
    c->Dn = walk_find_driver(&gw, key);
    c->Ours = c->Dn >= 0 && tr_eq(gw.N[c->Dn].Service, "xhci98");
    usb_name(h, TR_IOCTL_USB_GET_ROOT_HUB_NAME, 0, c->RootHub, sizeof(c->RootHub));
    CloseHandle(h);
    return ucCount++;
}

static void usb_controllers(void)
{
    int n;
    char name[16];

    hcdOpenError = 0;
    hcdNameRefused = 0;
    for (n = 0; n < 64; n++) {
        tr_sprintf(name, sizeof(name), "HCD%d", n);
        ctrl_add(name, name);
    }
    if (envNt && api.GetClassDevs != NULL && api.EnumIface != NULL &&
        api.IfaceDetail != NULL && api.Destroy != NULL) {
        HDEVINFO set = api.GetClassDevs(&trGuidHc, NULL, NULL,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set != INVALID_HANDLE_VALUE) {
            DWORD i;
            for (i = 0; i < 64; i++) {
                SP_DEVICE_INTERFACE_DATA ifd;
                union {
                    SP_DEVICE_INTERFACE_DETAIL_DATA_A d;
                    char raw[TR_NAME_LEN + 16];
                } det;
                DWORD need = 0;
                memset(&ifd, 0, sizeof(ifd));
                ifd.cbSize = sizeof(ifd);
                if (!api.EnumIface(set, NULL, &trGuidHc, i, &ifd)) {
                    break;
                }
                memset(&det, 0, sizeof(det));
                det.d.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
                if (api.IfaceDetail(set, &ifd, &det.d, sizeof(det), &need, NULL)) {
                    ctrl_add(det.d.DevicePath, NULL);
                }
            }
            api.Destroy(set);
        }
    }
}

/* Every controller's ports, whatever the view shows (section 6.5). */
static void usb_scan_all(void)
{
    int i;

    usb_controllers();
    for (i = 0; i < ucCount; i++) {
        if (uc[i].RootHub[0] != '\0') {
            usb_scan_hub(i, uc[i].RootHub, "", 0, -1);
        }
    }
}

/* 1 when devnode i is a PCI-to-PCI bridge (class 0604). */
static int dn_is_bridge(int i)
{
    return i >= 0 && (inst_msz_has_class(gw.N[i].Hw, gw.N[i].HwBytes, "0604") ||
                      inst_msz_has_class(gw.N[i].Compat, gw.N[i].CompatBytes, "0604"));
}

/* The bus and device of devnode i: CM_DRP_BUSNUMBER and _ADDRESS on NT, the
 * instance id on 98 SE and ME (root-bus functions only). */
static int dn_busdev(int i, unsigned int *bus, unsigned int *dev)
{
    if (envNt) {
        if (!gw.N[i].HasLoc) {
            return 0;
        }
        *bus = (unsigned int)gw.N[i].Bus;
        *dev = (unsigned int)(gw.N[i].Addr >> 16);
        return 1;
    }
    return tr_pci_loc9x(gw.N[i].Id, bus, dev);
}

/* Whether USB controller devnode dn is a Thunderbolt chip's own xHCI: a
 * function on the same bus and device as an NHI (an integrated
 * controller), or a function behind a bridge of the switch an NHI is
 * behind (a discrete one, as Alpine Ridge's xHCI on the downstream port
 * beside the NHI's). Never by a guess: an NHI must be in the walk. The
 * generation is the NHI's published connection manager. */
static int ctrl_tb_kind(int dn)
{
    int i;
    int k;
    unsigned int bus;
    unsigned int dev;
    int haveLoc;

    if (dn < 0) {
        return TR_TB_NOT;
    }
    haveLoc = dn_busdev(dn, &bus, &dev);
    for (i = 0; i < gw.Count; i++) {
        int match = 0;
        unsigned int b2;
        unsigned int d2;
        if (!gw.N[i].IsNhi || i == dn) {
            continue;
        }
        if (haveLoc && dn_busdev(i, &b2, &d2) && b2 == bus && d2 == dev &&
            gw.N[i].Parent == gw.N[dn].Parent) {
            match = 1;
        } else if (gw.N[dn].Parent >= 0 && gw.N[i].Parent >= 0 &&
                   dn_is_bridge(gw.N[dn].Parent) && dn_is_bridge(gw.N[i].Parent) &&
                   gw.N[gw.N[dn].Parent].Parent >= 0 &&
                   gw.N[gw.N[dn].Parent].Parent == gw.N[gw.N[i].Parent].Parent &&
                   dn_is_bridge(gw.N[gw.N[dn].Parent].Parent)) {
            match = 1;
        }
        if (!match) {
            continue;
        }
        for (k = 0; k < nhiCount; k++) {
            if (nhi[k].Node == i && nhi[k].R != NULL) {
                TR_U32 cm = inst_get32(nhi[k].R, CM_ST_O_CM);
                return cm == CM_ST_CM_ICM ? TR_TB_TB3
                       : cm == CM_ST_CM_USB4 ? TR_TB_USB4 : TR_TB_UNKNOWN;
            }
        }
        return TR_TB_UNKNOWN;
    }
    return TR_TB_NOT;
}

/* The router name of a tunnel port that is an ancestor of devnode dn. */
static void behind_name(int dn, char *out, size_t cap)
{
    int i = dn;

    out[0] = '\0';
    while (i >= 0) {
        if (gw.N[i].PortNhi >= 0) {
            const NHI *h = &nhi[gw.N[i].PortNhi];
            TR_U32 bdf = port_bdf(&gw, nhi, i);
            TR_U32 n = inst_get32(h->R, CM_ST_O_ROUTER_COUNT);
            TR_U32 r;
            for (r = 0; r < n; r++) {
                const unsigned char *e = h->R + CM_ST_HDR_BYTES + r * CM_ST_ENTRY_BYTES;
                if (inst_get32(e, CM_ST_E_BRIDGE) == bdf) {
                    char name[TR_NAME_CHARS];
                    tr_router_name(e, name, sizeof(name));
                    tr_sprintf(out, cap, "behind %s", name);
                    return;
                }
            }
            tr_copy(out, "behind a Thunderbolt port", cap);
            return;
        }
        i = gw.N[i].Parent;
    }
}

static void build_usb(int root)
{
    int u = add_tnode(root, K_USBROOT, 0, 0, 0, -1, "USB", "R:usb");
    int i;

    if (u < 0) {
        return;
    }
    for (i = 0; i < ucCount; i++) {
        UCTRL *c = &uc[i];
        char label[200];
        char key[280];
        char suffix[96];
        char rhLabel[80];
        unsigned int ports = 0;
        unsigned int n2 = 0;
        unsigned int n3 = 0;
        int known = 1;
        int k;
        int t;
        int rh;
        if (!c->Ours && !(viewFlags & TR_V_WINUSB)) {
            continue;
        }
        if (c->Dn >= 0) {
            behind_name(c->Dn, c->Behind, sizeof(c->Behind));
        }
        {
            unsigned int ven = 0;
            unsigned int dev = 0;
            int ids = c->Dn >= 0 && tr_pci_ids(gw.N[c->Dn].Id, &ven, &dev);
            tr_ctrl_suffix(suffix, sizeof(suffix), ids, ven, dev,
                           ctrl_tb_kind(c->Dn));
        }
        tr_sprintf(label, sizeof(label), "%s  (%s)%s%s%s%s",
                   c->Dn >= 0 ? dn_name(&gw.N[c->Dn]) : "USB host controller",
                   c->Ours ? "xhci98" : "Windows", suffix, c->Behind[0] ? " (" : "",
                   c->Behind, c->Behind[0] ? ")" : "");
        tr_key(key, sizeof(key), "C", c->DrvKey, c->Path);
        t = add_tnode(u, K_CTRL, i, 0, 0, c->Dn, label, key);
        if (t < 0) {
            return;
        }
        for (k = 0; k < upCount; k++) {
            if (up[k].Ctrl == i && up[k].ParentUp == -1) {
                ports++;
                if (!up[k].HaveV2 || (up[k].V2Proto & 0x07UL) == 0) {
                    known = 0;
                } else if (up[k].V2Proto & 0x04UL) {
                    n3++;
                } else {
                    n2++;
                }
            }
        }
        {
            /* The total from the hub descriptor; the split only when every
             * advertised port was scanned (the scan has a budget). */
            unsigned int adv = 0;
            int busPow;
            if (c->HaveNode && tr_node_parse(c->Node, sizeof(c->Node), &adv, &busPow)) {
                if (adv != ports) {
                    known = 0;
                }
                ports = adv;
            }
        }
        tr_roothub_label(rhLabel, sizeof(rhLabel), ports, known, n2, n3);
        tr_key(key, sizeof(key), "H", c->DrvKey, "root");
        rh = add_tnode(t, K_ROOTHUB, i, 0, 0, -1, rhLabel, key);
        if (rh >= 0 && c->RootErr == 1) {
            add_tnode(rh, K_NOTE, 0, 0, 0, -1, "(the hub could not be opened)",
                      "N:hub");
        } else if (rh >= 0 && c->RootErr == 2) {
            add_tnode(rh, K_NOTE, 0, 0, 0, -1, "(the hub did not report its ports)",
                      "N:hub");
        } else if (rh >= 0) {
            usb_add_ports(rh, i, -1);
        }
    }
}

/* ======================================================================== */
/* The Thunderbolt branch (record 25 section 3.2)                            */
/* ======================================================================== */

/* A router entry of the model's record for NHI k, or NULL when k or r is
 * outside it. */
static const unsigned char *router_entry(int k, int r)
{
    if (k < 0 || k >= nhiCount || nhi[k].R == NULL || r < 0 ||
        (TR_U32)r >= inst_get32(nhi[k].R, CM_ST_O_ROUTER_COUNT) ||
        (TR_U32)r >= CM_ST_MAX_ROUTERS) {
        return NULL;
    }
    return nhi[k].R + CM_ST_HDR_BYTES + (TR_U32)r * CM_ST_ENTRY_BYTES;
}

static void router_key(const NHI *h, const unsigned char *e, char *out,
                       size_t cap)
{
    TR_U32 kind = inst_get32(e, CM_ST_E_KIND);
    TR_U32 lo = h->HaveQ ? h->Q.EpochLo : 0;

    if (kind == CM_ST_KIND_ICM) {
        tr_sprintf(out, cap, "T:%s:%08lX:i%08lX.%08lX.%08lX", h->Id,
                   (unsigned long)lo,
                   (unsigned long)inst_get32(e, CM_ST_E_PLACE),
                   (unsigned long)inst_get32(e, CM_ST_E_IDS),
                   (unsigned long)inst_get32(e, CM_ST_E_SHORT_ID));
    } else {
        tr_sprintf(out, cap, "T:%s:%08lX:r%08lX%08lX", h->Id, (unsigned long)lo,
                   (unsigned long)inst_get32(e, CM_ST_E_ROUTE_HI),
                   (unsigned long)inst_get32(e, CM_ST_E_ROUTE_LO));
    }
}

static void add_devs_below(int parentT, int dn, int depth)
{
    int i;

    for (i = dn + 1; i < gw.Count; i++) {
        if (gw.N[i].Parent == dn) {
            char label[200];
            char key[280];
            int t;
            char name[160];
            if (gw.N[i].UnitOf == i) {
                unit_label(i, name, sizeof(name));
            } else {
                tr_copy(name, dn_name(&gw.N[i]), sizeof(name));
            }
            if (gw.N[i].UnitOf == i && !dn_started(&gw.N[i])) {
                char tag[64];
                char sen[8];
                tr_stopped_text(gw.N[i].Problem, tag, sizeof(tag), sen, sizeof(sen));
                tr_sprintf(label, sizeof(label), "%s  [%s]", name, tag);
            } else if (usb_listed(i)) {
                usb_label(i, name, sizeof(name));
                tr_sprintf(label, sizeof(label), "%s%s", name, usb_suffix(i));
            } else {
                tr_sprintf(label, sizeof(label), "%s%s", name,
                           gw.N[i].UnitOf == i && ej_why_not(i) == NULL
                               ? "  [can be stopped]" : "");
            }
            tr_key(key, sizeof(key), "D", gw.N[i].Id, NULL);
            t = add_tnode(parentT, K_DEV, 0, 0, 0, i, label, key);
            if (t >= 0 && depth < 12) {
                add_devs_below(t, i, depth + 1);
            }
        }
    }
}

static void add_router(int parentT, int k, TR_U32 r, int portNode,
                       int *unitsShown)
{
    const NHI *h = &nhi[k];
    const unsigned char *e = h->R + CM_ST_HDR_BYTES + r * CM_ST_ENTRY_BYTES;
    TR_U32 tun = inst_get32(e, CM_ST_E_TUNNELS);
    char name[TR_NAME_CHARS];
    char gen[32];
    char label[200];
    char key[280];
    char tkey[300];
    int t;
    int c;
    int i;
    int unit = -1;

    tr_router_name(e, name, sizeof(name));
    tr_gen_text(inst_get32(e, CM_ST_E_GEN), gen, sizeof(gen));
    tr_sprintf(label, sizeof(label), "%s  (%s), %s", name, gen,
               tr_appr_text(inst_get32(e, CM_ST_E_APPROVAL)));
    router_key(h, e, key, sizeof(key));
    t = add_tnode(parentT, K_ROUTER, k, (int)r, 0, -1, label, key);
    if (t < 0) {
        return;
    }
    if (portNode >= 0) {
        for (i = portNode + 1; i < gw.Count; i++) {
            if (gw.N[i].Parent == portNode) {
                unit = i;
                break;
            }
        }
    }
    for (c = 0; c < 3; c++) {
        TR_U32 v = (tun >> (4 * c)) & 0xFUL;
        static const char *const names[3] = { "PCIe tunnel", "USB3 tunnel",
                                              "DisplayPort tunnel" };
        int tt;
        if (c == 0 && unit >= 0) {
            char shown[160];
            if (gw.N[unit].UnitOf == unit) {
                unit_label(unit, shown, sizeof(shown));
            } else {
                tr_copy(shown, dn_name(&gw.N[unit]), sizeof(shown));
            }
            tr_sprintf(label, sizeof(label), "%s: %s  - %s", names[c],
                       tr_tun_text(v), shown);
        } else {
            tr_sprintf(label, sizeof(label), "%s: %s", names[c], tr_tun_text(v));
        }
        tr_sprintf(tkey, sizeof(tkey), "%s:t%d", key, c);
        tt = add_tnode(t, K_TUNNEL, k, (int)r, c, -1, label, tkey);
        if (c == 0 && tt >= 0 && portNode >= 0 && !*unitsShown &&
            (viewFlags & TR_V_BEHIND)) {
            add_devs_below(tt, portNode, 0);
            *unitsShown = 1;
        }
    }
}

static void build_tb(int root)
{
    int tb = add_tnode(root, K_TBROOT, 0, 0, 0, -1, "Thunderbolt and USB4", "R:tb");
    int k;

    if (tb < 0) {
        return;
    }
    if (nhiCount == 0) {
        add_tnode(tb, K_NOTE, 0, 0, 0, -1,
                  "(no Thunderbolt or USB4 controller driven by xhcicm)", "N:tb");
    }
    for (k = 0; k < nhiCount; k++) {
        NHI *h = &nhi[k];
        const DNODE *d = &gw.N[h->Node];
        char label[200];
        char key[280];
        int t;
        TR_U32 ports;
        TR_U32 active;
        TR_U32 n;
        TR_U32 j;
        TR_U32 r;
        int *placed;

        if (h->R == NULL) {
            tr_sprintf(label, sizeof(label),
                       "%s  (the driver has not published its device list)",
                       dn_name(d));
        } else {
            tr_sprintf(label, sizeof(label), "%s  [%s], %s%s", dn_name(d),
                       tr_cm_text(inst_get32(h->R, CM_ST_O_CM)),
                       tr_stage_text(inst_get32(h->R, CM_ST_O_STAGE)),
                       !h->HaveQ ? "  (not confirmed: eject unavailable)"
                       : !tr_query_current(&h->Q) ? "  (not current)" : "");
        }
        tr_key(key, sizeof(key), "N", h->Id, NULL);
        t = add_tnode(tb, K_NHI, k, 0, 0, h->Node, label, key);
        if (t < 0 || h->R == NULL) {
            continue;
        }
        ports = inst_get32(h->R, CM_ST_O_PORT_COUNT);
        active = inst_get32(h->R, CM_ST_O_PORT_ACTIVE);
        n = inst_get32(h->R, CM_ST_O_ROUTER_COUNT);
        placed = (int *)calloc(n + 1, sizeof(int));
        if (placed == NULL) {
            continue;
        }
        for (j = 0; j < ports && j < CM_ST_MAX_PORTS; j++) {
            TR_U32 bdf = inst_get32(h->R, CM_ST_O_PORTS + 4 * j);
            int portNode = -1;
            int pt;
            int any = 0;
            int unitsShown = 0;
            int i;
            for (i = 0; i < gw.Count; i++) {
                if (gw.N[i].PortNhi == k && gw.N[i].PortIdx == (int)j) {
                    portNode = i;
                }
            }
            tr_sprintf(label, sizeof(label), "Port %lu (bridge %02lX:%02lX.%lu): %s",
                       (unsigned long)(j + 1), (unsigned long)CM_ST_BDF_BUS(bdf),
                       (unsigned long)CM_ST_BDF_DEV(bdf),
                       (unsigned long)CM_ST_BDF_FN(bdf),
                       (active & (1UL << j)) ? "active" : "not active");
            tr_sprintf(key, sizeof(key), "P:%s:%08lX", h->Id, (unsigned long)bdf);
            pt = add_tnode(t, K_PORT, k, (int)j, 0, portNode, label, key);
            if (pt < 0) {
                break;
            }
            for (r = 0; r < n; r++) {
                const unsigned char *e = h->R + CM_ST_HDR_BYTES + r * CM_ST_ENTRY_BYTES;
                if (inst_get32(e, CM_ST_E_BRIDGE) == bdf) {
                    add_router(pt, k, r, portNode, &unitsShown);
                    placed[r] = 1;
                    any = 1;
                }
            }
            if (!any) {
                if (portNode >= 0 && (viewFlags & TR_V_BEHIND)) {
                    add_devs_below(pt, portNode, 0);
                }
                tr_copy(tn[pt].Label + strlen(tn[pt].Label), ", no device",
                        sizeof(tn[pt].Label) - strlen(tn[pt].Label));
            }
        }
        for (r = 0; r < n; r++) {
            if (!placed[r]) {
                int none = 1;
                add_router(t, k, r, -1, &none);
            }
        }
        free(placed);
    }
}

/* ======================================================================== */
/* Refresh                                                                   */
/* ======================================================================== */

static void model_free_usb(void)
{
    int i;

    for (i = 0; i < upCount; i++) {
        free(up[i].Cfg);
        free(up[i].Bos);
    }
    upCount = 0;
    ucCount = 0;
}

/* The device tree, the NHIs and their units; the USB branch too when
 * withUsb. */
static void refresh_model(int withUsb)
{
    int root;

    model_free_usb();
    tnCount = 0;
    usbDevices = 0;
    tbDevices = 0;
    modelGen = chgCount;
    if (!walk(&gw)) {
        gw.Count = 0;
    }
    /* A new model: the poll compares against it again, not against an
     * answer the model may not hold. */
    for (root = 0; root < TR_SNAP_NHI; root++) {
        free(pollBuf[root]);
        pollBuf[root] = NULL;
        pollId[root][0] = '\0';
    }
    nhi_find(&gw);
    derive(&gw, nhi, nhiCount);
    usb_scan_all();
    usb_derive();
    if (envNt) {
        disk_letters_nt(&gw);
    }
    for (root = 0; root < nhiCount; root++) {
        if (nhi[root].R != NULL) {
            tbDevices += (int)inst_get32(nhi[root].R, CM_ST_O_ROUTER_COUNT);
        }
    }
    if (!withUsb) {
        return;
    }
    root = add_tnode(-1, K_ROOT, 0, 0, 0, -1, "My Computer", "R:root");
    if (viewFlags & TR_V_TB) {
        build_tb(root);
    }
    if (viewFlags & TR_V_USB) {
        build_usb(root);
    }
    if (gw.Bad) {
        add_tnode(root, K_NOTE, gw.Bad, 0, 0, -1,
                  "(some device entries could not be read whole)", "N:bad");
    }
}

/* ======================================================================== */
/* The details                                                               */
/* ======================================================================== */

/* One logical configuration of devnode dn (kind TR_LC_*), each descriptor
 * a line; 1 when it had one. Read only, no rights needed. */
static int res_conf(TR_TEXT *t, DWORD dn, ULONG kind, const char *title)
{
    TR_CMH lc = 0;
    TR_CMH rd;
    TR_CMH next;
    ULONG rid = 0;
    int any = 0;
    int guard = 0;

    if (api.FirstLogConf == NULL || api.NextResDes == NULL ||
        api.ResDesSize == NULL || api.ResDesData == NULL ||
        api.FirstLogConf(&lc, dn, kind) != TR_CR_SUCCESS) {
        return 0;
    }
    tr_printf(t, "  %s:\n", title);
    rd = lc;
    while (guard++ < 64 &&
           api.NextResDes(&next, rd, 0UL, &rid, 0) == TR_CR_SUCCESS) {
        ULONG size = 0;
        unsigned char buf[512];
        if (rd != lc && api.FreeResDes != NULL) {
            api.FreeResDes(rd);
        }
        rd = next;
        any = 1;
        if (api.ResDesSize(&size, rd, 0) == TR_CR_SUCCESS && size == 0) {
            /* 98 SE: a type only a private arbitrator knows (67 on a PCI
             * bridge); CONFIGMG arbitrates memory, I/O, DMA and IRQ alone. */
            tr_printf(t, "    Resource type %lu (no data)\n", (unsigned long)rid);
            continue;
        }
        if (api.ResDesSize(&size, rd, 0) != TR_CR_SUCCESS || size > sizeof(buf) ||
            api.ResDesData(rd, buf, size, 0) != TR_CR_SUCCESS) {
            tr_printf(t, "    (a resource of type %lu, %lu bytes, that could not be "
                         "read)\n", (unsigned long)rid, (unsigned long)size);
            continue;
        }
#ifdef _WIN64
        tr_res_text(t, rid, buf, size, kind == TR_LC_ALLOC || kind == TR_LC_BOOT, 1);
#else
        tr_res_text(t, rid, buf, size, kind == TR_LC_ALLOC || kind == TR_LC_BOOT, 0);
#endif
    }
    if (rd != lc && api.FreeResDes != NULL) {
        api.FreeResDes(rd);
    }
    if (api.FreeLogConf != NULL) {
        api.FreeLogConf(lc);
    }
    if (!any) {
        tr_put(t, "    none\n");
    }
    return 1;
}

/* The resources of devnode dn (record 25 revision 10, addendum): what is
 * allocated, and, with a problem code, what it asked for. */
static void details_resources(TR_TEXT *t, const DNODE *d)
{
    if (api.FirstLogConf == NULL || api.NextResDes == NULL) {
        return;     /* this Windows does not offer the calls */
    }
    if (!res_conf(t, d->Dn, TR_LC_ALLOC, "Resources")) {
        tr_put(t, "  Resources:     none allocated\n");
    }
    if (d->Problem != 0) {
        res_conf(t, d->Dn, TR_LC_BOOT, "Asked for (boot configuration)");
        if (!res_conf(t, d->Dn, TR_LC_FILTERED, "Asked for (filtered list)")) {
            res_conf(t, d->Dn, TR_LC_BASIC, "Asked for (first of the lists)");
        }
    }
}

static void details_devnode(TR_TEXT *t, int dn)
{
    const DNODE *d;
    char path[300];
    char ver[64];

    if (dn < 0) {
        return;
    }
    d = &gw.N[dn];
    tr_printf(t, "\nDevice Manager entry\n");
    tr_printf(t, "  Description:   %s\n", d->Desc[0] ? d->Desc : "(none)");
    tr_printf(t, "  Instance ID:   %s\n", d->Id);
    tr_printf(t, "  Hardware ID:   %s\n", d->Hw[0] ? d->Hw : "(none)");
    tr_printf(t, "  Driver:        %s\n", d->Service[0] ? d->Service : "(none)");
    if (d->Driver[0] != '\0') {
        tr_printf(t, "  Driver key:    %s\n", d->Driver);
        driver_key_path(d, path, sizeof(path));
        if (reg_text(HKEY_LOCAL_MACHINE, path, "DriverVersion", ver,
                     sizeof(ver)) > 0) {
            tr_printf(t, "  Version:       %s\n", ver);
        }
    }
    tr_printf(t, "  Status:        0x%08lX (%s)\n", (unsigned long)d->Status,
              dn_started(d) ? "started" : "not started");
    tr_printf(t, "  Problem code:  %lu\n", (unsigned long)d->Problem);
    if (envNt && d->HasCaps) {
        tr_printf(t, "  Removable:     %s; surprise removal OK: %s\n",
                  (d->Caps & TR_CM_DEVCAP_REMOVABLE) ? "yes" : "no",
                  (d->Caps & TR_CM_DEVCAP_SURPRISEOK) ? "yes" : "no");
    }
    if (inst_starts(d->Id, "PCI\\")) {
        unsigned int bus;
        unsigned int dev;
        if (envNt && d->HasLoc) {
            tr_printf(t, "  PCI location:  %02lX:%02lX.%lu\n", (unsigned long)d->Bus,
                      (unsigned long)(d->Addr >> 16), (unsigned long)(d->Addr & 0xFFFFUL));
        } else if (!envNt && tr_pci_loc9x(d->Id, &bus, &dev)) {
            const char *fn = strstr(d->Id, "&FUNC_");
            tr_printf(t, "  PCI location:  %02X:%02X.%lu\n", bus, dev,
                      fn != NULL ? strtoul(fn + 6, NULL, 16) : 0UL);
        }
    }
    details_resources(t, d);
}

static void details_unit(TR_TEXT *t, int dn)
{
    const DNODE *d = &gw.N[dn];
    int u = d->UnitOf;
    int i;

    if (u < 0) {
        return;
    }
    {
        char name[160];
        unit_label(u, name, sizeof(name));
        tr_printf(t, "\nUnit behind a Thunderbolt port: %s\n", name);
        if (strcmp(name, dn_name(&gw.N[u])) != 0) {
            tr_printf(t, "  What stops: %s and everything on it.\n",
                      dn_name(&gw.N[u]));
        }
    }
    if (ej_why_not(u) == NULL) {
        tr_put(t, "  It can be stopped here (Action > Safely Remove).\n");
    } else if (!dn_started(&gw.N[u])) {
        char tag[64];
        char sen[96];
        tr_stopped_text(gw.N[u].Problem, tag, sizeof(tag), sen, sizeof(sen));
        tr_printf(t, "  %s\n", sen);
    } else {
        tr_printf(t, "  It cannot be stopped here now: %s.\n", ej_why_not(u));
    }
    if (envNt && gw.N[u].HasCaps) {
        if ((gw.N[u].Caps & TR_CM_DEVCAP_REMOVABLE) &&
            !(gw.N[u].Caps & TR_CM_DEVCAP_SURPRISEOK)) {
            tr_put(t, "  Windows lists it in Safely Remove Hardware too.\n");
        } else if (inst_unit_not_marked(1, gw.N[u].Caps)) {
            /* Whatever the cause (the first plug, a rebuilt stack:
             * EJRB.1). */
            tr_put(t, "  Not marked removable: Windows may not list it in "
                      "Safely Remove Hardware.\n  Safely Remove here stops "
                      "it with administrator rights, asking for them\n  when "
                      "needed.\n");
        } else {
            tr_put(t, "  Windows does not list it in Safely Remove Hardware; "
                      "stop it from here.\n");
        }
    } else if (!envNt) {
        tr_printf(t, "  %s\n", (gw.N[u].Status & TR_DN_REMOVABLE)
                                  ? "Marked removable by the Thunderbolt driver."
                                  : "Not marked removable.");
    }
    tr_put(t, "  Devices that stop with it:\n");
    for (i = u; i < gw.Count; i++) {
        if (gw.N[i].UnitOf == u) {
            tr_printf(t, "    %s\n", dn_name(&gw.N[i]));
        }
    }
}

static void details_nhi(TR_TEXT *t, int k)
{
    const NHI *h = &nhi[k];
    const unsigned char *R = h->R;
    TR_U32 j;
    TR_U32 ports;

    tr_printf(t, "Thunderbolt / USB4 host interface\n");
    if (R == NULL) {
        tr_printf(t, "  The Thunderbolt driver has not published its device "
                     "list (%s).\n", h->Why);
        return;
    }
    tr_printf(t, "  Source:              %s\n",
              h->HaveQ ? "the driver's query device" : "the registry record (shown only)");
    if (h->HaveQ) {
        tr_printf(t, "  Query device:        %s%s\n", h->QName,
                  h->ByName ? " (matched by name; this system gives no "
                              "instance ID)" : "");
        tr_printf(t, "  Last pass:           %lu, %lu ms ago%s\n",
                  (unsigned long)h->Q.Pass, (unsigned long)h->Q.AgeMs,
                  tr_query_current(&h->Q) ? "" : " (not current)");
        tr_printf(t, "  Liveness:            %s\n", tr_live_text(h->Q.Live));
        tr_printf(t, "  Start epoch:         %08lX%08lX\n",
                  (unsigned long)h->Q.EpochHi, (unsigned long)h->Q.EpochLo);
        if (h->Q.Flags & CM_Q_F_STOPPING) {
            tr_put(t, "  The controller is stopping.\n");
        }
    } else {
        tr_printf(t, "  Not confirmed: eject unavailable (%s).\n", h->Why);
    }
    tr_printf(t, "  NHI PCI ID:          %04lX:%04lX\n",
              (unsigned long)(inst_get32(R, CM_ST_O_NHI_ID) & 0xFFFFUL),
              (unsigned long)(inst_get32(R, CM_ST_O_NHI_ID) >> 16));
    tr_printf(t, "  Connection manager:  %s\n", tr_cm_text(inst_get32(R, CM_ST_O_CM)));
    tr_printf(t, "  Profile:             %lu\n",
              (unsigned long)inst_get32(R, CM_ST_O_PROFILE));
    tr_printf(t, "  Stage:               %s",
              tr_stage_text(inst_get32(R, CM_ST_O_STAGE)));
    if (inst_get32(R, CM_ST_O_STAGE) == CM_ST_STAGE_INERT) {
        tr_printf(t, " (reason %lu)", (unsigned long)inst_get32(R, CM_ST_O_INERT));
    }
    tr_put(t, "\n");
    if (inst_get32(R, CM_ST_O_SECURITY) != CM_ST_UNKNOWN) {
        tr_printf(t, "  Security level:      %lu\n",
                  (unsigned long)inst_get32(R, CM_ST_O_SECURITY));
    }
    if (inst_get32(R, CM_ST_O_FW_VERSION) != CM_ST_UNKNOWN) {
        tr_printf(t, "  Firmware version:    0x%08lX\n",
                  (unsigned long)inst_get32(R, CM_ST_O_FW_VERSION));
    }
    if (h->HaveQ) {
        /* The embedded record's own generation is stamped only by the
         * registry write; the query carries its own (cm_query.h). */
        tr_printf(t, "  Content generation:  %lu (pass %lu)\n",
                  (unsigned long)h->Q.ContentGen, (unsigned long)h->Q.Pass);
    } else {
        tr_printf(t, "  Record generation:   %lu\n",
                  (unsigned long)inst_get32(R, CM_ST_O_GENERATION));
    }
    ports = inst_get32(R, CM_ST_O_PORT_COUNT);
    for (j = 0; j < ports && j < CM_ST_MAX_PORTS; j++) {
        TR_U32 bdf = inst_get32(R, CM_ST_O_PORTS + 4 * j);
        tr_printf(t, "  Tunnel port %lu:       PCI %02lX:%02lX.%lu%s\n",
                  (unsigned long)(j + 1), (unsigned long)CM_ST_BDF_BUS(bdf),
                  (unsigned long)CM_ST_BDF_DEV(bdf), (unsigned long)CM_ST_BDF_FN(bdf),
                  (inst_get32(R, CM_ST_O_PORT_ACTIVE) & (1UL << j)) ? ", active" : "");
    }
    details_devnode(t, h->Node);
}

static void details_router(TR_TEXT *t, int k, int r)
{
    const NHI *h;
    const unsigned char *e = router_entry(k, r);
    TR_U32 kind;
    TR_U32 place;
    TR_U32 ids;
    TR_U32 tun;
    TR_U32 bridge;
    TR_EXT x;
    int haveX;
    char name[TR_NAME_CHARS];
    char gen[32];
    char link[96];
    int i;

    if (e == NULL) {
        tr_put(t, "This device is no longer in the list; press F5.\n");
        return;
    }
    h = &nhi[k];
    kind = inst_get32(e, CM_ST_E_KIND);
    place = inst_get32(e, CM_ST_E_PLACE);
    ids = inst_get32(e, CM_ST_E_IDS);
    tun = inst_get32(e, CM_ST_E_TUNNELS);
    bridge = inst_get32(e, CM_ST_E_BRIDGE);
    haveX = h->HaveQ && tr_query_ext(&h->Q, (TR_U32)r, &x);
    tr_router_name(e, name, sizeof(name));
    tr_gen_text(inst_get32(e, CM_ST_E_GEN), gen, sizeof(gen));
    tr_link_text_gen(inst_get32(e, CM_ST_E_GEN), kind,
                     inst_get32(e, CM_ST_E_LANES), haveX ? &x : NULL, link,
                     sizeof(link));
    tr_printf(t, "Thunderbolt / USB4 device\n");
    tr_printf(t, "  Name:          %s\n", name);
    tr_printf(t, "  Kind:          %s\n",
              kind == CM_ST_KIND_ICM ? "announced by the controller's firmware"
              : kind == CM_ST_KIND_USB4_PORT ? "router on a host USB4 port"
                                             : "router behind another router");
    tr_printf(t, "  Generation:    %s\n", gen);
    tr_printf(t, "  Position:      %s %lu, depth %lu\n",
              kind == CM_ST_KIND_ICM ? "link" : "port",
              (unsigned long)(place & 0xFFUL), (unsigned long)((place >> 8) & 0xFFUL));
    tr_printf(t, "  Route:         %08lX%08lX\n",
              (unsigned long)inst_get32(e, CM_ST_E_ROUTE_HI),
              (unsigned long)inst_get32(e, CM_ST_E_ROUTE_LO));
    if (ids != CM_ST_UNKNOWN) {
        tr_printf(t, "  IDs:           %04lX:%04lX\n", (unsigned long)(ids & 0xFFFFUL),
                  (unsigned long)(ids >> 16));
    } else if (haveX && x.Ids != 0) {
        tr_printf(t, "  IDs:           %04lX:%04lX\n", (unsigned long)(x.Ids & 0xFFFFUL),
                  (unsigned long)(x.Ids >> 16));
    }
    if (inst_get32(e, CM_ST_E_SHORT_ID) != CM_ST_UNKNOWN) {
        tr_printf(t, "  UUID ends:     %04lX\n",
                  (unsigned long)inst_get32(e, CM_ST_E_SHORT_ID));
    }
    tr_printf(t, "  Approval:      %s\n", tr_appr_text(inst_get32(e, CM_ST_E_APPROVAL)));
    tr_printf(t, "  Link:          %s\n", link);
    if (haveX && x.LinkState != CM_Q_UNKNOWN) {
        tr_printf(t, "  Link state:    %lu (as read)\n", (unsigned long)x.LinkState);
    }
    tr_printf(t, "  Tunnels:       PCIe %s, USB3 %s, DisplayPort %s\n",
              tr_tun_text(tun), tr_tun_text(tun >> 4), tr_tun_text(tun >> 8));
    if (bridge != CM_ST_UNKNOWN) {
        tr_printf(t, "  Tunnel port:   PCI %02lX:%02lX.%lu\n",
                  (unsigned long)CM_ST_BDF_BUS(bridge),
                  (unsigned long)CM_ST_BDF_DEV(bridge),
                  (unsigned long)CM_ST_BDF_FN(bridge));
        for (i = 0; i < gw.Count; i++) {
            if (gw.N[i].PortNhi == k && port_bdf(&gw, nhi, i) == bridge) {
                int c;
                for (c = i + 1; c < gw.Count; c++) {
                    if (gw.N[c].Parent == i) {
                        details_unit(t, c);
                    }
                }
            }
        }
    }
}

static void details_hubnode(TR_TEXT *t, const unsigned char *ni)
{
    tr_printf(t, "  Power:         %s\n", ni[75] ? "bus-powered" : "self-powered");
    tr_dec_hub(t, ni + 4, 71);
}

static void details_uport(TR_TEXT *t, int i, int fetch)
{
    UPORT *p = &up[i];
    const UCTRL *c = &uc[p->Ctrl];
    int speed;
    unsigned int ma;
    int self;
    char ssp[96];

    tr_printf(t, "USB port %s on %s\n", p->Path,
              c->Dn >= 0 ? dn_name(&gw.N[c->Dn]) : "a USB host controller");
    if (!p->HaveConn) {
        tr_put(t, "  The hub did not report this port.\n");
        return;
    }
    tr_printf(t, "  Connection:    %s\n", tr_conn_status_text(p->Conn.Status));
    if (p->Conn.Status != TR_CS_CONNECTED) {
        return;
    }
    if (fetch) {
        usb_fetch(p);
    }
    speed = uport_speed(p);
    tr_printf(t, "  Speed:         %s%s\n", tr_speed_name(speed),
              p->Ex ? "" : " (from the original connection request)");
    if (p->HaveV2) {
        char protos[48];
        tr_protocols_text(p->V2Proto, protos, sizeof(protos));
        tr_printf(t, "  Port supports: %s; device %s\n", protos,
                  (p->V2Flags & TR_V2_F_SSP_CAPABLE) ? "SuperSpeedPlus capable"
                  : (p->V2Flags & TR_V2_F_SS_CAPABLE) ? "SuperSpeed capable"
                                                      : "not SuperSpeed capable");
    }
    if (p->Bos != NULL && tr_bos_ssp_text(p->Bos, p->BosN, ssp, sizeof(ssp))) {
        tr_printf(t, "  Supported:     %s (the negotiated lane rate and count "
                     "are not reported)\n", ssp);
    }
    tr_printf(t, "  Address:       %u\n", p->Conn.Address);
    tr_printf(t, "  VID:PID:       %04X:%04X\n",
              (unsigned int)p->Conn.Dev[8] | ((unsigned int)p->Conn.Dev[9] << 8),
              (unsigned int)p->Conn.Dev[10] | ((unsigned int)p->Conn.Dev[11] << 8));
    tr_printf(t, "  Class:         0x%02X (%s)\n", (unsigned int)p->Conn.Dev[4],
              tr_class_name(p->Conn.Dev[4]));
    if (p->HaveStr[0]) {
        tr_printf(t, "  Manufacturer:  %s\n", p->Str[0]);
    }
    if (p->HaveStr[1]) {
        tr_printf(t, "  Product:       %s\n", p->Str[1]);
    }
    if (p->HaveStr[2]) {
        tr_printf(t, "  Serial number: %s\n", p->Str[2]);
    }
    if (!p->Fetched) {
        tr_put(t, "  (strings, configuration and BOS: View > Descriptors "
                  "reads them)\n");
    }
    tr_printf(t, "  Configuration: %u\n", p->Conn.Config);
    if (p->Cfg != NULL && tr_config_power(p->Cfg, p->CfgN, speed, &ma, &self)) {
        tr_printf(t, "  MaxPower:      %u mA (%s)\n", ma,
                  self ? "self-powered" : "bus-powered");
    }
    tr_printf(t, "  Open pipes:    %lu\n", (unsigned long)p->Conn.OpenPipes);
    tr_printf(t, "  Driver key:    %s\n", p->DrvKey[0] ? p->DrvKey : "(not reported)");
    if (p->Conn.IsHub) {
        tr_printf(t, "  Hub:           %s\n", p->HubName[0] ? p->HubName : "(no name)");
        if (p->HaveHubNode) {
            details_hubnode(t, p->HubNode);
        }
    }
    details_usb_eject(t, p->Dn);
    details_devnode(t, p->Dn);
    if ((viewFlags & TR_V_DESC) || reportMode) {
        int k;
        tr_put(t, "\n");
        tr_dec_device(t, p->Conn.Dev, 18);
        if (p->Cfg != NULL) {
            tr_dec_config(t, p->Cfg, p->CfgN);
        }
        if (p->Bos != NULL) {
            tr_dec_bos(t, p->Bos, p->BosN);
        }
        for (k = 0; k < 3; k++) {
            if (p->StrN[k] != 0) {
                tr_printf(t, "String descriptor %u (%s)\n",
                          (unsigned int)p->Conn.Dev[14 + k],
                          k == 0 ? "manufacturer" : k == 1 ? "product"
                                                         : "serial number");
                tr_printf(t, "  \"%s\"\n", p->HaveStr[k] ? p->Str[k] : "(malformed)");
                tr_hex(t, p->StrRaw[k], p->StrN[k], 2);
            }
        }
        for (k = 0; k < p->Conn.PipeCount; k++) {
            tr_printf(t, "Open pipe: endpoint 0x%02X\n", (unsigned int)p->Conn.Pipe[k][2]);
        }
    }
}

static void details_of(TR_TEXT *t, int i, int fetch)
{
    const TNODE *n = &tn[i];
    int k;

    switch (n->Kind) {
    case K_ROOT:
        tr_printf(t, "xHCI98 USB Tree %s\n\n%d USB device%s, %d Thunderbolt/USB4 "
                     "device%s.\n", XHCITR_VERSION, usbDevices,
                  tr_plural(usbDevices), tbDevices, tr_plural(tbDevices));
        if (gw.Bad) {
            tr_printf(t, "%d device entries could not be read whole.\n", gw.Bad);
        }
        break;
    case K_TBROOT:
        tr_printf(t, "%d Thunderbolt or USB4 host interface%s driven by xhcicm.\n",
                  nhiCount, tr_plural(nhiCount));
        for (k = 0; k < nhiCount; k++) {
            tr_printf(t, "  %s\n", dn_name(&gw.N[nhi[k].Node]));
        }
        break;
    case K_NHI:
        details_nhi(t, n->A);
        break;
    case K_PORT:
        tr_printf(t, "Thunderbolt tunnel port %d of %s\n", n->B + 1,
                  dn_name(&gw.N[nhi[n->A].Node]));
        if (n->Dn >= 0) {
            details_devnode(t, n->Dn);
        } else {
            tr_put(t, "  Its PCI bridge was not found in the device tree.\n");
        }
        break;
    case K_ROUTER:
    case K_TUNNEL:
        details_router(t, n->A, n->B);
        break;
    case K_DEV:
        tr_printf(t, "%s\n", dn_name(&gw.N[n->Dn]));
        details_unit(t, n->Dn);
        details_devnode(t, n->Dn);
        break;
    case K_USBROOT:
        tr_printf(t, "%d USB host controller%s, %d USB device%s.\n", ucCount,
                  tr_plural(ucCount), usbDevices, tr_plural(usbDevices));
        if (hcdOpenError != 0) {
            tr_printf(t, "\\\\.\\HCD0 did not open (error %lu).\n",
                      (unsigned long)hcdOpenError);
        }
        if (hcdNameRefused != 0) {
            tr_printf(t, "%d controllers did not report their driver key.\n",
                      hcdNameRefused);
        }
        break;
    case K_CTRL: {
        const UCTRL *c = &uc[n->A];
        unsigned int ports = 0;
        int bus;
        tr_printf(t, "USB host controller (%s)\n", c->Ours ? "xhci98" : "Windows");
        tr_printf(t, "  Opened as:     %s\n", c->Hcd[0] ? c->Hcd : c->Path);
        if (c->HaveNode && tr_node_parse(c->Node, sizeof(c->Node), &ports, &bus)) {
            tr_printf(t, "  Root ports:    %u\n", ports);
        }
        tr_printf(t, "  Root hub:      %s\n", c->RootHub[0] ? c->RootHub : "(not reported)");
        if (c->Behind[0]) {
            tr_printf(t, "  Location:      %s\n", c->Behind);
        }
        details_devnode(t, c->Dn);
        break;
    }
    case K_ROOTHUB:
        tr_printf(t, "Root hub of %s\n", uc[n->A].Path);
        if (uc[n->A].HaveNode) {
            details_hubnode(t, uc[n->A].Node);
        }
        break;
    case K_UPORT:
        details_uport(t, n->A, fetch);
        break;
    default:
        tr_printf(t, "%s\n", n->Label);
        break;
    }
}

/* ======================================================================== */
/* The report (record 25 sections 3.1 and 7.3)                               */
/* ======================================================================== */

static int depth_of(int i)
{
    int d = 0;

    while (tn[i].Parent >= 0) {
        i = tn[i].Parent;
        d++;
    }
    return d;
}

static int write_report(const char *path)
{
    static char text[TR_DETAIL_BYTES];
    TR_TEXT t;
    TR_TEXT d;
    HANDLE f;
    SYSTEMTIME st;
    DWORD w;
    int i;
    int ok = 1;

    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return 0;
    }
    reportMode = 1;
    GetLocalTime(&st);
    tr_text_init(&t, text, sizeof(text));
    tr_printf(&t, "xHCI98 USB Tree %s - report\nWindows %lu.%lu%s, %04u-%02u-%02u "
                  "%02u:%02u\n%d USB device%s, %d Thunderbolt/USB4 device%s\n\n",
              XHCITR_VERSION, (unsigned long)envMajor, (unsigned long)envMinor,
              envNt ? "" : " (98/ME)", (unsigned int)st.wYear,
              (unsigned int)st.wMonth, (unsigned int)st.wDay,
              (unsigned int)st.wHour, (unsigned int)st.wMinute, usbDevices,
              tr_plural(usbDevices), tbDevices, tr_plural(tbDevices));
    ok = WriteFile(f, text, (DWORD)t.Len, &w, NULL) && w == t.Len;
    for (i = 0; i < tnCount && ok; i++) {
        tr_text_init(&d, detail, sizeof(detail));
        details_of(&d, i, 1);
        tr_text_init(&t, text, sizeof(text));
        tr_report_node(&t, depth_of(i), tn[i].Label, detail);
        tr_put(&t, "\n");
        ok = WriteFile(f, text, (DWORD)t.Len, &w, NULL) && w == t.Len;
    }
    CloseHandle(f);
    reportMode = 0;
    return ok;
}

/* ======================================================================== */
/* The shared mutex and the quit event (record 25 sections 6.3 and 7.1)      */
/* ======================================================================== */

typedef BOOL (WINAPI *PFN_ALLOCSID)(PSID_IDENTIFIER_AUTHORITY, BYTE, DWORD,
                                    DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                    DWORD, PSID *);
typedef PVOID (WINAPI *PFN_FREESID)(PSID);
typedef DWORD (WINAPI *PFN_SIDLEN)(PSID);
typedef BOOL (WINAPI *PFN_INITSD)(PSECURITY_DESCRIPTOR, DWORD);
typedef BOOL (WINAPI *PFN_INITACL)(PACL, DWORD, DWORD);
typedef BOOL (WINAPI *PFN_ADDACE)(PACL, DWORD, DWORD, PSID);
typedef BOOL (WINAPI *PFN_SETDACL)(PSECURITY_DESCRIPTOR, BOOL, PACL, BOOL);

typedef struct _TR_SA {
    SECURITY_ATTRIBUTES Sa;
    SECURITY_DESCRIPTOR Sd;
    unsigned char Acl[256];
    int Ok;
} TR_SA;

/* NT: everyone gets `world`, SYSTEM and Administrators everything, so an
 * unelevated tray and an elevated XHCIINST can both open the object. */
static void make_sa(TR_SA *s, DWORD world)
{
    HMODULE adv = GetModuleHandleA("advapi32.dll");
    PFN_ALLOCSID allocSid = (PFN_ALLOCSID)proc(adv, "AllocateAndInitializeSid");
    PFN_FREESID freeSid = (PFN_FREESID)proc(adv, "FreeSid");
    PFN_INITSD initSd = (PFN_INITSD)proc(adv, "InitializeSecurityDescriptor");
    PFN_INITACL initAcl = (PFN_INITACL)proc(adv, "InitializeAcl");
    PFN_ADDACE addAce = (PFN_ADDACE)proc(adv, "AddAccessAllowedAce");
    PFN_SETDACL setDacl = (PFN_SETDACL)proc(adv, "SetSecurityDescriptorDacl");
    SID_IDENTIFIER_AUTHORITY worldAuth = SECURITY_WORLD_SID_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID sWorld = NULL;
    PSID sSystem = NULL;
    PSID sAdmins = NULL;

    memset(s, 0, sizeof(*s));
    if (!envNt || allocSid == NULL || freeSid == NULL || initSd == NULL ||
        initAcl == NULL || addAce == NULL || setDacl == NULL) {
        return;
    }
    if (allocSid(&worldAuth, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &sWorld) &&
        allocSid(&ntAuth, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0,
                 &sSystem) &&
        allocSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sAdmins) &&
        initAcl((PACL)s->Acl, sizeof(s->Acl), ACL_REVISION) &&
        addAce((PACL)s->Acl, ACL_REVISION, world, sWorld) &&
        addAce((PACL)s->Acl, ACL_REVISION, GENERIC_ALL, sSystem) &&
        addAce((PACL)s->Acl, ACL_REVISION, GENERIC_ALL, sAdmins) &&
        initSd(&s->Sd, SECURITY_DESCRIPTOR_REVISION) &&
        setDacl(&s->Sd, TRUE, (PACL)s->Acl, FALSE)) {
        s->Sa.nLength = sizeof(s->Sa);
        s->Sa.lpSecurityDescriptor = &s->Sd;
        s->Sa.bInheritHandle = FALSE;
        s->Ok = 1;
    }
    if (sWorld != NULL) {
        freeSid(sWorld);
    }
    if (sSystem != NULL) {
        freeSid(sSystem);
    }
    if (sAdmins != NULL) {
        freeSid(sAdmins);
    }
}

#define TR_MUTEX_RIGHTS (SYNCHRONIZE | 0x0001)  /* MUTEX_MODIFY_STATE */

/* The eject mutex XHCIINST shares: NULL when another program holds it past
 * 10 s or it cannot be had. */
static HANDLE ej_lock(void)
{
    const char *name = envNt ? "Global\\XhciinstTbAsk" : "XhciinstTbAsk";
    HANDLE m = NULL;
    DWORD r;

    m = OpenMutexA(TR_MUTEX_RIGHTS, FALSE, name);
    if (m == NULL) {
        TR_SA sa;
        make_sa(&sa, TR_MUTEX_RIGHTS);
        m = CreateMutexA(sa.Ok ? &sa.Sa : NULL, FALSE, name);
        if (m == NULL) {
            /* A lost race to create it opens the winner's. */
            m = OpenMutexA(TR_MUTEX_RIGHTS, FALSE, name);
        }
    }
    if (m == NULL) {
        return NULL;
    }
    r = WaitForSingleObject(m, 10000);
    if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) {
        return m;
    }
    CloseHandle(m);
    return NULL;
}

static void ej_unlock(HANDLE m)
{
    if (m != NULL) {
        ReleaseMutex(m);
        CloseHandle(m);
    }
}

/* The installer's quit event (record 25 section 7.1): the tray never
 * creates it; it looks once a second whether it exists and is set. */
static int quit_asked(void)
{
    HANDLE e = OpenEventA(SYNCHRONIZE, FALSE,
                          envNt ? "Global\\Xhci98TrQuit" : "Xhci98TrQuit");
    int set;

    if (e == NULL) {
        return 0;
    }
    set = WaitForSingleObject(e, 0) == WAIT_OBJECT_0;
    CloseHandle(e);
    return set;
}

/* ======================================================================== */
/* Eject (record 25 section 6.2)                                             */
/* ======================================================================== */

typedef struct _EJREQ {
    char UnitId[INST_ID_CHARS];
    char PortId[INST_ID_CHARS];
    char NhiId[INST_ID_CHARS];
    char QName[24];
    int NameUnique;         /* the condition of a match by name           */
    int ByName;
    char Desc[160];         /* what the user sees: disks and letters      */
    char UnitName[128];     /* what stops: the unit's own devnode         */
    TR_U32 EpochLo;
    TR_U32 EpochHi;
    TR_U32 PortBdf;
    int DevCount;
    char Devs[TR_EJ_DEVS][INST_ID_CHARS];
} EJREQ;

/* The request as the tree last showed the unit: 1 when it is one. */
static int ej_prepare(int u, EJREQ *q)
{
    const DNODE *d;
    int i;
    int k;

    memset(q, 0, sizeof(*q));
    if (u < 0 || u >= gw.Count || !gw.N[u].Ejectable || gw.N[u].Parent < 0) {
        return 0;
    }
    d = &gw.N[u];
    k = gw.N[d->Parent].PortNhi;
    if (k < 0 || !nhi[k].HaveQ) {
        return 0;
    }
    tr_copy(q->UnitId, d->Id, sizeof(q->UnitId));
    tr_copy(q->PortId, gw.N[d->Parent].Id, sizeof(q->PortId));
    tr_copy(q->NhiId, nhi[k].Id, sizeof(q->NhiId));
    tr_copy(q->QName, nhi[k].QName, sizeof(q->QName));
    q->NameUnique = nhi[k].NameUnique;
    q->ByName = nhi[k].ByName;
    unit_label(u, q->Desc, sizeof(q->Desc));
    tr_copy(q->UnitName, dn_name(d), sizeof(q->UnitName));
    q->EpochLo = nhi[k].Q.EpochLo;
    q->EpochHi = nhi[k].Q.EpochHi;
    q->PortBdf = port_bdf(&gw, nhi, d->Parent);
    for (i = u; i < gw.Count; i++) {
        if (gw.N[i].UnitOf == u) {
            if (q->DevCount >= TR_EJ_DEVS) {
                return 0;   /* more devices than one confirmation lists */
            }
            tr_copy(q->Devs[q->DevCount++], gw.N[i].Id, sizeof(q->Devs[0]));
        }
    }
    return 1;
}

/* Why a unit cannot be stopped now, or NULL. */
static const char *ej_why_not(int u)
{
    int k;
    const char *why = NULL;

    if (u < 0 || u >= gw.Count || gw.N[u].Parent < 0) {
        return "it is not behind a Thunderbolt port";
    }
    k = gw.N[gw.N[u].Parent].PortNhi;
    if (k < 0) {
        return "it is not behind a Thunderbolt port";
    }
    if (!dn_started(&gw.N[u])) {
        /* Held for the eject (Code 47): the ICM keeps the tunnel until the
         * unplug, so it is still listed. Any other problem is a failed
         * start, never "unplug it". */
        static char tag[64];
        char sen[8];
        tr_stopped_text(gw.N[u].Problem, tag, sizeof(tag), sen, sizeof(sen));
        return tag;
    }
    if (!nhi[k].HaveQ) {
        return "not confirmed: eject unavailable";
    }
    if (!gw.N[u].Ejectable) {
        return "it holds the Thunderbolt controller's own functions";
    }
    if (!tr_query_eject_ready(&nhi[k].Q, &why)) {
        return why;
    }
    if (envNt ? api.Eject == NULL : (api.QueryRemove == NULL || api.Remove == NULL)) {
        return "this Windows does not offer the call";
    }
    return NULL;
}

/* Every devnode the request holds is listed; a list that does not fit is
 * refused, never shortened (nothing is stopped that was not shown). */
static int ej_confirm(HWND owner, const EJREQ *q)
{
    static char text[TR_EJ_DEVS * (INST_ID_CHARS + 8) + 512];
    TR_TEXT t;
    int i;

    tr_text_init(&t, text, sizeof(text));
    {
        static char lineBuf[TR_EJ_DEVS][256];
        const char *lines[TR_EJ_DEVS];
        int k;
        int disks = 0;
        int u = walk_find_id(&gw, q->UnitId);
        if (u >= 0) {
            for (k = u; k < gw.Count && disks < TR_EJ_DEVS; k++) {
                if (gw.N[k].UnitOf == u && gw.N[k].IsDisk) {
                    tr_disk_line(gw.N[k].Friendly[0] ? gw.N[k].Friendly
                                                     : dn_name(&gw.N[k]),
                                 gw.N[k].Letters, lineBuf[disks],
                                 sizeof(lineBuf[disks]));
                    lines[disks] = lineBuf[disks];
                    disks++;
                }
            }
        }
        tr_confirm_lead(&t, q->UnitName, q->Desc, lines, disks);
    }
    for (i = 0; i < q->DevCount; i++) {
        int n = walk_find_id(&gw, q->Devs[i]);
        tr_printf(&t, "    %s\n", n >= 0 ? dn_name(&gw.N[n]) : q->Devs[i]);
    }
    tr_put(&t, "\nClose any program that uses them first.");
    if (t.Overflow) {
        MessageBoxA(owner, "The list of devices to stop is too long to show, so "
                    "nothing is stopped from here.", "Stop a Thunderbolt device",
                    MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        return 0;
    }
    return MessageBoxA(owner, text, "Stop a Thunderbolt device",
                       MB_OKCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND) == IDOK;
}

/* The query of the request's NHI: 1 when it answered for that NHI. */
static int ej_query(const EJREQ *q, unsigned char **buf, TR_QUERY *a)
{
    return query_read(q->QName, q->NhiId, buf, a, q->NameUnique, NULL);
}

/* The unit in a fresh walk, checked against what the user confirmed. */
static int ej_find(WALK *w, NHI *one, const EJREQ *q)
{
    int u = walk_find_id(w, q->UnitId);
    int i;
    int n = 0;

    derive(w, one, 1);
    if (u < 0 || !w->N[u].Ejectable || w->N[u].Parent < 0 ||
        !tr_eq(w->N[w->N[u].Parent].Id, q->PortId) ||
        port_bdf(w, one, w->N[u].Parent) != q->PortBdf) {
        return -1;
    }
    for (i = u; i < w->Count; i++) {
        if (w->N[i].UnitOf == u) {
            int k;
            int found = 0;
            for (k = 0; k < q->DevCount; k++) {
                if (tr_eq(q->Devs[k], w->N[i].Id)) {
                    found = 1;
                }
            }
            if (!found) {
                return -1;
            }
            n++;
        }
    }
    return n == q->DevCount ? u : -1;
}

/* A storage function with no PCI function below it, neither a bridge nor
 * a USB controller (record 24 section 5.4; xhciinst.c's tb_single). */
static int ej_single(const WALK *w, int u)
{
    const DNODE *d = &w->N[u];
    int i;

    if (d->IsUsbHc || d->Candidate ||
        inst_msz_has_class(d->Hw, d->HwBytes, "0604") ||
        inst_msz_has_class(d->Compat, d->CompatBytes, "0604")) {
        return 0;
    }
    if (!inst_msz_has_class(d->Hw, d->HwBytes, "01") &&
        !inst_msz_has_class(d->Compat, d->CompatBytes, "01")) {
        return 0;
    }
    for (i = u + 1; i < w->Count; i++) {
        if (w->N[i].UnitOf == u && inst_starts(w->N[i].Id, "PCI\\")) {
            return 0;
        }
    }
    return 1;
}

/* Under the USB4 profile, the query watched for the router's PCIe tunnel
 * closed after the eject (record 24 section 5.2; xhciinst.c). */
static const char *ej_watch_tunnel(const EJREQ *q)
{
    DWORD start = GetTickCount();

    for (;;) {
        unsigned char *b;
        TR_QUERY a;
        if (ej_query(q, &b, &a)) {
            TR_U32 n = inst_get32(a.Rec, CM_ST_O_ROUTER_COUNT);
            TR_U32 r;
            int usb4 = inst_get32(a.Rec, CM_ST_O_CM) == CM_ST_CM_USB4;
            int closed = 0;
            for (r = 0; r < n; r++) {
                const unsigned char *e = a.Rec + CM_ST_HDR_BYTES + r * CM_ST_ENTRY_BYTES;
                if (inst_get32(e, CM_ST_E_BRIDGE) == q->PortBdf &&
                    (inst_get32(e, CM_ST_E_TUNNELS) & 0xFUL) == CM_ST_TUN_EJECTED) {
                    closed = 1;
                }
            }
            free(b);
            if (!usb4) {
                return NULL;
            }
            if (closed) {
                return "The PCIe tunnel was closed.";
            }
        }
        if (GetTickCount() - start >= 10000) {
            return "The Thunderbolt driver has not confirmed that the PCIe tunnel "
                   "was closed.";
        }
        Sleep(250);
    }
}

/* After an accepted eject: a steady reading, the port read again, and
 * record 24 section 5.4's advice (xhciinst.c's tb_after). */
static int ej_after(const EJREQ *q, int single)
{
    WALK w;
    unsigned char *b1 = NULL;
    unsigned char *b2 = NULL;
    TR_QUERY a1;
    TR_QUERY a2;
    NHI *one;
    int whole = 0;
    int walked = 0;
    int tries;
    int port;
    int others = 0;
    int i;
    int kind;
    TR_U32 usb3 = CM_ST_TUN_UNKNOWN;

    memset(&w, 0, sizeof(w));
    one = (NHI *)calloc(1, sizeof(NHI));
    if (one == NULL) {
        return INST_UNPLUG_CHECK;
    }
    for (tries = 0; tries < 3 && !whole; tries++) {
        free(b1);
        free(b2);
        b1 = b2 = NULL;
        walked = 0;
        if (!ej_query(q, &b1, &a1) || a1.EpochLo != q->EpochLo ||
            a1.EpochHi != q->EpochHi) {
            break;
        }
        walked = walk(&w);
        if (!walked || !ej_query(q, &b2, &a2)) {
            walked = 0;
            break;
        }
        whole = w.Bad == 0 && tr_query_same_content(&a1, &a2);
    }
    if (!walked) {
        free(b1);
        free(b2);
        free(w.N);
        free(one);
        return INST_UNPLUG_CHECK;
    }
    tr_copy(one->Id, q->NhiId, sizeof(one->Id));
    one->HaveQ = 1;
    one->Q = a2;
    one->R = a2.Rec;
    derive(&w, one, 1);
    port = walk_find_id(&w, q->PortId);
    if (port >= 0) {
        for (i = port + 1; i < w.Count; i++) {
            if (w.N[i].Parent != port || !dn_started(&w.N[i])) {
                continue;
            }
            if (tr_eq(w.N[i].Id, q->UnitId)) {
                kind = INST_UNPLUG_RESTARTED;
                goto done;
            }
            others = 1;
        }
    } else {
        whole = 0;
    }
    if (whole) {
        TR_U32 n = inst_get32(a2.Rec, CM_ST_O_ROUTER_COUNT);
        TR_U32 ports = inst_get32(a2.Rec, CM_ST_O_PORT_COUNT);
        TR_U32 active = inst_get32(a2.Rec, CM_ST_O_PORT_ACTIVE);
        TR_U32 r;
        TR_U32 j;
        int routers = 0;
        int portKnown = 0;
        /* The port must still be an active member of the set, as
         * xhciinst.c's tb_after requires (record 24 section 5.4). */
        for (j = 0; j < ports && j < CM_ST_MAX_PORTS; j++) {
            if ((active & (1UL << j)) != 0 &&
                inst_get32(a2.Rec, CM_ST_O_PORTS + 4 * j) == q->PortBdf) {
                portKnown = 1;
            }
        }
        for (r = 0; portKnown && r < n; r++) {
            const unsigned char *e = a2.Rec + CM_ST_HDR_BYTES + r * CM_ST_ENTRY_BYTES;
            if (inst_get32(e, CM_ST_E_BRIDGE) == q->PortBdf) {
                routers++;
                usb3 = (inst_get32(e, CM_ST_E_TUNNELS) >> 4) & 0xFUL;
            }
        }
        if (routers != 1) {
            usb3 = CM_ST_TUN_UNKNOWN;
        }
    }
    kind = inst_unplug_kind(whole, others, single, usb3);
done:
    free(b1);
    free(b2);
    free(w.N);
    free(one);
    return kind;
}

/* ---- NT: a unit not marked removable (record 24 section 7.2) ------------- */

typedef BOOL (WINAPI *PFN_OPENTOKEN)(HANDLE, DWORD, PHANDLE);
typedef BOOL (WINAPI *PFN_TOKENINFO)(HANDLE, int, LPVOID, DWORD, PDWORD);
typedef BOOL (WINAPI *PFN_CHECKMEMBER)(HANDLE, PSID, PBOOL);
typedef BOOL (WINAPI *PFN_ALLOCSID2)(PSID_IDENTIFIER_AUTHORITY, BYTE, DWORD,
                                     DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                     DWORD, PSID *);
typedef PVOID (WINAPI *PFN_FREESID2)(PSID);
#define TR_TOKEN_ELEVATION  20      /* TokenElevation (Vista and later) */

/* Vista and 7: 1 elevated, 0 not, -1 not read. */
static int ej_elevated_token(void)
{
    HMODULE adv = GetModuleHandleA("advapi32.dll");
    PFN_OPENTOKEN openToken = (PFN_OPENTOKEN)proc(adv, "OpenProcessToken");
    PFN_TOKENINFO tokenInfo = (PFN_TOKENINFO)proc(adv, "GetTokenInformation");
    HANDLE token;
    DWORD elevated = 0;
    DWORD n = 0;
    int r = -1;

    if (openToken == NULL || tokenInfo == NULL ||
        !openToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return -1;
    }
    if (tokenInfo(token, TR_TOKEN_ELEVATION, &elevated, sizeof(elevated), &n) &&
        n == sizeof(elevated)) {
        r = elevated != 0;
    }
    CloseHandle(token);
    return r;
}

/* 0 on Vista and 7 when this process's token is not elevated, so the
 * fallback's right is known to be missing; otherwise 1, Windows' answer
 * deciding. */
static int ej_may_remove(void)
{
    return !envNt || envMajor < 6 || ej_elevated_token() != 0;
}

/* NT: 1 when this process runs with an administrator's rights (Vista and
 * 7: elevated; 2000 and XP: a member of Administrators); 1 too when it
 * cannot be read, Windows' answer then deciding. */
static int ej_is_admin(void)
{
    HMODULE adv;
    PFN_CHECKMEMBER check;
    PFN_ALLOCSID2 allocSid;
    PFN_FREESID2 freeSid;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID admins = NULL;
    BOOL member = FALSE;
    int r = 1;

    if (!envNt) {
        return 1;
    }
    if (envMajor >= 6) {
        return ej_elevated_token() != 0;
    }
    adv = GetModuleHandleA("advapi32.dll");
    check = (PFN_CHECKMEMBER)proc(adv, "CheckTokenMembership");
    allocSid = (PFN_ALLOCSID2)proc(adv, "AllocateAndInitializeSid");
    freeSid = (PFN_FREESID2)proc(adv, "FreeSid");
    if (check == NULL || allocSid == NULL || freeSid == NULL) {
        return 1;
    }
    if (allocSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) {
        if (check(NULL, admins, &member)) {
            r = member != FALSE;
        }
        freeSid(admins);
    }
    return r;
}

/* CM_Query_And_Remove_SubTreeW, its veto name in the ANSI code page, the
 * flags as inst_removal_retry says. */
static DWORD ej_query_and_remove(DWORD dn, ULONG *veto, char *name, int cap)
{
    WCHAR wide[MAX_PATH];
    ULONG flags = INST_REMOVE_UI_NOT_OK | INST_REMOVE_NO_RESTART;
    DWORD cr;

    for (;;) {
        *veto = 0;
        wide[0] = 0;
        cr = api.QueryAndRemove(dn, veto, wide, MAX_PATH, flags);
        if (!inst_removal_retry(cr, flags)) {
            break;
        }
        flags = INST_REMOVE_UI_NOT_OK;
    }
    wide[MAX_PATH - 1] = 0;
    name[0] = '\0';
    if (WideCharToMultiByte(CP_ACP, 0, wide, -1, name, cap, NULL, NULL) == 0) {
        name[0] = '\0';
    }
    name[cap - 1] = '\0';
    return cr;
}

/* The outcome of a unit's eject on NT after CM_Request_Device_EjectA
 * answered *cr and *veto (through tr_eject_cr): that answer, or for a unit
 * not marked removable the fallback's (*notRemovable set) or the re-plug
 * refusal. *cr, *veto and vetoName end as the answer read. */
static int ej_nt_answer(DWORD dn, int capsKnown, ULONG caps, DWORD *cr,
                        ULONG *veto, char *vetoName, const char **text,
                        int *withName, int *notRemovable)
{
    int notMarked = inst_unit_not_marked(capsKnown, caps);
    int step = inst_eject_nt_step(*cr, *veto, notMarked,
                                  notMarked ? ej_may_remove() : 1);

    *notRemovable = 0;
    if (step == INST_NT_ADMIN) {
        *withName = 0;
        *text = inst_admin_text();
        return INST_EJ_RIGHTS;
    }
    if (step != INST_NT_FALLBACK) {
        return inst_eject_nt(*cr, *veto, text, withName);
    }
    /* The same query-remove and remove an eject runs, by the routine that
     * makes them for any device; a veto still stops it. */
    *notRemovable = 1;
    if (api.QueryAndRemove == NULL) {
        *cr = INST_CR_CALL_NOT_IMPLEMENTED;
        *veto = 0;
    } else {
        *cr = ej_query_and_remove(dn, veto, vetoName, MAX_PATH);
        *cr = tr_eject_cr(*cr, *veto);
    }
    return inst_eject_nt_removal(*cr, *veto, text, withName);
}

/* ---- NT: the stop with administrator rights (record 25 section 6.6) ---- */

#define TR_ELEV_WAIT_MS     300000UL

static int ejElevBusy;      /* an elevated stop is being waited for      */
static int ejElevQuit;      /* the program was asked to end meanwhile    */

static BOOL CALLBACK elev_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextA(dlg, IDC_ELEV_TEXT, (const char *)lp);
        if (envMajor >= 6) {
            /* BCM_SETSHIELD: the button starts an elevation. */
            SendDlgItemMessageA(dlg, IDOK, 0x160C, 0, 1);
        }
        SetForegroundWindow(dlg);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, LOWORD(wp));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* 1 when the user asked for the stop with administrator rights. */
static int ej_elev_ask(HWND owner, const EJREQ *q)
{
    char text[512];

    tr_sprintf(text, sizeof(text), "%s\n\n%s", q->Desc, tr_elev_question());
    return DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_ELEVATE), owner,
                           (DLGPROC)elev_proc, (LPARAM)text) == IDOK;
}

/* Starts this program elevated with /eject id=, /result and /confirmed,
 * waits for it with the window answering, and reads its answer from the
 * section this run created: XHCIINST's exit code, the text in msg. No
 * file is written or read, so the elevated run touches nothing a user
 * could plant. A quit during the wait (Exit, or an installer) ends the
 * wait at once; ejElevQuit is then set and the quit posted again. */
static int ej_elev_run(HWND owner, const EJREQ *q, char *msg, size_t cap)
{
    static char answer[TR_RES_BYTES + 1];
    static char amsg[TR_RES_BYTES + 1];
    SHELLEXECUTEINFOA se;
    TR_SA sa;
    HANDLE sec;
    void *view;
    char name[64];
    char digest[TR_DIGEST_CHARS];
    char exe[MAX_PATH];
    char args[INST_ID_CHARS + 128];
    int launched = 0;
    DWORD err = 0;
    int finished = 0;
    DWORD exitCode = INST_EXIT_FAILED;
    int haveAnswer = 0;
    int answerCode = 0;
    int quitCode = 0;

    amsg[0] = '\0';
    ejElevQuit = 0;
    if (GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0) {
        return tr_elev_result(0, GetLastError(), 0, 0, 0, 0, "", msg, cap);
    }
    tr_sprintf(name, sizeof(name), TR_RES_PREFIX "%lu-%lu",
               (unsigned long)GetCurrentProcessId(),
               (unsigned long)GetTickCount());
    /* The user and SYSTEM and the Administrators, who may be another
     * account under XP's Run As, may write it. */
    make_sa(&sa, 0x0001 /* SECTION_QUERY */);
    SetLastError(0);
    sec = CreateFileMappingA(INVALID_HANDLE_VALUE, sa.Ok ? &sa.Sa : NULL,
                             PAGE_READWRITE, 0, TR_RES_BYTES, name);
    if (sec == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        err = sec == NULL ? GetLastError() : ERROR_ALREADY_EXISTS;
        if (sec != NULL) {
            CloseHandle(sec);
        }
        return tr_elev_result(0, err, 0, 0, 0, 0, "", msg, cap);
    }
    view = MapViewOfFile(sec, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, TR_RES_BYTES);
    if (view == NULL) {
        err = GetLastError();
        CloseHandle(sec);
        return tr_elev_result(0, err, 0, 0, 0, 0, "", msg, cap);
    }
    memset(view, 0, TR_RES_BYTES);
    tr_res_digest((const char (*)[TR_ID_CHARS])q->Devs, q->DevCount, digest);
    tr_sprintf(args, sizeof(args), "/eject \"id=%s\" /result %s /confirmed %s",
               q->UnitId, name, digest);
    memset(&se, 0, sizeof(se));
    se.cbSize = sizeof(se);
    se.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    se.hwnd = owner;
    se.lpVerb = "runas";
    se.lpFile = exe;
    se.lpParameters = args;
    se.nShow = SW_SHOWNORMAL;
    ejElevBusy = 1;
    if (ShellExecuteExA(&se) && se.hProcess != NULL) {
        DWORD start = GetTickCount();
        launched = 1;
        while (!finished && !ejElevQuit) {
            DWORD r;
            MSG m;
            DWORD spent = GetTickCount() - start;
            if (spent >= TR_ELEV_WAIT_MS) {
                break;
            }
            r = MsgWaitForMultipleObjects(1, &se.hProcess, FALSE,
                                          TR_ELEV_WAIT_MS - spent, QS_ALLINPUT);
            if (r == WAIT_OBJECT_0) {
                finished = 1;
                break;
            }
            if (r != WAIT_OBJECT_0 + 1) {
                break;
            }
            while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) {
                    ejElevQuit = 1;
                    quitCode = (int)m.wParam;
                    break;
                }
                TranslateMessage(&m);
                DispatchMessageA(&m);
            }
        }
        if (finished && !GetExitCodeProcess(se.hProcess, &exitCode)) {
            exitCode = INST_EXIT_FAILED;
        }
        CloseHandle(se.hProcess);
    } else {
        err = GetLastError();
    }
    ejElevBusy = 0;
    if (finished) {
        memcpy(answer, view, TR_RES_BYTES);
        answer[TR_RES_BYTES] = '\0';
        haveAnswer = tr_res_read_answer(answer, &answerCode, amsg, sizeof(amsg));
    }
    UnmapViewOfFile(view);
    CloseHandle(sec);
    if (ejElevQuit) {
        PostQuitMessage(quitCode);
        msg[0] = '\0';
        return INST_EXIT_REFUSED;
    }
    return tr_elev_result(launched, err, finished, exitCode, haveAnswer,
                          answerCode, amsg, msg, cap);
}

/* The elevated run's answer, into the window's section by its name; 0
 * when there is none to write to. */
static int elev_child_answer(int code, const char *text)
{
    static char buf[TR_RES_BYTES];
    HANDLE sec;
    void *view;
    size_t n = tr_res_answer(code, text, buf, sizeof(buf));

    if (n == 0) {
        return 0;
    }
    sec = OpenFileMappingA(FILE_MAP_WRITE, FALSE, opts.ResultName);
    if (sec == NULL) {
        return 0;
    }
    view = MapViewOfFile(sec, FILE_MAP_WRITE, 0, 0, TR_RES_BYTES);
    if (view == NULL) {
        CloseHandle(sec);
        return 0;
    }
    memcpy(view, buf, n + 1);
    UnmapViewOfFile(view);
    CloseHandle(sec);
    return 1;
}

/* The transaction (record 25 section 6.2 steps 2 to 8). Returns
 * XHCIINST's exit code; msg the answer; *rights set for a rights failure
 * Windows answered (the elevated retry is the caller's, after this has
 * released the mutex). */
static int ej_run(const EJREQ *q, char *msg, size_t cap, int *rights)
{
    HANDLE m;
    unsigned char *b1 = NULL;
    unsigned char *b2 = NULL;
    unsigned char *b3 = NULL;
    TR_QUERY q1;
    TR_QUERY q2;
    TR_QUERY q3;
    WALK w;
    NHI *one = NULL;
    const char *why;
    const char *text;
    const char *tunnelNote = NULL;
    DWORD start;
    DWORD cr;
    int u;
    int tries;
    int got;
    int single;
    int outcome;
    int code = INST_EXIT_REFUSED;
    TR_TEXT t;

    *rights = 0;
    tr_text_init(&t, msg, cap);
    memset(&w, 0, sizeof(w));
    if (envNt ? api.Eject == NULL : (api.QueryRemove == NULL || api.Remove == NULL)) {
        tr_put(&t, "This Windows does not offer the call that stops a device.");
        return INST_EXIT_REFUSED;
    }
    m = ej_lock();
    if (m == NULL) {
        tr_put(&t, "Another program is removing a device; try again when it has "
                   "finished.");
        return INST_EXIT_REFUSED;
    }
    if (!ej_query(q, &b1, &q1)) {
        tr_put(&t, "The Thunderbolt driver's device list cannot be read now; "
                   "nothing was stopped.");
        goto out;
    }
    if (q1.EpochLo != q->EpochLo || q1.EpochHi != q->EpochHi) {
        tr_put(&t, "The Thunderbolt controller restarted; the device list "
                   "changed. Nothing was stopped.");
        goto out;
    }
    if (!tr_query_eject_ready(&q1, &why)) {
        tr_printf(&t, "%s Nothing was stopped.", why);
        goto out;
    }
    start = GetTickCount();
    got = 0;
    while (GetTickCount() - start < CM_Q_NEW_PASS_WAIT_MS) {
        Sleep(100);
        free(b2);
        b2 = NULL;
        if (ej_query(q, &b2, &q2) &&
            (q2.EpochLo != q1.EpochLo || q2.EpochHi != q1.EpochHi)) {
            tr_put(&t, "The Thunderbolt controller restarted; the device list "
                       "changed. Nothing was stopped.");
            goto out;
        }
        if (b2 != NULL && tr_query_new_pass(&q1, &q2)) {
            if (!tr_query_eject_ready(&q2, &why)) {
                tr_printf(&t, "%s Nothing was stopped.", why);
                goto out;
            }
            got = 1;
            break;
        }
    }
    if (!got) {
        tr_put(&t, "The Thunderbolt/USB4 controller is not answering; nothing "
                   "was stopped.");
        goto out;
    }
    one = (NHI *)calloc(1, sizeof(NHI));
    if (one == NULL) {
        tr_put(&t, "Out of memory; nothing was stopped.");
        goto out;
    }
    tr_copy(one->Id, q->NhiId, sizeof(one->Id));
    one->HaveQ = 1;
    got = 0;
    for (tries = 0; tries < 2 && !got; tries++) {
        if (!walk(&w)) {
            break;
        }
        if (w.Bad != 0) {
            continue;
        }
        free(b3);
        b3 = NULL;
        if (!ej_query(q, &b3, &q3)) {
            break;
        }
        if (q3.EpochLo != q->EpochLo || q3.EpochHi != q->EpochHi) {
            tr_put(&t, "The Thunderbolt controller restarted; the device list "
                       "changed. Nothing was stopped.");
            goto out;
        }
        if (!tr_query_eject_ready(&q3, &why)) {
            tr_printf(&t, "%s Nothing was stopped.", why);
            goto out;
        }
        if (q->ByName && !name_unique(&w, q->NhiId, q->QName)) {
            tr_put(&t, "Another Thunderbolt controller now has the same query "
                       "device name; nothing was stopped.");
            goto out;
        }
        if (tr_query_same_content(&q2, &q3)) {
            got = 1;
        } else {
            free(b2);
            b2 = b3;
            q2 = q3;
            b3 = NULL;
        }
    }
    if (!got) {
        tr_put(&t, "The device list could not be read whole and steady; nothing "
                   "was stopped.");
        goto out;
    }
    one->Q = q3;
    one->R = q3.Rec;
    u = ej_find(&w, one, q);
    if (u < 0) {
        tr_put(&t, "The device list changed since it was shown; nothing was "
                   "stopped. Look at the tree again.");
        goto out;
    }
    if (!dn_started(&w.N[u])) {
        char tag[64];
        char sen[96];
        if (tr_stopped_text(w.N[u].Problem, tag, sizeof(tag), sen, sizeof(sen))) {
            tr_printf(&t, "%s is stopped already: you can unplug it.", q->Desc);
        } else {
            tr_printf(&t, "%s is not running. %s Nothing was stopped.", q->Desc, sen);
        }
        goto out;
    }
    single = ej_single(&w, u);
    /* Immediately before the call: still eligible, nothing changed. */
    {
        unsigned char *b4 = NULL;
        TR_QUERY q4;
        int same = ej_query(q, &b4, &q4) && tr_query_eject_ready(&q4, &why) &&
                   tr_query_same_content(&q3, &q4);
        free(b4);
        if (!same) {
            tr_put(&t, "The Thunderbolt device list changed just now; nothing "
                       "was stopped. Look at the tree again.");
            goto out;
        }
    }
    if (envNt) {
        ULONG veto = 0;
        char vetoName[MAX_PATH];
        int withName;
        int notRemovable = 0;
        vetoName[0] = '\0';
        cr = api.Eject(w.N[u].Dn, &veto, vetoName, MAX_PATH, 0);
        vetoName[MAX_PATH - 1] = '\0';
        cr = tr_eject_cr(cr, veto);
        outcome = ej_nt_answer(w.N[u].Dn, w.N[u].HasCaps, w.N[u].Caps, &cr,
                               &veto, vetoName, &text, &withName,
                               &notRemovable);
        if (outcome != INST_EJ_DONE) {
            if (withName) {
                char name[MAX_PATH + 16];
                if (veto == 0 || vetoName[0] == '\0') {
                    tr_sprintf(name, sizeof(name), "veto %lu", (unsigned long)veto);
                } else {
                    tr_copy(name, vetoName, sizeof(name));
                }
                /* The text carries one %s. */
                {
                    char line[1024];
                    _snprintf(line, sizeof(line) - 1, text, name);
                    line[sizeof(line) - 1] = '\0';
                    tr_put(&t, line);
                }
            } else {
                tr_put(&t, text);
            }
            if (outcome == INST_EJ_FAILED) {
                tr_printf(&t, "\n(code 0x%08lX)", (unsigned long)cr);
            }
            *rights = outcome == INST_EJ_RIGHTS;
            code = inst_eject_code(outcome);
            goto out;
        }
        tr_printf(&t, "Windows has stopped %s.\n", q->Desc);
        if (notRemovable) {
            tr_put(&t, "Until you unplug it, Windows may start it again when it "
                       "next looks for new hardware.\n");
        }
        tunnelNote = ej_watch_tunnel(q);
    } else {
        cr = api.QueryRemove(w.N[u].Dn, TR_UI_NOT_OK);
        outcome = inst_eject9x_query(cr, &text);
        if (outcome != INST_EJ_DONE) {
            tr_put(&t, text);
            if (cr != INST_CR_REMOVE_VETOED && outcome == INST_EJ_VETO) {
                tr_printf(&t, "\n(code 0x%08lX)", (unsigned long)cr);
            }
            code = inst_eject_code(outcome);
            goto out;
        }
        /* Accepted: the removal always follows (record 24 section 7.1). */
        cr = api.Remove(w.N[u].Dn, TR_UI_NOT_OK);
        outcome = inst_eject9x_remove(cr, &text);
        if (outcome != INST_EJ_DONE) {
            tr_printf(&t, "%s\n(code 0x%08lX)", text, (unsigned long)cr);
            code = inst_eject_code(outcome);
            goto out;
        }
        tr_printf(&t, "Windows has stopped %s.\n", q->Desc);
        tr_put(&t, "Until you unplug it, Device Manager's Refresh, or the "
                   "Thunderbolt driver looking at the port again, can start it "
                   "again.\n");
    }
    if (tunnelNote != NULL) {
        tr_printf(&t, "%s\n", tunnelNote);
    }
    u = ej_after(q, single);
    tr_printf(&t, "\n%s", inst_unplug_text(u));
    code = u == INST_UNPLUG_RESTARTED ? INST_EXIT_STEPS : INST_EXIT_DONE;
out:
    ej_unlock(m);
    free(b1);
    free(b2);
    free(b3);
    free(w.N);
    free(one);
    return code;
}

/* Vista and 7: the eject again, elevated, as a one-shot run (record 25
 * section 6.4). */
static void ej_elevated(HWND owner, const char *unitId)
{
    SHELLEXECUTEINFOA se;
    char exe[MAX_PATH];
    char args[INST_ID_CHARS + 32];

    if (envMajor < 6 || GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0) {
        return;
    }
    tr_sprintf(args, sizeof(args), "/eject \"id=%s\"", unitId);
    memset(&se, 0, sizeof(se));
    se.cbSize = sizeof(se);
    se.hwnd = owner;
    se.lpVerb = "runas";
    se.lpFile = exe;
    se.lpParameters = args;
    se.nShow = SW_SHOWNORMAL;
    ShellExecuteExA(&se);
}

static void tray_balloon(const char *title, const char *text, int warn);

/* ======================================================================== */
/* Plain USB devices (record 25 section 6.5, revision 7)                     */
/* ======================================================================== */

#define TR_GETIDLIST_EJECTRELATIONS     0x00000004UL
#define TR_GETIDLIST_REMOVALRELATIONS   0x00000008UL

/* Why the last set read was not known, for the details. */
static char setFail[INST_ID_CHARS * 2 + 64];

/* The relations of one kind of devnode `id` added to set (walk indexes):
 * 1 read (possibly none), 0 not read or naming a devnode the walk lacks. */
static int usb_relations(const WALK *w, const char *id, ULONG flags, int *set,
                         int cap, int *n, int *over)
{
    ULONG len = 0;
    char *buf;
    const char *p;
    DWORD cr;

    if (api.ListSize == NULL || api.List == NULL) {
        tr_copy(setFail, "this Windows does not offer the relations call",
                sizeof(setFail));
        return 0;
    }
    cr = api.ListSize(&len, id, flags);
    if (cr == TR_CR_NO_SUCH_VALUE) {
        return 1;   /* Windows 7: a devnode with no relations of the kind */
    }
    if (cr != TR_CR_SUCCESS) {
        tr_sprintf(setFail, sizeof(setFail), "%s relations of %s: answer 0x%08lX",
                   flags == TR_GETIDLIST_EJECTRELATIONS ? "ejection" : "removal",
                   id, (unsigned long)cr);
        return 0;
    }
    if (len <= 1) {
        return 1;
    }
    if (len > 65536UL) {
        tr_copy(setFail, "a relations list too long", sizeof(setFail));
        return 0;
    }
    buf = (char *)calloc(1, len + 2);
    if (buf == NULL) {
        return 0;
    }
    cr = api.List(id, buf, len, flags);
    if (cr != TR_CR_SUCCESS) {
        tr_sprintf(setFail, sizeof(setFail), "the relations list of %s: answer "
                   "0x%08lX", id, (unsigned long)cr);
        free(buf);
        return 0;
    }
    for (p = buf; *p != '\0' && p < buf + len; p += strlen(p) + 1) {
        int k = walk_find_id(w, p);
        int j;
        int seen = 0;
        if (k < 0) {
            tr_sprintf(setFail, sizeof(setFail), "%s is not in the device list",
                       p);
            free(buf);
            return 0;
        }
        for (j = 0; j < *n; j++) {
            if (set[j] == k) {
                seen = 1;
            }
        }
        if (!seen) {
            if (*n < cap) {
                set[(*n)++] = k;
            } else {
                *over = 1;
            }
        }
    }
    free(buf);
    return 1;
}

/* The set of devnode d (section 6.5): the device, every devnode below it,
 * and on NT the removal and ejection relations of every member and what is
 * below them, until nothing new is added. 1 when known; *over when more
 * than cap. */
static int usb_set(const WALK *w, int d, int *set, int cap, int *n, int *over)
{
    int i;
    int known = 1;

    *n = 0;
    *over = 0;
    if (d < 0 || cap < 1) {
        return 0;
    }
    set[(*n)++] = d;
    for (i = 0; i < *n; i++) {
        int m = set[i];
        int c;
        for (c = 0; c < w->Count; c++) {
            if (w->N[c].Parent == m) {
                int j;
                int seen = 0;
                for (j = 0; j < *n; j++) {
                    if (set[j] == c) {
                        seen = 1;
                    }
                }
                if (!seen) {
                    if (*n < cap) {
                        set[(*n)++] = c;
                    } else {
                        *over = 1;
                    }
                }
            }
        }
        if (envNt && known) {
            if (!usb_relations(w, w->N[m].Id, TR_GETIDLIST_REMOVALRELATIONS, set,
                               cap, n, over) ||
                !usb_relations(w, w->N[m].Id, TR_GETIDLIST_EJECTRELATIONS, set,
                               cap, n, over)) {
                known = 0;
            }
        }
    }
    return known;
}

/* The routines the eject needs, and the watcher that counts changes. */
static int usb_call_available(void)
{
    return watchOk && (envNt ? api.Eject != NULL
                             : (api.QueryRemove != NULL && api.Remove != NULL));
}

/* The facts of the rule for devnode d of walk w and its set. */
static void usb_facts(const WALK *w, int d, int portIsHub, int devClass,
                      const int *set, int n, int known, int over, TR_USBF *f)
{
    int i;

    memset(f, 0, sizeof(*f));
    f->Nt = envNt;
    f->HaveDevnode = d >= 0;
    f->IsHub = portIsHub;
    f->Radio = devClass == 0xE0;
    if (d < 0) {
        return;
    }
    f->Started = (w->N[d].Status & INST_DN_STARTED) != 0;
    f->Problem = w->N[d].Problem;
    f->SetKnown = known;
    f->SetCount = over ? TR_USB_SET_MAX + 1 : n;
    f->ReadWhole = 1;
    f->CallAvailable = usb_call_available();
    for (i = 0; i < n; i++) {
        const DNODE *m = &w->N[set[i]];
        if (m->IsHubId) {
            f->IsHub = 1;
        }
        if (m->IsUsbHc) {
            f->IsController = 1;
        }
        if (m->IsHid) {
            f->Hid = 1;
        }
        if (tr_msz_has_prefix(m->Hw, m->HwBytes, "USB\\Class_E0") ||
            tr_msz_has_prefix(m->Compat, m->CompatBytes, "USB\\Class_E0")) {
            f->Radio = 1;
        }
        if (m->IsDisk) {
            f->Disks++;
        }
        if (m->IsVolume) {
            f->Volumes++;
        }
        if (!m->ReadOk || m->Incomplete) {
            f->ReadWhole = 0;   /* a member, or a child of one, not read */
        }
    }
}

/* Every device at a USB port of the model: its rule and its set. */
static void usb_derive(void)
{
    static int set[TR_USB_SET_MAX + 1];
    int k;

    for (k = 0; k < gw.Count; k++) {
        gw.N[k].UPort = -1;
        gw.N[k].UsbOf = -1;
        gw.N[k].UsbRule = TR_UE_DETAILS;
        gw.N[k].UsbWhy = NULL;
    }
    for (k = 0; k < upCount; k++) {
        UPORT *p = &up[k];
        if (p->Dn >= 0 && p->HaveConn && p->Conn.Status == TR_CS_CONNECTED &&
            gw.N[p->Dn].UPort < 0) {
            gw.N[p->Dn].UPort = k;
        }
    }
    for (k = 0; k < gw.Count; k++) {
        TR_USBF f;
        int n;
        int over;
        int known;
        int i;
        const UPORT *p;
        if (gw.N[k].UPort < 0) {
            continue;
        }
        p = &up[gw.N[k].UPort];
        known = usb_set(&gw, k, set, TR_USB_SET_MAX + 1, &n, &over);
        usb_facts(&gw, k, p->Conn.IsHub, p->Conn.Dev[4], set, n, known, over, &f);
        gw.N[k].UsbRule = tr_usb_rule(&f, &gw.N[k].UsbWhy);
        if (gw.N[k].UsbRule != TR_UE_DETAILS) {
            for (i = 0; i < n; i++) {
                if (gw.N[set[i]].UsbOf < 0) {
                    gw.N[set[i]].UsbOf = k;
                }
            }
        }
    }
}

/* 1 when devnode d is a USB device section 6.5 lists. */
static int usb_listed(int d)
{
    return d >= 0 && d < gw.Count && gw.N[d].UPort >= 0 &&
           gw.N[d].UsbRule != TR_UE_DETAILS;
}

/* What the user sees for USB device d: its disks and their letters, else
 * its own name. */
static void usb_label(int d, char *out, size_t cap)
{
    char names[512];
    char letters[TR_LETTERS_BYTES];
    TR_TEXT t;
    int i;

    tr_text_init(&t, names, sizeof(names));
    letters[0] = '\0';
    for (i = 0; usb_listed(d) && i < gw.Count; i++) {
        if (gw.N[i].UsbOf == d && gw.N[i].IsDisk) {
            tr_printf(&t, "%s%s", t.Len ? ", " : "",
                      gw.N[i].Friendly[0] ? gw.N[i].Friendly : dn_name(&gw.N[i]));
            tr_letters_add(letters, gw.N[i].Letters);
        }
    }
    tr_unit_label(dn_name(&gw.N[d]), names, letters, out, cap);
}

/* The letters of USB device d's disks as text ("F:, G:"), or its name. */
static void usb_what(int d, char *out, size_t cap)
{
    char letters[TR_LETTERS_BYTES];
    int i;

    letters[0] = '\0';
    for (i = 0; i < gw.Count; i++) {
        if (gw.N[i].UsbOf == d && gw.N[i].IsDisk) {
            tr_letters_add(letters, gw.N[i].Letters);
        }
    }
    tr_letters_text(letters, out, cap);
    if (out[0] == '\0') {
        tr_copy(out, dn_name(&gw.N[d]), cap);
    }
}

static const char *usb_suffix(int d)
{
    if (!usb_listed(d)) {
        return "";
    }
    if (gw.N[d].UsbRule == TR_UE_OFFER) {
        return "  [can be stopped]";
    }
    if (gw.N[d].Problem == TR_CM_PROB_HELD_FOR_EJECT) {
        return "  [stopped - unplug it]";
    }
    return "";
}

/* Why device d cannot be stopped now, or NULL. */
static const char *usb_why(int d)
{
    if (!usb_listed(d)) {
        return "it is not a USB device that can be stopped here";
    }
    return gw.N[d].UsbRule == TR_UE_OFFER ? NULL : gw.N[d].UsbWhy;
}

/* The Safely Remove line of a USB device's details. */
static void details_usb_eject(TR_TEXT *t, int d)
{
    int i;

    if (d < 0 || gw.N[d].UPort < 0) {
        return;
    }
    if (gw.N[d].UsbRule == TR_UE_OFFER) {
        tr_put(t, "  Safely Remove: can be stopped here (Action > Safely Remove)\n");
    } else if (gw.N[d].UsbRule == TR_UE_LISTED) {
        static int set[TR_USB_SET_MAX + 1];
        int n;
        int over;
        tr_printf(t, "  Safely Remove: not now - %s\n", gw.N[d].UsbWhy);
        setFail[0] = '\0';
        if (!usb_set(&gw, d, set, TR_USB_SET_MAX + 1, &n, &over) &&
            setFail[0] != '\0') {
            tr_printf(t, "  (%s)\n", setFail);
        }
    } else {
        tr_printf(t, "  Safely Remove: not offered - %s\n",
                  gw.N[d].UsbWhy != NULL ? gw.N[d].UsbWhy : "not a device to stop");
    }
    if (gw.N[d].UsbRule != TR_UE_DETAILS) {
        tr_put(t, "  Devices that stop with it:\n");
        for (i = 0; i < gw.Count; i++) {
            if (gw.N[i].UsbOf == d) {
                tr_printf(t, "    %s\n", dn_name(&gw.N[i]));
            }
        }
    }
}

/* ---- the transaction (section 6.5) ---------------------------------------- */

typedef struct _USBREQ {
    char Id[INST_ID_CHARS];
    char ParentId[INST_ID_CHARS];
    char Label[160];        /* disks and letters, or the device's name      */
    char Name[128];         /* the device's own devnode                     */
    char What[TR_LETTERS_TEXT_BYTES + 32]; /* letters, or the name         */
    int PortIsHub;
    int DevClass;
    LONG Gen;               /* the change count before the model was read  */
    int Count;
    char Ids[TR_USB_SET_MAX][INST_ID_CHARS];
} USBREQ;

/* The request as the model shows device d: 1 when it is offered. */
static int usb_prepare(int d, USBREQ *q)
{
    static int set[TR_USB_SET_MAX + 1];
    int n;
    int over;
    int i;

    memset(q, 0, sizeof(*q));
    if (!usb_listed(d) || gw.N[d].UsbRule != TR_UE_OFFER) {
        return 0;
    }
    if (!usb_set(&gw, d, set, TR_USB_SET_MAX + 1, &n, &over) || over ||
        n > TR_USB_SET_MAX) {
        return 0;   /* the relations grew since the rule was read */
    }
    tr_copy(q->Id, gw.N[d].Id, sizeof(q->Id));
    if (gw.N[d].Parent >= 0) {
        tr_copy(q->ParentId, gw.N[gw.N[d].Parent].Id, sizeof(q->ParentId));
    }
    usb_label(d, q->Label, sizeof(q->Label));
    tr_copy(q->Name, dn_name(&gw.N[d]), sizeof(q->Name));
    usb_what(d, q->What, sizeof(q->What));
    q->PortIsHub = up[gw.N[d].UPort].Conn.IsHub;
    q->DevClass = up[gw.N[d].UPort].Conn.Dev[4];
    q->Gen = modelGen;
    for (i = 0; i < n; i++) {
        tr_copy(q->Ids[q->Count++], gw.N[set[i]].Id, sizeof(q->Ids[0]));
    }
    return 1;
}

static int usb_confirm(HWND owner, const USBREQ *q)
{
    static char text[TR_USB_SET_MAX * (INST_ID_CHARS + 8) + 4096];
    static char lineBuf[TR_USB_SET_MAX][256];
    const char *lines[TR_USB_SET_MAX];
    TR_TEXT t;
    int disks = 0;
    int i;

    tr_text_init(&t, text, sizeof(text));
    for (i = 0; i < q->Count && disks < TR_USB_SET_MAX; i++) {
        int k = walk_find_id(&gw, q->Ids[i]);
        if (k >= 0 && gw.N[k].IsDisk) {
            tr_disk_line(gw.N[k].Friendly[0] ? gw.N[k].Friendly : dn_name(&gw.N[k]),
                         gw.N[k].Letters, lineBuf[disks], sizeof(lineBuf[disks]));
            lines[disks] = lineBuf[disks];
            disks++;
        }
    }
    tr_confirm_lead(&t, q->Name, q->Label, lines, disks);
    for (i = 0; i < q->Count; i++) {
        int k = walk_find_id(&gw, q->Ids[i]);
        tr_printf(&t, "    %s\n", k >= 0 ? dn_name(&gw.N[k]) : q->Ids[i]);
    }
    tr_put(&t, "\nClose any program that uses them first.");
    if (t.Overflow) {
        MessageBoxA(owner, "The list of devices to stop is too long to show, so "
                    "nothing is stopped from here.", "Stop a USB device",
                    MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        return 0;
    }
    return MessageBoxA(owner, text, "Stop a USB device",
                       MB_OKCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND) == IDOK;
}

/* After an accepted call: what a new walk reads (section 6.5 step 5). */
static int usb_after(const USBREQ *q, TR_TEXT *t)
{
    WALK w;
    int d;
    int code = INST_EXIT_DONE;

    memset(&w, 0, sizeof(w));
    if (!walk(&w)) {
        tr_put(t, "\nIts state could not be read again: check that it is not "
                  "running before you unplug it.");
        free(w.N);
        return code;
    }
    d = walk_find_id(&w, q->Id);
    if (d >= 0 && dn_started(&w.N[d])) {
        tr_put(t, "\nThe device has started again, so it is not safe to unplug.");
        code = INST_EXIT_STEPS;
    } else if ((d < 0 && w.Bad == 0) ||
               (d >= 0 && w.N[d].Problem == TR_CM_PROB_HELD_FOR_EJECT)) {
        tr_put(t, "\nYou can now unplug the device.");
    } else {
        tr_put(t, "\nIts state could not be read again: check that it is not "
                  "running before you unplug it.");
    }
    free(w.N);
    return code;
}

/* The transaction (section 6.5 steps 2 to 5). Returns XHCIINST's exit
 * code; msg the answer; *rights for a rights refusal. */
static int usb_run(const USBREQ *q, char *msg, size_t cap, int *rights)
{
    static int set[TR_USB_SET_MAX + 1];
    const char *nowIds[TR_USB_SET_MAX];
    const char *thenIds[TR_USB_SET_MAX];
    char answer[1024];
    HANDLE m;
    WALK w;
    TR_USBF f;
    const char *why;
    int d;
    int n;
    int over;
    int known;
    int i;
    int outcome;
    int code = INST_EXIT_REFUSED;
    DWORD cr;
    TR_TEXT t;

    *rights = 0;
    tr_text_init(&t, msg, cap);
    memset(&w, 0, sizeof(w));
    if (!usb_call_available()) {
        tr_put(&t, "This Windows does not offer the call that stops a device.");
        return INST_EXIT_REFUSED;
    }
    m = ej_lock();
    if (m == NULL) {
        tr_put(&t, "Another program is removing a device; try again when it has "
                   "finished.");
        return INST_EXIT_REFUSED;
    }
    if (!walk(&w)) {
        tr_put(&t, "The device list cannot be read now; nothing was stopped.");
        goto out;
    }
    d = walk_find_id(&w, q->Id);
    if (d < 0) {
        tr_printf(&t, "%s is no longer connected; nothing was stopped.", q->Label);
        goto out;
    }
    known = usb_set(&w, d, set, TR_USB_SET_MAX + 1, &n, &over);
    usb_facts(&w, d, q->PortIsHub, q->DevClass, set, n, known, over, &f);
    if (n > TR_USB_SET_MAX || q->Count < 1 || q->Count > TR_USB_SET_MAX) {
        tr_printf(&t, "%s cannot be stopped now: too many devices would stop "
                      "with it to list. Nothing was stopped.", q->Label);
        goto out;
    }
    for (i = 0; i < n; i++) {
        nowIds[i] = w.N[set[i]].Id;
    }
    for (i = 0; i < q->Count; i++) {
        thenIds[i] = q->Ids[i];
    }
    if (!tr_usb_recheck(w.N[d].Parent >= 0 ? w.N[w.N[d].Parent].Id : "",
                        q->ParentId, nowIds, n, thenIds, q->Count, &f, &why)) {
        tr_printf(&t, "%s cannot be stopped now: %s. Nothing was stopped.",
                  q->Label, why != NULL ? why : "it changed");
        goto out;
    }
    /* Immediately before the call: no device change since the model. */
    if (chgCount != q->Gen) {
        tr_put(&t, "The device list changed; nothing was stopped. Look at the "
                   "tree again.");
        goto out;
    }
    if (envNt) {
        ULONG veto = 0;
        char vetoName[MAX_PATH];
        vetoName[0] = '\0';
        cr = api.Eject(w.N[d].Dn, &veto, vetoName, MAX_PATH, 0);
        vetoName[MAX_PATH - 1] = '\0';
        {
            int inSet = 0;
            for (i = 0; i < q->Count; i++) {
                if (vetoName[0] != '\0' && tr_eq(vetoName, q->Ids[i])) {
                    inSet = 1;
                }
            }
            outcome = tr_usb_answer_nt(cr, veto, vetoName, inSet, q->What,
                                       answer, sizeof(answer));
        }
        if (outcome != INST_EJ_DONE) {
            tr_put(&t, answer);
            *rights = outcome == INST_EJ_RIGHTS;
            code = inst_eject_code(outcome);
            goto out;
        }
        tr_printf(&t, "Windows has stopped %s.", q->Label);
        code = usb_after(q, &t);
    } else {
        const char *text;
        cr = api.QueryRemove(w.N[d].Dn, TR_UI_NOT_OK);
        outcome = tr_usb_answer9x(cr, q->What, answer, sizeof(answer));
        if (outcome != INST_EJ_DONE) {
            tr_put(&t, answer);
            code = inst_eject_code(outcome);
            goto out;
        }
        /* Accepted: the removal always follows (record 24 section 7.1). */
        cr = api.Remove(w.N[d].Dn, TR_UI_NOT_OK);
        outcome = inst_eject9x_remove(cr, &text);
        if (outcome != INST_EJ_DONE) {
            tr_printf(&t, "%s\n(code 0x%08lX)", text, (unsigned long)cr);
            code = inst_eject_code(outcome);
            goto out;
        }
        tr_printf(&t, "Windows has stopped %s.", q->Label);
        code = usb_after(q, &t);
        tr_put(&t, "\nUntil you unplug it, Device Manager's Refresh can start it "
                   "again.");
    }
out:
    ej_unlock(m);
    free(w.N);
    return code;
}

/* The whole eject of USB device d from the window or the tray. */
static void usb_eject_ui(HWND owner, int d, int fromTray)
{
    USBREQ *q = (USBREQ *)calloc(1, sizeof(USBREQ));
    char msg[2048];
    const char *why = usb_why(d);
    int rights = 0;
    int code;
    HCURSOR old;

    if (q == NULL) {
        return;
    }
    if (why != NULL || !usb_prepare(d, q)) {
        tr_sprintf(msg, sizeof(msg), "%s cannot be stopped here: %s.",
                   d >= 0 ? dn_name(&gw.N[d]) : "This device",
                   why != NULL ? why : "what would stop with it changed");
        MessageBoxA(owner, msg, "xHCI98 USB Tree", MB_OK | MB_ICONINFORMATION);
        free(q);
        return;
    }
    if (!usb_confirm(owner, q)) {
        free(q);
        return;
    }
    old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    code = usb_run(q, msg, sizeof(msg), &rights);
    tr_join_lines(msg);
    SetCursor(old);
    if (rights && envMajor >= 6) {
        char ask[2300];
        tr_sprintf(ask, sizeof(ask), "%s\n\nTry again as an administrator?", msg);
        if (MessageBoxA(owner, ask, "xHCI98 USB Tree",
                        MB_YESNO | MB_ICONWARNING) == IDYES) {
            ej_elevated(owner, q->Id);
        }
    } else if (fromTray && shellV5 && strlen(msg) < 250) {
        tray_balloon(code == INST_EXIT_DONE ? "Device stopped" : "Device not stopped",
                     msg, code != INST_EXIT_DONE);
    } else {
        MessageBoxA(owner, msg, "xHCI98 USB Tree",
                    MB_OK | (code == INST_EXIT_DONE ? MB_ICONINFORMATION
                                                    : MB_ICONWARNING));
    }
    free(q);
}

/* /eject id= for a USB device: the whole transaction once. */
static int usb_run_eject(int d)
{
    USBREQ *q = (USBREQ *)calloc(1, sizeof(USBREQ));
    char msg[2048];
    const char *why = usb_why(d);
    int rights;
    int code;

    if (q == NULL) {
        return INST_EXIT_FAILED;
    }
    if (why != NULL || !usb_prepare(d, q)) {
        tr_sprintf(msg, sizeof(msg), "%s cannot be stopped here: %s.",
                   dn_name(&gw.N[d]),
                   why != NULL ? why : "what would stop with it changed");
        say(msg, 1);
        free(q);
        return INST_EXIT_REFUSED;
    }
    if (!usb_confirm(NULL, q)) {
        free(q);
        return INST_EXIT_REFUSED;
    }
    code = usb_run(q, msg, sizeof(msg), &rights);
    tr_join_lines(msg);
    say(msg, code != INST_EXIT_DONE);
    free(q);
    return code;
}

/* ---- the watcher (section 6.5 step 1) -------------------------------------- */

static LRESULT CALLBACK watch_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DEVICECHANGE) {
        InterlockedIncrement((LONG *)&chgCount);
        return TRUE;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* A thread with a hidden top-level window and a loop of its own: it counts
 * every device-change broadcast whatever the main thread waits on. */
static DWORD WINAPI watch_thread(LPVOID arg)
{
    HANDLE ready = (HANDLE)arg;
    WNDCLASSA wc;
    HWND h;
    MSG m;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = watch_proc;
    wc.hInstance = hInst;
    wc.lpszClassName = "Xhci98TrWatch";
    RegisterClassA(&wc);
    h = CreateWindowExA(WS_EX_TOOLWINDOW, "Xhci98TrWatch", "", WS_POPUP, 0, 0, 0,
                        0, NULL, NULL, hInst, NULL);
    watchOk = h != NULL;
    SetEvent(ready);
    if (h == NULL) {
        return 1;
    }
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        DispatchMessageA(&m);
    }
    return 0;
}

static void watch_start(void)
{
    HANDLE ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th;
    DWORD tid;

    if (ready == NULL) {
        return;
    }
    th = CreateThread(NULL, 0, watch_thread, ready, 0, &tid);
    if (th != NULL) {
        WaitForSingleObject(ready, 5000);
        CloseHandle(th);
    }
    CloseHandle(ready);
}

/* The whole eject of one unit, or of a USB device (section 6.5), from the
 * window or the tray. */
static void ej_unit(HWND owner, int u, int fromTray)
{
    EJREQ *q;
    char msg[2048];
    const char *why;
    int rights = 0;
    int code;
    HCURSOR old;

    if (usb_listed(u)) {
        usb_eject_ui(owner, u, fromTray);
        return;
    }
    q = (EJREQ *)calloc(1, sizeof(EJREQ));
    why = ej_why_not(u);
    if (q == NULL) {
        return;
    }
    if (why != NULL || !ej_prepare(u, q)) {
        tr_sprintf(msg, sizeof(msg), "%s cannot be stopped here: %s.",
                   u >= 0 ? dn_name(&gw.N[u]) : "This device",
                   why != NULL ? why : "it is not a unit behind a tunnel port");
        MessageBoxA(owner, msg, "xHCI98 USB Tree", MB_OK | MB_ICONINFORMATION);
        free(q);
        return;
    }
    if (ejElevBusy) {
        MessageBoxA(owner, "Another device is being stopped with administrator "
                    "rights; try again when it has finished.", "xHCI98 USB Tree",
                    MB_OK | MB_ICONINFORMATION);
        free(q);
        return;
    }
    if (!ej_confirm(owner, q)) {
        free(q);
        return;
    }
    if (tr_elev_needed(envNt, inst_unit_not_marked(gw.N[u].HasCaps,
                                                   gw.N[u].Caps),
                       ej_is_admin())) {
        /* Not marked removable, and this process is no administrator:
         * the stop is offered with administrator rights (section 6.6). */
        if (!ej_elev_ask(owner, q)) {
            free(q);
            return;
        }
        code = ej_elev_run(owner, q, msg, sizeof(msg));
        if (ejElevQuit) {
            free(q);
            return;
        }
    } else {
        old = SetCursor(LoadCursor(NULL, IDC_WAIT));
        code = ej_run(q, msg, sizeof(msg), &rights);
        SetCursor(old);
        if (rights && envMajor >= 6) {
            char ask[2300];
            tr_join_lines(msg);
            tr_sprintf(ask, sizeof(ask), "%s\n\nTry again as an administrator?",
                       msg);
            if (MessageBoxA(owner, ask, "xHCI98 USB Tree",
                            MB_YESNO | MB_ICONWARNING) != IDYES) {
                free(q);
                return;
            }
            code = ej_elev_run(owner, q, msg, sizeof(msg));
        }
    }
    if (ejElevQuit) {
        free(q);
        return;
    }
    tr_join_lines(msg);
    if (fromTray && shellV5 && strlen(msg) < 250) {
        tray_balloon(code == INST_EXIT_DONE ? "Device stopped" : "Device not stopped",
                     msg, code != INST_EXIT_DONE);
    } else {
        MessageBoxA(owner, msg, "xHCI98 USB Tree",
                    MB_OK | (code == INST_EXIT_DONE ? MB_ICONINFORMATION
                                                    : MB_ICONWARNING));
    }
    free(q);
}

/* ======================================================================== */
/* Settings (HKCU\Software\xHCI98\XHCITR)                                    */
/* ======================================================================== */

#define TR_SETTINGS_KEY "Software\\xHCI98\\XHCITR"

static RECT winRect;
static int haveWinRect;

static void settings_load(void)
{
    HKEY k;
    DWORD t;
    DWORD v;
    DWORD n;

    if (RegOpenKeyExA(HKEY_CURRENT_USER, TR_SETTINGS_KEY, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return;
    }
    n = sizeof(v);
    if (RegQueryValueExA(k, "View", NULL, &t, (LPBYTE)&v, &n) == ERROR_SUCCESS &&
        t == REG_DWORD) {
        viewFlags = v & TR_V_ALL;
    }
    n = sizeof(v);
    if (RegQueryValueExA(k, "Splitter", NULL, &t, (LPBYTE)&v, &n) == ERROR_SUCCESS &&
        t == REG_DWORD && v >= 80 && v < 4000) {
        splitX = (int)v;
    }
    n = sizeof(winRect);
    if (RegQueryValueExA(k, "Window", NULL, &t, (LPBYTE)&winRect, &n) ==
            ERROR_SUCCESS &&
        t == REG_BINARY && n == sizeof(winRect) &&
        winRect.right - winRect.left >= 200 && winRect.bottom - winRect.top >= 150) {
        haveWinRect = 1;
    }
    RegCloseKey(k);
}

/* Start at login, per user (record 25 section 7.4): the DWORD under the
 * settings key; 1 when it reads as a DWORD, with *value. */
static int login_read(DWORD *value)
{
    HKEY k;
    DWORD t;
    DWORD n = sizeof(*value);
    int ok;

    *value = 1;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, TR_SETTINGS_KEY, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return 0;
    }
    ok = RegQueryValueExA(k, TR_LOGIN_VALUE, NULL, &t, (LPBYTE)value, &n) ==
             ERROR_SUCCESS && t == REG_DWORD && n == sizeof(*value);
    RegCloseKey(k);
    if (!ok) {
        *value = 1;
    }
    return ok;
}

static int login_on(void)
{
    DWORD v;
    int have = login_read(&v);

    return tr_tray_login_start(1, have, v);
}

static void login_set(int on)
{
    HKEY k;
    DWORD disp;
    DWORD v = on ? 1UL : 0UL;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, TR_SETTINGS_KEY, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, &disp) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExA(k, TR_LOGIN_VALUE, 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    RegCloseKey(k);
}

/* The machine's Run value (INSTALL writes it; INSTALL /notray deletes it):
 * without it nothing starts at login, whatever the user chooses here. */
static int login_machine(void)
{
    HKEY k;
    DWORD t;
    DWORD n = 0;
    LONG r;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_TRAY_RUN_KEY, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return 0;
    }
    r = RegQueryValueExA(k, INST_TRAY_RUN_VALUE, NULL, &t, NULL, &n);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

/* The item in a menu: checked as the user chose, grey with the reason when
 * the machine starts nothing at login. */
static void login_item(HMENU m, int append)
{
    int machine = login_machine();
    UINT flags = MF_STRING | (login_on() ? MF_CHECKED : MF_UNCHECKED) |
                 (machine ? 0 : MF_GRAYED);
    const char *text = machine ? "Start at &login"
                               : "Start at login (off for this computer)";

    if (append) {
        AppendMenuA(m, flags, IDM_LOGIN, text);
    } else {
        ModifyMenuA(m, IDM_LOGIN, MF_BYCOMMAND | flags, IDM_LOGIN, text);
    }
}

static void settings_save(void)
{
    HKEY k;
    DWORD disp;
    DWORD v;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, TR_SETTINGS_KEY, 0, NULL, 0,
                        KEY_WRITE, NULL, &k, &disp) != ERROR_SUCCESS) {
        return;
    }
    v = viewFlags;
    RegSetValueExA(k, "View", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    v = (DWORD)splitX;
    RegSetValueExA(k, "Splitter", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    if (haveWinRect) {
        RegSetValueExA(k, "Window", 0, REG_BINARY, (const BYTE *)&winRect,
                       sizeof(winRect));
    }
    RegCloseKey(k);
}

/* ======================================================================== */
/* The window (record 25 section 3)                                          */
/* ======================================================================== */

static int tree_selected(void)
{
    HTREEITEM it;
    TVITEMA tvi;

    if (hTree == NULL) {
        return -1;
    }
    it = TreeView_GetSelection(hTree);
    if (it == NULL) {
        return -1;
    }
    memset(&tvi, 0, sizeof(tvi));
    tvi.mask = TVIF_PARAM;
    tvi.hItem = it;
    if (!TreeView_GetItem(hTree, &tvi) || (int)tvi.lParam < 0 ||
        (int)tvi.lParam >= tnCount) {
        return -1;
    }
    return (int)tvi.lParam;
}

/* The unit or the USB device (section 6.5) a tree node stops, or -1. */
static int unit_of_node(int i)
{
    const TNODE *n;

    if (i < 0) {
        return -1;
    }
    n = &tn[i];
    if (n->Dn >= 0 && (n->Kind == K_DEV || n->Kind == K_UPORT) && usb_listed(n->Dn)) {
        return n->Dn;   /* a USB device by itself, even behind a dock */
    }
    if (n->Dn >= 0 && (n->Kind == K_DEV || n->Kind == K_UPORT || n->Kind == K_CTRL) &&
        gw.N[n->Dn].UnitOf >= 0) {
        return gw.N[n->Dn].UnitOf;
    }
    if (n->Kind == K_ROUTER || n->Kind == K_TUNNEL) {
        const unsigned char *e = router_entry(n->A, n->B);
        TR_U32 bdf;
        int k;
        int found = -1;
        if (e == NULL) {
            return -1;
        }
        bdf = inst_get32(e, CM_ST_E_BRIDGE);
        for (k = 0; k < gw.Count; k++) {
            if (gw.N[k].PortNhi == n->A && port_bdf(&gw, nhi, k) == bdf) {
                int c;
                for (c = k + 1; c < gw.Count; c++) {
                    if (gw.N[c].Parent == k && gw.N[c].UnitOf == c) {
                        if (found >= 0) {
                            return -1;
                        }
                        found = c;
                    }
                }
            }
        }
        return found;
    }
    return -1;
}

static void show_details(void)
{
    TR_TEXT t;
    int i = tree_selected();

    tr_text_init(&t, detail, sizeof(detail));
    if (i >= 0) {
        details_of(&t, i, (viewFlags & TR_V_DESC) ? 1 : 0);
    }
    SetWindowTextA(hEdit, detail);
}

static void update_status(void)
{
    char text[128];

    tr_sprintf(text, sizeof(text), "%d USB device%s, %d Thunderbolt/USB4 device%s%s",
               usbDevices, tr_plural(usbDevices), tbDevices, tr_plural(tbDevices),
               viewStale ? " - changed, press F5" : "");
    SendMessageA(hStatus, SB_SETTEXTA, 0, (LPARAM)text);
}

/* The keys of the expanded nodes and the selection, kept across a
 * rebuild (record 25 section 3.4). */
static char *keepKeys;
static size_t keepLen;
static char keepSel[280];

static void keep_state(void)
{
    int i;
    int sel = tree_selected();
    size_t need = 1;

    free(keepKeys);
    keepKeys = NULL;
    keepLen = 0;
    keepSel[0] = '\0';
    if (sel >= 0) {
        tr_copy(keepSel, tn[sel].Key, sizeof(keepSel));
    }
    for (i = 0; i < tnCount; i++) {
        need += strlen(tn[i].Key) + 1;
    }
    keepKeys = (char *)malloc(need);
    if (keepKeys == NULL) {
        return;
    }
    for (i = 0; i < tnCount; i++) {
        TVITEMA tvi;
        memset(&tvi, 0, sizeof(tvi));
        tvi.mask = TVIF_STATE;
        tvi.hItem = tn[i].Item;
        tvi.stateMask = TVIS_EXPANDED;
        if (tn[i].Item != NULL && TreeView_GetItem(hTree, &tvi) &&
            (tvi.state & TVIS_EXPANDED)) {
            size_t l = strlen(tn[i].Key);
            memcpy(keepKeys + keepLen, tn[i].Key, l + 1);
            keepLen += l + 1;
        }
    }
}

static int was_expanded(const char *key)
{
    size_t o = 0;

    while (keepKeys != NULL && o < keepLen) {
        if (strcmp(keepKeys + o, key) == 0) {
            return 1;
        }
        o += strlen(keepKeys + o) + 1;
    }
    return 0;
}

static void fill_tree(void)
{
    int i;
    HTREEITEM select = NULL;

    SendMessageA(hTree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(hTree);
    for (i = 0; i < tnCount; i++) {
        TVINSERTSTRUCTA ins;
        memset(&ins, 0, sizeof(ins));
        ins.hParent = tn[i].Parent >= 0 ? tn[tn[i].Parent].Item : TVI_ROOT;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM;
        ins.item.pszText = tn[i].Label;
        ins.item.lParam = (LPARAM)i;
        tn[i].Item = (HTREEITEM)SendMessageA(hTree, TVM_INSERTITEMA, 0,
                                             (LPARAM)&ins);
        if (keepSel[0] != '\0' && strcmp(tn[i].Key, keepSel) == 0) {
            select = tn[i].Item;
        }
    }
    for (i = 0; i < tnCount; i++) {
        if (tn[i].Item != NULL && (firstBuild || was_expanded(tn[i].Key))) {
            TreeView_Expand(hTree, tn[i].Item, TVE_EXPAND);
        }
    }
    firstBuild = 0;
    if (select == NULL && tnCount > 0) {
        select = tn[0].Item;
    }
    if (select != NULL) {
        TreeView_SelectItem(hTree, select);
        TreeView_EnsureVisible(hTree, select);
        SendMessageA(hTree, WM_HSCROLL, SB_LEFT, 0);
    }
    SendMessageA(hTree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTree, NULL, TRUE);
    show_details();
    update_status();
}

static void refresh_window(void)
{
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));

    if (hMain != NULL) {
        keep_state();
    }
    viewStale = 0;
    refresh_model(hMain != NULL);
    if (hMain != NULL) {
        fill_tree();
    }
    SetCursor(old);
}

static void layout(void)
{
    RECT rc;
    RECT sb;
    int h;

    if (hMain == NULL) {
        return;
    }
    GetClientRect(hMain, &rc);
    if (IsIconic(hMain) || rc.right < 130) {
        /* Minimised (width 0): nothing to lay out, and splitX is kept. */
        return;
    }
    SendMessageA(hStatus, WM_SIZE, 0, 0);
    GetWindowRect(hStatus, &sb);
    h = rc.bottom - (sb.bottom - sb.top);
    if (h < 0) {
        h = 0;
    }
    drawnSplit = splitX;
    if (drawnSplit > rc.right - 60) {
        drawnSplit = rc.right - 60;
    }
    if (drawnSplit < 60) {
        drawnSplit = 60;
    }
    MoveWindow(hTree, 0, 0, drawnSplit, h, TRUE);
    MoveWindow(hEdit, drawnSplit + 4, 0, rc.right - drawnSplit - 4, h, TRUE);
}

static void save_report_dialog(void)
{
    OPENFILENAMEA ofn;
    char file[MAX_PATH];
    char msg[MAX_PATH + 64];

    tr_copy(file, "xhcitr-report.txt", sizeof(file));
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMain;
    ofn.lpstrFilter = "Text files (*.txt)\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrDefExt = "txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameA(&ofn)) {
        return;
    }
    if (!write_report(file)) {
        tr_sprintf(msg, sizeof(msg), "The report could not be written to %s.", file);
        MessageBoxA(hMain, msg, "xHCI98 USB Tree", MB_OK | MB_ICONWARNING);
    }
    show_details();
}

static void copy_details(void)
{
    DWORD s = 0;
    DWORD e = 0;
    HGLOBAL g;
    size_t n;

    SendMessageA(hEdit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    if (e > s) {
        SendMessageA(hEdit, WM_COPY, 0, 0);
        return;
    }
    n = strlen(detail) + 1;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n);
    if (g == NULL) {
        return;
    }
    memcpy(GlobalLock(g), detail, n);
    GlobalUnlock(g);
    if (OpenClipboard(hMain)) {
        EmptyClipboard();
        if (SetClipboardData(CF_TEXT, g) == NULL) {
            GlobalFree(g);
        }
        CloseClipboard();
    } else {
        GlobalFree(g);
    }
}

typedef int (WINAPI *PFN_DEVPROPS)(HWND, LPCSTR, LPCSTR, BOOL);

/* Device Manager's properties of a devnode (2000 to 7). */
static void properties(int i)
{
    int dn = i >= 0 ? tn[i].Dn : -1;
    HMODULE dm;
    char cmd[INST_ID_CHARS + 96];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    if (dn < 0 || !envNt) {
        return;
    }
    dm = LoadLibraryA("devmgr.dll");
    if (dm == NULL) {
        return;
    }
    if (GetProcAddress(dm, "DeviceProperties_RunDLLA") != NULL) {
        tr_sprintf(cmd, sizeof(cmd),
                   "rundll32.exe devmgr.dll,DeviceProperties_RunDLL /DeviceID %s",
                   gw.N[dn].Id);
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    } else {
        PFN_DEVPROPS props = (PFN_DEVPROPS)GetProcAddress(dm, "DevicePropertiesA");
        if (props != NULL) {
            props(hMain, NULL, gw.N[dn].Id, FALSE);
        }
    }
    FreeLibrary(dm);
}

static BOOL CALLBACK about_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    char text[512];

    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        tr_sprintf(text, sizeof(text),
                   "xHCI98 USB Tree\r\nversion %s, part of the xhci98 %s release\r\n\r\n"
                   "Shows every USB and Thunderbolt/USB4 controller and device, "
                   "and stops a USB storage device or a device behind a "
                   "Thunderbolt or USB4 port safely.\r\n\r\n"
                   "Free software under the GNU General Public License, version 2.",
                   XHCITR_VERSION, XHCI_VER_STR);
        SetDlgItemTextA(dlg, IDC_ABOUT_TEXT, text);
        SendDlgItemMessageA(dlg, IDC_ABOUT_ICON, STM_SETICON, (WPARAM)hIconBig, 0);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void about(HWND owner)
{
    DialogBoxA(hInst, MAKEINTRESOURCEA(IDD_ABOUT), owner, (DLGPROC)about_proc);
}

static const struct {
    UINT Id;
    DWORD Flag;
} viewMenu[] = {
    { IDM_V_TB, TR_V_TB },
    { IDM_V_USB, TR_V_USB },
    { IDM_V_WINUSB, TR_V_WINUSB },
    { IDM_V_EMPTY, TR_V_EMPTY },
    { IDM_V_DESC, TR_V_DESC },
    { IDM_V_BEHIND, TR_V_BEHIND },
    { IDM_V_AUTO, TR_V_AUTO }
};

static void init_menu(HMENU m)
{
    int i;
    int sel = tree_selected();
    int u = unit_of_node(sel);

    for (i = 0; i < (int)(sizeof(viewMenu) / sizeof(viewMenu[0])); i++) {
        CheckMenuItem(m, viewMenu[i].Id,
                      MF_BYCOMMAND | ((viewFlags & viewMenu[i].Flag) ? MF_CHECKED
                                                                    : MF_UNCHECKED));
    }
    EnableMenuItem(m, IDM_EJECT, MF_BYCOMMAND |
                                     (u >= 0 && (usb_listed(u) ? usb_why(u) == NULL
                                                               : ej_why_not(u) == NULL)
                                          ? MF_ENABLED : MF_GRAYED));
    if (GetMenuState(m, IDM_LOGIN, MF_BYCOMMAND) != (UINT)-1) {
        login_item(m, 0);
    }
    EnableMenuItem(m, IDM_PROPS, MF_BYCOMMAND |
                                     (envNt && sel >= 0 && tn[sel].Dn >= 0
                                          ? MF_ENABLED : MF_GRAYED));
}

static void open_window(void);
static void quit_all(void);

static int command(HWND w, UINT id)
{
    int i;

    for (i = 0; i < (int)(sizeof(viewMenu) / sizeof(viewMenu[0])); i++) {
        if (viewMenu[i].Id == id) {
            viewFlags ^= viewMenu[i].Flag;
            settings_save();
            if (viewMenu[i].Flag == TR_V_DESC) {
                show_details();
            } else if (viewMenu[i].Flag != TR_V_AUTO) {
                refresh_window();
            }
            return 1;
        }
    }
    switch (id) {
    case IDM_REFRESH:
        refresh_window();
        return 1;
    case IDM_LOGIN:
        login_set(!login_on());
        return 1;
    case IDM_SAVE:
        save_report_dialog();
        return 1;
    case IDM_EXIT:
        quit_all();
        return 1;
    case IDM_COPY:
        copy_details();
        return 1;
    case IDM_SELALL:
        SetFocus(hEdit);
        SendMessageA(hEdit, EM_SETSEL, 0, -1);
        return 1;
    case IDM_EJECT: {
        int u = unit_of_node(tree_selected());
        if (u >= 0) {
            ej_unit(w, u, 0);
            refresh_window();
        }
        return 1;
    }
    case IDM_PROPS:
        properties(tree_selected());
        return 1;
    case IDM_ABOUT:
        about(w);
        return 1;
    }
    return 0;
}

static LRESULT CALLBACK main_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        hTree = CreateWindowExA(WS_EX_CLIENTEDGE, WC_TREEVIEWA, "",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES |
                                    TVS_LINESATROOT | TVS_HASBUTTONS |
                                    TVS_SHOWSELALWAYS,
                                0, 0, 10, 10, w, (HMENU)1, hInst, NULL);
        hEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                    WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                                    ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                0, 0, 10, 10, w, (HMENU)2, hInst, NULL);
        hStatus = CreateWindowExA(0, STATUSCLASSNAMEA, "",
                                  WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                  w, (HMENU)3, hInst, NULL);
        SendMessageA(hTree, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0);
        SendMessageA(hEdit, WM_SETFONT, (WPARAM)hFixed, 0);
        SendMessageA(hEdit, EM_LIMITTEXT, 0, 0);
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_SETCURSOR:
        if ((HWND)wp == w && LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(w, &p);
            if (p.x >= drawnSplit && p.x < drawnSplit + 4) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN:
        if ((short)LOWORD(lp) >= drawnSplit - 2 &&
            (short)LOWORD(lp) < drawnSplit + 6) {
            dragging = 1;
            SetCapture(w);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (dragging) {
            RECT rc;
            int x = (short)LOWORD(lp);
            GetClientRect(w, &rc);
            if (x > rc.right - 60) {
                x = rc.right - 60;
            }
            if (x < 60) {
                x = 60;
            }
            splitX = x;
            layout();
        }
        return 0;
    case WM_LBUTTONUP:
        if (dragging) {
            dragging = 0;
            ReleaseCapture();
            settings_save();
        }
        return 0;
    case WM_INITMENUPOPUP:
        init_menu((HMENU)wp);
        return 0;
    case WM_COMMAND:
        if (command(w, LOWORD(wp))) {
            return 0;
        }
        break;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->hwndFrom == hTree) {
            if (nh->code == TVN_SELCHANGEDA || nh->code == TVN_SELCHANGEDW) {
                show_details();
            } else if (nh->code == NM_RCLICK) {
                TVHITTESTINFO ht;
                POINT p;
                HMENU bar = GetMenu(w);
                GetCursorPos(&p);
                ht.pt = p;
                ScreenToClient(hTree, &ht.pt);
                if (TreeView_HitTest(hTree, &ht) != NULL) {
                    TreeView_SelectItem(hTree, ht.hItem);
                }
                if (bar != NULL) {
                    HMENU action = GetSubMenu(bar, 3);
                    init_menu(action);
                    TrackPopupMenu(action, TPM_RIGHTBUTTON, p.x, p.y, 0, w, NULL);
                }
                return TRUE;
            } else if (nh->code == NM_RETURN) {
                properties(tree_selected());
                return TRUE;
            }
        }
        break;
    }
    case WM_CLOSE: {
        WINDOWPLACEMENT wpl;
        memset(&wpl, 0, sizeof(wpl));
        wpl.length = sizeof(wpl);
        if (GetWindowPlacement(w, &wpl)) {
            winRect = wpl.rcNormalPosition;
            haveWinRect = 1;
        }
        settings_save();
        DestroyWindow(w);
        return 0;
    }
    case WM_DESTROY:
        hMain = NULL;
        hTree = NULL;
        hEdit = NULL;
        hStatus = NULL;
        if (hTray == NULL || !iconAdded) {
            /* No icon to come back from: the program ends with its window. */
            quit_all();
        }
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static void open_window(void)
{
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int cx = 820;
    int cy = 560;

    if (hMain != NULL) {
        ShowWindow(hMain, SW_RESTORE);
        SetForegroundWindow(hMain);
        return;
    }
    if (haveWinRect) {
        x = winRect.left;
        y = winRect.top;
        cx = winRect.right - winRect.left;
        cy = winRect.bottom - winRect.top;
    } else {
        RECT wa;
        if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0)) {
            if (cx > wa.right - wa.left) {
                cx = wa.right - wa.left;
            }
            if (cy > wa.bottom - wa.top) {
                cy = wa.bottom - wa.top;
            }
        }
    }
    hMain = CreateWindowExA(0, "Xhci98TrMain", "xHCI98 USB Tree",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, cx, cy,
                            NULL, LoadMenuA(hInst, MAKEINTRESOURCEA(IDR_MENU)),
                            hInst, NULL);
    if (hMain == NULL) {
        return;
    }
    firstBuild = 1;
    ShowWindow(hMain, SW_SHOWNORMAL);
    UpdateWindow(hMain);
    refresh_window();
    SetFocus(hTree);
}

/* ======================================================================== */
/* The tray (record 25 section 7)                                            */
/* ======================================================================== */

static void tray_tip(TR_NID *n)
{
    char tip[128];

    if (!shellV5 && lastEvent[0] != '\0') {
        tr_sprintf(tip, sizeof(tip), "xHCI98 USB Tree: %d TB/USB4 - %s",
                   tbDevices, lastEvent);
    } else {
        tr_sprintf(tip, sizeof(tip), "xHCI98 USB Tree: %d Thunderbolt/USB4 device%s",
                   tbDevices, tr_plural(tbDevices));
    }
    tr_copy(n->szTip, tip, shellV5 ? sizeof(n->szTip) : 64);
}

static void tray_add(void)
{
    TR_NID n;

    memset(&n, 0, sizeof(n));
    n.cbSize = shellV5 ? TR_NID_V2 : TR_NID_V1;
    n.hWnd = hTray;
    n.uID = 1;
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    n.uCallbackMessage = WM_TRAYICON;
    n.hIcon = hIconSmall;
    tray_tip(&n);
    iconAdded = Shell_NotifyIconA(NIM_ADD, (PNOTIFYICONDATAA)&n) != 0;
    if (iconAdded && shellV5) {
        TR_NID_VERSION(n) = 3;
        trayV3 = Shell_NotifyIconA(NIM_SETVERSION, (PNOTIFYICONDATAA)&n) != 0;
    }
}

static void tray_update(void)
{
    TR_NID n;

    if (!iconAdded) {
        return;
    }
    memset(&n, 0, sizeof(n));
    n.cbSize = shellV5 ? TR_NID_V2 : TR_NID_V1;
    n.hWnd = hTray;
    n.uID = 1;
    n.uFlags = NIF_TIP;
    tray_tip(&n);
    Shell_NotifyIconA(NIM_MODIFY, (PNOTIFYICONDATAA)&n);
}

static void tray_remove(void)
{
    TR_NID n;

    if (!iconAdded) {
        return;
    }
    memset(&n, 0, sizeof(n));
    n.cbSize = shellV5 ? TR_NID_V2 : TR_NID_V1;
    n.hWnd = hTray;
    n.uID = 1;
    Shell_NotifyIconA(NIM_DELETE, (PNOTIFYICONDATAA)&n);
    iconAdded = 0;
}

static void tray_balloon(const char *title, const char *text, int warn)
{
    TR_NID n;

    tr_copy(lastEvent, text, sizeof(lastEvent));
    if (!iconAdded) {
        return;
    }
    if (!shellV5) {
        tray_update();
        return;
    }
    memset(&n, 0, sizeof(n));
    n.cbSize = TR_NID_V2;
    n.hWnd = hTray;
    n.uID = 1;
    n.uFlags = NIF_INFO;
    tr_copy(n.szInfo, text, sizeof(n.szInfo));
    tr_copy(n.szInfoTitle, title, sizeof(n.szInfoTitle));
    TR_NID_VERSION(n) = 10000;
    n.dwInfoFlags = warn ? NIIF_WARNING : NIIF_INFO;
    Shell_NotifyIconA(NIM_MODIFY, (PNOTIFYICONDATAA)&n);
}

/* One second's poll: every NHI's query, the snapshot and its events
 * (record 25 section 7.2). */
static void poll(void)
{
    TR_SNAP *now = (TR_SNAP *)malloc(sizeof(TR_SNAP));
    TR_EVENT ev[8];
    int count;
    int k;
    int i;
    int changed = 0;
    int routers = 0;

    if (now == NULL) {
        return;
    }
    tr_snap_init(now);
    now->Valid = 1;
    for (k = 0; k < nhiCount; k++) {
        unsigned char *b;
        TR_QUERY q;
        TR_U32 g;
        const TR_QUERY *base;
        if (pollBuf[k] != NULL && tr_eq(pollId[k], nhi[k].Id)) {
            base = &pollQ[k];
        } else {
            base = nhi[k].HaveQ ? &nhi[k].Q : NULL;
        }
        if (nhi[k].QName[0] == '\0' || !query_read(nhi[k].QName, nhi[k].Id, &b, &q, nhi[k].NameUnique, NULL)) {
            if (base != NULL) {
                changed = 1;    /* it answered before: read it all again */
            }
            free(pollBuf[k]);
            pollBuf[k] = NULL;
            continue;
        }
        g = inst_get32(q.Rec, CM_ST_O_GENERATION);
        if (base == NULL || !nhi[k].HaveQ ||
            g != inst_get32(base->Rec, CM_ST_O_GENERATION) ||
            q.ContentGen != base->ContentGen || q.Flags != base->Flags ||
            q.Live != base->Live || q.EpochLo != base->EpochLo ||
            q.EpochHi != base->EpochHi ||
            tr_query_current(&q) != tr_query_current(base)) {
            changed = 1;
        }
        routers += (int)inst_get32(q.Rec, CM_ST_O_ROUTER_COUNT);
        /* The NHI by its instance ID, not its place in the list, which a
         * reading of the tree may change. */
        tr_snap_add(now, inst_fnv1a(INST_FNV_START, (const unsigned char *)nhi[k].Id,
                                    strlen(nhi[k].Id)), &q);
        free(pollBuf[k]);
        pollBuf[k] = b;
        pollQ[k] = q;
        tr_copy(pollId[k], nhi[k].Id, sizeof(pollId[k]));
    }
    count = tr_diff(&snapOld, now, ev, 8);
    for (i = 0; i < count; i++) {
        char text[256];
        tr_event_text(&ev[i], text, sizeof(text));
        tray_balloon(tr_event_title(ev[i].Kind), text,
                     ev[i].Kind == TR_EV_STOPPED || ev[i].Kind == TR_EV_FAILED);
    }
    memcpy(&snapOld, now, sizeof(*now));
    free(now);
    if (changed) {
        /* The NHIs' state is read again with the devices. */
        if (hMain != NULL && (viewFlags & TR_V_AUTO)) {
            refresh_window();
        } else if (hMain != NULL) {
            /* The model stays the one the tree shows until F5. */
            viewStale = 1;
            update_status();
        } else {
            refresh_model(0);
        }
        (void)routers;
        tray_update();
    }
}

static void tray_unit_menu(void)
{
    HMENU m = CreatePopupMenu();
    POINT p;
    int i;
    int g;
    UINT cmd;

    if (m == NULL) {
        return;
    }
    if (hMain == NULL) {
        refresh_model(0);
    }
    trayUnitCount = 0;
    /* Two groups, each under a grey heading and only when it has an entry
     * (record 25 section 6.5): USB devices, then the Thunderbolt units. */
    for (g = 0; g < 2; g++) {
        int heading = 0;
        for (i = 0; i < gw.Count && trayUnitCount < 64; i++) {
            const char *why;
            char text[256];
            char name[160];
            if (g == 0) {
                if (!usb_listed(i)) {
                    continue;
                }
                why = usb_why(i);
                usb_label(i, name, sizeof(name));
            } else {
                if (gw.N[i].UnitOf != i || gw.N[i].Parent < 0 ||
                    gw.N[gw.N[i].Parent].PortNhi < 0) {
                    continue;
                }
                why = ej_why_not(i);
                unit_label(i, name, sizeof(name));
            }
            if (!heading) {
                if (trayUnitCount > 0) {
                    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
                }
                AppendMenuA(m, MF_STRING | MF_GRAYED, 0,
                            g == 0 ? "USB devices" : "Thunderbolt and USB4 devices");
                heading = 1;
            }
            if (why == NULL) {
                tr_sprintf(text, sizeof(text), "Safely remove %s", name);
            } else {
                tr_sprintf(text, sizeof(text), "Safely remove %s (%s)", name, why);
            }
            tr_copy(trayUnitIds[trayUnitCount], gw.N[i].Id,
                    sizeof(trayUnitIds[0]));
            AppendMenuA(m, MF_STRING | (why == NULL ? 0 : MF_GRAYED),
                        IDM_UNIT0 + trayUnitCount, text);
            trayUnitCount++;
        }
    }
    if (trayUnitCount == 0) {
        AppendMenuA(m, MF_STRING | MF_GRAYED, 0, "No device to stop");
    }
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, IDM_OPEN, "Open xHCI98 USB Tree");
    GetCursorPos(&p);
    SetForegroundWindow(hTray);
    cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0,
                               hTray, NULL);
    PostMessageA(hTray, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == IDM_OPEN) {
        open_window();
    } else if (cmd >= IDM_UNIT0 && cmd < (UINT)(IDM_UNIT0 + trayUnitCount)) {
        /* The tree may have been read again while the menu was open: the
         * unit is found again by its instance ID. */
        ej_unit(hTray, walk_find_id(&gw, trayUnitIds[cmd - IDM_UNIT0]), 1);
        if (hMain != NULL) {
            refresh_window();
        } else {
            refresh_model(0);
        }
    }
}

static void tray_context_menu(void)
{
    HMENU m = CreatePopupMenu();
    POINT p;
    UINT cmd;

    if (m == NULL) {
        return;
    }
    AppendMenuA(m, MF_STRING, IDM_OPEN, "&Open");
    AppendMenuA(m, MF_STRING, IDM_REFRESH, "&Refresh");
    AppendMenuA(m, MF_STRING, IDM_ABOUT, "&About");
    login_item(m, 1);
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, IDM_EXIT, "E&xit");
    SetMenuDefaultItem(m, IDM_OPEN, FALSE);
    GetCursorPos(&p);
    SetForegroundWindow(hTray);
    cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0,
                               hTray, NULL);
    PostMessageA(hTray, WM_NULL, 0, 0);
    DestroyMenu(m);
    switch (cmd) {
    case IDM_OPEN:
        open_window();
        break;
    case IDM_REFRESH:
        refresh_window();
        tray_update();
        break;
    case IDM_ABOUT:
        about(NULL);
        break;
    case IDM_LOGIN:
        login_set(!login_on());
        break;
    case IDM_EXIT:
        quit_all();
        break;
    }
}

static void quit_all(void)
{
    if (quitting) {
        return;
    }
    quitting = 1;
    if (hMain != NULL) {
        SendMessageA(hMain, WM_CLOSE, 0, 0);
    }
    tray_remove();
    if (hTray != NULL) {
        DestroyWindow(hTray);
    }
    PostQuitMessage(0);
}

static LRESULT CALLBACK tray_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == msgTaskbarCreated && msgTaskbarCreated != 0) {
        iconAdded = 0;
        tray_add();
        return 0;
    }
    if (msg == msgShow && msgShow != 0) {
        open_window();
        return 0;
    }
    switch (msg) {
    case WM_TRAYICON:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
            if (trayNoClick) {
                /* The button-up that ends a double click opens nothing. */
                trayNoClick = 0;
                break;
            }
            SetTimer(w, TIMER_CLICK, GetDoubleClickTime(), NULL);
            break;
        case WM_LBUTTONDBLCLK:
            KillTimer(w, TIMER_CLICK);
            trayNoClick = 1;
            open_window();
            break;
        case WM_USER + 1:       /* NIN_KEYSELECT: the keyboard's click */
            tray_unit_menu();
            break;
        case WM_RBUTTONUP:
            if (!trayV3) {
                tray_context_menu();
            }
            break;
        case WM_CONTEXTMENU:    /* version 3: after the right button's up */
            tray_context_menu();
            break;
        }
        return 0;
    case WM_TIMER:
        if (wp == TIMER_CLICK) {
            KillTimer(w, TIMER_CLICK);
            tray_unit_menu();
        } else if (wp == TIMER_POLL) {
            pollCount++;
            if (quit_asked()) {
                quit_all();
                return 0;
            }
            poll();
        } else if (wp == TIMER_CHANGE) {
            KillTimer(w, TIMER_CHANGE);
            refresh_window();
            tray_update();
        }
        return 0;
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVNODES_CHANGED) {
            SetTimer(w, TIMER_CHANGE, 500, NULL);
        }
        return TRUE;
    case WM_CLOSE:
        quit_all();
        return 0;
    case WM_DESTROY:
        tray_remove();
        hTray = NULL;
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* ======================================================================== */
/* The one-shot modes and the start                                          */
/* ======================================================================== */

/* Where a one-shot mode's words go: the redirected standard output when
 * there is one, else a message box. */
static void say(const char *text, int warn)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD w;

    if (out != NULL && out != INVALID_HANDLE_VALUE &&
        GetFileType(out) != FILE_TYPE_UNKNOWN) {
        /* Console text: at most 79 columns (record 25 section 6.5). */
        static char wrapped[8192];
        tr_wrap(text, wrapped, sizeof(wrapped), 79);
        WriteFile(out, wrapped, (DWORD)strlen(wrapped), &w, NULL);
        WriteFile(out, "\r\n", 2, &w, NULL);
        return;
    }
    MessageBoxA(NULL, text, "xHCI98 USB Tree",
                MB_OK | (warn ? MB_ICONWARNING : MB_ICONINFORMATION));
}

static int run_report(void)
{
    char msg[TR_PATH_CHARS + 64];

    refresh_model(1);
    if (!write_report(opts.ReportPath)) {
        tr_sprintf(msg, sizeof(msg), "The report could not be written to %s.",
                   opts.ReportPath);
        say(msg, 1);
        return INST_EXIT_FAILED;
    }
    return INST_EXIT_DONE;
}

/* /eject id=<instance id>: the whole transaction once, then exit (record
 * 25 section 6.4). The user confirmed in the run that started this one. */
static int run_eject(void)
{
    EJREQ *q = (EJREQ *)calloc(1, sizeof(EJREQ));
    char msg[2048];
    const char *why;
    int u;
    int rights;
    int code;

    char digest[TR_DIGEST_CHARS];

    if (q == NULL) {
        return INST_EXIT_FAILED;
    }
    refresh_model(0);
    u = walk_find_id(&gw, opts.EjectId);
    if (u >= 0 && gw.N[u].UPort >= 0) {
        free(q);
        return usb_run_eject(u);
    }
    if (u < 0 || gw.N[u].UnitOf != u) {
        tr_sprintf(msg, sizeof(msg), "No USB device, and no device behind a "
                   "Thunderbolt port, has the instance ID %s.", opts.EjectId);
        code = INST_EXIT_REFUSED;
        goto answer;
    }
    why = ej_why_not(u);
    if (why != NULL || !ej_prepare(u, q)) {
        tr_sprintf(msg, sizeof(msg), "%s cannot be stopped here: %s.",
                   dn_name(&gw.N[u]), why != NULL ? why : "it is not a unit");
        code = INST_EXIT_REFUSED;
        goto answer;
    }
    /* This run's request is confirmed as it stands now: a device that
     * appeared under the unit while the elevation was asked is listed, and
     * the transaction holds to exactly this list. With /confirmed, the
     * window's confirmation stands for it when this list has that
     * digest. */
    tr_res_digest((const char (*)[TR_ID_CHARS])q->Devs, q->DevCount, digest);
    if (!(opts.Confirmed && strcmp(digest, opts.ConfirmedDigest) == 0) &&
        !ej_confirm(NULL, q)) {
        code = INST_EXIT_REFUSED;
        tr_copy(msg, "Cancelled; nothing was stopped.", sizeof(msg));
        goto answer;
    }
    /* Not the elevated run itself: a unit not marked removable is offered
     * with administrator rights here too (section 6.6). */
    if (!opts.Result &&
        tr_elev_needed(envNt, inst_unit_not_marked(gw.N[u].HasCaps,
                                                   gw.N[u].Caps),
                       ej_is_admin())) {
        if (!ej_elev_ask(NULL, q)) {
            code = INST_EXIT_REFUSED;
            tr_copy(msg, "Cancelled; nothing was stopped.", sizeof(msg));
            goto answer;
        }
        code = ej_elev_run(NULL, q, msg, sizeof(msg));
        tr_join_lines(msg);
        goto answer;
    }
    code = ej_run(q, msg, sizeof(msg), &rights);
    tr_join_lines(msg);
answer:
    if (opts.Result) {
        if (elev_child_answer(code, msg)) {
            free(q);
            return code;
        }
    }
    say(msg, code != INST_EXIT_DONE);
    free(q);
    return code;
}

/* The pure half's checks on the built image (selftest.cmd). */
static int run_selftest(void)
{
    static const unsigned char dev[18] = {
        18, 1, 0x00, 0x03, 0, 0, 0, 9, 0x81, 0x07, 0x81, 0x55, 0x00, 0x01, 1, 2, 3, 1
    };
    char text[4096];
    char err[256];
    TR_TEXT t;
    TR_OPTS o;
    TR_CONN c;
    unsigned char ci[TR_CI_BYTES];
    char *argv1[3];
    int checks = 0;
    int fails = 0;
    char line[96];

#define ST(cond) do { checks++; if (!(cond)) { fails++; } } while (0)
    tr_text_init(&t, text, sizeof(text));
    tr_dec_device(&t, dev, sizeof(dev));
    ST(strstr(text, "0x0781") != NULL);
    ST(strstr(text, "0x5581") != NULL);
    memset(ci, 0, sizeof(ci));
    ci[0] = 3;
    memcpy(ci + 4, dev, 18);
    ci[23] = 3;
    ci[31] = 1;
    ST(tr_conn_parse(ci, sizeof(ci), 1, &c) && c.Speed == 3 && c.Port == 3);
    ST(tr_speed(2, 1, TR_V2_F_SS_NOW) == TR_SPEED_SUPER);
    argv1[0] = "x";
    argv1[1] = "/frob";
    argv1[2] = NULL;
    ST(tr_parse_args(2, argv1, &o, err, sizeof(err)) == 1);
    argv1[1] = "/tray";
    ST(tr_parse_args(2, argv1, &o, err, sizeof(err)) == 0 && o.Tray);
    ST(sizeof(TR_USB_CONN_EX) == 35 && sizeof(TR_USB_CONN_V2) == 16);
#ifndef _WIN64
    ST(sizeof(TR_NIDA) == TR_NIDA_V2_BYTES);
#endif
#undef ST
    tr_sprintf(line, sizeof(line), "%d checks, %d failures", checks, fails);
    say(line, fails != 0);
    return fails == 0 ? INST_EXIT_DONE : INST_EXIT_REFUSED;
}

typedef struct _TR_DLLVER {
    DWORD cbSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformID;
} TR_DLLVER;
typedef HRESULT (CALLBACK *PFN_DLLVER)(TR_DLLVER *);

static int shell_version(void)
{
    HMODULE sh = GetModuleHandleA("shell32.dll");
    PFN_DLLVER get = (PFN_DLLVER)proc(sh, "DllGetVersion");
    TR_DLLVER v;

    if (get == NULL) {
        return 4;
    }
    memset(&v, 0, sizeof(v));
    v.cbSize = sizeof(v);
    if (get(&v) != S_OK) {
        return 4;
    }
    return (int)v.dwMajorVersion;
}

static int register_classes(void)
{
    WNDCLASSA wc;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = tray_proc;
    wc.hInstance = hInst;
    wc.hIcon = hIconBig;
    wc.lpszClassName = "Xhci98TrTray";
    if (!RegisterClassA(&wc)) {
        return 0;
    }
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = main_proc;
    wc.hInstance = hInst;
    wc.hIcon = hIconBig;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszClassName = "Xhci98TrMain";
    return RegisterClassA(&wc) != 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdLine, int show)
{
    OSVERSIONINFOA osv;
    char err[300];
    HANDLE single;
    MSG msg;
    int code;

    (void)prev;
    (void)cmdLine;
    (void)show;
    hInst = inst;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    memset(&osv, 0, sizeof(osv));
    osv.dwOSVersionInfoSize = sizeof(osv);
    GetVersionExA(&osv);
    envNt = osv.dwPlatformId == VER_PLATFORM_WIN32_NT;
    envMajor = osv.dwMajorVersion;
    envMinor = osv.dwMinorVersion;
    if (tr_parse_args(__argc, __argv, &opts, err, sizeof(err)) != 0) {
        say(err, 1);
        return INST_EXIT_REFUSED;
    }
    if (opts.SelfTest) {
        return run_selftest();
    }
    if (opts.Tray) {
        /* The start at login: the user's choice first, before any window,
         * icon or thread (record 25 section 7.4). */
        DWORD v;
        int have = login_read(&v);
        if (!tr_tray_login_start(1, have, v)) {
            return INST_EXIT_DONE;
        }
    }
    InitCommonControls();
    load_routines();
    watch_start();
    settings_load();
    uc = (UCTRL *)calloc(TR_MAX_CTRL, sizeof(UCTRL));
    up = (UPORT *)calloc(TR_MAX_UPORTS, sizeof(UPORT));
    tn = (TNODE *)calloc(TR_MAX_TNODES, sizeof(TNODE));
    if (uc == NULL || up == NULL || tn == NULL) {
        say("Out of memory.", 1);
        return INST_EXIT_FAILED;
    }
    if (opts.Report) {
        return run_report();
    }
    if (opts.Eject) {
        return run_eject();
    }

    /* One tray per session; a second plain start asks the first to show
     * its window (record 25 section 7.1). */
    msgShow = RegisterWindowMessageA("Xhci98TrShow");
    SetLastError(0);
    single = CreateMutexA(NULL, FALSE, envNt ? "Local\\Xhci98TrTray" : "Xhci98TrTray");
    if (single == NULL) {
        /* Not created: one may exist that this user cannot take over. */
        single = OpenMutexA(SYNCHRONIZE, FALSE,
                            envNt ? "Local\\Xhci98TrTray" : "Xhci98TrTray");
        if (single == NULL) {
            say("The xHCI98 USB Tree cannot tell whether it is already running, "
                "so it does not start a second one.", 1);
            return INST_EXIT_REFUSED;
        }
        SetLastError(ERROR_ALREADY_EXISTS);
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowA("Xhci98TrTray", NULL);
        DWORD start = GetTickCount();
        /* The first one may still be starting (it waits for an installer
         * for up to 20 s before its window exists). */
        while (other == NULL && !opts.Tray && GetTickCount() - start < 25000) {
            Sleep(250);
            other = FindWindowA("Xhci98TrTray", NULL);
        }
        if (other != NULL && !opts.Tray) {
            PostMessageA(other, msgShow, 0, 0);
        }
        CloseHandle(single);
        return INST_EXIT_DONE;
    }
    if (quit_asked()) {
        /* An installer is replacing the tool: wait for it to finish. */
        DWORD start = GetTickCount();
        while (quit_asked() && GetTickCount() - start < 20000) {
            Sleep(250);
        }
    }
    hIconBig = LoadIconA(inst, MAKEINTRESOURCEA(IDI_APP));
    hIconSmall = (HICON)LoadImageA(inst, MAKEINTRESOURCEA(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    if (hIconSmall == NULL) {
        hIconSmall = hIconBig;
    }
    hFixed = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    hAccel = LoadAcceleratorsA(inst, MAKEINTRESOURCEA(IDR_ACCEL));
    if (!register_classes()) {
        return INST_EXIT_FAILED;
    }
    shellV5 = shell_version() >= 5;
    msgTaskbarCreated = RegisterWindowMessageA("TaskbarCreated");
    /* A hidden top-level window, so the TaskbarCreated broadcast and
     * XHCIINST's WM_CLOSE both reach it. */
    hTray = CreateWindowExA(WS_EX_TOOLWINDOW, "Xhci98TrTray", "xHCI98 USB Tree",
                            WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (hTray == NULL) {
        return INST_EXIT_FAILED;
    }
    refresh_model(0);
    tray_add();
    poll();
    SetTimer(hTray, TIMER_POLL, 1000, NULL);
    if (!opts.Tray) {
        open_window();
    }

    code = INST_EXIT_DONE;
    for (;;) {
        WaitMessage();
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                goto done;
            }
            if (hMain != NULL && hAccel != NULL &&
                (msg.hwnd == hMain || IsChild(hMain, msg.hwnd)) &&
                TranslateAcceleratorA(hMain, hAccel, &msg)) {
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
done:
    tray_remove();
    if (hFixed != NULL) {
        DeleteObject(hFixed);
    }
    if (single != NULL) {
        CloseHandle(single);
    }
    return code;
}
