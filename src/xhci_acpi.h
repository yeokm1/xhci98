/*
 * xhci_acpi.h - the answer of xhci98.sys's firmware tables request,
 * IOCTL_XHCI98_ACPI (design record 13 section 8.13; design record 32
 * section 12.8.16; design record 25 section 21 item 9; roadmap-usb4 task
 * 46.21, the owner's option B): on 98 SE and ME, the computer's ACPI root
 * pointer, root table, DSDT and every SSDT whole, read when asked. Shared
 * byte for byte by the driver (hcd_acpi.c, xhci_acpi.c), XHCITR.EXE and
 * the host tests, so it uses no DDK or Windows type: constants only.
 *
 * The layout is xhcicm.sys's firmware tables answer's, version 2
 * (src\cm\cm_query.h CM_QA_*: the same 64-byte header, the same 32-byte
 * entries, kinds and held states, the same paging), with its own magic
 * and version, and each entry's word 20 the reason a table is not kept
 * whole. A layout change is a new XHA_VERSION.
 */

#ifndef XHCI_ACPI_H
#define XHCI_ACPI_H

/*
 * CTL_CODE(FILE_DEVICE_UNKNOWN 0x22, 0xA49, METHOD_BUFFERED,
 * FILE_ANY_ACCESS), on the controller's own device. Input: two dwords, the
 * body offset, then exactly one of XHA_IN_READ and XHA_IN_RELEASE; any
 * other length, bit or combination: STATUS_INVALID_PARAMETER. Output under
 * XHA_HDR_BYTES: STATUS_BUFFER_TOO_SMALL. Otherwise STATUS_SUCCESS with the
 * header and as much of the body from that offset as fits, at most
 * XHA_PART_MAX bytes of it (the answer at most 32 KiB); an offset past the
 * body, or no set held: the header alone, XHA_F_PAST, generation 0. A
 * driver from before it answers STATUS_INVALID_DEVICE_REQUEST.
 */
#define XHA_IOCTL               0x00222924UL
#define XHA_MAGIC               0x41434858UL    /* 'XHCA' */
#define XHA_VERSION             1UL

#define XHA_IN_READ             0x01UL
#define XHA_IN_RELEASE          0x02UL

#define XHA_HDR_BYTES           64UL
#define XHA_O_MAGIC             0UL
#define XHA_O_VERSION           4UL
#define XHA_O_BYTES             8UL     /* this answer: header + part    */
#define XHA_O_FLAGS             12UL    /* XHA_F_*                       */
/* 16..23 0 (xhcicm's epoch words)                                      */
#define XHA_O_STATE             24UL    /* XHA_S_*                       */
#define XHA_O_OUTCOME           28UL    /* state 9: 1 the root table, 2
                                           MCFG, 3 a root entry's header
                                           not read in the bootstrap, 4
                                           the time bound passed in it   */
/* 32 0 (xhcicm's tier)                                                 */
#define XHA_O_BODY_BYTES        36UL
#define XHA_O_ENT_SHAPE         40UL    /* count 15:0, entry bytes 31:16 */
#define XHA_O_PART_AT           44UL
#define XHA_O_PART_BYTES        48UL
#define XHA_O_SET_GEN           52UL    /* the set's generation, never 0
                                           for a set; 0 in a header with
                                           no set                        */
/* 56..63 reserved, 0 */

#define XHA_F_CUT               0x02UL  /* a table kept as its header for
                                           the size limit                */
#define XHA_F_NOMEM             0x04UL
#define XHA_F_PAST              0x08UL  /* offset past the body, or none */
#define XHA_F_FULL              0x10UL  /* always: a full set's answer   */
#define XHA_F_INCOMPLETE        0x20UL  /* a table not kept whole        */
#define XHA_F_NO_DSDT           0x40UL  /* no DSDT kept whole            */

#define XHA_S_READ              1UL
#define XHA_S_NOT_OS            2UL     /* not served on this Windows    */
#define XHA_S_OFF               4UL     /* XhciAcpiTables 0              */
#define XHA_S_NO_RSDP           6UL
#define XHA_S_NOMEM             7UL
#define XHA_S_BUSY              8UL
#define XHA_S_REFUSED           9UL

/* The body: the entries, then the bytes held, each at a dword boundary. */
#define XHA_ENT_BYTES           32UL
#define XHA_E_SIG               0UL
#define XHA_E_KIND              4UL     /* kind 7:0, held 15:8, index
                                           31:16 (the root entry's; the
                                           DSDT's: the FADT's entry)     */
#define XHA_E_LENGTH            8UL     /* the length its header gives   */
#define XHA_E_OFFSET            12UL
#define XHA_E_HELD              16UL
#define XHA_E_REASON            20UL    /* XHA_R_*: why not kept whole   */
/* 24..31 reserved, 0 */

#define XHA_K_RSDP              1UL
#define XHA_K_ROOT              2UL
#define XHA_K_ENTRY             3UL
#define XHA_K_DSDT              4UL

#define XHA_H_NONE              0UL
#define XHA_H_WHOLE             1UL
#define XHA_H_HEADER            2UL

#define XHA_R_NONE              0UL     /* kept whole, or a header by its
                                           kind (not FACP, SSDT or DSDT) */
#define XHA_R_SIGNATURE         1UL     /* no longer a table at its
                                           address                       */
#define XHA_R_LENGTH            2UL
#define XHA_R_CHECKSUM          3UL
#define XHA_R_BOUNDS            4UL     /* outside the memory it may be
                                           read from                     */
#define XHA_R_LIMIT             5UL     /* past the size limit           */
#define XHA_R_NOMEM             6UL
#define XHA_R_TIMEOUT           7UL
#define XHA_R_READ              8UL     /* the read itself failed        */

#define XHA_TBL_HDR             36UL
#define XHA_MAX_ENTRIES         67UL    /* the pointer, the root, 64, the
                                           DSDT                          */
#define XHA_MAX_ROOT            64UL
#define XHA_DATA_MAX            0x400000UL
#define XHA_FACP_CAP            0x1000UL
#define XHA_SSDT_CAP            0x40000UL
#define XHA_DSDT_CAP            0x100000UL
#define XHA_MCFG_CAP            0x1000UL
#define XHA_MCFG_WINDOWS        16UL
#define XHA_MAP_MAX             0x10000UL       /* a mapping, pages counted */
#define XHA_TIMEOUT_MS          20000UL
#define XHA_MAX_BODY            (XHA_MAX_ENTRIES * XHA_ENT_BYTES + \
                                 XHA_DATA_MAX + 4UL * XHA_MAX_ENTRIES)
#define XHA_PART_MAX            32704UL
#define XHA_ASK_BYTES           (XHA_HDR_BYTES + XHA_PART_MAX)

#endif /* XHCI_ACPI_H */
