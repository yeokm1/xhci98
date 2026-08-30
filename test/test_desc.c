/*
 * test_desc.c - the configuration-descriptor snoop (src/xhci_desc.c) and the
 * Table 6-12 conversion it feeds (src/xhci_ctx.c), roadmap task 9-A.2.
 *
 * Two halves of one subject, which is why the suite links both files: the walk
 * recovers an isochronous endpoint's `bInterval` and the context builder is what
 * turns it into the number the hardware sees. A suite that stopped at the table
 * would have tested a lookup and left the arithmetic - the half a wrong sign
 * lives in - to a VM run.
 *
 * Descriptors here are hand-built byte for byte, and the expected Intervals are
 * hand-computed and typed out rather than recomputed through the same expression
 * the code uses - test_ctx.c's rule, because a test that re-derives the answer
 * only checks that the code is consistent with itself.
 *
 * The chunking sweep at the end is the one property the driver's caller depends
 * on and cannot check for itself: the fold reads the reply through a 32-byte
 * window off usbport's mapping, so **feeding the same descriptor at every chunk
 * size from 1 upwards must produce the same table**. A state machine that
 * happened to work only when a descriptor did not straddle a chunk boundary
 * would pass every other vector in this file.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_usbport.h"
#include "../src/xhci_desc.h"

static int failures;
static int checks;

#define CHECK(cond, what) check_impl((cond) ? 1 : 0, (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s:%d: %s\n", "test_desc.c", line, what);
    }
}

#define CHECK_EQ(got, want, what) \
    check_eq_impl((unsigned long)(got), (unsigned long)(want), (what), __LINE__)

static void check_eq_impl(unsigned long got, unsigned long want,
                          const char *what, int line)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s:%d: %s (got 0x%08lX, want 0x%08lX)\n",
               "test_desc.c", line, what, got, want);
    }
}

/* ------------------------------------------------------------------ */
/* Wire helpers                                                        */
/* ------------------------------------------------------------------ */

static XHCI_SETUP_PACKET setupOf(UCHAR type, UCHAR request, USHORT value,
                                 USHORT index, USHORT length)
{
    XHCI_SETUP_PACKET s;

    s.bmRequestType = type;
    s.bRequest = request;
    s.wValue = value;
    s.wIndex = index;
    s.wLength = length;
    return s;
}

/* GET_DESCRIPTOR(Configuration, index), as usbport sends it. */
static XHCI_SETUP_PACKET getConfig(UCHAR index, USHORT length)
{
    return setupOf(0x80, 0x06, (USHORT)(0x0200 | index), 0, length);
}

/*
 * A configuration descriptor built from parts, so every vector states its own
 * bytes. `wTotalLength` is filled in from what was appended, because a vector
 * that had to hand-count it would be testing the vector.
 */
#define DESC_MAX 256

typedef struct {
    UCHAR bytes[DESC_MAX];
    ULONG length;
} DESCRIPTOR;

static void descBegin(DESCRIPTOR *d)
{
    ULONG i;

    for (i = 0; i < DESC_MAX; i++) {
        d->bytes[i] = 0;
    }
    d->length = 9;
    d->bytes[0] = 9;            /* bLength */
    d->bytes[1] = 0x02;         /* CONFIGURATION */
    d->bytes[4] = 1;            /* bNumInterfaces */
    d->bytes[5] = 1;            /* bConfigurationValue */
}

static void descAppend(DESCRIPTOR *d, const UCHAR *bytes, ULONG count)
{
    ULONG i;

    /* A vector whose descriptor outgrew the buffer would be silently
     * truncated and would then test the truncation rather than what it
     * says it tests - which cost one debugging round already. */
    CHECK(d->length + count <= DESC_MAX, "the vector fits its buffer");
    for (i = 0; i < count && d->length < DESC_MAX; i++) {
        d->bytes[d->length++] = bytes[i];
    }
}

/* Close it out: wTotalLength is what was actually appended. */
static void descEnd(DESCRIPTOR *d)
{
    d->bytes[2] = (UCHAR)(d->length & 0xFF);
    d->bytes[3] = (UCHAR)((d->length >> 8) & 0xFF);
}

static void descInterface(DESCRIPTOR *d, UCHAR number, UCHAR alternate,
                          UCHAR endpoints)
{
    UCHAR iface[9];

    iface[0] = 9;
    iface[1] = 0x04;            /* INTERFACE */
    iface[2] = number;
    iface[3] = alternate;
    iface[4] = endpoints;
    iface[5] = 0x01;            /* Audio */
    iface[6] = 0x02;            /* AudioStreaming */
    iface[7] = 0x00;
    iface[8] = 0x00;
    descAppend(d, iface, 9);
}

/*
 * A USB Audio isochronous endpoint descriptor: nine bytes, not seven, because
 * the audio class adds bRefresh and bSynchAddress - which is exactly the shape
 * that would break a walk assuming every endpoint descriptor is seven bytes.
 */
static void descIsoEndpoint(DESCRIPTOR *d, UCHAR address, ULONG mps,
                            UCHAR bInterval)
{
    UCHAR ep[9];

    ep[0] = 9;
    ep[1] = 0x05;               /* ENDPOINT */
    ep[2] = address;
    ep[3] = 0x0D;               /* isochronous | synchronous | data */
    ep[4] = (UCHAR)(mps & 0xFF);
    ep[5] = (UCHAR)((mps >> 8) & 0xFF);
    ep[6] = bInterval;
    ep[7] = 0;
    ep[8] = 0;
    descAppend(d, ep, 9);
}

static void descInterruptEndpoint(DESCRIPTOR *d, UCHAR address, ULONG mps,
                                  UCHAR bInterval)
{
    UCHAR ep[7];

    ep[0] = 7;
    ep[1] = 0x05;               /* ENDPOINT */
    ep[2] = address;
    ep[3] = 0x03;               /* interrupt */
    ep[4] = (UCHAR)(mps & 0xFF);
    ep[5] = (UCHAR)((mps >> 8) & 0xFF);
    ep[6] = bInterval;
    descAppend(d, ep, 7);
}

/* A class-specific descriptor of an unknown type - the thing a walk has to
 * count through rather than understand. */
static void descClassBlob(DESCRIPTOR *d, UCHAR length)
{
    UCHAR blob[64];
    ULONG i;

    for (i = 0; i < sizeof(blob); i++) {
        blob[i] = 0xAA;
    }
    blob[0] = length;
    blob[1] = 0x24;             /* CS_INTERFACE */
    descAppend(d, blob, length);
}

/* Walk a whole descriptor in one feed. */
static ULONG descWalk(PXHCI_DESC_PARSE parse, const DESCRIPTOR *d)
{
    XhciDescParseBegin(parse);
    XhciDescParseFeed(parse, d->bytes, d->length);
    return XhciDescParseEnd(parse);
}

/* ...and in chunks of exactly `chunk` bytes, which must give the same answer. */
static ULONG descWalkChunked(PXHCI_DESC_PARSE parse, const DESCRIPTOR *d,
                             ULONG chunk)
{
    ULONG offset;
    ULONG take;

    XhciDescParseBegin(parse);
    for (offset = 0; offset < d->length; offset += take) {
        take = d->length - offset;
        if (take > chunk) {
            take = chunk;
        }
        XhciDescParseFeed(parse, &d->bytes[offset], take);
    }
    return XhciDescParseEnd(parse);
}

