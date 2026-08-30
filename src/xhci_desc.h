/*
 * xhci_desc.h - the configuration-descriptor snoop (src/xhci_desc.c), roadmap
 * task 9-A.2.
 *
 * ## The question this exists to answer
 *
 * An isochronous Endpoint Context's Interval is `log2` of the endpoint's
 * service interval in 125 us units, and until this file existed this driver
 * **assumed** it: 0 on High Speed, 3 on Full Speed
 * (`XHCI_EP_INTERVAL_ISOCH_HS`/`_FS`), which is correct exactly when the
 * endpoint descriptor's `bInterval` is 1.
 *
 * That assumption is not an oversight and it is not recoverable from usbport.
 * usbport forces an isochronous `Period` to 1 and carries no interval at all;
 * the only statement it makes about the rate is how it stamps each packet -
 * `StartFrame + (i >> 3)` on High Speed, `StartFrame + i` otherwise
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 4, task 9-0.1) - and those stamps are a
 * fixed indexing of the URB written the same way whatever the descriptor says.
 * So `IsoCadenceMismatches`, which compares those stamps against the programmed
 * Interval, has the assumption on **both** sides and cannot see it: a
 * High-Speed endpoint with `bInterval = 4` - one ESIT per millisecond, a common
 * USB Audio shape - is serviced eight times too fast and agrees with itself
 * (batch 9-A review round 3, roadmap task 9-A.2).
 *
 * The one channel that carries `bInterval` is the **configuration descriptor**,
 * which crosses EP0 as a `GET_DESCRIPTOR(Configuration)` reply. Task 7b-A.1
 * already built the machinery to read a control reply's bytes - snoop the SETUP
 * on placement, read the SG list's `MappedSystemVa` in the completion drain
 * before `UsbPortCompleteTransfer` hands the mapping back - and this file is the
 * second consumer of it.
 *
 * ## What this file is, and what it deliberately is not
 *
 * It is the **walk and the table**: which isochronous endpoints the current
 * configuration declares, and what `bInterval` each one asked for. It decides
 * nothing - `XhciBuildEndpointParams` converts a recorded `bInterval` into an
 * Endpoint Context Interval (`XhciIsochIntervalFromBInterval`, Table 6-12), and
 * the endpoint open is what asks. Nothing here is specific to audio: the walk
 * would record any isochronous endpoint, and a device that declares none simply
 * commits an empty table.
 *
 * It is **not** a general descriptor cache. Interrupt endpoints have a
 * `bInterval` too and it is deliberately not recorded here: usbport *does*
 * convert theirs into `Period`, so the interrupt path has a channel already and
 * a second one would be two statements of one fact. Only the isochronous type -
 * the one usbport discards - is kept.
 *
 * ## Which descriptor is in force, which was the first review round's subject
 *
 * A descriptor says what a configuration and an alternate setting *would*
 * declare. What the device is actually running is said by two other requests on
 * the same pipe, and the first draft of this file recorded neither - it kept one
 * table for the last configuration walked and refused an endpoint whose
 * alternate settings disagreed, on the stated ground that the miniport cannot
 * tell which alternate is in force. **That ground was false**: `SET_INTERFACE`
 * crosses this very pipe carrying the interface in `wIndex` and the alternate in
 * `wValue`, and src/xhci_topo.c has snooped it since task 7b-A.1. So:
 *
 *   - `SET_CONFIGURATION` names the `bConfigurationValue` in force. A walked
 *     table that describes any other configuration is refused rather than
 *     committed, and a committed one is thrown away when the selection moves.
 *   - `SET_INTERFACE` names the alternate in force for one interface, and every
 *     recorded endpoint carries the interface and alternate that declared it -
 *     so alternate settings no longer collapse onto one entry and no longer have
 *     to be refused for disagreeing.
 *
 * Both are acted on at the transfer's **completion**, not at its placement: a
 * request that fails changed nothing on the device, and throwing away a correct
 * reading because a `SET_CONFIGURATION` stalled would put the endpoint back on
 * the assumption this file exists to remove.
 *
 * ## The declared limitation: one configuration is held, not several
 *
 * A device record keeps **one** walked table. usbport may read more than one of
 * a multi-configuration device's descriptors before it selects anything, and
 * the later read replaces the earlier - so if it then selects the *earlier*
 * one, the table is discarded (`DescConfigsSuperseded`) and every isochronous
 * endpoint of that device falls back to the assumption.
 *
 * That is a **lost reading and never a wrong one** - the two guards make sure of
 * it: a table for a configuration that is not selected is refused
 * (`DescConfigsInactive`), and a selected configuration the table does not
 * describe throws it away. What is not done is caching a table per
 * configuration, and the reason is not that it is hard: the state lives in the
 * miniport extension usbport allocates, one per device record, and this driver
 * has no private pool (AGENTS.md) - so the second table costs every device on
 * the bus whether or not any of them has two configurations. Nothing in this
 * project's reachable device population has been *measured* to have more than
 * one, `DescConfigsSuperseded` is what would say otherwise on a target, and
 * that reading is the lever if it ever does.
 *
 * It is **pure core** in the design doc 03 section 2 sense: computation over
 * caller-supplied bytes, no MMIO, no DDK, no IRQL, no usbport service, no lock.
 * Like src/xhci_topo.c it speaks the usbport transfer ABI, so src/xhci_desc.c
 * includes src/xhci_usbport.h; the setup packet reaches this header through a
 * forward declaration only.
 *
 * It never dereferences a pointer usbport owns. The reply's bytes are fed in by
 * the caller, in chunks of whatever size the caller finds convenient, and the
 * walk is a byte-level state machine for exactly that reason - a configuration
 * descriptor is up to 65,535 bytes and this driver has neither a private pool to
 * copy it into nor a DISPATCH_LEVEL stack to spare (AGENTS.md). **Feeding the
 * same bytes in different chunk sizes must produce the same table**, which is a
 * property the host suite checks rather than a claim.
 *
 * ## Where the numbers come from
 *
 * Every offset and constant below is the Windows 2000 DDK's own
 * `C:\NTDDK\inc\usb100.h` (inside `PSHPACK1.H`, so the layouts are packed):
 * `USB_CONFIGURATION_DESCRIPTOR` (`bLength`, `bDescriptorType`, `wTotalLength`
 * at 2:3), `USB_ENDPOINT_DESCRIPTOR` (`bEndpointAddress` at 2, `bmAttributes` at
 * 3, `bInterval` at 6), `USB_CONFIGURATION_DESCRIPTOR_TYPE` = 2,
 * `USB_ENDPOINT_DESCRIPTOR_TYPE` = 5, `USB_ENDPOINT_TYPE_MASK` = 3 and
 * `USB_ENDPOINT_TYPE_ISOCHRONOUS` = 1. They are transcribed rather than included
 * for the reason every other constant in the pure core is: the host suite
 * compiles these files with no DDK at all.
 *
 * The legal range of `bInterval` for an isochronous endpoint is 1..16, from the
 * conversion table in docs/usb-xhci-info/xhci-data-structures.md ("Interval conversion", spec
 * 6.2.3.6 Table 6-12): High Speed maps it to `bInterval - 1` with a valid range
 * of 0-15 and Full Speed to `bInterval + 2` with 3-18, and both of those ranges
 * are exactly the image of 1..16.
 *
 * C89 only. Every function is callable at any IRQL.
 */

