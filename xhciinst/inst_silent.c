/*
 * inst_silent.c - the silent install's list merged into the registry
 * (inst_silent.h). Moved from inst_eng.c's silent_list_merge unchanged in
 * what it does, so that XHCITR's Settings window turns XhciSilentInstall
 * on exactly as Setup does (design record 25 section 22; roadmap-usb4.md
 * task 47.35). IRQL: none (user mode). Windows 98 SE and ME.
 */

#include <windows.h>
#include <string.h>
#include "inst_core.h"
#include "inst_silent.h"

int inst_silent_merge(char *text, int *wrote, int *values)
{
    char *p;
    char *nl;
    char name[32];
    unsigned char data[64];
    unsigned char back[64];
    size_t dn;
    DWORD n;
    DWORD bn;
    DWORD type;
    HKEY k;
    int in = 0;
    int ok = 1;

    *wrote = 0;
    *values = 0;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_SILENT_KEY, 0, NULL, 0,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &k, NULL) !=
        ERROR_SUCCESS) {
        return INST_SM_OPEN;
    }
    for (p = text; *p != '\0' && ok; p = nl) {
        nl = strchr(p, '\n');
        if (nl == NULL) {
            nl = p + strlen(p);
        } else {
            *nl++ = '\0';
        }
        if (strlen(p) > 0 && p[strlen(p) - 1] == '\r') {
            p[strlen(p) - 1] = '\0';
        }
        if (p[0] == '[') {
            in = inst_eq(p, "[HKEY_LOCAL_MACHINE\\" INST_SILENT_KEY "]");
            continue;
        }
        if (!in || p[0] != '"') {
            continue;
        }
        /* "name"=hex:aa,bb,... (inst_core.c, host-tested) */
        if (!inst_reg_hex_line(p, name, sizeof(name), data, sizeof(data),
                               &dn)) {
            ok = 0;
            break;
        }
        n = (DWORD)dn;
        bn = sizeof(back);
        /* A value already as SILENT.REG has it is not written again: an
         * unchanged rerun says "No changes were made." */
        if (RegQueryValueExA(k, name, NULL, &type, back, &bn) == ERROR_SUCCESS &&
            type == REG_BINARY && bn == n && memcmp(back, data, n) == 0) {
            (*values)++;
            continue;
        }
        if (RegSetValueExA(k, name, 0, REG_BINARY, data, n) == ERROR_SUCCESS) {
            *wrote = 1;     /* a write happened, whatever follows */
        } else {
            ok = 0;
            break;
        }
        bn = sizeof(back);
        if (RegQueryValueExA(k, name, NULL, &type, back, &bn) != ERROR_SUCCESS ||
            type != REG_BINARY || bn != n || memcmp(back, data, n) != 0) {
            ok = 0;
            break;
        }
        (*values)++;
    }
    RegCloseKey(k);
    return (!ok || *values == 0) ? INST_SM_MISMATCH : INST_SM_DONE;
}
