/*
 * inst_silent.h - the silent install's list merged into the registry
 * (inst_silent.c): Setup's silent_list_merge and the xHCI98 USB Tree's
 * Settings window share it (design record 25 section 22; roadmap-usb4.md
 * task 47.35), so that turning XhciSilentInstall on does the same in
 * either. Windows 98 SE and ME only.
 */

#ifndef INST_SILENT_H
#define INST_SILENT_H

#define INST_SILENT_KEY "System\\CurrentControlSet\\Services\\USB\\SilentClasses"

/* What the merge came to. */
#define INST_SM_DONE        0   /* every value of the list in place       */
#define INST_SM_OPEN        1   /* the SilentClasses key would not open   */
#define INST_SM_MISMATCH    2   /* a line did not parse, a value did not
                                   write or read back, or no value at all */

/* tools\silent\SILENT.REG's text (REGEDIT4; changed in place as it is
 * read) merged into HKLM\<INST_SILENT_KEY>: each value of that key's
 * section written as REG_BINARY where it is not already the same, and
 * read back. *wrote is set when any value was written, *values counts the
 * values in place. */
int inst_silent_merge(char *text, int *wrote, int *values);

#endif /* INST_SILENT_H */