#ifndef XHCI_DESC_H
#define XHCI_DESC_H

/*
 * Only the compat shim, for the same reason src/xhci_topo.h takes only that:
 * XHCI_DEVICE embeds XHCI_DESC_ISO_TABLE, so src/xhci.h includes this header,
 * which means nothing here may include xhci.h back and the usbport ABI must
 * stay out of it.
 */
#include "xhci_compat.h"

struct _XHCI_SETUP_PACKET;

/* ------------------------------------------------------------------ */
/* The wire vocabulary                                                 */
/* ------------------------------------------------------------------ */

/*
 * `bmRequestType` whole, and the recipient half is load-bearing: a
 * *device*-recipient standard IN request is the only shape a configuration
 * descriptor is asked for in. src/xhci_topo.h records the same lesson from the
 * other direction - batch 6-V's classifier read eleven "hub-class setups" on a
 * bus with no hub on it because it matched the type and ignored the recipient.
 */
#define XHCI_DESC_RT_STANDARD_IN    0x80U   /* IN | standard | device recipient */

/*
 * ...and the OUT request that says which configuration is in force, whose
 * recipient is the device as well. A configuration descriptor is only worth
 * anything if the device is running the configuration it describes.
 */
#define XHCI_DESC_RT_STANDARD_OUT   0x00U   /* OUT | standard | device recipient */

/* bRequest, from C:\NTDDK\inc\usb100.h (USB_REQUEST_*). */
#define XHCI_DESC_REQ_GET_DESCRIPTOR 0x06U
#define XHCI_DESC_REQ_SET_CONFIGURATION 0x09U

