/*
 * di_sys.c - the setup engine's calls of XHCIDI.EXE (roadmap-usb4.md task
 * 42-A.1, design record 27 sections 5 and 9), and nothing else. Read, not
 * unit-tested: its proof is the 42-V.1 legs (record 27 section 12).
 *
 * Every routine, structure, field and flag used here is one setupx.h (the
 * Windows 98 SE DDK's inc16\setupx.h) declares, each flag and field cited
 * by its documentation block (record 27 section 2). The way a devnode is
 * taken out of the engine's list and rebuilt with DiCreateDeviceInfo is the
 * DDK's network sample's (src\net\ipnew\ipmodem.cpp); the registry read is
 * that of src\net\ipnew\ipoption.cpp (SURegOpenKey and SURegQueryValueEx
 * on HKEY_LOCAL_MACHINE, no SURegInit). Every action goes through
 * DiCallClassInstaller, with DI_NODI_DEFAULTACTION left clear so the
 * engine's default (DiSelectBestCompatDrv, DiInstallDevice,
 * DiRemoveDevice) runs when the class installer answers ERR_DI_DO_DEFAULT
 * or there is none. Never DI_NOFILECOPY, DI_FORCECOPY or
 * DI_DONOTCALLCONFIGMG (record 27 decision H4); never a registry write.
 *
 * Runs once per start, on the one thread a Win16 task has. The engine's
 * dialogs are its own, owned by the hwnd the installer passed.
 */

#include <windows.h>
#include <setupx.h>
#include "di_sys.h"

#define DI_ENUM_MAX 32

/*
 * The SUReg routines return the kernel's registry codes ("use just like
 * those in kernel", setupx.h), 0 for success: ERROR_SUCCESS, which the
 * 98 windows.h defines only below WINVER 0x0400 and winerror.h otherwise.
 */
#define DI_REG_OK 0L

/*
 * One line to the result file, as soon as it is formatted (record 27
 * section 4.2: written in order, the END line last). _lwrite goes straight
 * to MS-DOS's write, which the installer's own handle to the same file sees
 * at once on these systems; a short write marks the run bad, and the exit
 * code says so.
 */
void di_emit(DI_RUN *run, const char *line)
{
    UINT len = (UINT)lstrlen(line);
    if (run->file == HFILE_ERROR) {
        run->bad = 1;
        return;
    }
    if (_lwrite(run->file, line, len) != len) {
        run->bad = 1;
    }
}

static void di_call(DI_RUN *run, const char *routine, const char *arg,
                    RETERR err)
{
    di_emit(run, di_out_call(&run->out, routine, arg, (unsigned)err));
}

/*
 * The end of a job at a call that did not return OK: ERR_DI_USER_CANCEL,
 * from whichever call a Windows dialog was closed in, is cancelled
 * (section 5.4); anything else failed, with an ERROR line first.
 */
static void di_fail(DI_RUN *run, RETERR err)
{
    if (err == ERR_DI_USER_CANCEL) {
        di_emit(run, di_out_result(&run->out, DI_V_CANCELLED, (unsigned)err));
        return;
    }
    di_emit(run, di_out_error(&run->out, di_err_name((unsigned)err),
                              (unsigned)err));
    di_emit(run, di_out_result(&run->out, DI_V_FAILED, (unsigned)err));
}

static void di_fail_self(DI_RUN *run, const char *name)
{
    di_emit(run, di_out_error(&run->out, name, 0));
    di_emit(run, di_out_result_text(&run->out, DI_V_FAILED, name));
}

/*
 * The devnode's entry in the engine's list of an enumerator's devices
 * (record 27 section 5.1, DIGCF_PRESENT | DIGCF_ALLCLASSES for an install;
 * section 9, DIGCF_ALLCLASSES alone for a removal, so an entry that is not
 * present is found by its registry entry): the one whose szRegSubkey is
 * Enum\<device id>, compared without case.
 */
static LPDEVICE_INFO di_find(LPDEVICE_INFO list, const char *devid)
{
    LPDEVICE_INFO e;
    for (e = list; e != NULL; e = e->lpNextDi) {
        if (di_subkey_matches(e->szRegSubkey, devid)) {
            return e;
        }
    }
    return NULL;
}

