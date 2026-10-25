/*
 * vsd_list.h - the pure half of S98VSD.VXD (roadmap-usb4.md task 43.46,
 * design record 30 sections 3.3, 3.4 and 4.1 to 4.3): which DCB the layer
 * claims, which request carries data, and the physical list built from
 * page-table entries with every input checked.
 *
 * DDK-free and C89, so test\test_vsd_list.c builds it on the host with the
 * same source the VxD links (design record 03's rule). No file-scope
 * static anywhere in this half: the 98 DDK's LINK 5.12 mislinks Open
 * Watcom's static-symbol records (design record 30 section 6.1).
 *
 * The callers supply the two reads the VxD makes through the system: the
 * page-table entries (_CopyPageTable) and the descriptors of a logical
 * scatter/gather list (plain memory there, an array on the host).
 */

#ifndef VSD_LIST_H
#define VSD_LIST_H

typedef unsigned long VSD_U32;

/* One physical run; the layout of the DDK's SGD (SGD.H: SG_buff_ptr,
 * SG_buff_size), which SCSIPORT.PDR walks at IOR_sgd_lin_phys. */
typedef struct VSD_RUN {
    VSD_U32 Addr;
    VSD_U32 Len;
} VSD_RUN;

/* The largest request the list is sized for (section 3.3 condition 4). */
#define VSD_MAX_XFER        0x200000UL
#define VSD_PAGE            4096UL
#define VSD_MAX_PAGES       (VSD_MAX_XFER / VSD_PAGE)
/* The layer's own limit on a logical list's fragments (section 4.2). */
#define VSD_MAX_FRAGS       255UL
/* Room: two partial pages a fragment over P whole ones (section 3.4). */
#define VSD_ROOM_RUNS       (VSD_MAX_PAGES + 2UL * VSD_MAX_FRAGS)
/* A fragment's page-table entries: at most P + 1 pages, one to spare. */
#define VSD_SCRATCH         (VSD_MAX_PAGES + 2UL)

/* The page-table entry bits read (the 386 PTE). */
#define VSD_PTE_PRESENT     0x00000001UL
#define VSD_PTE_FRAME       0xFFFFF000UL

/* Results of VsdBuild; VSD_OK = 0. Each refusal is counted by its value. */
#define VSD_OK              0
#define VSD_R_COUNT         1   /* a sector count whose shift overflows      */
#define VSD_R_FRAGS         2   /* more than VSD_MAX_FRAGS, no terminator     */
#define VSD_R_EMPTY         3   /* an empty logical list under a count        */
#define VSD_R_MAX           4   /* more bytes than the maximum                */
#define VSD_R_WRAP          5   /* an address plus a length wraps             */
#define VSD_R_COVER         6   /* fewer bytes than IOR_xfer_count's          */
#define VSD_R_SRB_COVER     7   /* fewer bytes than the SRB's length          */
#define VSD_R_SRB_FIRST     8   /* the SRB's DataBuffer is not the first byte */
#define VSD_R_SCRATCH       9   /* a fragment of more pages than the scratch  */
#define VSD_R_PTE           10  /* the page-table service failed              */
#define VSD_R_PRESENT       11  /* a page not present                         */
#define VSD_R_ROOM          12  /* more runs than the room                    */
#define VSD_R_KINDS         13

/* The request's inputs, as IOS's own builder reads them (section 2.2
 * item 2) and the SRB's two fields section 4.3 checks. */
typedef struct VSD_REQ {
    VSD_U32 BufferPtr;      /* IOR_buffer_ptr                             */
    VSD_U32 XferCount;      /* IOR_xfer_count                             */
    int Sg;                 /* IORF_SCATTER_GATHER                        */
    int Chars;              /* IORF_CHAR_COMMAND: counts are bytes        */
    unsigned Shift;         /* DCB_apparent_blk_shift                     */
    VSD_U32 MaxXfer;        /* the smaller of DCB_max_xfer_len, VSD_MAX   */
    int SrbValid;           /* IORF_SRB_VALID                             */
    VSD_U32 SrbBuffer;      /* the SRB's DataBuffer                       */
    VSD_U32 SrbLength;      /* the SRB's DataTransferLength               */
} VSD_REQ;

/* The page-table read: npages entries from page into ptes; non-zero is
 * success (VMM's _CopyPageTable). */
typedef VSD_U32 (*VSD_PTE_FN)(void *ctx, VSD_U32 page, VSD_U32 npages,
                              VSD_U32 *ptes);
