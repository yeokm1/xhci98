/*
 * inst_bootp.h - whether the disk Windows starts from sits below a
 * devnode that names a filter (design record 23 section 5.14; roadmap-usb4
 * task 44.32), shared by the installer's engine (inst_eng.c) and the mark
 * service (..\xhcimark\xhcimark.c). C89.
 */

#ifndef INST_BOOTP_H
#define INST_BOOTP_H

/* 1 when the disk holding the Windows directory, or any ancestor of its
 * devnode up to the root, names svc in its device-specific UpperFilters
 * or LowerFilters value; 0 when none does; -1 when it could not be read
 * (a routine missing, the volume on more than one disk, the disk's
 * devnode not found). NT only; PASSIVE, any thread. */
int inst_boot_path_names(const char *svc);

/* 1 when the devnode of instanceId is the system disk's or one of its
 * ancestors up to the root, 0 when it is not, -1 when that could not be
 * read (as above, or no present devnode of that id). Record 23 section
 * 5.4.2: a controller with the Windows disk behind it is not taken over. */
int inst_boot_path_has(const char *instanceId);

#endif
