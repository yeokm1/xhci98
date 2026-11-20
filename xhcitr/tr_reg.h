/*
 * tr_reg.h - XHCITR's listing of a USB device's driver keys (design record
 * 25 section 25; roadmap-usb4.md task 50.29): every value's name, type and
 * size, and its data as text within the record's limits unless withheld,
 * for the device's devnode and the devnodes below it. tr_reg.c is pure
 * (test\test_trreg.c links it under /Za); tr_regw.c is the half that calls
 * Windows: the key's open and RegEnumValueA. Read only. C89.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TR_REG_H
#define TR_REG_H

#include "tr_core.h"

/* The registry's value types, as winnt.h numbers them. */
#define TR_REG_NONE                 0UL
#define TR_REG_SZ                   1UL
#define TR_REG_EXPAND_SZ            2UL
#define TR_REG_BINARY               3UL
#define TR_REG_DWORD                4UL
#define TR_REG_DWORD_BIG_ENDIAN     5UL
#define TR_REG_LINK                 6UL
#define TR_REG_MULTI_SZ             7UL
#define TR_REG_RESOURCE_LIST        8UL
#define TR_REG_FULL_RESOURCE        9UL
#define TR_REG_RESOURCE_REQ         10UL
#define TR_REG_QWORD                11UL

/* The limits (section 25 item 2). */
#define TR_REG_STR_CHARS            512     /* a string's characters shown */
#define TR_REG_MSZ_STRINGS          32      /* a multi-string's strings    */
#define TR_REG_MSZ_CHARS            2048    /* ... and their characters    */
#define TR_REG_HEX_BYTES            256     /* bytes shown in hexadecimal  */
#define TR_REG_NAME_SHOWN           128     /* a value name's characters   */
#define TR_REG_READ_BYTES           65536UL /* the most of a value read    */
#define TR_REG_NAME_CHARS           16384UL /* the name buffer: NT's 16383 */
#define TR_REG_KEY_VALUES           256     /* values listed of one key    */
#define TR_REG_KEY_TEXT             65536UL /* text written for one key    */
#define TR_REG_COUNT_MAX            100000UL /* values counted past the limits */
/* The most one value's lines take, an indent of TR_REG_INDENT_MAX included
 * and every quote in its text doubled: tr_reg_key_text starts a value only
 * while this much of the key's text is left. */
#define TR_REG_VALUE_MAX            8192UL
#define TR_REG_INDENT_MAX           32
/* The screen's work buffer: a value's bytes as text, then its UTF-16 view
 * and that view in the system's code page. */
#define TR_REG_PROJ_BYTES           (2UL * TR_REG_READ_BYTES + 2UL)

/* A screening answer (the device report's privacy rule, section 25 item
 * 3): the text may be shown; it holds one of this machine's names; or the
 * names were not read, so no free text is shown. */
#define TR_REG_SHOW                 0
#define TR_REG_WITHHOLD             1
#define TR_REG_UNSCREENED           2

/* text: the bytes as stored, NULs left out and control bytes as blanks,
 * NUL-terminated; a byte of the system's code page (0x80 and up) is kept,
 * so a name written in it reaches the screen whole. */
typedef int (*TR_REG_SCREEN)(const char *text, void *context);
/* count UTF-16LE units (2 * count bytes at units, none NUL) into out as the
 * system code page's text, NUL-terminated within outBytes; returns its
 * length, 0 when nothing converted. */
typedef size_t (*TR_REG_NARROW)(const unsigned char *units, size_t count,
                                char *out, size_t outBytes, void *context);

typedef struct _TR_REG_SCR {
    TR_REG_SCREEN Screen;
    TR_REG_NARROW Narrow;       /* NULL: no UTF-16 view                    */
    void *Context;
} TR_REG_SCR;

/* The device report's screen over a long text: has (one window, at most
 * TR_REG_WINDOW characters, NUL-terminated in chunk, which holds
 * TR_REG_WINDOW + 1) asked of windows that overlap by TR_REG_WINDOW -
 * TR_REG_WINDOW_STEP, so a name of up to that many characters lies whole in
 * one. 1 when any window holds a name. */
#define TR_REG_WINDOW               1800
#define TR_REG_WINDOW_STEP          1500
typedef int (*TR_REG_HAS)(const char *chunk, void *context);
int tr_reg_screen_windows(const char *text, char *chunk, TR_REG_HAS has,
                          void *context);

/* RegEnumValueA's answers this half reads. */
#define TR_REG_OK                   0UL
#define TR_REG_MORE_DATA            234UL   /* ERROR_MORE_DATA     */
#define TR_REG_NO_MORE              259UL   /* ERROR_NO_MORE_ITEMS */

/* RegEnumValueA's contract: value `index`'s name into name (nameChars in:
 * its room in characters, a NUL included; out: the name's length, no NUL),
 * its type, and with data its data (bytes in: data's room; out: the bytes
 * written, or with TR_REG_MORE_DATA the bytes needed). With data NULL only
 * the size is answered. */