/* The descriptor read: the logical list's descriptor at addr (its count,
 * then its buffer, BLOCKDEV.H's _BlockDev_Scatter_Gather); VsdBuild forms
 * addr only after checking that it does not wrap. */
typedef void (*VSD_DESC_FN)(void *ctx, VSD_U32 addr, VSD_U32 *count,
                            VSD_U32 *buffer);

typedef struct VSD_IO {
    VSD_PTE_FN Pte;
    VSD_DESC_FN Desc;
    void *Ctx;
} VSD_IO;

/* Count to bytes: 1 and *bytes on success, 0 when the shift overflows. */
int VsdBytes(VSD_U32 count, int chars, unsigned shift, VSD_U32 *bytes);

/* Build the list into runs[VSD_ROOM_RUNS] using scratch[VSD_SCRATCH];
 * *nruns and *total out. VSD_OK or a VSD_R_ value; nothing is written to
 * the request. */
int VsdBuild(const VSD_REQ *req, const VSD_IO *io, VSD_RUN *runs,
             VSD_U32 *scratch, VSD_U32 *nruns, VSD_U32 *total);

/* VsdBuild's steps, external only because the 98 DDK's linker mislinks a
 * static one (vsd_list.h's head); not for other callers. VsdWraps: n bytes
 * from a do not end below 2^32 (the exclusive end wraps or is 2^32). */
int VsdWraps(VSD_U32 a, VSD_U32 n);
int VsdWalk(const VSD_REQ *req, const VSD_IO *io, VSD_U32 *nfrag,
            VSD_U32 *sum, VSD_U32 *first);
int VsdAddFrag(const VSD_IO *io, VSD_U32 buf, VSD_U32 bytes, VSD_RUN *runs,
               VSD_U32 *scratch, VSD_U32 *nruns);

/* Claim results (section 3.3); VSD_CLAIM = 0 claims. */
#define VSD_CLAIM           0
#define VSD_C_LOGICAL       1
#define VSD_C_PORT          2
#define VSD_C_NOPHYS        3
#define VSD_C_MAX           4
#define VSD_C_KINDS         5

#define VSD_DCB_DEV_LOGICAL 0x00004000UL    /* DCB.H DCB_DEV_LOGICAL     */
#define VSD_DCB_DEV_RMM     0x00002000UL    /* DCB.H DCB_DEV_RMM         */
#define VSD_DMD_PHYS_SGD    0x00000800UL    /* DCB.H DCB_dmd_phys_sgd    */

/* port: DCB_port_name's 8 bytes. */
int VsdClaimTest(const unsigned char *port, VSD_U32 devflags, VSD_U32 dmd,
                 VSD_U32 maxxfer);

/* 1 when the IOR function carries no data by its definition (IOR.H),
 * section 4.1 item 2; every other value, an undefined one included, is 0. */
int VsdNoDataFunc(unsigned func);

/* What the request routine does with a request (section 4.1). An
 * IORF_PHYS_SGDS that arrives is not a reason to pass a request down:
 * with phys_sgd no longer demanded IOS's own double buffer arrives with
 * that flag and a list holding the buffer's LINEAR address (IOS
 * IOS_SendCommand, QuickInstall object 1 0x100E65-0x100EC8 [static];
 * IOR.H on IORF_DOUBLE_BUFFER: the SGD pointers are virtual), so every
 * request with data gets this layer's list, built from IOR_buffer_ptr. */
#define VSD_P_BUILD         0   /* build the list, then call down          */
#define VSD_P_NODATA        1   /* no data: call down untouched            */
#define VSD_P_SRB_NODATA    2   /* refused: SRB data on a no-data request  */
#define VSD_P_NO_SRB        3   /* refused: data and no SRB (section 4.3)  */

#define VSD_IORF_SRB_VALID  0x00008000UL    /* IOR.H IORF_SRB_VALID      */
#define VSD_IORF_PHYS_SGDS  0x04000000UL    /* IOR.H IORF_PHYS_SGDS      */
#define VSD_IORF_DBLBUF     0x00000040UL    /* IOR.H IORF_DOUBLE_BUFFER  */

/* flags: IOR_flags; count: IOR_xfer_count; func: IOR_func; srbLen: the
 * SRB's DataTransferLength when IORF_SRB_VALID is set, else ignored. */
int VsdPlan(VSD_U32 flags, VSD_U32 count, unsigned func, VSD_U32 srbLen);

#endif