/*
 * ...and the one that says which alternate setting of an interface is in force.
 * `bmRequestType` 0x01 is OUT | standard | **interface** recipient - the same
 * value src/xhci_topo.h names `XHCI_TOPO_RT_SET_INTERFACE`, for the same
 * request read for a different reason.
 */
#define XHCI_DESC_RT_STANDARD_IFACE 0x01U
#define XHCI_DESC_REQ_SET_INTERFACE 0x0BU

/* Descriptor types, and the three this walk knows by name. */
#define XHCI_DESC_TYPE_CONFIGURATION 0x02U
#define XHCI_DESC_TYPE_INTERFACE     0x04U
#define XHCI_DESC_TYPE_ENDPOINT      0x05U

/* bmAttributes bits 1:0 of an endpoint descriptor. */
#define XHCI_DESC_EP_TYPE_MASK      0x03U
#define XHCI_DESC_EP_TYPE_ISOCH     0x01U

/*
 * Offsets inside a descriptor, from the DDK's packed structures. `bLength` and
 * `bDescriptorType` are common to every descriptor; the rest belong to the two
 * types above.
 */
#define XHCI_DESC_OFF_LENGTH        0U
#define XHCI_DESC_OFF_TYPE          1U
#define XHCI_DESC_OFF_TOTAL_LO      2U      /* configuration: wTotalLength */
#define XHCI_DESC_OFF_TOTAL_HI      3U
#define XHCI_DESC_OFF_CONFIG_VALUE  5U      /* configuration: bConfigurationValue */
#define XHCI_DESC_OFF_IF_NUMBER     2U      /* interface: bInterfaceNumber */
#define XHCI_DESC_OFF_IF_ALTERNATE  3U      /* interface: bAlternateSetting */
#define XHCI_DESC_OFF_EP_ADDRESS    2U      /* endpoint: bEndpointAddress  */
#define XHCI_DESC_OFF_EP_ATTRIBUTES 3U
#define XHCI_DESC_OFF_EP_INTERVAL   6U

/*
 * The smallest descriptor that can exist at all (`bLength` and
 * `bDescriptorType`), and the two the walk needs whole. A `bLength` below the
 * first would never advance the walk, which is why it is a refusal rather than a
 * skip: a zero-length descriptor is an infinite loop, not a lost reading.
 */
#define XHCI_DESC_MIN_BYTES         2U
#define XHCI_DESC_CONFIG_BYTES      9U
#define XHCI_DESC_INTERFACE_BYTES   9U
#define XHCI_DESC_ENDPOINT_BYTES    7U

/*
 * How much of each descriptor the walk keeps. Nine bytes covers the
 * configuration header, whose `wTotalLength` is the whole point, and an endpoint
 * descriptor's seven with room to spare - an audio-class endpoint descriptor is
 * nine bytes and its two extra fields are not read here. Everything past this is
 * counted through and discarded, which is what makes a class-specific descriptor
 * of any size cost nothing.
 */
#define XHCI_DESC_HEAD_BYTES        9U

/*
 * The legal `bInterval` for an isochronous endpoint, both ends inclusive - see
 * the file header. A descriptor outside it is recorded as malformed rather than
 * clamped: clamping would program a cadence the device never asked for, which is
 * the defect this file exists to remove.
 */
#define XHCI_DESC_BINTERVAL_MIN     1U
#define XHCI_DESC_BINTERVAL_MAX     16U

/* ------------------------------------------------------------------ */
/* Shape                                                               */
/* ------------------------------------------------------------------ */

/*
 * How many isochronous endpoint *declarations* of one device are kept, and how
 * many interfaces its alternate selections are tracked for.
 *
 * The entries are per (interface, alternate, endpoint address) rather than per
 * address, because that is what a descriptor actually declares: a USB Audio
 * streaming interface typically has a zero-bandwidth alternate 0 and two or
 * three working alternates, each declaring the same endpoint address, and they
 * may differ in `bInterval`. Eight covers that shape with a feedback endpoint
 * and room; `Dropped` is what says a ninth was met - a full table silently
 * forgetting one is the failure mode this project has paid for three times
 * (batch 6-0's `PassThru`, batch 7b-A.1.0's accepted open, the topology graph's
 * own node table).
 */
#define XHCI_DESC_ISO_ENDPOINTS     8
#define XHCI_DESC_INTERFACES        4