static RETERR di_list(DI_RUN *run, const DI_CMD *cmd, HWND hwnd,
                      int present, LPLPDEVICE_INFO list)
{
    char enumerator[DI_ENUM_MAX];
    RETERR err;
    *list = NULL;
    if (!di_enumerator(cmd->devid, enumerator, sizeof enumerator)) {
        return ERR_DI_INVALID_DEVICE_ID;
    }
    err = DiGetClassDevsEx(list, NULL, enumerator, hwnd,
                           present ? (DIGCF_PRESENT | DIGCF_ALLCLASSES)
                                   : DIGCF_ALLCLASSES);
    di_call(run, "DiGetClassDevsEx",
            present ? "DIGCF_PRESENT|DIGCF_ALLCLASSES" : "DIGCF_ALLCLASSES",
            err);
    return err;
}

/*
 * The ids the devnode's Enum key carries, for a no-match (record 27 section
 * 5.2: the disagreement with the installer's own match is reported, with
 * the ids the helper read). Read, never written.
 */
static void di_ids(DI_RUN *run, LPDEVICE_INFO lpdi)
{
    static const char *const names[2] = { "HardwareID", "CompatibleIDs" };
    HKEY hk;
    int i;
    if (SURegOpenKey(lpdi->hRegKey, lpdi->szRegSubkey, &hk) != DI_REG_OK) {
        di_emit(run, di_out_ids(&run->out, "unreadable", ""));
        return;
    }
    for (i = 0; i < 2; i++) {
        static char value[512];
        DWORD size = sizeof value - 1;
        value[0] = '\0';
        if (SURegQueryValueEx(hk, names[i], NULL, NULL, value, &size) !=
            DI_REG_OK) {
            value[0] = '\0';
        }
        value[sizeof value - 1] = '\0';
        di_emit(run, di_out_ids(&run->out, names[i], value));
    }
    SURegCloseKey(hk);
}

/*
 * Record 27 section 5: the devnode found among the present devices, its
 * DEVICE_INFO rebuilt with the INF's class, the compatible list built from
 * this INF alone, the choice left to the class installer, the copy source
 * pointed at the INF's directory for the install and put back after.
 */
