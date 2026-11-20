/*
 * tr_regw.c - XHCITR's listing of a USB device's driver key, the half that
 * calls Windows (design record 25 section 25; roadmap-usb4.md task 50.29):
 * the key opened with KEY_READ and its values read by RegEnumValueA, both
 * ADVAPI32 calls the tool imports already, on every system it runs on.
 * Nothing is written. C89.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "tr_reg.h"

/* One caller at a time (Save Report on the window's thread or /report, and
 * the snapshot's engine, each in a process of its own). */
static unsigned char regData[TR_REG_READ_BYTES];
static char regName[TR_REG_NAME_CHARS];
/* WCHAR-aligned: the screen's UTF-16 view starts it. */
static WCHAR regProjW[TR_REG_PROJ_BYTES / 2 + 1];

static TR_U32 regw_enum(void *context, TR_U32 index, char *name,
                        TR_U32 *nameChars, TR_U32 *type, unsigned char *data,
                        TR_U32 *bytes)
{
    DWORD n = (DWORD)*nameChars;
    DWORD ty = 0;
    DWORD b = (DWORD)*bytes;
    LONG rc;

    rc = RegEnumValueA((HKEY)context, (DWORD)index, name, &n, NULL, &ty,
                       data, &b);
    *nameChars = (TR_U32)n;
    *type = (TR_U32)ty;
    *bytes = (TR_U32)b;
    return (TR_U32)rc;
}

/* The screen's UTF-16 view in the system's code page: what a name kept as
 * UTF-16 reads as there, so the screen finds it as it finds an ANSI one. */
static size_t regw_narrow(const unsigned char *units, size_t count, char *out,
                          size_t outBytes, void *context)
{
    int n;

    (void)context;
    if (count == 0 || outBytes < 2) {
        return 0;
    }
    n = WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)units, (int)count, out,
                            (int)(outBytes - 1), NULL, NULL);
    if (n <= 0) {
        return 0;
    }
    out[n] = '\0';
    return (size_t)n;
}

int tr_regw_key_text(TR_TEXT *t, const char *indent, int nt,
                     const char *driver, char *path, unsigned long pathBytes,
                     TR_REG_SCREEN screen, void *context, int width)
{
    const char *root = nt ? "SYSTEM\\CurrentControlSet\\Control\\Class\\"
                          : "System\\CurrentControlSet\\Services\\Class\\";
    char keyPath[400];
    TR_REG_KEY k;
    TR_REG_SCR scr;
    HKEY key = NULL;
    LONG rc;
    int listed;

    if (path != NULL && pathBytes != 0) {
        path[0] = '\0';
    }
    memset(&k, 0, sizeof(k));
    k.Width = width;
    if (driver == NULL || strlen(root) + strlen(driver) >= sizeof(keyPath)) {
        k.OpenError = ERROR_FILENAME_EXCED_RANGE;
        return tr_reg_key_text(t, indent, &k);
    }
    strcpy(keyPath, root);
    strcat(keyPath, driver);
    if (path != NULL && strlen(keyPath) + 6 <= pathBytes) {
        strcpy(path, "HKLM\\");
        strcat(path, keyPath);
    }
    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS) {
        k.OpenError = rc != 0 ? (TR_U32)rc : 0xFFFFFFFFUL;
        return tr_reg_key_text(t, indent, &k);
    }
    k.Enum = regw_enum;
    k.Context = (void *)key;
    k.Data = regData;
    k.Name = regName;
    k.Proj = (char *)regProjW;
    if (screen != NULL) {
        scr.Screen = screen;
        scr.Narrow = regw_narrow;
        scr.Context = context;
        k.Scr = &scr;
    }
    listed = tr_reg_key_text(t, indent, &k);
    RegCloseKey(key);
    return listed;
}