/*
 * Most vectors below care only about what a walk *recorded*, so they install
 * the walked table into a fresh state and ask that. The vectors that are about
 * the selection machinery drive `descState` directly instead.
 */
static XHCI_DESC_STATE descState;

static ULONG stateInterval(const XHCI_DESC_PARSE *parse, ULONG address,
                           ULONG *value)
{
    XhciDescStateReset(&descState);
    if (XhciDescCommit(&descState, &parse->Table) != XHCI_DESC_COMMIT_OK) {
        return 0;
    }
    return XhciDescIsoInterval(&descState, address, value);
}

/* ------------------------------------------------------------------ */
/* The snoop                                                           */
/* ------------------------------------------------------------------ */

static void testSnoopSelection(void)
{
    XHCI_DESC_SNOOP snoop;
    XHCI_SETUP_PACKET s;

    s = getConfig(0, 9);
    XhciDescObserveSetup(3, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_CONFIG_REPLY,
             "GET_DESCRIPTOR(Configuration) is worth capturing");
    CHECK_EQ(snoop.Address, 3, "and it names the device it went to");

    s = getConfig(2, 64);
    XhciDescObserveSetup(3, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_CONFIG_REPLY,
             "a request for another configuration index is captured too - the "
             "index is not the key, bConfigurationValue is");

    /* SET_CONFIGURATION, the other half: no reply to read, but it says which
     * configuration the device is running. */
    s = setupOf(0x00, 0x09, 0x0002, 0, 0);
    XhciDescObserveSetup(3, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_SELECT_CONFIG,
             "SET_CONFIGURATION is a selection, not a reply");
    CHECK_EQ(snoop.Value, 2, "of bConfigurationValue 2");

    /* SET_CONFIGURATION(0) is an unconfigure and is a selection like any
     * other. */
    s = setupOf(0x00, 0x09, 0x0000, 0, 0);
    XhciDescObserveSetup(3, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_SELECT_CONFIG,
             "unconfiguring is a selection too");
    CHECK_EQ(snoop.Value, 0, "of configuration 0");

    /* SET_INTERFACE, which src/xhci_topo.c watches for multi-TT and this
     * file watches to know which of an interface's endpoint declarations is
     * the one usbport is about to open. */
    s = setupOf(0x01, 0x0B, 0x0002, 0x0001, 0);
    XhciDescObserveSetup(3, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_SELECT_INTERFACE,
             "SET_INTERFACE is an alternate-setting selection");
    CHECK_EQ(snoop.Index, 1, "wIndex is the interface");
    CHECK_EQ(snoop.Value, 2, "and wValue the alternate setting");

    /*
     * Address 0 is skipped for the reason the topology snoop skips it: the
     * table lives on a device record and an unaddressed device has none to key
     * it on. Nothing is lost - usbport reads a configuration descriptor only
     * after SET_ADDRESS.
     */
    s = getConfig(0, 9);
    XhciDescObserveSetup(0, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_NONE,
             "an address-0 request is not captured");

    /* GET_DESCRIPTOR(Device) - the request that shares the pipe, the request
     * type and the bRequest, and differs only in wValue's high byte. */
    s = setupOf(0x80, 0x06, 0x0100, 0, 18);
    XhciDescObserveSetup(3, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_NONE,
             "GET_DESCRIPTOR(Device) is a different descriptor type");

    /* GET_DESCRIPTOR(String) likewise. */
    s = setupOf(0x80, 0x06, 0x0302, 0x0409, 255);
    XhciDescObserveSetup(3, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_NONE, "nor a string descriptor");

    /*
     * The hub descriptor request src/xhci_topo.c snoops, which is class-type
     * with the *same* bRequest and, as both shipping hub drivers send it,
     * `wValue = 0x0000` - so a walk keyed on bRequest alone would have read a
     * hub descriptor as a configuration.
     */
    s = setupOf(0xA0, 0x06, 0x0000, 0, 71);
    XhciDescObserveSetup(3, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_NONE,
             "a class-type GET_DESCRIPTOR is the hub request, not this one");

    /* An OUT request with the same bRequest byte pattern. */
    s = setupOf(0x00, 0x06, 0x0200, 0, 0);
    XhciDescObserveSetup(3, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_NONE, "and not an OUT request");

    /* A NULL packet answers "nothing", it does not fault. */
    XhciDescObserveSetup(3, NULL, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_NONE, "a NULL setup captures nothing");

    /* ...and neither does an address-0 SET_CONFIGURATION, which cannot happen -
     * a device is configured only after it has an address - and is refused by
     * the same gate for the same reason. */
    s = setupOf(0x00, 0x09, 0x0001, 0, 0);
    XhciDescObserveSetup(0, &s, &snoop);
    CHECK_EQ(snoop.Action, XHCI_DESC_ACT_NONE,
             "an address-0 selection is not observed");
}

/* ------------------------------------------------------------------ */
/* The walk                                                            */
/* ------------------------------------------------------------------ */

static void testAudioConfiguration(void)
{
    XHCI_DESC_PARSE parse;
    DESCRIPTOR d;
    ULONG value;

    /*
     * The shape of a real USB Audio device: a control interface, a streaming
     * interface with a zero-bandwidth alternate 0 and a working alternate 1
     * carrying one isochronous endpoint, and class-specific descriptors between
     * them that the walk must count through without understanding.
     */
    descBegin(&d);
    descInterface(&d, 0, 0, 0);
    descClassBlob(&d, 10);
    descInterface(&d, 1, 0, 0);
    descInterface(&d, 1, 1, 1);
    descClassBlob(&d, 7);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descEnd(&d);

    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT,
             "a complete configuration commits");
    CHECK_EQ(parse.Table.Valid, 1, "and the table says so");
    CHECK_EQ(parse.Table.Count, 1, "one isochronous endpoint recorded");
    CHECK_EQ(parse.Table.Dropped, 0, "nothing dropped");
    CHECK_EQ(parse.Table.BadInterval, 0, "no bInterval refused");
    CHECK_EQ(parse.Table.BadDescriptor, 0, "and nothing that would not parse");
    CHECK_EQ(stateInterval(&parse, 0x01, &value), 1,
             "the endpoint is found by its address");
    CHECK_EQ(value, 4, "and carries the bInterval the descriptor declared");

    /* An address that is not in the configuration, and the IN counterpart of
     * one that is - the direction bit is part of the key. */
    CHECK_EQ(stateInterval(&parse, 0x02, &value), 0,
             "an endpoint the configuration does not declare is not answered");
    CHECK_EQ(stateInterval(&parse, 0x81, &value), 0,
             "and 0x81 is a different endpoint from 0x01");
}