typedef struct _XHCI_DESC_ISO_EP {
    /*
     * `bEndpointAddress` whole, direction bit included, because that is the
     * form `USBPORT_ENDPOINT_PROPERTIES.EndpointAddress` arrives in. 0 marks a
     * free entry and cannot collide: address 0 is the default control endpoint,
     * which is never isochronous, so an isochronous descriptor claiming it is a
     * malformed descriptor and is refused as one.
     */
    ULONG Address;
    ULONG BInterval;            /* XHCI_DESC_BINTERVAL_MIN..MAX */
    /* The interface descriptor this declaration sat under - `bInterfaceNumber`
     * and `bAlternateSetting`. An endpoint descriptor outside any interface is
     * refused rather than filed under a guess. */
    ULONG Interface;
    ULONG Alternate;
    /*
     * One interface *and one alternate setting* declared this endpoint address
     * twice, with different cadences. No selection can choose between them -
     * they are the same interface in the same setting - so the entry is
     * unusable rather than answered from whichever arrived first. A conforming
     * descriptor does not do this; one that does gets the fallback, not a
     * confident wrong answer.
     */
    ULONG Unusable;
} XHCI_DESC_ISO_EP;

typedef struct _XHCI_DESC_ISO_TABLE {
    /*
     * A **whole** configuration has been walked into this table.
     *
     * usbport reads a configuration descriptor twice - a short probe for
     * `wTotalLength`, then the full thing - so a partial reply is the ordinary
     * first half of every enumeration and not an error. Only a complete walk
     * commits, because a truncated one would say "this device has no
     * isochronous endpoint past byte 64" and be believed.
     */
    ULONG Valid;
    /*
     * `bConfigurationValue` of the configuration this table describes - offset
     * 5 of the configuration descriptor, which is the number SET_CONFIGURATION
     * names. It is not the index the GET_DESCRIPTOR asked by, and the two are
     * unrelated.
     */
    ULONG ConfigValue;
    ULONG Count;
    ULONG Dropped;              /* declarations past XHCI_DESC_ISO_ENDPOINTS */
    ULONG BadInterval;          /* a `bInterval` no Interval can be made of */
    /* ...and every other reason a declaration was refused: an endpoint
     * descriptor too short to carry `bInterval`, one outside any interface, one
     * claiming endpoint address 0, a duplicate within one alternate, an
     * interface descriptor too short to name itself. Counted apart from the
     * line above because one says the device asked for a cadence that does not
     * exist and the other says the bytes did not parse. */
    ULONG BadDescriptor;
    XHCI_DESC_ISO_EP Endpoint[XHCI_DESC_ISO_ENDPOINTS];
} XHCI_DESC_ISO_TABLE, *PXHCI_DESC_ISO_TABLE;

/* One interface's currently selected alternate setting. */
typedef struct _XHCI_DESC_ALT {
    ULONG Used;
    ULONG Interface;
    ULONG Alternate;
} XHCI_DESC_ALT;

/*
 * Everything one device record knows: what its configuration descriptor
 * declared, and what the device was last told to run.
 *
 * The two are kept apart because they arrive through different requests and
 * have different lifetimes - a commit replaces the whole table and must not
 * disturb a selection, and a selection must survive a re-read of the same
 * configuration. A single structure got this wrong in the first draft.
 */
typedef struct _XHCI_DESC_STATE {
    XHCI_DESC_ISO_TABLE Table;
    /*
     * `bConfigurationValue` the device was last told to run, or 0 for "no
     * SET_CONFIGURATION has been observed". 0 is not ambiguous here:
     * SET_CONFIGURATION(0) *unconfigures* a device, which has no endpoints to
     * program, so both readings of 0 refuse the same things.
     */
    ULONG SelectedConfig;
    ULONG AltDropped;           /* interfaces past XHCI_DESC_INTERFACES */
    XHCI_DESC_ALT Alt[XHCI_DESC_INTERFACES];
} XHCI_DESC_STATE, *PXHCI_DESC_STATE;

/* ------------------------------------------------------------------ */
/* What one setup packet asks the caller to do about its reply         */
/* ------------------------------------------------------------------ */

/*
 * What one setup packet asks of this file. Three of the four do something: one
 * carries a reply worth walking, and two say what the device is now running.
 *
 * All three are carried on the transfer record and acted on at its
 * **completion**, because a request that failed changed nothing on the device.
 */
