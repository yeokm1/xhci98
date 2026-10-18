/*
 * cm_cfgmg.c - CONFIGMG.VXD's services called from ring 0 through the VMM
 * dynalink (design record 21 section 6.1; roadmap-usb4.md tasks 39-A.5
 * and 39-A.8), for the Windows 98 SE and ME route of cm_reenum.c. The
 * numbers are cm_cfgmg.h's, read from the Windows 98 DDK; this file is the
 * one place that executes them.
 *
 * THE THUNK. A service is reached by executing INT 20h followed by the
 * dword (VxD id << 16) | service. With bit 15 of the service word set the
 * VMM, on the first execution, rewrites those six bytes into a jump to the
 * service, so a cdecl call into the six bytes reaches the service with the
 * caller's arguments and return address, and the service's return comes
 * straight back to the caller. Each service gets six bytes of its own
 * (eight, for alignment) in one non-paged block, since the VMM rewrites
 * them in place: never in the image, which the loader may share or
 * protect. Every service but Get_Version returns a CONFIGRET in EAX;
 * Get_Version returns its WORD in AX, and only AX is read.
 *
 * THE GUARDS (section 6.1), run by CmCfgmgOpen before any other service:
 * Get_Version's high byte must read 4 (98 SE's 4.0B and ME's 4.90 both
 * answer 04xxh), and Locate_DevNode with no id must give a devnode whose
 * id reads back as the root's, "HTREE\ROOT\0". Either failing leaves the
 * route closed for the domain's life, so a wrong number is caught before
 * a service with arguments is called through it.
 *
 * PCI.VXD's two configuration-space services (record 21 section 6.1.1,
 * the bus numbering) are reached the same way, under PCI.VXD's VxD id; their
 * thunks are built with CONFIGMG's and are opened separately, by
 * CmCfgmgPciOpen's guard: a read of a known function through
 * _PCI_Read_Config must equal what HalGetBusData read there, before
 * _PCI_Write_Config is ever called.
 *
 * Only x86 builds carry the thunk: no amd64 target runs CONFIGMG, and the
 * amd64 build's CmCfgmgOpen refuses.
 *
 * IRQL: PASSIVE_LEVEL, on the connection-manager thread, which is a
 * thread of the system virtual machine as CONFIGMG requires
 * (REENUM-EXPERIMENT, "Caveats"). Called on Windows 98 SE and ME only.
 */

#include "cm.h"
#include "cm_cfgmg.h"

/* The services this file builds a thunk for, in CMG_T_* order. */
#define CMG_T_VERSION       0
#define CMG_T_LOCATE        1
#define CMG_T_CHILD         2
#define CMG_T_SIBLING       3
#define CMG_T_DEVICE_ID     4
#define CMG_T_QUERY_REMOVE  5
#define CMG_T_REMOVE        6
#define CMG_T_REENUMERATE   7
#define CMG_T_STATUS        8
#define CMG_T_CHANGE_STATUS 9
#define CMG_T_PCI_READ      10
#define CMG_T_PCI_WRITE     11
/* Design record 28 (task 42-A.15). */
#define CMG_T_ADD_EMPTY_LC  12
#define CMG_T_FIRST_LC      13
#define CMG_T_NEXT_LC       14
#define CMG_T_ADD_RD        15
#define CMG_T_NEXT_RD       16
#define CMG_T_RD_SIZE       17
#define CMG_T_RD_DATA       18
#define CMG_T_READ_REG      19
#define CMG_T_REPROCESS     20
#define CMG_T_LC_PRIORITY   21
#define CMG_T_SETUP         22
#define CMG_T_REG_OPEN      23
#define CMG_T_REG_CLOSE     24
#define CMG_T_REG_QUERY     25
#define CMG_T_COUNT         26

#define CMG_THUNK_BYTES     8

#if defined(_X86_)