static void testOnlyIsochronousIsKept(void)
{
    XHCI_DESC_PARSE parse;
    DESCRIPTOR d;
    ULONG value;

    /*
     * Interrupt endpoints have a `bInterval` too and are deliberately not
     * recorded: usbport converts theirs into `Period`, so the interrupt path
     * has a channel already and a second one would be two statements of one
     * fact.
     */
    descBegin(&d);
    descInterface(&d, 0, 0, 2);
    descInterruptEndpoint(&d, 0x81, 8, 10);
    descIsoEndpoint(&d, 0x02, 192, 1);
    descEnd(&d);

    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.Count, 1, "only the isochronous endpoint is kept");
    CHECK_EQ(stateInterval(&parse, 0x81, &value), 0,
             "an interrupt endpoint is not in the table");
    CHECK_EQ(stateInterval(&parse, 0x02, &value), 1,
             "the isochronous one is");
    CHECK_EQ(value, 1, "bInterval 1");
}

static void testAlternateSettings(void)
{
    XHCI_DESC_PARSE parse;
    DESCRIPTOR d;
    ULONG value;

    /*
     * Three alternates of one interface declaring the same endpoint at the same
     * cadence and different packet sizes - the ordinary audio shape. Three
     * *declarations*, because that is what the descriptor contains, and they
     * answer the same thing however the interface is set.
     */
    descBegin(&d);
    descInterface(&d, 1, 0, 0);
    descInterface(&d, 1, 1, 1);
    descIsoEndpoint(&d, 0x01, 96, 4);
    descInterface(&d, 1, 2, 1);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descInterface(&d, 1, 3, 1);
    descIsoEndpoint(&d, 0x01, 288, 4);
    descEnd(&d);

    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.Count, 3, "one declaration per alternate setting");
    CHECK_EQ(parse.Table.Endpoint[0].Interface, 1, "under interface 1");
    CHECK_EQ(parse.Table.Endpoint[0].Alternate, 1, "alternate 1");
    CHECK_EQ(parse.Table.Endpoint[2].Alternate, 3, "...through alternate 3");
    CHECK_EQ(stateInterval(&parse, 0x01, &value), 1, "answered");
    CHECK_EQ(value, 4, "with the cadence they all declared");

    /*
     * **The same endpoint declared at different cadences, which the first draft
     * of this file refused on the stated ground that the miniport cannot tell
     * which alternate is in force.** It can: `SET_INTERFACE` crosses the same
     * pipe, and src/xhci_topo.c has snooped it since task 7b-A.1. So the
     * selection decides, and each alternate gets the cadence it declared.
     */
    descBegin(&d);
    descInterface(&d, 1, 1, 1);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descInterface(&d, 1, 2, 1);
    descIsoEndpoint(&d, 0x01, 192, 1);
    descEnd(&d);

    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "still committed");
    CHECK_EQ(parse.Table.Count, 2, "two declarations");
    XhciDescStateReset(&descState);
    CHECK_EQ(XhciDescCommit(&descState, &parse.Table), XHCI_DESC_COMMIT_OK,
             "installed");

    /*
     * Nothing selected yet, and the two declarations disagree: refused rather
     * than guessed. This is the pass-2 shape and it is the only case where the
     * endpoint still falls back to the assumption.
     */
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 0,
             "with no selection and no agreement, nothing is answered");
    CHECK_EQ(XhciDescIsoDeclared(&descState, 0x01), 1,
             "though the device certainly declared the endpoint - which is what "
             "the caller counts apart from never having read a descriptor");

    XhciDescSelectInterface(&descState, 1, 1);
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 1,
             "selecting alternate 1 resolves it");
    CHECK_EQ(value, 4, "to alternate 1's cadence");

    XhciDescSelectInterface(&descState, 1, 2);
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 1,
             "and selecting alternate 2 moves it");
    CHECK_EQ(value, 1, "to alternate 2's");

    /* An alternate the descriptor does not declare this endpoint in: pass 1
     * finds nothing, pass 2 cannot agree, so it is refused rather than
     * answered from whichever alternate happens to be first. */
    XhciDescSelectInterface(&descState, 1, 0);
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 0,
             "an alternate that does not declare it answers nothing");

    /*
     * A SET_CONFIGURATION puts every interface back in alternate 0, so a
     * selection may not survive one - the device has left the alternate it
     * named.
     */
    XhciDescSelectInterface(&descState, 1, 2);
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 1, "(selected)");
    CHECK_EQ(XhciDescSelectConfig(&descState, descState.Table.ConfigValue), 0,
             "re-selecting the same configuration keeps the table");
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 0,
             "but every interface is back in alternate 0, which declares "
             "nothing here");

    /*
     * **A table the walk had no room for cannot be reasoned about as a set**
     * (review round 2). Pass 1 reads one declaration and is exact; pass 2 asks
     * whether *all* of them agree, and a set with a declaration missing can
     * agree with itself and still be wrong - so a dropped declaration disables
     * the second pass rather than being ignored by it.
     */
    descBegin(&d);
    descInterface(&d, 1, 1, 1);
    descIsoEndpoint(&d, 0x01, 192, 2);
    descInterface(&d, 1, 2, 1);
    descIsoEndpoint(&d, 0x01, 192, 2);
    descInterface(&d, 1, 3, 1);
    descIsoEndpoint(&d, 0x01, 192, 2);
    descInterface(&d, 1, 4, 1);
    descIsoEndpoint(&d, 0x01, 192, 2);
    descInterface(&d, 1, 5, 1);
    descIsoEndpoint(&d, 0x01, 192, 2);
    descInterface(&d, 1, 6, 1);
    descIsoEndpoint(&d, 0x01, 192, 2);
    descInterface(&d, 1, 7, 1);
    descIsoEndpoint(&d, 0x01, 192, 2);
    descInterface(&d, 1, 8, 1);
    descIsoEndpoint(&d, 0x01, 192, 2);
    descInterface(&d, 1, 9, 1);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.Count, XHCI_DESC_ISO_ENDPOINTS, "the table filled");
    CHECK_EQ(parse.Table.Dropped, 1, "with the ninth alternate dropped");
    XhciDescStateReset(&descState);
    (void)XhciDescCommit(&descState, &parse.Table);
    XhciDescSelectInterface(&descState, 1, 9);
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 0,
             "the alternate whose declaration was dropped is not answered from "
             "the eight that agree with each other");
    XhciDescSelectInterface(&descState, 1, 3);
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 1,
             "while an alternate that *is* stored is still exact - pass 1 reads "
             "one declaration and does not reason about the set");
    CHECK_EQ(value, 2, "with its own cadence");

    /*
     * **More interfaces than the tracked set, which is where review round 3
     * found a confident wrong answer**: an untracked interface used to read as
     * running alternate 0, so a lookup answered alternate 0's cadence for an
     * interface that is demonstrably running something else. An unknown
     * alternate now refuses.
     */
    descBegin(&d);
    descInterface(&d, 9, 0, 1);
    descIsoEndpoint(&d, 0x05, 192, 1);
    descInterface(&d, 9, 1, 1);
    descIsoEndpoint(&d, 0x05, 192, 4);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    XhciDescStateReset(&descState);
    (void)XhciDescCommit(&descState, &parse.Table);
    for (value = 0; value < XHCI_DESC_INTERFACES; value++) {
        XhciDescSelectInterface(&descState, value, 1);
    }
    CHECK_EQ(descState.AltDropped, 0, "(the tracked set is exactly full)");
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x05, &value), 1,
             "interface 9 has no selection, so it is running alternate 0");
    CHECK_EQ(value, 1, "which declares bInterval 1");

    XhciDescSelectInterface(&descState, 9, 1);
    CHECK_EQ(descState.AltDropped, 1, "the ninth interface had no room");
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x05, &value), 0,
             "and its alternate is now unknown rather than assumed to be 0 - "
             "so the endpoint falls back rather than being answered from the "
             "alternate it has demonstrably left");

    /*
     * **...and that uncertainty may not outlive the configuration it was about**
     * (review round 4). A SET_CONFIGURATION returns every interface to alternate
     * 0 and this file knows it, so a selection it could not keep stops being a
     * reason to doubt anything. Leaving the flag set made a device's own valid
     * alternate-0 declarations permanently unanswerable.
     */
    CHECK_EQ(XhciDescSelectConfig(&descState, descState.Table.ConfigValue), 0,
             "(re-selecting the configuration the table describes)");
    CHECK_EQ(descState.AltDropped, 0, "the lost selections are irrelevant now");
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x05, &value), 1,
             "so interface 9 is answerable again");
    CHECK_EQ(value, 1, "at alternate 0, where the configuration reset left it");

    /* ...and re-selecting one already tracked is an update, not a new slot. */
    XhciDescStateReset(&descState);
    for (value = 0; value < XHCI_DESC_INTERFACES + 2; value++) {
        XhciDescSelectInterface(&descState, value, 1);
    }
    CHECK_EQ(descState.AltDropped, 2, "two interfaces past the tracked set");
    XhciDescSelectInterface(&descState, 0, 3);
    CHECK_EQ(descState.AltDropped, 2, "an interface already tracked is updated");
    CHECK_EQ(descState.Alt[0].Alternate, 3, "in place");
}