#define XHCI_DESC_ACT_NONE              0
#define XHCI_DESC_ACT_CONFIG_REPLY      1   /* GET_DESCRIPTOR(Configuration) */
#define XHCI_DESC_ACT_SELECT_CONFIG     2   /* SET_CONFIGURATION */
#define XHCI_DESC_ACT_SELECT_INTERFACE  3   /* SET_INTERFACE */

typedef struct _XHCI_DESC_SNOOP {
    ULONG Action;               /* XHCI_DESC_ACT_* */
    ULONG Address;              /* the device the request went to */
    /*
     * SELECT_CONFIG: `Value` is the `bConfigurationValue`.
     * SELECT_INTERFACE: `Index` is the interface and `Value` the alternate.
     * Unread for the other two.
     */
    ULONG Value;
    ULONG Index;
} XHCI_DESC_SNOOP, *PXHCI_DESC_SNOOP;

/* ------------------------------------------------------------------ */
/* The streaming walk                                                  */
/* ------------------------------------------------------------------ */

/*
 * One reply's walk. Lives on the caller's stack for the length of one fold: the
 * whole reply is readable at that moment, so there is no state to carry between
 * completions and none is kept.
 */
typedef struct _XHCI_DESC_PARSE {
    ULONG Bad;                  /* the walk stopped making sense */
    ULONG Started;              /* the configuration header was read */
    ULONG TotalLength;          /* wTotalLength, once Started */
    ULONG Received;             /* bytes fed, capped at TotalLength */
    ULONG Length;               /* bLength of the descriptor being collected */
    ULONG Pos;                  /* how much of it has arrived */
    /* The interface descriptor every endpoint descriptor after it belongs to
     * (USB 9.6.5/9.6.6). `InInterface` is 0 before the first one and after one
     * too short to name itself, which makes an endpoint there a refusal rather
     * than a declaration filed under the previous interface. */
    ULONG InInterface;
    ULONG Interface;
    ULONG Alternate;
    UCHAR Head[XHCI_DESC_HEAD_BYTES];
    XHCI_DESC_ISO_TABLE Table;
} XHCI_DESC_PARSE, *PXHCI_DESC_PARSE;

/*
 * What a finished walk was. A checked partition - the caller counts all three
 * and their sum is the number of replies folded, which is how a channel that
 * quietly stops working is told apart from a bus with no isochronous device on
 * it (batch 7b-A.1.0's net, a third time).
 */
#define XHCI_DESC_FOLD_COMMIT       0   /* a whole configuration; use the table */
#define XHCI_DESC_FOLD_PARTIAL      1   /* fewer bytes than wTotalLength arrived */
#define XHCI_DESC_FOLD_MALFORMED    2   /* the descriptors did not fit their own lengths */

/*
 * The chunk size the driver's fold happens to use. **Nothing here depends on
 * it** - the walk is a byte-level state machine and the host suite feeds the
 * same descriptors at every size from 1 upwards to prove it - so this is a stack
 * budget and not a contract: 32 bytes at DISPATCH_LEVEL, under usbport's frames,
 * to read a descriptor that may be 65,535.
 */
#define XHCI_DESC_FOLD_CHUNK        32U

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/*
 * Observe one EP0 SETUP packet sent to a device whose usbport address is
 * `address`, and say what its completion should do about it.
 *
 * Answers XHCI_DESC_ACT_NONE for a NULL argument, for address 0 (an unaddressed
 * device has no record to key a table on) and for every request that is none of
 * the three above.
 */
VOID XhciDescObserveSetup(ULONG address,
                          const struct _XHCI_SETUP_PACKET *setup,
                          PXHCI_DESC_SNOOP snoop);

/* Start a walk. */
VOID XhciDescParseBegin(PXHCI_DESC_PARSE parse);

/*
 * Feed the next `length` bytes of the reply, in order. Any chunking is legal and
 * every chunking gives the same result; bytes past `wTotalLength` are ignored,
 * as is everything after the walk has gone bad.
 */
VOID XhciDescParseFeed(PXHCI_DESC_PARSE parse,
                       const UCHAR *data,
                       ULONG length);

/*
 * Finish the walk and return XHCI_DESC_FOLD_*. On XHCI_DESC_FOLD_COMMIT the
 * table in `parse->Table` is marked valid and is the one to keep; on anything
 * else the caller keeps whatever it already had.
 */
ULONG XhciDescParseEnd(PXHCI_DESC_PARSE parse);

/* Empty a walked table. */
VOID XhciDescTableReset(PXHCI_DESC_ISO_TABLE table);