typedef TR_U32 (*TR_REG_ENUM)(void *context, TR_U32 index, char *name,
                              TR_U32 *nameChars, TR_U32 *type,
                              unsigned char *data, TR_U32 *bytes);

typedef struct _TR_REG_KEY {
    TR_U32 OpenError;           /* 0 opened, else the open's error        */
    TR_REG_ENUM Enum;
    void *Context;
    unsigned char *Data;        /* TR_REG_READ_BYTES                      */
    char *Name;                 /* TR_REG_NAME_CHARS                      */
    char *Proj;                 /* TR_REG_PROJ_BYTES                      */
    const TR_REG_SCR *Scr;      /* NULL: no screen                        */
    int Width;                  /* its notes' width, 0 TR_TEXT_COLS (task
                                   50.39, tr_fit_text)                  */
} TR_REG_KEY;

/* 1 when a value of this name may hold a key or password, by section 25
 * item 3's rule (the owner's, 2026-11-17). Reads nothing before the
 * name's first character or past its NUL. */
int tr_reg_secret_name(const char *name);

/* The type's name ("REG_SZ"), or NULL for a type this tool does not
 * name. */
const char *tr_reg_type_name(TR_U32 type);

/* One value's lines into t, each begun with indent (at most
 * TR_REG_INDENT_MAX characters): its name, type and data[0..bytes), the
 * whole value as read. proj (TR_REG_PROJ_BYTES, or NULL with no screen) is
 * the screen's work buffer. */
void tr_reg_value_text(TR_TEXT *t, const char *indent, const char *name,
                       TR_U32 type, const unsigned char *data, TR_U32 bytes,
                       char *proj, const TR_REG_SCR *scr);

/* A key's values into t, each begun with indent, then the count, or why
 * none: the key not opened, an enumeration error, the limits. t should
 * have TR_REG_KEY_TEXT and a few lines of room. Returns the values
 * listed. */
int tr_reg_key_text(TR_TEXT *t, const char *indent, TR_REG_KEY *k);

/* ---- the devnodes of one device (section 25 item 1) ---------------------- */

/* A device's own devnode is level 0; the walk follows the devnodes below it,
 * whatever their enumerator, to TR_REG_TREE_DEPTH levels and
 * TR_REG_TREE_NODES devnodes, and never below a hub. */
#define TR_REG_TREE_DEPTH           4
#define TR_REG_TREE_NODES           32
#define TR_REG_TREE_VISITS          4096    /* devnodes looked at, counted */

/* Child and Sibling answer TR_REG_REL_OK with the devnode, TR_REG_REL_NONE
 * when there is none, TR_REG_REL_PARTIAL when the devnodes were not all
 * read (the tool's walk says so with no number), or the configuration
 * manager's error. */
#define TR_REG_REL_OK               0UL
#define TR_REG_REL_NONE             0xFFFFFFFEUL
#define TR_REG_REL_PARTIAL          0xFFFFFFFFUL

typedef struct _TR_REG_TREE {
    TR_U32 (*Child)(void *context, TR_U32 node, TR_U32 *child);
    TR_U32 (*Sibling)(void *context, TR_U32 node, TR_U32 *next);
    int (*IsHub)(void *context, TR_U32 node);
    /* The devnode's block at its level; 0 stops the walk (an output that
     * failed). */
    int (*Emit)(void *context, TR_U32 node, int level);
    /* One line of the walk's own (a limit, a devnode list not read) under
     * the devnodes of `level`; 0 stops the walk. */
    int (*Note)(void *context, int level, const char *line);
    void *Context;
} TR_REG_TREE;

/* device's devnode, then the devnodes below it, depth first, each by Emit;
 * the limits said by Note. Returns the devnodes emitted, or -1 when Emit or
 * Note stopped it. */
int tr_reg_tree_walk(const TR_REG_TREE *w, TR_U32 device);

/* 1 when a devnode's device id names a hub by itself: a root hub (ROOT_HUB
 * in it) or xhci98.sys's hubs (USB\XHCI98_HUB...), which carry no class
 * id. Windows' own hubs are told by USB\Class_09 among their ids
 * (tr_node_is_hub). */
int tr_reg_hub_id(const char *id);

/* ---- the Windows half (tr_regw.c) --------------------------------------- */

/* HKLM's driver key of a devnode whose Driver value is `driver`, under
 * SYSTEM\CurrentControlSet\Control\Class on NT (nt nonzero), else under
 * System\CurrentControlSet\Services\Class, listed by tr_reg_key_text.
 * The key's path into path (pathBytes, 300 or more) where it fits. screen
 * NULL: no screen; else the device report's, with the UTF-16 view in the
 * system's code page. width: the notes' width (TR_REG_KEY's Width, 0
 * TR_TEXT_COLS). One caller at a time: its buffers are static. Returns
 * the values listed. */
int tr_regw_key_text(TR_TEXT *t, const char *indent, int nt,
                     const char *driver, char *path, unsigned long pathBytes,
                     TR_REG_SCREEN screen, void *context, int width);

#endif /* TR_REG_H */