static const USHORT cmgServices[CMG_T_COUNT] = {
    CMG_SVC_GET_VERSION,
    CMG_SVC_LOCATE_DEVNODE,
    CMG_SVC_GET_CHILD,
    CMG_SVC_GET_SIBLING,
    CMG_SVC_GET_DEVICE_ID,
    CMG_SVC_QUERY_REMOVE_SUBTREE,
    CMG_SVC_REMOVE_SUBTREE,
    CMG_SVC_REENUMERATE_DEVNODE,
    CMG_SVC_GET_DEVNODE_STATUS,
    CMG_SVC_CHANGE_DEVNODE_STATUS,
    CMG_PCI_SVC_READ_CONFIG,
    CMG_PCI_SVC_WRITE_CONFIG,
    CMG_SVC_ADD_EMPTY_LOG_CONF,
    CMG_SVC_GET_FIRST_LOG_CONF,
    CMG_SVC_GET_NEXT_LOG_CONF,
    CMG_SVC_ADD_RES_DES,
    CMG_SVC_GET_NEXT_RES_DES,
    CMG_SVC_GET_RES_DES_DATA_SIZE,
    CMG_SVC_GET_RES_DES_DATA,
    CMG_SVC_READ_REGISTRY_VALUE,
    CMG_SVC_REPROCESS_DEVNODE,
    CMG_SVC_GET_SET_LOG_CONF_PRIORITY,
    CMG_SVC_SETUP_DEVNODE,
    CMG_VMM_REG_OPEN_KEY,
    CMG_VMM_REG_CLOSE_KEY,
    CMG_VMM_REG_QUERY_VALUE_EX
};

/* Each thunk's VxD: CONFIGMG's, but for PCI.VXD's two and the VMM's
 * three registry services. */
static ULONG cmgVxdOf(ULONG which)
{
    if (which == CMG_T_PCI_READ || which == CMG_T_PCI_WRITE) {
        return (ULONG)CMG_PCI_VXD_ID;
    }
    if (which == CMG_T_REG_OPEN || which == CMG_T_REG_CLOSE ||
        which == CMG_T_REG_QUERY) {
        return (ULONG)CMG_VMM_VXD_ID;
    }
    return (ULONG)CMG_VXD_ID;
}

typedef ULONG (__cdecl *CMG_FN0)(VOID);
typedef ULONG (__cdecl *CMG_FN1)(ULONG a);
typedef ULONG (__cdecl *CMG_FN2)(ULONG a, ULONG b);
typedef ULONG (__cdecl *CMG_FN3)(ULONG a, ULONG b, ULONG c);
typedef ULONG (__cdecl *CMG_FN4)(ULONG a, ULONG b, ULONG c, ULONG d);
typedef ULONG (__cdecl *CMG_FN5)(ULONG a, ULONG b, ULONG c, ULONG d,
                                 ULONG e);
typedef ULONG (__cdecl *CMG_FN6)(ULONG a, ULONG b, ULONG c, ULONG d,
                                 ULONG e, ULONG f);
typedef ULONG (__cdecl *CMG_FN7)(ULONG a, ULONG b, ULONG c, ULONG d,
                                 ULONG e, ULONG f, ULONG g);

static PVOID cmgThunk(PCM_CFGMG cfg, ULONG which)
{
    return (PVOID)(cfg->Thunks + which * CMG_THUNK_BYTES);
}

/* The root's id as Get_Device_ID returns it. */
static const char cmgRootId[] = "HTREE\\ROOT\\0";