static void testTableFullAndBadIntervals(void)
{
    XHCI_DESC_PARSE parse;
    DESCRIPTOR d;
    ULONG value;
    ULONG i;

    /* One more declaration than the table holds: the last is counted, never
     * silently forgotten. */
    descBegin(&d);
    descInterface(&d, 0, 1, 9);
    descIsoEndpoint(&d, 0x01, 192, 1);
    descIsoEndpoint(&d, 0x02, 192, 1);
    descIsoEndpoint(&d, 0x03, 192, 1);
    descIsoEndpoint(&d, 0x04, 192, 1);
    descIsoEndpoint(&d, 0x81, 192, 1);
    descIsoEndpoint(&d, 0x82, 192, 1);
    descIsoEndpoint(&d, 0x83, 192, 1);
    descIsoEndpoint(&d, 0x84, 192, 1);
    descIsoEndpoint(&d, 0x85, 192, 1);
    descEnd(&d);

    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.Count, XHCI_DESC_ISO_ENDPOINTS, "the table filled");
    CHECK_EQ(parse.Table.Dropped, 1, "and the ninth declaration was counted");
    CHECK_EQ(stateInterval(&parse, 0x85, &value), 0,
             "a dropped endpoint is not answered from a neighbour's entry");

    /*
     * `bInterval` outside 1..16 has no Interval to convert to. Refused rather
     * than clamped - a clamp would program a cadence the device never asked
     * for, which is the defect this whole task removes.
     */
    descBegin(&d);
    descInterface(&d, 0, 1, 2);
    descIsoEndpoint(&d, 0x01, 192, 0);
    descIsoEndpoint(&d, 0x02, 192, 17);
    descEnd(&d);

    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    /*
     * Each takes an entry and each is born unusable (review round 4): the entry
     * is what makes a later valid duplicate of the same endpoint find the
     * contradiction rather than becoming the answer.
     */
    CHECK_EQ(parse.Table.Count, 2, "each takes an entry");
    CHECK_EQ(parse.Table.Endpoint[0].Unusable, 1, "and each is unusable");
    CHECK_EQ(parse.Table.Endpoint[1].Unusable, 1, "both of them");
    CHECK_EQ(parse.Table.BadInterval, 2,
             "both counted as a bInterval no Interval can be made of");
    CHECK_EQ(parse.Table.BadDescriptor, 0,
             "and neither as a descriptor that would not parse - the bytes were "
             "fine and the device asked for a cadence that does not exist");
    CHECK_EQ(stateInterval(&parse, 0x01, &value), 0,
             "bInterval 0 answers nothing");
    CHECK_EQ(stateInterval(&parse, 0x02, &value), 0,
             "bInterval 17 answers nothing");

    /*
     * ...and an out-of-range declaration beside a valid one for the same
     * endpoint in the same alternate setting poisons it, in **either** order.
     * Two conflicting statements about one endpoint leave nothing to choose
     * with, whether or not one of them is convertible.
     */
    descBegin(&d);
    descInterface(&d, 0, 1, 2);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descIsoEndpoint(&d, 0x01, 192, 0);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.Endpoint[0].Unusable, 1,
             "a valid declaration is poisoned by an out-of-range duplicate");
    CHECK_EQ(stateInterval(&parse, 0x01, &value), 0, "and answers nothing");

    descBegin(&d);
    descInterface(&d, 0, 1, 2);
    descIsoEndpoint(&d, 0x01, 192, 0);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.Endpoint[0].Unusable, 1, "and so is the other order");
    CHECK_EQ(stateInterval(&parse, 0x01, &value), 0, "answering nothing too");

    /* 16 is legal at both ends of the range, and 1 is the other end. */
    descBegin(&d);
    descInterface(&d, 0, 1, 1);
    descIsoEndpoint(&d, 0x01, 192, 16);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.BadInterval, 0, "bInterval 16 is legal");
    CHECK_EQ(stateInterval(&parse, 0x01, &value), 1, "answered");
    CHECK_EQ(value, 16, "with 16");

    /*
     * An endpoint descriptor before any interface descriptor. USB 9.6.6 puts
     * every endpoint after the interface it belongs to, so this one belongs to
     * nothing that can be selected - and filing it under a guess is what the
     * whole alternate-setting resolution exists to avoid.
     */
    descBegin(&d);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descInterface(&d, 0, 0, 1);
    descIsoEndpoint(&d, 0x02, 192, 2);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.Count, 1, "only the endpoint inside an interface");
    CHECK_EQ(parse.Table.BadDescriptor, 1, "the other is counted");
    CHECK_EQ(stateInterval(&parse, 0x01, &value), 0,
             "an endpoint outside any interface answers nothing");
    CHECK_EQ(stateInterval(&parse, 0x02, &value), 1, "the one inside answers");
    CHECK_EQ(value, 2, "with its own bInterval");

    /*
     * ...and an interface descriptor too short to name itself takes the context
     * with it, rather than leaving the endpoints after it filed under the
     * *previous* interface.
     */
    descBegin(&d);
    descInterface(&d, 3, 2, 1);
    descIsoEndpoint(&d, 0x02, 192, 2);
    descInterface(&d, 0, 0, 1);
    d.bytes[d.length - 9] = 4;      /* shrink the interface descriptor */
    for (i = d.length - 5; i < d.length; i++) {
        d.bytes[i] = 0;
    }
    d.length -= 5;
    descIsoEndpoint(&d, 0x01, 192, 4);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.Count, 1, "the endpoint under the short interface is "
             "not filed under the interface before it");
    CHECK_EQ(parse.Table.BadDescriptor, 2, "both are counted");
    CHECK_EQ(parse.Table.Endpoint[0].Interface, 3, "and the good one kept its "
             "own interface");
    CHECK_EQ(parse.Table.Endpoint[0].Alternate, 2, "and alternate");

    /*
     * **One interface and one alternate declaring the same endpoint twice, and
     * differently.** No selection can choose between them - they are the same
     * setting - so the entry stops being usable rather than answering from
     * whichever arrived first, which is what the first draft did while its
     * comment said it could not choose (review round 2).
     */
    descBegin(&d);
    descInterface(&d, 1, 1, 2);
    descIsoEndpoint(&d, 0x83, 192, 1);
    descIsoEndpoint(&d, 0x83, 192, 4);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.Count, 1, "the duplicate takes no second entry");
    CHECK_EQ(parse.Table.BadDescriptor, 1, "and is counted");
    CHECK_EQ(parse.Table.Endpoint[0].Unusable, 1, "marking the entry unusable");
    XhciDescStateReset(&descState);
    (void)XhciDescCommit(&descState, &parse.Table);
    XhciDescSelectInterface(&descState, 1, 1);
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x83, &value), 0,
             "which the selected-alternate pass refuses");
    XhciDescSelectInterface(&descState, 1, 0);
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x83, &value), 0,
             "and so does the fallback pass");
    CHECK_EQ(XhciDescIsoDeclared(&descState, 0x83), 1,
             "though the address was certainly declared");

    /* A duplicate that *agrees* is redundant rather than ambiguous: still
     * counted as a descriptor that should not have been sent, still usable. */
    descBegin(&d);
    descInterface(&d, 1, 1, 2);
    descIsoEndpoint(&d, 0x83, 192, 4);
    descIsoEndpoint(&d, 0x83, 96, 4);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.BadDescriptor, 1, "the duplicate is counted");
    CHECK_EQ(parse.Table.Endpoint[0].Unusable, 0, "but nothing is ambiguous");
    CHECK_EQ(stateInterval(&parse, 0x83, &value), 1, "and it still answers");
    CHECK_EQ(value, 4, "with the cadence both declarations named");

    /* An isochronous endpoint claiming address 0 - the default control
     * endpoint, which cannot be isochronous. */
    descBegin(&d);
    descInterface(&d, 0, 1, 1);
    descIsoEndpoint(&d, 0x00, 192, 1);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.Count, 0, "endpoint address 0 is not recorded");
    CHECK_EQ(parse.Table.BadDescriptor, 1,
             "and is counted as a declaration that did not parse, not as a bad "
             "bInterval - its bInterval was legal");
    CHECK_EQ(parse.Table.BadInterval, 0, "so that row stays 0");
}

