/*
 * cm_tagid.h - the pure half of the tunnel id on Windows 98 SE and ME
 * (design record 31 section 5, roadmap-usb4 task 44.3): the CONFIGMG
 * worker (cm_cfgwk.c) gives every NVMe-class devnode below an active
 * member the id PCI\XHCI98_TUNNEL&CC_010802 with Add_ID, as a compatible
 * id, and re-runs the install of one that has no driver. This file holds
 * the id list's test, the step's decisions and the per-boot record of the
 * devnodes the worker has dealt with. DDK-free and host-tested
 * (test\test_cm_tagid.c).
 *
 * IRQL: any. Takes no lock; every function works on the caller's memory.
 */

#ifndef CM_TAGID_H
#define CM_TAGID_H

#include "../xhci_compat.h"

/* The id (record 31 section 3.1) and the class that earns it (3.3). */
#define CM_TAG_ID           "PCI\\XHCI98_TUNNEL&CC_010802"
#define CM_TAG_CLASS        "PCI\\CC_010802"

/*
 * 1 when `id` is one whole entry of the id list in list[0..bytes): the
 * entries are separated by commas (the form Windows 98's Enum key keeps
 * HardwareID and CompatibleIDs in) or by NULs (a MULTI_SZ), the list ends
 * at `bytes` or at two NULs in a row, spaces and tabs around an entry are
 * not part of it, and letters compare without case. NULL or empty: 0.
 */
ULONG CmTagListHas(const char *list, ULONG bytes, const char *id);

/*
 * The step for one devnode (record 31 section 5.1), from its CompatibleIDs
 * read (readOk: the value was read whole) and what the list holds.
 */
#define CM_TAG_ADD          0UL /* Add_ID it                              */
#define CM_TAG_NO_READ      1UL /* the list not read: tried again later   */
#define CM_TAG_NOT_CLASS    2UL /* not of the NVMe class: never the id    */
#define CM_TAG_HAS_ID       3UL /* the id there already (a later boot)    */
ULONG CmTagDecide(ULONG readOk, ULONG hasClass, ULONG hasId);


/*
 * The devnodes dealt with this boot (once per devnode per boot, record 31
 * section 5.1): a devnode handle and its instance id's hash, since a
 * handle may name another devnode after a removal. The oldest entry gives
 * way when the table is full.
 */
#define CM_TAG_SEEN_MAX     64UL
typedef struct _CM_TAG_SEEN {
    ULONG Count;
    ULONG Next;             /* the entry the next full add replaces       */
    ULONG Node[CM_TAG_SEEN_MAX];
    ULONG Hash[CM_TAG_SEEN_MAX];
} CM_TAG_SEEN, *PCM_TAG_SEEN;
VOID CmTagSeenInit(PCM_TAG_SEEN s);
ULONG CmTagSeenHas(const CM_TAG_SEEN *s, ULONG node, ULONG hash);
VOID CmTagSeenAdd(PCM_TAG_SEEN s, ULONG node, ULONG hash);

#endif /* CM_TAGID_H */