BOOLEAN CmCfgmgOpen(PCM_CFGMG cfg)
{
    PUCHAR t;
    ULONG i;
    ULONG version;
    ULONG root;
    ULONG cr;
    char id[CMG_MAX_DEVICE_ID_LEN + 1];

    cfg->Open = 0;
    cfg->PciOpen = 0;
    cfg->Why = CM_CFGMG_WHY_NONE;
    cfg->Thunks = (PUCHAR)CmAlloc(CMG_T_COUNT * CMG_THUNK_BYTES);
    if (cfg->Thunks == NULL) {
        cfg->Why = CM_CFGMG_WHY_THUNK;
        return FALSE;
    }
    for (i = 0; i < CMG_T_COUNT; i++) {
        t = cfg->Thunks + i * CMG_THUNK_BYTES;
        t[0] = 0xCD;                                    /* INT            */
        t[1] = CMG_DYNALINK_INT;                        /* 20h            */
        t[2] = (UCHAR)(cmgServices[i] & 0xFF);
        t[3] = (UCHAR)(((cmgServices[i] | CMG_DYNALINK_JMP) >> 8) & 0xFF);
        t[4] = (UCHAR)(cmgVxdOf(i) & 0xFF);
        t[5] = (UCHAR)((cmgVxdOf(i) >> 8) & 0xFF);
        t[6] = 0xCC;                                    /* never reached  */
        t[7] = 0xCC;
    }
    version = ((CMG_FN0)cmgThunk(cfg, CMG_T_VERSION))() & 0xFFFFUL;
    cfg->Version = version;
    if (((version >> 8) & 0xFFUL) != CMG_VERSION_MAJOR) {
        cfg->Why = CM_CFGMG_WHY_VERSION;
        CmCfgmgClose(cfg);
        return FALSE;
    }
    root = 0;
    cr = ((CMG_FN3)cmgThunk(cfg, CMG_T_LOCATE))((ULONG)(ULONG_PTR)&root, 0,
                                                0);
    if (cr != CMG_CR_SUCCESS || root == 0) {
        cfg->Why = CM_CFGMG_WHY_ROOT;
        CmCfgmgClose(cfg);
        return FALSE;
    }
    cfg->Open = 1;
    cr = CmCfgmgDeviceId(cfg, root, id, sizeof(id));
    for (i = 0; cr == CMG_CR_SUCCESS && i < sizeof(cmgRootId); i++) {
        if (id[i] != cmgRootId[i]) {
            cr = CMG_CR_FAILURE;
        }
    }
    if (cr != CMG_CR_SUCCESS) {
        cfg->Why = CM_CFGMG_WHY_ROOT;
        CmCfgmgClose(cfg);
        return FALSE;
    }
    cfg->Root = root;
    return TRUE;
}

VOID CmCfgmgClose(PCM_CFGMG cfg)
{
    cfg->Open = 0;
    cfg->PciOpen = 0;
    if (cfg->Thunks != NULL) {
        CmFree(cfg->Thunks);
        cfg->Thunks = NULL;
    }
}

ULONG CmCfgmgChild(PCM_CFGMG cfg, ULONG devnode, PULONG child)
{
    *child = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN3)cmgThunk(cfg, CMG_T_CHILD))((ULONG)(ULONG_PTR)child,
                                                 devnode, 0);
}

ULONG CmCfgmgSibling(PCM_CFGMG cfg, ULONG devnode, PULONG sibling)
{
    *sibling = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN3)cmgThunk(cfg, CMG_T_SIBLING))((ULONG)(ULONG_PTR)sibling,
                                                   devnode, 0);
}

ULONG CmCfgmgDeviceId(PCM_CFGMG cfg, ULONG devnode, char *buffer,
                      ULONG bytes)
{
    ULONG cr;

    if (bytes == 0) {
        return CMG_CR_BUFFER_SMALL;
    }
    buffer[0] = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    /* One byte kept back, so the id always ends in a NUL here. */
    cr = ((CMG_FN4)cmgThunk(cfg, CMG_T_DEVICE_ID))(devnode,
                                                   (ULONG)(ULONG_PTR)buffer,
                                                   bytes - 1, 0);
    buffer[bytes - 1] = 0;
    return cr;
}

ULONG CmCfgmgQueryRemove(PCM_CFGMG cfg, ULONG devnode)
{
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN2)cmgThunk(cfg, CMG_T_QUERY_REMOVE))(
        devnode, CMG_QUERY_REMOVE_UI_NOT_OK);
}

ULONG CmCfgmgRemove(PCM_CFGMG cfg, ULONG devnode)
{
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN2)cmgThunk(cfg, CMG_T_REMOVE))(devnode,
                                                  CMG_REMOVE_UI_NOT_OK);
}

ULONG CmCfgmgReenumerate(PCM_CFGMG cfg, ULONG devnode)
{
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN2)cmgThunk(cfg, CMG_T_REENUMERATE))(
        devnode, CMG_REENUMERATE_NORMAL);
}

ULONG CmCfgmgStatus(PCM_CFGMG cfg, ULONG devnode, PULONG status,
                    PULONG problem)
{
    *status = 0;
    *problem = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN4)cmgThunk(cfg, CMG_T_STATUS))((ULONG)(ULONG_PTR)status,
                                                  (ULONG)(ULONG_PTR)problem,
                                                  devnode, 0);
}

