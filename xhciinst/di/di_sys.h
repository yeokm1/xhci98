/*
 * di_sys.h - the system part of XHCIDI.EXE: the result file and the setup
 * engine's calls of design record 27 sections 5 and 9. Win16, large model;
 * included after windows.h and setupx.h.
 */

#ifndef XHCI_DI_SYS_H
#define XHCI_DI_SYS_H

#include "di_core.h"

typedef struct _DI_RUN {
    HFILE file;
    int bad;
    DI_OUT out;
} DI_RUN;

void di_emit(DI_RUN *run, const char *line);
void di_sys_install(DI_RUN *run, const DI_CMD *cmd, HWND hwnd);
void di_sys_remove(DI_RUN *run, const DI_CMD *cmd, HWND hwnd);
int di_sys_version_number(char *out, unsigned size);

#endif
