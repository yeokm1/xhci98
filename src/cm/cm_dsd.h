/*
 * cm_dsd.h - the pure half of the ACPI-named tunnel-port source (design
 * record 22 section 7.6.1, task 46.23): the reader of a tunnel root port's
 * _DSD answer as IOCTL_ACPI_EVAL_METHOD returns it, and the ACPI name
 * normalisation both sides of the match use.
 *
 * The answer's layout is the WDK's acpiioct.h ACPI_EVAL_OUTPUT_BUFFER
 * (Signature 'BoeA', Length, Count, then Count ACPI_METHOD_ARGUMENTs: Type
 * and DataLength, 16 bits each, then the data, each argument taking
 * 4 + max(4, DataLength) bytes; a package's data is its elements'
 * arguments). cm_dsdos.c holds the two layouts equal with compile-time
 * checks. The properties are [ms-usb4acpi]'s: the device-properties UUID
 * daffd814-6eba-4d8c-8a91-bc9bbf4aa301, "usb4-host-interface" (a reference
 * to the NHI's ACPI device, read here as a name string) and
 * "usb4-port-number" (an integer, which may be 0, read for the log alone).
 *
 * C89, DDK-free, no 64-bit arithmetic. IRQL: any.
 */

#ifndef CM_DSD_H
#define CM_DSD_H

#include "../xhci_compat.h"

#define CM_DSD_NAME_MAX         128UL   /* a normalised name, NUL excluded */
#define CM_DSD_SEGMENTS_MAX     16UL
#define CM_DSD_ANSWER_MAX       1024UL  /* the answer buffer the filter gives */

/* The answer's signature, 'BoeA' as the WDK's multi-character constant. */
#define CM_DSD_OUT_SIGNATURE    0x426F6541UL

#define CM_DSD_ARG_INTEGER      0x0UL
#define CM_DSD_ARG_STRING       0x1UL
#define CM_DSD_ARG_BUFFER       0x2UL
#define CM_DSD_ARG_PACKAGE      0x3UL

/* CmDsdRead's result. */
#define CM_DSD_OK               0UL /* a usable usb4-host-interface        */
#define CM_DSD_BAD_HEADER       1UL /* short, wrong signature or length    */
#define CM_DSD_BAD_SHAPE        2UL /* an argument outside the answer, or
                                       not the _DSD shape                  */
#define CM_DSD_NO_UUID          3UL /* no device-properties package        */
#define CM_DSD_NO_KEY           4UL /* no usb4-host-interface              */
#define CM_DSD_DUPLICATE        5UL /* a key twice, or the UUID twice      */
#define CM_DSD_BAD_REF          6UL /* the reference not a name string, or
                                       not a name this reader takes        */
#define CM_DSD_BAD_PORT         7UL /* usb4-port-number not an integer     */

typedef struct _CM_DSD {
    char Ref[CM_DSD_NAME_MAX + 1];  /* normalised, NUL terminated          */
    ULONG RefLen;
    ULONG HavePort;                 /* usb4-port-number was present        */
    ULONG Port;                     /* its value (log only; 0 allowed)     */
} CM_DSD, *PCM_DSD;

/* Reads `len` bytes of an ACPI_EVAL_OUTPUT_BUFFER answer to _DSD. */
ULONG CmDsdRead(const UCHAR *buf, ULONG len, PCM_DSD out);

/*
 * An ACPI name normalised: an optional leading backslash, then 1 to
 * CM_DSD_SEGMENTS_MAX segments of 1 to 4 characters from A-Z, 0-9 and _
 * (lower case taken as upper), separated by dots; written as "\SEG_.SEG_"
 * with every segment padded to four with '_' (ACPI's padding, so "\_SB"
 * and "\_SB_" are one name). `in` ends at `inLen` or at a NUL. Returns the
 * output length, 0 when the name is not one this takes (a carat, an empty
 * segment, a fifth character, room exhausted).
 */
ULONG CmDsdNormName(const char *in, ULONG inLen, char *out, ULONG cap);

/* The same over a UTF-16 string (DEVPKEY_Device_BiosDeviceName), any
 * character above 0x7F refused. `chars` excludes nothing; a NUL ends it. */
ULONG CmDsdNormNameW(const USHORT *in, ULONG chars, char *out, ULONG cap);

/* How many of `claims` current claims naming one NHI the ACPI-named
 * source takes: one, or none when there is not exactly one. Each AMD NHI
 * has one tunnel root port, so a firmware naming one NHI for two or more
 * ports is trusted for none of them (design record 22 section 7.6.1). */
ULONG CmDsdClaimsTaken(ULONG claims);

/* TRUE when a listed ACPI-named member added under `memberSerial` takes a
 * renewed claim made under `claimSerial`: its root port's filter was
 * restarted and read the _DSD again, the claim naming the same NHI and the
 * only one naming it (`sole`). The member adopts the new serial (design
 * record 22 section 7.6.1). */
BOOLEAN CmDsdRenewed(ULONG memberSerial, ULONG claimSerial, BOOLEAN sole);

/* TRUE when both normalised names are present and equal. */
BOOLEAN CmDsdSameName(const char *a, const char *b);

#endif /* CM_DSD_H */