static void testPartialAndMalformed(void)
{
    XHCI_DESC_PARSE parse;
    DESCRIPTOR d;
    DESCRIPTOR probe;
    ULONG value;
    ULONG i;

    descBegin(&d);
    descInterface(&d, 0, 1, 1);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descEnd(&d);

    /*
     * **The short probe read is the ordinary first half of every enumeration**,
     * not an error: usbport asks for the nine-byte header to learn
     * `wTotalLength` and then re-reads the whole thing.
     */
    probe = d;
    probe.length = 9;
    CHECK_EQ(descWalk(&parse, &probe), XHCI_DESC_FOLD_PARTIAL,
             "the header-only probe read is partial, not malformed");
    CHECK_EQ(parse.Table.Valid, 0, "and commits nothing");
    CHECK_EQ(stateInterval(&parse, 0x01, &value), 0,
             "an uncommitted table answers nothing even if it holds entries");

    /* Truncated before the header is even complete. */
    probe = d;
    probe.length = 4;
    CHECK_EQ(descWalk(&parse, &probe), XHCI_DESC_FOLD_PARTIAL,
             "four bytes is partial too");

    /* Nothing at all. */
    probe = d;
    probe.length = 0;
    CHECK_EQ(descWalk(&parse, &probe), XHCI_DESC_FOLD_PARTIAL,
             "an empty reply is partial");

    /* One byte short of the last endpoint descriptor. */
    probe = d;
    probe.length = d.length - 1;
    CHECK_EQ(descWalk(&parse, &probe), XHCI_DESC_FOLD_PARTIAL,
             "stopping one byte short of wTotalLength is partial");
    CHECK_EQ(parse.Table.Valid, 0, "so the endpoint it would have carried is "
             "not committed as an absence");

    /*
     * A zero `bLength` is the one malformation that stops the walk rather than
     * costing one descriptor: it can never advance, so a walk that skipped it
     * would loop for ever.
     */
    probe = d;
    probe.bytes[9] = 0;
    CHECK_EQ(descWalk(&parse, &probe), XHCI_DESC_FOLD_MALFORMED,
             "a zero-length descriptor is malformed");

    /* A descriptor whose bLength runs past wTotalLength: the walk ends
     * mid-descriptor, which is a contradiction rather than a short reply. */
    probe = d;
    probe.bytes[9] = 200;
    CHECK_EQ(descWalk(&parse, &probe), XHCI_DESC_FOLD_MALFORMED,
             "a descriptor running past wTotalLength is malformed");

    /* A reply that is not a configuration descriptor at all - the shape a
     * mis-keyed snoop would deliver. */
    probe = d;
    probe.bytes[1] = 0x01;      /* DEVICE */
    CHECK_EQ(descWalk(&parse, &probe), XHCI_DESC_FOLD_MALFORMED,
             "a reply whose first descriptor is not a configuration is refused");

    /* wTotalLength smaller than the header it was read from. */
    probe = d;
    probe.bytes[2] = 4;
    probe.bytes[3] = 0;
    CHECK_EQ(descWalk(&parse, &probe), XHCI_DESC_FOLD_MALFORMED,
             "a wTotalLength below the header's own bLength is malformed");

    /*
     * An endpoint descriptor too short to carry `bInterval`. The walk stays
     * synchronized - `bLength` advanced it - so only that endpoint is lost and
     * the configuration still commits.
     */
    descBegin(&d);
    descInterface(&d, 0, 1, 2);
    descIsoEndpoint(&d, 0x01, 192, 4);
    d.bytes[d.length - 9] = 6;  /* shrink the endpoint descriptor to 6 bytes */
    for (i = d.length - 3; i < d.length; i++) {
        d.bytes[i] = 0;
    }
    d.length -= 3;
    descIsoEndpoint(&d, 0x02, 192, 2);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT,
             "a short endpoint descriptor does not fail the configuration");
    CHECK_EQ(parse.Table.BadDescriptor, 1, "it is counted");
    CHECK_EQ(parse.Table.Count, 1, "and the endpoint after it still lands");
    CHECK_EQ(stateInterval(&parse, 0x02, &value), 1,
             "the walk stayed synchronized");
    CHECK_EQ(value, 2, "with the right bInterval");
}