ULONG CmCfgmgAddRemovable(PCM_CFGMG cfg, ULONG devnode)
{
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN3)cmgThunk(cfg, CMG_T_CHANGE_STATUS))(
        devnode, CMG_DN_REMOVABLE, CMG_CHANGE_STATUS_ADD_FLAGS);
}

/* PCI.VXD's (bus, devfunc) for a packed routing id. */
#define CMG_PCI_BUS(bdf)     (((bdf) >> 8) & 0xFFUL)
#define CMG_PCI_DEVFN(bdf)   ((bdf) & 0xFFUL)

BOOLEAN CmCfgmgPciOpen(PCM_CFGMG cfg, ULONG bdf, ULONG halId)
{
    ULONG id;

    if (cfg->PciOpen) {
        return TRUE;
    }
    if (!cfg->Open || halId == 0xFFFFFFFFUL || (halId & 0xFFFFUL) == 0) {
        return FALSE;
    }
    id = ((CMG_FN3)cmgThunk(cfg, CMG_T_PCI_READ))(CMG_PCI_BUS(bdf),
                                                  CMG_PCI_DEVFN(bdf), 0);
    cfg->PciGuard = id;
    if (id != halId) {
        return FALSE;
    }
    cfg->PciOpen = 1;
    return TRUE;
}

BOOLEAN CmCfgmgPciRead(PCM_CFGMG cfg, ULONG bdf, ULONG offset, PULONG value)
{
    if (!cfg->PciOpen || (offset & 3UL) != 0 || offset > 252UL) {
        return FALSE;
    }
    *value = ((CMG_FN3)cmgThunk(cfg, CMG_T_PCI_READ))(CMG_PCI_BUS(bdf),
                                                      CMG_PCI_DEVFN(bdf),
                                                      offset);
    return TRUE;
}

BOOLEAN CmCfgmgPciWrite(PCM_CFGMG cfg, ULONG bdf, ULONG offset, ULONG value)
{
    if (!cfg->PciOpen || (offset & 3UL) != 0 || offset > 252UL) {
        return FALSE;
    }
    (VOID)((CMG_FN4)cmgThunk(cfg, CMG_T_PCI_WRITE))(CMG_PCI_BUS(bdf),
                                                    CMG_PCI_DEVFN(bdf),
                                                    offset, value);
    return TRUE;
}

/* ---- design record 28: logical configurations (task 42-A.15) ---------- */

ULONG CmCfgmgAddEmptyLogConf(PCM_CFGMG cfg, ULONG devnode, ULONG priority,
                             ULONG flags, PULONG lc)
{
    *lc = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN4)cmgThunk(cfg, CMG_T_ADD_EMPTY_LC))(
        (ULONG)(ULONG_PTR)lc, devnode, priority, flags);
}

ULONG CmCfgmgFirstLogConf(PCM_CFGMG cfg, ULONG devnode, ULONG kind,
                          PULONG lc)
{
    *lc = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN3)cmgThunk(cfg, CMG_T_FIRST_LC))((ULONG)(ULONG_PTR)lc,
                                                    devnode, kind);
}

ULONG CmCfgmgNextLogConf(PCM_CFGMG cfg, ULONG lc, PULONG next)
{
    *next = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN3)cmgThunk(cfg, CMG_T_NEXT_LC))((ULONG)(ULONG_PTR)next,
                                                   lc, 0);
}

ULONG CmCfgmgLogConfPriority(PCM_CFGMG cfg, ULONG lc, PULONG priority)
{
    *priority = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN3)cmgThunk(cfg, CMG_T_LC_PRIORITY))(
        (ULONG)(ULONG_PTR)priority, lc, CMG_GET_LOG_CONF_PRIORITY);
}

ULONG CmCfgmgSetLogConfPriority(PCM_CFGMG cfg, ULONG lc, ULONG priority)
{
    ULONG p;

    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    p = priority;
    return ((CMG_FN3)cmgThunk(cfg, CMG_T_LC_PRIORITY))(
        (ULONG)(ULONG_PTR)&p, lc, CMG_SET_LOG_CONF_PRIORITY);
}

ULONG CmCfgmgNextResDes(PCM_CFGMG cfg, ULONG from, PULONG rd, PULONG type)
{
    *rd = 0;
    *type = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN5)cmgThunk(cfg, CMG_T_NEXT_RD))(
        (ULONG)(ULONG_PTR)rd, from, CMG_RESTYPE_ALL,
        (ULONG)(ULONG_PTR)type, 0);
}