void di_sys_install(DI_RUN *run, const DI_CMD *cmd, HWND hwnd)
{
    static char cls[MAX_CLASS_NAME_LEN];
    static char infname[DI_PATH_MAX + 1];
    static char dir[MAX_PATH_LEN];
    static char oldpath[MAX_PATH_LEN];
    static char mfg[DI_PATH_MAX + 1];
    static char provider[DI_PATH_MAX + 1];
    LPDEVICE_INFO list = NULL;
    LPDEVICE_INFO lpdi = NULL;
    LPDEVICE_INFO e;
    LPDRIVER_NODE dn;
    ATOM atom = 0;
    int ldd_saved = 0;
    unsigned long flags;
    RETERR err;

    err = di_list(run, cmd, hwnd, 1, &list);
    if (err != OK) {
        di_fail(run, err);
        goto cleanup;
    }
    e = di_find(list, cmd->devid);
    di_emit(run, di_out_found(&run->out, e != NULL));
    if (e == NULL) {
        di_emit(run, di_out_result(&run->out, DI_V_NOT_PRESENT, 0));
        goto cleanup;
    }

    /*
     * The INF's class, not the devnode's (section 5.1): a devnode with no
     * driver yet has its enumerator's class, often none or Unknown, and the
     * network sample passes the class of what it installs (ipndi.cpp).
     */
    cls[0] = '\0';
    err = DiGetINFClass((LPSTR)cmd->inf, INFTYPE_TEXT, cls, sizeof cls);
    di_call(run, "DiGetINFClass", cls[0] != '\0' ? cls : "-", err);
    if (err != OK) {
        di_fail(run, err);
        goto cleanup;
    }
    err = DiCreateDeviceInfo(&lpdi, e->szDescription, e->dnDevnode,
                             e->hRegKey, e->szRegSubkey, cls, hwnd);
    di_call(run, "DiCreateDeviceInfo", NULL, err);
    if (err != OK) {
        lpdi = NULL;
        di_fail(run, err);
        goto cleanup;
    }
    DiDestroyDeviceInfoList(list);
    list = NULL;

    /*
     * DI_ENUMSINGLEINF: "Set if DiBuildCompatDrvList and DiBuildClassDrvList
     * should only search the INF file specified by atDriverPath" (the
     * DEVICE_INFO block), so no other INF's driver is chosen.
     * DI_QUIETINSTALL with the quiet flag: "Set if device install API should
     * be as silent as possible using default choices wherever possible".
     */
    atom = GlobalAddAtom(cmd->inf);
    if (atom == 0) {
        di_fail_self(run, DI_SELF_NOATOM);
        goto cleanup;
    }
    lpdi->atDriverPath = atom;
    lpdi->hwndParent = hwnd;
    lpdi->Flags |= DI_ENUMSINGLEINF;
    if (cmd->flags & DI_FLAG_QUIET) {
        lpdi->Flags |= DI_QUIETINSTALL;
    }

    err = DiBuildCompatDrvList(lpdi);
    di_call(run, "DiBuildCompatDrvList", NULL, err);
    if (err != OK) {
        di_fail(run, err);
        goto cleanup;
    }
    if (lpdi->lpCompatDrvList == NULL) {
        di_ids(run, lpdi);
        di_emit(run, di_out_result(&run->out, DI_V_NO_MATCH, 0));
        goto cleanup;
    }

    err = DiCallClassInstaller(DIF_SELECTBESTCOMPATDRV, lpdi);
    di_call(run, "DiCallClassInstaller", "DIF_SELECTBESTCOMPATDRV", err);
    /*
     * On 98 SE the dispatcher answers ERR_DI_DO_DEFAULT for this function
     * with DI_NODI_DEFAULTACTION clear, and selects nothing (the smoke run
     * of 2026-10-18, record 27 section 5.2): it performs no default of its
     * own for DIF_SELECTBESTCOMPATDRV. The helper then takes the node of
     * the best rank (the lowest number; "Ranks go from 0 to n, where 0 is
     * the most compatible", the DRIVER_NODE block), the first of equals, as
     * the DDK's network sample selects from lpCompatDrvList itself.
     */
    if (err == ERR_DI_DO_DEFAULT) {
        LPDRIVER_NODE best = NULL;
        LPDRIVER_NODE node;
        for (node = lpdi->lpCompatDrvList; node != NULL; node = node->lpNextDN) {
            if (best == NULL || node->Rank < best->Rank) {
                best = node;
            }
        }
        lpdi->lpSelectedDriver = best;
        di_emit(run, di_out_default(&run->out, "DIF_SELECTBESTCOMPATDRV"));
        err = OK;
    }
    if (err != OK) {
        di_fail(run, err);
        goto cleanup;
    }
    dn = lpdi->lpSelectedDriver;
    if (dn == NULL) {
        di_fail_self(run, DI_SELF_NOSELECT);
        goto cleanup;
    }
    infname[0] = '\0';
    GlobalGetAtomName(dn->atInfFileName, infname, sizeof infname);
    di_emit(run, di_out_driver(&run->out, infname,
                               dn->lpszSectionName != NULL ? dn->lpszSectionName : "",
                               dn->lpszDescription != NULL ? dn->lpszDescription : "",
                               dn->Rank, dn->DriverDate));
    /*
     * The node's own flags, DNF_OLDDRIVER and DNF_CURRENT_DRIVER among them
     * (the DRIVER_NODE block). The helper installs the chosen node whatever
     * they say: an install, an update or, with force, a reinstall is the
     * installer's decision (record 23 section 5.4), and whether the engine
     * itself declines a same-version install (ERR_DI_DONT_INSTALL) is the
     * legs' reading (record 27 section 5.2).
     */
    di_emit(run, di_out_node(&run->out, dn->Flags,
                             (cmd->flags & DI_FLAG_FORCE) != 0));
    if (!di_same_inf(infname, cmd->inf)) {
        di_fail_self(run, DI_SELF_OTHERINF);
        goto cleanup;
    }

    /*
     * The job's SECTION line (record 27 section 4.1; record 26 section
     * 6.7.12): the same driver installed from another section of the same
     * INF, one no model names, so no compatible list holds a node for it.
     * DiCreateDriverNode makes one (setupx.h) from the chosen node's own
     * rank, type, date, descriptions, provider, manufacturer and INF, with
     * the section the job names; its flags, driver date and private data
     * are the chosen node's too. The new node is the selected driver and is
     * on no list, so the list's destruction never frees it; the helper ends
     * after this one job, and the node is the engine's memory for that long.
     */
    if (cmd->section[0] != '\0' &&
        (dn->lpszSectionName == NULL ||
         !di_ieq(dn->lpszSectionName, cmd->section))) {
        LPDRIVER_NODE nn = NULL;
        mfg[0] = '\0';
        provider[0] = '\0';
        if (dn->atMfgName != 0) {
            GlobalGetAtomName(dn->atMfgName, mfg, sizeof mfg);
        }
        if (dn->atProviderName != 0) {
            GlobalGetAtomName(dn->atProviderName, provider, sizeof provider);
        }
        err = DiCreateDriverNode(&nn, dn->Rank, dn->InfType, dn->InfDate,
                                 dn->lpszDescription,
                                 dn->lpszDrvDescription != NULL ?
                                     dn->lpszDrvDescription :
                                     dn->lpszDescription,
                                 provider, mfg, infname, cmd->section,
                                 dn->dwPrivateData);
        di_call(run, "DiCreateDriverNode", cmd->section, err);
        if (err != OK) {
            di_fail(run, err);
            goto cleanup;
        }
        if (nn == NULL) {
            di_fail_self(run, DI_SELF_NOSECTION);
            goto cleanup;
        }
        nn->Flags = dn->Flags;
        nn->DriverDate = dn->DriverDate;
        lpdi->lpSelectedDriver = nn;
        dn = nn;
        infname[0] = '\0';
        GlobalGetAtomName(dn->atInfFileName, infname, sizeof infname);
        di_emit(run, di_out_driver(&run->out, infname,
                                   dn->lpszSectionName != NULL ? dn->lpszSectionName : "",
                                   dn->lpszDescription != NULL ? dn->lpszDescription : "",
                                   dn->Rank, dn->DriverDate));
        if (!di_same_inf(infname, cmd->inf)) {
            di_fail_self(run, DI_SELF_OTHERINF);
            goto cleanup;
        }
    }

    /*
     * LDID_SRCPATH, the "source of installation" (setupx.h's logical disk
     * ids), set to the INF's directory for the install and restored after
     * (section 5.3); a path that cannot be saved is not changed.
     */
    if (di_inf_dir(cmd->inf, dir, sizeof dir)) {
        oldpath[0] = '\0';
        err = CtlGetLddPath(LDID_SRCPATH, oldpath);
        di_call(run, "CtlGetLddPath", oldpath[0] != '\0' ? oldpath : "-", err);
        if (err == OK) {
            err = CtlSetLddPath(LDID_SRCPATH, dir);
            di_call(run, "CtlSetLddPath", dir, err);
            ldd_saved = 1;
        }
    }

    err = DiCallClassInstaller(DIF_INSTALLDEVICE, lpdi);
    di_call(run, "DiCallClassInstaller", "DIF_INSTALLDEVICE", err);
    flags = (unsigned long)lpdi->Flags;
    di_emit(run, di_out_flags(&run->out, flags));
    if (ldd_saved) {
        RETERR back = CtlSetLddPath(LDID_SRCPATH, oldpath);
        di_call(run, "CtlSetLddPath", oldpath[0] != '\0' ? oldpath : "-", back);
    }
    if (err != OK && err != ERR_DI_USER_CANCEL) {
        di_fail(run, err);
    } else {
        di_emit(run, di_out_result(&run->out,
                                   di_verdict(DI_JOB_INSTALL, (unsigned)err, flags),
                                   (unsigned)err));
    }

cleanup:
    if (lpdi != NULL) {
        DiDestroyDeviceInfoList(lpdi);
    }
    if (list != NULL) {
        DiDestroyDeviceInfoList(list);
    }
    if (atom != 0) {
        GlobalDeleteAtom(atom);
    }
}