static void testTrailingBytesIgnored(void)
{
    XHCI_DESC_PARSE parse;
    DESCRIPTOR d;
    ULONG value;

    /*
     * usbport asks for whatever the client driver's buffer holds, so a reply may
     * carry bytes past `wTotalLength` that belong to no descriptor of this
     * configuration. Walking into them would invent descriptors or declare the
     * configuration malformed on data it does not own.
     */
    descBegin(&d);
    descInterface(&d, 0, 1, 1);
    descIsoEndpoint(&d, 0x01, 192, 3);
    descEnd(&d);
    /* Garbage after the end, including a zero length that would otherwise be
     * fatal. */
    d.bytes[d.length] = 0;
    d.bytes[d.length + 1] = 0xFF;
    d.bytes[d.length + 2] = 0xFF;
    d.length += 3;

    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT,
             "bytes past wTotalLength are ignored, not walked");
    CHECK_EQ(stateInterval(&parse, 0x01, &value), 1, "answered");
    CHECK_EQ(value, 3, "with the declared bInterval");
}

static void testChunkingIsIrrelevant(void)
{
    XHCI_DESC_PARSE whole;
    XHCI_DESC_PARSE chunked;
    DESCRIPTOR d;
    ULONG chunk;
    ULONG i;
    ULONG mismatches;
    ULONG sizesTried;

    /*
     * **The property the driver's fold depends on.** It reads usbport's mapping
     * through a 32-byte window, so descriptor boundaries land anywhere inside a
     * feed - including a `bLength` in one chunk and its `bInterval` in the next.
     * Every chunk size must give the same table.
     */
    descBegin(&d);
    descInterface(&d, 0, 0, 0);
    descClassBlob(&d, 12);
    descInterface(&d, 1, 1, 1);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descInterface(&d, 2, 1, 2);
    descIsoEndpoint(&d, 0x82, 96, 2);
    descInterruptEndpoint(&d, 0x83, 8, 8);
    descEnd(&d);

    CHECK_EQ(descWalk(&whole, &d), XHCI_DESC_FOLD_COMMIT,
             "the reference walk commits");
    CHECK_EQ(whole.Table.Count, 2, "two isochronous endpoints");

    mismatches = 0;
    sizesTried = 0;
    for (chunk = 1; chunk <= d.length + 4; chunk++) {
        sizesTried++;
        if (descWalkChunked(&chunked, &d, chunk) != XHCI_DESC_FOLD_COMMIT) {
            mismatches++;
            continue;
        }
        if (chunked.Table.Count != whole.Table.Count ||
            chunked.Table.Dropped != whole.Table.Dropped ||
            chunked.Table.BadInterval != whole.Table.BadInterval ||
            chunked.Table.BadDescriptor != whole.Table.BadDescriptor) {
            mismatches++;
            continue;
        }
        for (i = 0; i < XHCI_DESC_ISO_ENDPOINTS; i++) {
            if (chunked.Table.Endpoint[i].Address !=
                    whole.Table.Endpoint[i].Address ||
                chunked.Table.Endpoint[i].BInterval !=
                    whole.Table.Endpoint[i].BInterval ||
                chunked.Table.Endpoint[i].Interface !=
                    whole.Table.Endpoint[i].Interface ||
                chunked.Table.Endpoint[i].Alternate !=
                    whole.Table.Endpoint[i].Alternate) {
                mismatches++;
                break;
            }
        }
    }
    CHECK_EQ(mismatches, 0, "every chunk size produces the same table");
    /* The never-firing sweep is the failure this check exists to prevent: a
     * loop that tried nothing passes a zero-mismatch assertion. */
    CHECK(sizesTried > 40, "and the sweep really tried the sizes");

    /* The driver's own window, named rather than assumed to be covered by the
     * sweep above. */
    CHECK_EQ(descWalkChunked(&chunked, &d, XHCI_DESC_FOLD_CHUNK),
             XHCI_DESC_FOLD_COMMIT, "including the 32-byte window the fold uses");
    CHECK_EQ(chunked.Table.Count, 2, "with both endpoints");
}

static void testConfigurationSelection(void)
{
    XHCI_DESC_PARSE parse;
    DESCRIPTOR d;
    ULONG value;

    /*
     * A table describes one configuration, and the walk reads which from
     * `bConfigurationValue` at offset 5 rather than from the index the request
     * asked by - the two are unrelated numbers, and SET_CONFIGURATION carries
     * the value.
     */
    descBegin(&d);
    d.bytes[5] = 2;             /* bConfigurationValue */
    descInterface(&d, 0, 1, 1);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");
    CHECK_EQ(parse.Table.ConfigValue, 2,
             "the table records the configuration it describes");

    /* Selecting the configuration the table describes keeps it. */
    XhciDescStateReset(&descState);
    CHECK_EQ(XhciDescCommit(&descState, &parse.Table), XHCI_DESC_COMMIT_OK,
             "a table installs while nothing has been selected");
    CHECK_EQ(XhciDescSelectConfig(&descState, 2), 0,
             "selecting the same configuration throws nothing away");
    CHECK_EQ(descState.Table.Valid, 1, "the table survives");
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 1,
             "and still answers");

    /*
     * Selecting a *different* one throws it away: usbport may read more than
     * one of a multi-configuration device's descriptors, and programming one
     * configuration's cadences onto another's endpoints is the same confident
     * wrongness the assumption was.
     */
    XhciDescStateReset(&descState);
    (void)XhciDescCommit(&descState, &parse.Table);
    CHECK_EQ(XhciDescSelectConfig(&descState, 1), 1,
             "selecting another configuration throws the table away");
    CHECK_EQ(descState.Table.Valid, 0, "so it is no longer valid");
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 0,
             "and the endpoint falls back to the assumption");

    /*
     * **And the other direction, which is the same guard from the other side**:
     * once a configuration is selected, a descriptor for any *other* one is
     * refused rather than installed. Without this the walk would replace the
     * running configuration's table with an inactive one and nothing would ever
     * notice, since no further SET_CONFIGURATION follows.
     */
    CHECK_EQ(XhciDescCommit(&descState, &parse.Table),
             XHCI_DESC_COMMIT_INACTIVE,
             "a descriptor for an inactive configuration is refused");
    CHECK_EQ(descState.Table.Valid, 0, "and installs nothing");
    CHECK_EQ(XhciDescSelectConfig(&descState, 2), 0,
             "(selecting the one it describes)");
    CHECK_EQ(XhciDescCommit(&descState, &parse.Table), XHCI_DESC_COMMIT_OK,
             "the same descriptor installs once its configuration is selected");
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 1, "and answers");
    CHECK_EQ(value, 4, "with the declared cadence");

    /* An unconfigure is a different configuration like any other. */
    CHECK_EQ(XhciDescSelectConfig(&descState, 0), 1,
             "and so does an unconfigure");

    /* A state that holds nothing cannot be superseded - there is nothing to
     * throw away, and reporting one would make the counter mean two things. */
    XhciDescStateReset(&descState);
    CHECK_EQ(XhciDescSelectConfig(&descState, 1), 0,
             "an empty state is not reported as superseded");
    CHECK_EQ(XhciDescSelectConfig(NULL, 1), 0, "nor is a NULL one");
    CHECK_EQ(XhciDescCommit(NULL, &parse.Table), XHCI_DESC_COMMIT_BAD_PARAM,
             "a NULL state commits nothing");
    CHECK_EQ(XhciDescCommit(&descState, NULL), XHCI_DESC_COMMIT_BAD_PARAM,
             "and a NULL table is not a commit either");
}