ULONG CmCfgmgResDesSize(PCM_CFGMG cfg, ULONG rd, PULONG size)
{
    *size = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN3)cmgThunk(cfg, CMG_T_RD_SIZE))((ULONG)(ULONG_PTR)size,
                                                   rd, 0);
}

ULONG CmCfgmgResDesData(PCM_CFGMG cfg, ULONG rd, PVOID buffer, ULONG bytes)
{
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN4)cmgThunk(cfg, CMG_T_RD_DATA))(
        rd, (ULONG)(ULONG_PTR)buffer, bytes, 0);
}

ULONG CmCfgmgAddResDes(PCM_CFGMG cfg, ULONG lc, ULONG type, PVOID data,
                       ULONG bytes, PULONG rd)
{
    *rd = 0;
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN6)cmgThunk(cfg, CMG_T_ADD_RD))(
        (ULONG)(ULONG_PTR)rd, lc, type, (ULONG)(ULONG_PTR)data, bytes, 0);
}

ULONG CmCfgmgReadSoftwareValue(PCM_CFGMG cfg, ULONG devnode,
                               const char *name, char *buffer, PULONG bytes)
{
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN7)cmgThunk(cfg, CMG_T_READ_REG))(
        devnode, 0, (ULONG)(ULONG_PTR)name, CMG_REG_SZ,
        (ULONG)(ULONG_PTR)buffer, (ULONG)(ULONG_PTR)bytes,
        CMG_REGISTRY_SOFTWARE);
}

ULONG CmCfgmgReprocess(PCM_CFGMG cfg, ULONG devnode)
{
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN2)cmgThunk(cfg, CMG_T_REPROCESS))(
        devnode, CMG_REPROCESS_ASYNCHRONOUS);
}

ULONG CmCfgmgSetupReady(PCM_CFGMG cfg, ULONG devnode)
{
    if (!cfg->Open) {
        return CMG_CR_FAILURE;
    }
    return ((CMG_FN2)cmgThunk(cfg, CMG_T_SETUP))(devnode,
                                                 CMG_SETUP_DEVNODE_READY);
}

/* The installer's Stor98 record: HKLM\Software\xhci98\Stor98's Version
 * (record 26 section 6.3). The VMM's error codes are Win32's (0 success,
 * 2 not found). */
static char cmgStorKey[] = "Software\\xhci98\\Stor98";
static char cmgStorValue[] = "Version";

ULONG CmCfgmgStor98Version(PCM_CFGMG cfg, char *buffer, PULONG bytes,
                           PULONG type)
{
    ULONG hkey;
    ULONG error;

    *type = 0;
    if (!cfg->Open) {
        return 0xFFFFFFFFUL;
    }
    hkey = 0;
    error = ((CMG_FN3)cmgThunk(cfg, CMG_T_REG_OPEN))(
        CMG_HKEY_LOCAL_MACHINE, (ULONG)(ULONG_PTR)cmgStorKey,
        (ULONG)(ULONG_PTR)&hkey);
    if (error != 0) {
        return error;
    }
    error = ((CMG_FN6)cmgThunk(cfg, CMG_T_REG_QUERY))(
        hkey, (ULONG)(ULONG_PTR)cmgStorValue, 0, (ULONG)(ULONG_PTR)type,
        (ULONG)(ULONG_PTR)buffer, (ULONG)(ULONG_PTR)bytes);
    (VOID)((CMG_FN1)cmgThunk(cfg, CMG_T_REG_CLOSE))(hkey);
    return error;
}

#else /* !_X86_ */

/* No amd64 target runs CONFIGMG: the route is refused, and every call
 * fails without executing anything. */
BOOLEAN CmCfgmgOpen(PCM_CFGMG cfg)
{
    cfg->Open = 0;
    cfg->PciOpen = 0;
    cfg->Thunks = NULL;
    cfg->Why = CM_CFGMG_WHY_ARCH;
    return FALSE;
}

VOID CmCfgmgClose(PCM_CFGMG cfg)
{
    cfg->Open = 0;
}

