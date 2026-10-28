/*
 * inst_bootp.c - whether the disk Windows starts from sits below a devnode
 * that names a filter (design record 23 section 5.14; roadmap-usb4 task
 * 44.32; owner, 2026-10-25), shared by the installer's engine and the mark
 * service.
 *
 * WHY. A device the system starts from needs every driver of its stack
 * boot start: Windows defers a device whose drivers are not all boot start
 * past the boot drivers' phase. xhcicm is set back to demand start after
 * the setup engine makes it a boot driver, which is safe only where no
 * devnode on the system disk's path names it; where one does (Windows on a
 * tunnelled drive, or a mark the CriticalDeviceDatabase copied onto the
 * system disk's controller), it stays a boot driver and the callers say so.
 *
 * HOW. The volume holding the Windows directory gives its disk number
 * (IOCTL_STORAGE_GET_DEVICE_NUMBER, or IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS
 * where the volume has exactly one extent); the disk interface whose device
 * answers that number gives the devnode; the walk reads each devnode's
 * UpperFilters and LowerFilters up to the root. Every SetupAPI and
 * configuration-manager routine is looked up at run time, so the importing
 * images keep KERNEL32 and ADVAPI32 alone. NT only (Windows 2000 and
 * later); never called on Windows 98 SE or ME. C89.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <stdlib.h>
#include <string.h>
#include "inst_core.h"
#include "inst_bootp.h"

/* winioctl.h's, written out: MSVC 6.0's headers predate the second. */
#define BP_IOCTL_STORAGE_GET_DEVICE_NUMBER      0x002D1080UL
#define BP_IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS 0x00560000UL
#define BP_CM_DRP_UPPERFILTERS                  0x00000012UL
#define BP_CM_DRP_LOWERFILTERS                  0x00000013UL
#define BP_FILTERS_BYTES                        1024
#define BP_DEPTH_MAX                            64

typedef struct _BP_DEVICE_NUMBER {
    DWORD DeviceType;
    DWORD DeviceNumber;
    DWORD PartitionNumber;
} BP_DEVICE_NUMBER;

typedef struct _BP_EXTENTS {
    DWORD NumberOfDiskExtents;
    DWORD Pad;                  /* the 8-byte alignment of the extent */
    DWORD DiskNumber;
    DWORD Pad2;
    LARGE_INTEGER StartingOffset;
    LARGE_INTEGER ExtentLength;
} BP_EXTENTS;

typedef HDEVINFO (WINAPI *PFN_BP_CLASSDEVS)(const GUID *, PCSTR, HWND, DWORD);
typedef BOOL (WINAPI *PFN_BP_ENUMIF)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *,
                                     DWORD, PSP_DEVICE_INTERFACE_DATA);
typedef BOOL (WINAPI *PFN_BP_IFDETAIL)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA,
                                       PSP_DEVICE_INTERFACE_DETAIL_DATA_A,
                                       DWORD, PDWORD, PSP_DEVINFO_DATA);
typedef BOOL (WINAPI *PFN_BP_DESTROY)(HDEVINFO);
typedef DWORD (WINAPI *PFN_BP_PARENT)(DWORD *, DWORD, ULONG);
typedef DWORD (WINAPI *PFN_BP_LOCATE)(DWORD *, char *, ULONG);
typedef DWORD (WINAPI *PFN_BP_PROP)(DWORD, ULONG, ULONG *, void *, ULONG *,
                                    ULONG);

/* GUID_DEVINTERFACE_DISK */
static const GUID bpDiskGuid = {
    0x53f56307, 0xb6bf, 0x11d0,
    { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b }
};

/* The disk number behind a device path: 1 read, 0 not. */
static int bp_number(const char *path, DWORD *disk, int volume)
{
    HANDLE h;
    BP_DEVICE_NUMBER n;
    BP_EXTENTS e;
    DWORD got = 0;
    int ok = 0;

    h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    memset(&n, 0, sizeof(n));
    if (DeviceIoControl(h, BP_IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &n,
                        sizeof(n), &got, NULL) && got >= sizeof(n)) {
        *disk = n.DeviceNumber;
        ok = 1;
    } else if (volume) {
        /* A volume on dynamic disks answers only this; more than one
         * extent fails with ERROR_MORE_DATA, which stays unread. */
        memset(&e, 0, sizeof(e));
        if (DeviceIoControl(h, BP_IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL,
                            0, &e, sizeof(e), &got, NULL) &&
            e.NumberOfDiskExtents == 1) {
            *disk = e.DiskNumber;
            ok = 1;
        }
    }
    CloseHandle(h);
    return ok;
}

/* 1 when the devnode's UpperFilters or LowerFilters names svc, 0 when
 * neither does, -1 when either could not be read (inst_bp_prop_verdict). */