static void testTableReset(void)
{
    XHCI_DESC_PARSE parse;
    DESCRIPTOR d;
    ULONG value;

    descBegin(&d);
    descInterface(&d, 0, 1, 1);
    descIsoEndpoint(&d, 0x01, 192, 4);
    descEnd(&d);
    CHECK_EQ(descWalk(&parse, &d), XHCI_DESC_FOLD_COMMIT, "committed");

    XhciDescStateReset(&descState);
    (void)XhciDescCommit(&descState, &parse.Table);
    XhciDescSelectInterface(&descState, 0, 1);
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 1,
             "the installed state answers");

    /* A state reset forgets the descriptor **and** what the device was told to
     * run - the two arrive through different requests and a re-enumeration
     * invalidates both. */
    XhciDescStateReset(&descState);
    CHECK_EQ(descState.Table.Valid, 0, "a reset state holds no table");
    CHECK_EQ(descState.Table.Count, 0, "and no declarations");
    CHECK_EQ(descState.SelectedConfig, 0, "no configuration");
    CHECK_EQ(descState.Alt[0].Used, 0, "and no alternate settings");
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, &value), 0,
             "so it answers nothing");

    /* NULL arguments answer rather than fault, on every entry point. */
    XhciDescTableReset(NULL);
    XhciDescStateReset(NULL);
    XhciDescSelectInterface(NULL, 0, 1);
    XhciDescParseBegin(NULL);
    XhciDescParseFeed(NULL, d.bytes, d.length);
    XhciDescParseFeed(&parse, NULL, 4);
    CHECK_EQ(XhciDescParseEnd(NULL), XHCI_DESC_FOLD_MALFORMED,
             "a NULL walk is not a commit");
    CHECK_EQ(XhciDescIsoInterval(NULL, 0x01, &value), 0, "nor a NULL state");
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x01, NULL), 0,
             "nor a NULL output");
    CHECK_EQ(XhciDescIsoDeclared(NULL, 0x01), 0, "nor a NULL declaration ask");
}

/* ------------------------------------------------------------------ */
/* The conversion, and the context it ends up in                       */
/* ------------------------------------------------------------------ */

static void testIntervalConversion(void)
{
    ULONG interval;

    /*
     * Hand-computed against docs/usb-xhci-info/xhci-data-structures.md's conversion table
     * (spec 6.2.3.6, Table 6-12), never through the expression the code uses.
     *
     *   HS: period = 2^(bInterval-1) microframes, Interval = bInterval - 1
     *   FS: period = 2^(bInterval-1) frames,      Interval = bInterval + 2
     */
    interval = 0xFFFFFFFFUL;
    CHECK_EQ(XhciIsochIntervalFromBInterval(1, XHCI_SPEED_HIGH, &interval),
             XHCI_CTX_OK, "HS bInterval 1 accepted");
    CHECK_EQ(interval, 0, "HS bInterval 1 is every microframe - Interval 0");
    CHECK_EQ(XhciIsochIntervalFromBInterval(4, XHCI_SPEED_HIGH, &interval),
             XHCI_CTX_OK, "HS bInterval 4 accepted");
    CHECK_EQ(interval, 3, "HS bInterval 4 is once a millisecond - Interval 3");
    CHECK_EQ(XhciIsochIntervalFromBInterval(16, XHCI_SPEED_HIGH, &interval),
             XHCI_CTX_OK, "HS bInterval 16 accepted");
    CHECK_EQ(interval, 15, "and lands exactly on the top of the 0-15 range");

    CHECK_EQ(XhciIsochIntervalFromBInterval(1, XHCI_SPEED_FULL, &interval),
             XHCI_CTX_OK, "FS bInterval 1 accepted");
    CHECK_EQ(interval, 3, "FS bInterval 1 is once a frame - Interval 3");
    CHECK_EQ(XhciIsochIntervalFromBInterval(4, XHCI_SPEED_FULL, &interval),
             XHCI_CTX_OK, "FS bInterval 4 accepted");
    CHECK_EQ(interval, 6, "FS bInterval 4 is once every 8 frames - Interval 6");
    CHECK_EQ(XhciIsochIntervalFromBInterval(16, XHCI_SPEED_FULL, &interval),
             XHCI_CTX_OK, "FS bInterval 16 accepted");
    CHECK_EQ(interval, 18, "and lands exactly on the top of the 3-18 range");

    /* The two ends of the legal range, refused rather than clamped. */
    CHECK_EQ(XhciIsochIntervalFromBInterval(0, XHCI_SPEED_HIGH, &interval),
             XHCI_CTX_BAD_PARAM, "bInterval 0 has no Interval");
    CHECK_EQ(XhciIsochIntervalFromBInterval(17, XHCI_SPEED_HIGH, &interval),
             XHCI_CTX_BAD_PARAM, "nor does 17");
    CHECK_EQ(XhciIsochIntervalFromBInterval(255, XHCI_SPEED_FULL, &interval),
             XHCI_CTX_BAD_PARAM, "nor 255");

    /* Low Speed has no isochronous endpoints at all (USB 2.0 section 5.6). */
    CHECK_EQ(XhciIsochIntervalFromBInterval(1, XHCI_SPEED_LOW, &interval),
             XHCI_CTX_BAD_PARAM, "Low Speed has no isochronous endpoint");
    CHECK_EQ(XhciIsochIntervalFromBInterval(1, 0xFF, &interval),
             XHCI_CTX_BAD_PARAM, "nor does a speed this driver cannot decode");
    CHECK_EQ(XhciIsochIntervalFromBInterval(1, XHCI_SPEED_HIGH, NULL),
             XHCI_CTX_BAD_PARAM, "and a NULL output is refused");
}