ULONG CmCfgmgChild(PCM_CFGMG cfg, ULONG devnode, PULONG child)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    *child = 0;
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgSibling(PCM_CFGMG cfg, ULONG devnode, PULONG sibling)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    *sibling = 0;
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgDeviceId(PCM_CFGMG cfg, ULONG devnode, char *buffer,
                      ULONG bytes)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    if (bytes != 0) {
        buffer[0] = 0;
    }
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgQueryRemove(PCM_CFGMG cfg, ULONG devnode)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgRemove(PCM_CFGMG cfg, ULONG devnode)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgReenumerate(PCM_CFGMG cfg, ULONG devnode)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgStatus(PCM_CFGMG cfg, ULONG devnode, PULONG status,
                    PULONG problem)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    *status = 0;
    *problem = 0;
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgAddRemovable(PCM_CFGMG cfg, ULONG devnode)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    return CMG_CR_FAILURE;
}

BOOLEAN CmCfgmgPciOpen(PCM_CFGMG cfg, ULONG bdf, ULONG halId)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(bdf);
    UNREFERENCED_PARAMETER(halId);
    return FALSE;
}

BOOLEAN CmCfgmgPciRead(PCM_CFGMG cfg, ULONG bdf, ULONG offset, PULONG value)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(bdf);
    UNREFERENCED_PARAMETER(offset);
    *value = 0xFFFFFFFFUL;
    return FALSE;
}

BOOLEAN CmCfgmgPciWrite(PCM_CFGMG cfg, ULONG bdf, ULONG offset, ULONG value)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(bdf);
    UNREFERENCED_PARAMETER(offset);
    UNREFERENCED_PARAMETER(value);
    return FALSE;
}

ULONG CmCfgmgAddEmptyLogConf(PCM_CFGMG cfg, ULONG devnode, ULONG priority,
                             ULONG flags, PULONG lc)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    UNREFERENCED_PARAMETER(priority);
    UNREFERENCED_PARAMETER(flags);
    *lc = 0;
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgFirstLogConf(PCM_CFGMG cfg, ULONG devnode, ULONG kind,
                          PULONG lc)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    UNREFERENCED_PARAMETER(kind);
    *lc = 0;
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgNextLogConf(PCM_CFGMG cfg, ULONG lc, PULONG next)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(lc);
    *next = 0;
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgLogConfPriority(PCM_CFGMG cfg, ULONG lc, PULONG priority)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(lc);
    *priority = 0;
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgSetLogConfPriority(PCM_CFGMG cfg, ULONG lc, ULONG priority)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(lc);
    UNREFERENCED_PARAMETER(priority);
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgNextResDes(PCM_CFGMG cfg, ULONG from, PULONG rd, PULONG type)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(from);
    *rd = 0;
    *type = 0;
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgResDesSize(PCM_CFGMG cfg, ULONG rd, PULONG size)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(rd);
    *size = 0;
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgResDesData(PCM_CFGMG cfg, ULONG rd, PVOID buffer, ULONG bytes)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(rd);
    UNREFERENCED_PARAMETER(buffer);
    UNREFERENCED_PARAMETER(bytes);
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgAddResDes(PCM_CFGMG cfg, ULONG lc, ULONG type, PVOID data,
                       ULONG bytes, PULONG rd)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(lc);
    UNREFERENCED_PARAMETER(type);
    UNREFERENCED_PARAMETER(data);
    UNREFERENCED_PARAMETER(bytes);
    *rd = 0;
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgReadSoftwareValue(PCM_CFGMG cfg, ULONG devnode,
                               const char *name, char *buffer, PULONG bytes)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    UNREFERENCED_PARAMETER(name);
    UNREFERENCED_PARAMETER(buffer);
    UNREFERENCED_PARAMETER(bytes);
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgReprocess(PCM_CFGMG cfg, ULONG devnode)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgSetupReady(PCM_CFGMG cfg, ULONG devnode)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(devnode);
    return CMG_CR_FAILURE;
}

ULONG CmCfgmgStor98Version(PCM_CFGMG cfg, char *buffer, PULONG bytes,
                           PULONG type)
{
    UNREFERENCED_PARAMETER(cfg);
    UNREFERENCED_PARAMETER(buffer);
    UNREFERENCED_PARAMETER(bytes);
    *type = 0;
    return 0xFFFFFFFFUL;
}

#endif /* _X86_ */
