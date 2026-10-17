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
#define CMG_T_COUNT         12

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
    CMG_PCI_SVC_WRITE_CONFIG
};

/* Each thunk's VxD: CONFIGMG's, but for PCI.VXD's two. */
static ULONG cmgVxdOf(ULONG which)
{
    return (which == CMG_T_PCI_READ || which == CMG_T_PCI_WRITE)
               ? (ULONG)CMG_PCI_VXD_ID
               : (ULONG)CMG_VXD_ID;
}

typedef ULONG (__cdecl *CMG_FN0)(VOID);
typedef ULONG (__cdecl *CMG_FN2)(ULONG a, ULONG b);
typedef ULONG (__cdecl *CMG_FN3)(ULONG a, ULONG b, ULONG c);
typedef ULONG (__cdecl *CMG_FN4)(ULONG a, ULONG b, ULONG c, ULONG d);

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

#endif /* _X86_ */