/*
 * Forget everything known about a device - its descriptor **and** what it was
 * told to run. Called where a record begins a new enumeration, because a
 * configuration descriptor describes the device that answered it and the device
 * on that port may not be the same one.
 */
VOID XhciDescStateReset(PXHCI_DESC_STATE state);

/*
 * Install a walked table, if it describes the configuration the device is
 * running. Returns XHCI_DESC_COMMIT_*.
 *
 * A table for any other configuration is **refused**, not installed: usbport
 * may read more than one of a multi-configuration device's descriptors, and the
 * last one walked is not the one in force. Where no SET_CONFIGURATION has been
 * observed yet - the ordinary enumeration order, since the descriptors are read
 * before the device is configured - the table is installed and the selection
 * that follows is what validates it.
 */
#define XHCI_DESC_COMMIT_OK         0
#define XHCI_DESC_COMMIT_INACTIVE   1
#define XHCI_DESC_COMMIT_BAD_PARAM  2
ULONG XhciDescCommit(PXHCI_DESC_STATE state, const XHCI_DESC_ISO_TABLE *table);

/*
 * A SET_CONFIGURATION selected `configValue`, and it **completed** - see the
 * file header on why a placement is not enough. Keeps a table that describes
 * that configuration and throws away one that describes any other, returning 1
 * when it threw one away. Every interface goes back to alternate 0, which is
 * what a SET_CONFIGURATION does to the device.
 */
ULONG XhciDescSelectConfig(PXHCI_DESC_STATE state, ULONG configValue);

/*
 * A SET_INTERFACE selected `alternate` on interface `interfaceNumber`, and it
 * completed. This is what makes alternate settings resolvable rather than a
 * conflict to be refused.
 */
VOID XhciDescSelectInterface(PXHCI_DESC_STATE state,
                             ULONG interfaceNumber,
                             ULONG alternate);

/*
 * The `bInterval` this device declared for the isochronous endpoint at
 * `endpointAddress`, given the alternate settings it is running. Returns 1 when
 * `*bInterval` was written.
 *
 * The resolution is in two passes, and the second is what keeps this robust to
 * an ordering the repository has not established - whether usbport's pipe
 * reopen precedes or follows its `SET_INTERFACE`:
 *
 *   1. A declaration under an interface's *selected* alternate wins. An
 *      interface no SET_INTERFACE has named is running alternate 0, which is
 *      what a SET_CONFIGURATION leaves it in.
 *   2. Failing that, every declaration of this address is consulted, and they
 *      answer only if they agree. This is the case where the endpoint exists
 *      only in an alternate that has not been selected yet.
 *
 * A disagreement that neither pass resolves answers 0, and the endpoint falls
 * back to the cadence usbport is scheduling to. Ask `XhciDescIsoDeclared` to
 * tell that apart from "this device declared no such endpoint at all" - they
 * are different diagnoses and the caller counts them apart.
 */
ULONG XhciDescIsoInterval(const XHCI_DESC_STATE *state,
                          ULONG endpointAddress,
                          ULONG *bInterval);

/* Does the committed table declare this endpoint address at all? */
ULONG XhciDescIsoDeclared(const XHCI_DESC_STATE *state, ULONG endpointAddress);

/* Every offset the walk indexes must sit inside the head it keeps. */
XHCI_C_ASSERT(desc_config_head_fits, XHCI_DESC_OFF_TOTAL_HI < XHCI_DESC_HEAD_BYTES);
XHCI_C_ASSERT(desc_endpoint_head_fits,
              XHCI_DESC_OFF_EP_INTERVAL < XHCI_DESC_HEAD_BYTES);
/* ...and a descriptor the walk reads whole must not be longer than that head. */
XHCI_C_ASSERT(desc_config_bytes_fit, XHCI_DESC_CONFIG_BYTES <= XHCI_DESC_HEAD_BYTES);
XHCI_C_ASSERT(desc_endpoint_bytes_fit,
              XHCI_DESC_ENDPOINT_BYTES <= XHCI_DESC_HEAD_BYTES);
XHCI_C_ASSERT(desc_interface_head_fits,
              XHCI_DESC_OFF_IF_ALTERNATE < XHCI_DESC_HEAD_BYTES);
XHCI_C_ASSERT(desc_config_value_fits,
              XHCI_DESC_OFF_CONFIG_VALUE < XHCI_DESC_HEAD_BYTES);

#endif /* XHCI_DESC_H */