static int bp_names(PFN_BP_PROP prop, DWORD dn, const char *svc)
{
    static const ULONG which[2] = { BP_CM_DRP_UPPERFILTERS,
                                    BP_CM_DRP_LOWERFILTERS };
    char *buf;
    ULONG cap;
    ULONG type;
    ULONG len;
    DWORD cr;
    int i;
    int v;
    int r = 0;

    for (i = 0; i < 2 && r == 0; i++) {
        cap = BP_FILTERS_BYTES;
        for (;;) {
            buf = (char *)malloc(cap + 2);
            if (buf == NULL) {
                return -1;
            }
            memset(buf, 0, cap + 2);
            len = cap;
            type = 0;
            cr = prop(dn, which[i], &type, buf, &len, 0);
            /* A success claiming more than the buffer proves nothing. */
            v = inst_bp_prop_verdict((INST_U32)cr, (INST_U32)type,
                                     len <= cap ? buf : NULL,
                                     len <= cap ? (size_t)len : 0, svc);
            free(buf);
            if (v != INST_BP_GROW || len <= cap || len > 65536UL) {
                break;
            }
            cap = len;
        }
        r = v == INST_BP_GROW ? -1 : v;
    }
    return r;
}

int inst_boot_path_names(const char *svc)
{
    HMODULE sa = LoadLibraryA("setupapi.dll");
    HMODULE cm = LoadLibraryA("cfgmgr32.dll");
    PFN_BP_CLASSDEVS classDevs = NULL;
    PFN_BP_ENUMIF enumIf = NULL;
    PFN_BP_IFDETAIL ifDetail = NULL;
    PFN_BP_DESTROY destroy = NULL;
    PFN_BP_PARENT parent = NULL;
    PFN_BP_LOCATE locate = NULL;
    PFN_BP_PROP prop = NULL;
    HDEVINFO set = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA ifd;
    SP_DEVINFO_DATA dev;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail = NULL;
    char win[MAX_PATH];
    char vol[8];
    DWORD want = 0;
    DWORD have;
    DWORD need;
    DWORD dn = 0;
    DWORD up;
    DWORD root = 0;
    DWORD i;
    int found = 0;
    int depth;
    int v;
    int r = -1;

    if (sa != NULL) {
        classDevs = (PFN_BP_CLASSDEVS)GetProcAddress(sa, "SetupDiGetClassDevsA");
        enumIf = (PFN_BP_ENUMIF)GetProcAddress(sa,
                                               "SetupDiEnumDeviceInterfaces");
        ifDetail = (PFN_BP_IFDETAIL)GetProcAddress(sa,
                       "SetupDiGetDeviceInterfaceDetailA");
        destroy = (PFN_BP_DESTROY)GetProcAddress(sa,
                                                 "SetupDiDestroyDeviceInfoList");
    }
    if (cm != NULL) {
        parent = (PFN_BP_PARENT)GetProcAddress(cm, "CM_Get_Parent");
        locate = (PFN_BP_LOCATE)GetProcAddress(cm, "CM_Locate_DevNodeA");
        prop = (PFN_BP_PROP)GetProcAddress(cm,
                   "CM_Get_DevNode_Registry_PropertyA");
    }
    if (classDevs == NULL || enumIf == NULL || ifDetail == NULL ||
        destroy == NULL || parent == NULL || locate == NULL ||
        prop == NULL) {
        goto cleanup;
    }
    if (GetSystemDirectoryA(win, sizeof(win)) < 2 || win[1] != ':') {
        goto cleanup;
    }
    strcpy(vol, "\\\\.\\C:");
    vol[4] = win[0];
    if (!bp_number(vol, &want, 1)) {
        goto cleanup;
    }
    set = classDevs(&bpDiskGuid, NULL, NULL,
                    DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        goto cleanup;
    }
    for (i = 0; !found && i < 256; i++) {
        memset(&ifd, 0, sizeof(ifd));
        ifd.cbSize = sizeof(ifd);
        if (!enumIf(set, NULL, &bpDiskGuid, i, &ifd)) {
            break;
        }
        need = 0;
        ifDetail(set, &ifd, NULL, 0, &need, NULL);
        if (need < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A) || need > 4096) {
            continue;
        }
        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(need);
        if (detail == NULL) {
            goto cleanup;
        }
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        memset(&dev, 0, sizeof(dev));
        dev.cbSize = sizeof(dev);
        if (ifDetail(set, &ifd, detail, need, NULL, &dev) &&
            bp_number(detail->DevicePath, &have, 0) && have == want) {
            dn = dev.DevInst;
            found = 1;
        }
        free(detail);
        detail = NULL;
    }
    if (!found) {
        goto cleanup;
    }
    /* Clear only once the root itself is read: a parent that cannot be
     * read, or a walk longer than the bound, proves nothing (-1). */
    if (locate(&root, NULL, 0) != 0) {
        goto cleanup;
    }
    for (depth = 0; depth < BP_DEPTH_MAX; depth++) {
        v = bp_names(prop, dn, svc);
        if (v != 0) {
            r = v;
            break;
        }
        if (dn == root) {
            r = 0;
            break;
        }
        if (parent(&up, dn, 0) != 0) {
            break;
        }
        dn = up;
    }

cleanup:
    if (set != INVALID_HANDLE_VALUE) {
        destroy(set);
    }
    if (cm != NULL) {
        FreeLibrary(cm);
    }
    if (sa != NULL) {
        FreeLibrary(sa);
    }
    return r;
}