static void testBuilderUsesTheDescriptor(void)
{
    XHCI_EP_PARAMS ep;
    ULONG block[XHCI_CONTEXT_DWORDS + 1];
    ULONG derived;
    ULONG floored;

    /*
     * **The defect task 9-A.2 exists to fix**, stated as two vectors that
     * differ only in whether a descriptor was read. A High-Speed isochronous
     * endpoint with `bInterval = 4` - one ESIT per millisecond, a common USB
     * Audio shape - was programmed at Interval 0, which is every 125 us: the
     * xHC consumes eight TDs per millisecond and the stream plays eight times
     * too fast.
     */
    derived = 0xFFFFFFFFUL;
    floored = 0xFFFFFFFFUL;
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 0UL,
                                     192UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 1UL, 0UL, 0x00302000UL,
                                     1UL, &ep, &floored, &derived),
             XHCI_CTX_OK, "HS isoch OUT with no descriptor accepted");
    CHECK_EQ(ep.Interval, 0, "and falls back to usbport's assumed cadence");
    CHECK_EQ(derived, 0, "reported as assumed");
    CHECK_EQ(floored, 0, "the floor is an interrupt-path reading only");

    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 0UL,
                                     192UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 1UL, 4UL, 0x00302000UL,
                                     1UL, &ep, &floored, &derived),
             XHCI_CTX_OK, "the same endpoint with bInterval 4 from a descriptor");
    CHECK_EQ(ep.Interval, 3, "is programmed once a millisecond, not eight times");
    CHECK_EQ(derived, 1, "reported as derived");

    /* The encoded context, because Interval is a field position as well as a
     * value: DW0 bits 23:16. */
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK, "context built");
    CHECK_EQ(block[0] & 0x00FF0000UL, 0x00030000UL,
             "and the Interval reaches DW0 23:16");

    /* Full Speed, where the same bInterval means frames rather than
     * microframes. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 1UL,
                                     192UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_FULL, 1UL, 0UL, 0x00302000UL,
                                     1UL, &ep, &floored, &derived),
             XHCI_CTX_OK, "FS isoch IN with no descriptor accepted");
    CHECK_EQ(ep.Interval, 3, "assumed one frame");
    CHECK_EQ(derived, 0, "as an assumption");
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 1UL,
                                     192UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_FULL, 1UL, 4UL, 0x00302000UL,
                                     1UL, &ep, &floored, &derived),
             XHCI_CTX_OK, "FS isoch IN with bInterval 4");
    CHECK_EQ(ep.Interval, 6, "is once every eight frames");
    CHECK_EQ(derived, 1, "derived");

    /*
     * A descriptor reading that agrees with the assumption - the outcome the
     * roadmap says may be the honest close for every device this project can
     * reach. It must still be reported as *derived*, because "the answer was
     * measured and happens to match" is a different statement from "the answer
     * was assumed", and the counter split is what makes the difference
     * readable on a target.
     */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 0UL,
                                     192UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 1UL, 1UL, 0x00302000UL,
                                     1UL, &ep, &floored, &derived),
             XHCI_CTX_OK, "HS isoch with bInterval 1");
    CHECK_EQ(ep.Interval, 0, "programs what the assumption would have");
    CHECK_EQ(derived, 1, "and still says it was derived");

    /*
     * The descriptor changes the Interval and **nothing else**: Max Burst,
     * CErr, Max ESIT Payload and the packet size are the speed's answers and
     * are not the descriptor's to move.
     */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 0UL,
                                     192UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 3UL, 8UL, 0x00302000UL,
                                     1UL, &ep, &floored, &derived),
             XHCI_CTX_OK, "a high-bandwidth HS isoch endpoint with bInterval 8");
    CHECK_EQ(ep.Interval, 7, "Interval derived");
    CHECK_EQ(ep.MaxBurstSize, 2, "Max Burst is still the transaction count - 1");
    CHECK_EQ(ep.ErrorCount, 0, "CErr is still 0 for isochronous");
    CHECK_EQ(ep.MaxEsitPayload, 192UL * 3UL,
             "and Max ESIT Payload is still MPS * (burst + 1) * (mult + 1)");

    /*
     * A `bInterval` on a **non**-isochronous endpoint is not read at all. The
     * table holds isochronous endpoints only, so this cannot arise through the
     * driver - and the check is what says the branch is where it looks.
     */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     8UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH, 1UL,
                                     9UL, 0x00302000UL, 1UL, &ep, &floored,
                                     &derived),
             XHCI_CTX_OK, "an interrupt endpoint with a bInterval passed in");
    CHECK_EQ(ep.Interval, 3, "keeps the Interval its Period gives");
    CHECK_EQ(derived, 0, "and is never reported as derived");

    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_BULK, 1UL, 512UL,
                                     0UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH, 1UL,
                                     4UL, 0x00302000UL, 1UL, &ep, &floored,
                                     &derived),
             XHCI_CTX_OK, "a bulk endpoint likewise");
    CHECK_EQ(ep.Interval, 0, "bulk has no service interval");
    CHECK_EQ(derived, 0, "and nothing was derived");

    /*
     * Low Speed is refused before the descriptor is ever consulted, so the
     * refusal is the speed's and not the conversion's - the same answer with or
     * without a reading.
     */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 0UL,
                                     8UL, 1UL, XHCI_SPEED_LOW, XHCI_SPEED_LOW,
                                     1UL, 4UL, 0x00302000UL, 1UL, &ep, &floored,
                                     &derived),
             XHCI_CTX_BAD_PARAM, "a Low-Speed isochronous endpoint is refused");
    CHECK_EQ(derived, 0, "with nothing derived");

    /* The out-parameters may be NULL - the driver passes them, a caller need
     * not. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 0UL,
                                     192UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 1UL, 4UL, 0x00302000UL,
                                     1UL, &ep, NULL, NULL),
             XHCI_CTX_OK, "NULL out-parameters are accepted");
    CHECK_EQ(ep.Interval, 3, "and the derivation still happens");
}

/*
 * End to end, in the order the driver does it: a reply's bytes become a table,
 * the table answers an endpoint address, and the answer becomes an Endpoint
 * Context Interval. Nothing in the middle is hand-fed.
 */
static void testEndToEnd(void)
{
    XHCI_DESC_PARSE parse;
    XHCI_EP_PARAMS ep;
    DESCRIPTOR d;
    ULONG bInterval;
    ULONG derived;

    descBegin(&d);
    descInterface(&d, 0, 0, 0);
    descClassBlob(&d, 9);
    descInterface(&d, 1, 0, 0);
    descInterface(&d, 1, 1, 1);
    descClassBlob(&d, 7);
    descIsoEndpoint(&d, 0x81, 200, 4);
    descEnd(&d);

    CHECK_EQ(descWalkChunked(&parse, &d, XHCI_DESC_FOLD_CHUNK),
             XHCI_DESC_FOLD_COMMIT, "the audio configuration commits");
    bInterval = 0;
    XhciDescStateReset(&descState);
    CHECK_EQ(XhciDescCommit(&descState, &parse.Table), XHCI_DESC_COMMIT_OK,
             "and installs");
    XhciDescSelectConfig(&descState, 1);
    XhciDescSelectInterface(&descState, 1, 1);
    CHECK_EQ(XhciDescIsoInterval(&descState, 0x81, &bInterval), 1,
             "the streaming endpoint is answered once its alternate is "
             "selected");
    derived = 0;
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 1UL,
                                     200UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 1UL, bInterval,
                                     0x00302000UL, 1UL, &ep, NULL, &derived),
             XHCI_CTX_OK, "and builds an endpoint");
    CHECK_EQ(derived, 1, "from the descriptor");
    CHECK_EQ(ep.Interval, 3,
             "at the one-millisecond cadence the device asked for");
    CHECK_EQ(ep.EpType, 5, "isoch IN is EP Type 5");
}

/* ------------------------------------------------------------------ */

int main(void)
{
    testSnoopSelection();
    testAudioConfiguration();
    testOnlyIsochronousIsKept();
    testAlternateSettings();
    testTableFullAndBadIntervals();
    testPartialAndMalformed();
    testTrailingBytesIgnored();
    testChunkingIsIrrelevant();
    testConfigurationSelection();
    testTableReset();
    testIntervalConversion();
    testBuilderUsesTheDescriptor();
    testEndToEnd();

    printf("%d checks, %d failures\n", checks, failures);
    return failures;
}