/*
 * Record 27 section 9: the entry found among every entry of the enumerator,
 * present or not, rebuilt with its own class (there is no INF), and removed
 * globally through the class installer, the engine's DiRemoveDevice the
 * default. REMOVEDEVICE_PARAMS is attached through lpClassInstallParams
 * with DI_CLASSINSTALLPARAMS ("Set if the lpClassInstallParams field points
 * to a class install parameter block") and detached again before the
 * DEVICE_INFO is destroyed, since it lives on this stack.
 */
void di_sys_remove(DI_RUN *run, const DI_CMD *cmd, HWND hwnd)
{
    REMOVEDEVICE_PARAMS rdp;
    LPDEVICE_INFO list = NULL;
    LPDEVICE_INFO lpdi = NULL;
    LPDEVICE_INFO e;
    unsigned long flags;
    RETERR err;

    err = di_list(run, cmd, hwnd, 0, &list);
    if (err != OK) {
        di_fail(run, err);
        goto cleanup;
    }
    e = di_find(list, cmd->devid);
    di_emit(run, di_out_found(&run->out, e != NULL));
    if (e == NULL) {
        di_emit(run, di_out_result(&run->out, DI_V_NOT_FOUND, 0));
        goto cleanup;
    }
    err = DiCreateDeviceInfo(&lpdi, e->szDescription, e->dnDevnode,
                             e->hRegKey, e->szRegSubkey,
                             e->szClassName[0] != '\0' ? e->szClassName : NULL,
                             hwnd);
    di_call(run, "DiCreateDeviceInfo",
            e->szClassName[0] != '\0' ? e->szClassName : "-", err);
    if (err != OK) {
        lpdi = NULL;
        di_fail(run, err);
        goto cleanup;
    }
    DiDestroyDeviceInfoList(list);
    list = NULL;

    rdp.cbSize = sizeof rdp;
    rdp.dwFlags = DI_REMOVEDEVICE_GLOBAL;
    rdp.dwConfigID = 0;
    lpdi->hwndParent = hwnd;
    lpdi->lpClassInstallParams = (LPARAM)(LPREMOVEDEVICE_PARAMS)&rdp;
    lpdi->Flags |= DI_CLASSINSTALLPARAMS;

    err = DiCallClassInstaller(DIF_REMOVE, lpdi);
    di_call(run, "DiCallClassInstaller", "DIF_REMOVE", err);
    flags = (unsigned long)lpdi->Flags;
    di_emit(run, di_out_flags(&run->out, flags));
    lpdi->Flags &= ~DI_CLASSINSTALLPARAMS;
    lpdi->lpClassInstallParams = 0;
    if (err != OK && err != ERR_DI_USER_CANCEL) {
        di_fail(run, err);
    } else {
        di_emit(run, di_out_result(&run->out,
                                   di_verdict(DI_JOB_REMOVE, (unsigned)err, flags),
                                   (unsigned)err));
    }

cleanup:
    if (lpdi != NULL) {
        DiDestroyDeviceInfoList(lpdi);
    }
    if (list != NULL) {
        DiDestroyDeviceInfoList(list);
    }
}

/*
 * HKLM\Software\Microsoft\Windows\CurrentVersion's VersionNumber, the
 * build string Windows 98 SE and ME keep there (4.10.2222, 4.90.3000).
 */
int di_sys_version_number(char *out, unsigned size)
{
    HKEY hk;
    DWORD cb = (DWORD)size - 1;
    DWORD rc;
    out[0] = '\0';
    if (SURegOpenKey(HKEY_LOCAL_MACHINE,
                     "Software\\Microsoft\\Windows\\CurrentVersion",
                     &hk) != DI_REG_OK) {
        return 0;
    }
    rc = SURegQueryValueEx(hk, "VersionNumber", NULL, NULL, out, &cb);
    SURegCloseKey(hk);
    out[size - 1] = '\0';
    if (rc != DI_REG_OK) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}
