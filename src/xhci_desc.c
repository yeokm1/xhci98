/*
 * xhci_desc.c - the configuration-descriptor snoop, roadmap task 9-A.2.
 *
 * See src/xhci_desc.h for why an isochronous endpoint's `bInterval` can only
 * come from here, where every wire constant was transcribed from, and what this
 * file deliberately does not record.
 *
 * Three rules, the same three src/xhci_topo.c holds:
 *
 *   **It decides nothing itself.** The walk records what a configuration
 *   declared; `XhciBuildEndpointParams` is what turns a `bInterval` into an
 *   Endpoint Context Interval, and the endpoint open is what asks.
 *
 *   **It never holds a pointer usbport owns.** Bytes arrive through
 *   `XhciDescParseFeed` in chunks the caller has already copied out of the
 *   mapped transfer buffer, inside the callback that owns it.
 *
 *   **It refuses rather than guesses.** A descriptor that does not fit its own
 *   `bLength`, a reply that stopped short of `wTotalLength`, a `bInterval`
 *   outside the range Table 6-12 can convert, more interfaces than the tracked
 *   set holds (`AltDropped`) or more isochronous endpoints than the table does
 *   (`Dropped`) - each is counted and dropped, and the endpoint falls back to
 *   the assumption the driver used before this file existed.
 *
 *   *(This list ended "two alternate settings that disagree" until the post-Phase 13 review rounds,
 *   and that is **not** a refusal this file makes: the first draft refused on
 *   that ground, on the reasoning that the miniport cannot tell which alternate
 *   is in force, and it can - `SET_INTERFACE` crosses the same pipe. So a
 *   selection resolves each alternate to the cadence it declared. What is left
 *   unanswered is a **lookup**, not a drop: `XhciDescIsoInterval` answers from
 *   the selected alternate when that alternate declares the endpoint, and
 *   otherwise only when every declaration agrees - so it refuses when they
 *   disagree and nothing resolves them, which is *either* no selection in force
 *   *or* a selection naming an alternate that does not declare this endpoint.
 *   Both are refusals to guess, both are counted by nothing, and both leave
 *   `XhciDescIsoDeclared` saying the endpoint was declared all the same.
 *   `test/test_desc.c`'s "the same endpoint declared at different cadences"
 *   vector pins them, including the alternate-0 case. **The first correction of
 *   this paragraph, on the same day, named only the no-selection half** - round
 *   11 caught that, off the vector three lines below the one being cited.)*
 *
 * C89 only. No MMIO, no DDK, no lock: the caller holds the controller lock
 * across every call.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_desc.h"

/* ------------------------------------------------------------------ */
/* The snoop                                                           */
/* ------------------------------------------------------------------ */

VOID XhciDescObserveSetup(ULONG address,
                          const XHCI_SETUP_PACKET *setup,
                          PXHCI_DESC_SNOOP snoop)
{
    ULONG type;

    if (snoop == NULL) {
        return;
    }
    snoop->Action = XHCI_DESC_ACT_NONE;
    snoop->Address = address;
    snoop->Value = 0;
    snoop->Index = 0;

    if (setup == NULL || address == 0) {
        return;
    }
    /*
     * Which configuration the device is now running. `wValue`'s low byte is a
     * `bConfigurationValue` - the number a configuration descriptor carries at
     * offset 5 - and not an index, which is why the table keeps that field and
     * not the index the GET_DESCRIPTOR asked by.
     */
    if (setup->bmRequestType == XHCI_DESC_RT_STANDARD_OUT &&
        setup->bRequest == XHCI_DESC_REQ_SET_CONFIGURATION) {
        snoop->Action = XHCI_DESC_ACT_SELECT_CONFIG;
        snoop->Value = (ULONG)setup->wValue & 0xFFUL;
        return;
    }
    /*
     * ...and which alternate setting of which interface. src/xhci_topo.c reads
     * the same request to decide a hub's Multi-TT bit; here it is what says
     * which of an interface's endpoint declarations describes the pipe usbport
     * is about to open.
     */
    if (setup->bmRequestType == XHCI_DESC_RT_STANDARD_IFACE &&
        setup->bRequest == XHCI_DESC_REQ_SET_INTERFACE) {
        snoop->Action = XHCI_DESC_ACT_SELECT_INTERFACE;
        snoop->Value = (ULONG)setup->wValue & 0xFFUL;
        snoop->Index = (ULONG)setup->wIndex & 0xFFUL;
        return;
    }
    if (setup->bmRequestType != XHCI_DESC_RT_STANDARD_IN ||
        setup->bRequest != XHCI_DESC_REQ_GET_DESCRIPTOR) {
        return;
    }
    /*
     * `wValue` is the descriptor type in the high byte and the index in the low
     * one (`USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX` in the DDK's usb100.h) - and
     * unlike the hub-descriptor request src/xhci_topo.c snoops, where both
     * shipping hub drivers send `wValue = 0x0000` and the type has to be read
     * out of the reply, a standard GET_DESCRIPTOR *must* carry it: the type is
     * the only thing that distinguishes this request from the device, string and
     * qualifier reads on the same pipe.
     */
    type = ((ULONG)setup->wValue >> 8) & 0xFFUL;
    if (type != XHCI_DESC_TYPE_CONFIGURATION) {
        return;
    }

    snoop->Action = XHCI_DESC_ACT_CONFIG_REPLY;
}

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

VOID XhciDescTableReset(PXHCI_DESC_ISO_TABLE table)
{
    ULONG i;

    if (table == NULL) {
        return;
    }
    table->Valid = 0;
    table->ConfigValue = 0;
    table->Count = 0;
    table->Dropped = 0;
    table->BadInterval = 0;
    table->BadDescriptor = 0;
    for (i = 0; i < XHCI_DESC_ISO_ENDPOINTS; i++) {
        table->Endpoint[i].Address = 0;
        table->Endpoint[i].BInterval = 0;
        table->Endpoint[i].Interface = 0;
        table->Endpoint[i].Alternate = 0;
        table->Endpoint[i].Unusable = 0;
    }
}

VOID XhciDescStateReset(PXHCI_DESC_STATE state)
{
    ULONG i;

    if (state == NULL) {
        return;
    }
    XhciDescTableReset(&state->Table);
    state->SelectedConfig = 0;
    state->AltDropped = 0;
    for (i = 0; i < XHCI_DESC_INTERFACES; i++) {
        state->Alt[i].Used = 0;
        state->Alt[i].Interface = 0;
        state->Alt[i].Alternate = 0;
    }
}

/*
 * Record one isochronous endpoint declaration, under the interface descriptor
 * that was in force when it arrived. IRQL: any.
 */
static VOID xhciDescRecord(PXHCI_DESC_ISO_TABLE table,
                           ULONG interfaceNumber,
                           ULONG alternate,
                           ULONG address,
                           ULONG bInterval)
{
    ULONG usable;
    ULONG i;

    /* Address 0 is the default control endpoint and cannot be isochronous, so
     * a descriptor claiming it did not parse as one. */
    if (address == 0) {
        table->BadDescriptor++;
        return;
    }
    /*
     * A `bInterval` outside 1..16 has no Interval to convert to (Table 6-12),
     * and clamping it would program a cadence the device did not ask for -
     * which is the very defect this file removes, restated one level down.
     * Counted apart from a parse failure because it says something different:
     * the bytes were fine and the device asked for a cadence that does not
     * exist.
     */
    usable = (bInterval >= XHCI_DESC_BINTERVAL_MIN &&
              bInterval <= XHCI_DESC_BINTERVAL_MAX) ? 1UL : 0UL;
    if (!usable) {
        table->BadInterval++;
    }

    for (i = 0; i < XHCI_DESC_ISO_ENDPOINTS; i++) {
        if (table->Endpoint[i].Address == address &&
            table->Endpoint[i].Interface == interfaceNumber &&
            table->Endpoint[i].Alternate == alternate) {
            /*
             * One alternate setting declaring the same endpoint twice, which a
             * conforming descriptor does not do. The duplicate is always
             * counted; it makes the entry **unusable unless the two say the
             * same usable thing**, because a selection names an alternate and
             * both of these are in the same one, so there is nothing left to
             * choose with. Two declarations that agree are redundant rather
             * than contradictory, and refusing them would throw away a reading
             * nothing disputes.
             *
             * **The range check above no longer returns through this test**
             * (review round 4): a valid declaration followed by an out-of-range
             * duplicate is two conflicting statements about one endpoint just
             * as surely as two valid ones are, and returning early left the
             * first answering confidently.
             */
            table->BadDescriptor++;
            if (table->Endpoint[i].BInterval != bInterval) {
                table->Endpoint[i].Unusable = 1;
            }
            /*
             * The test needs no `usable` term of its own and a draft that had
             * one was removing nothing: an out-of-range value is stored as 0 and
             * a stored usable one is 1..16, so an out-of-range duplicate always
             * differs from a valid entry, and an entry that was itself
             * out-of-range was born unusable. Two identical out-of-range
             * declarations are the one case where nothing is set here, and the
             * entry is already unusable.
             */
            return;
        }
    }
    for (i = 0; i < XHCI_DESC_ISO_ENDPOINTS; i++) {
        if (table->Endpoint[i].Address == 0) {
            table->Endpoint[i].Address = address;
            table->Endpoint[i].BInterval = usable ? bInterval : 0;
            table->Endpoint[i].Interface = interfaceNumber;
            table->Endpoint[i].Alternate = alternate;
            /*
             * An out-of-range declaration takes an entry rather than being
             * dropped on the floor, and the entry is born unusable: that is
             * what makes a *later* valid duplicate of the same endpoint find
             * the contradiction instead of becoming the answer. It costs a
             * table slot on a malformed device, which is the right place to
             * spend one.
             */
            table->Endpoint[i].Unusable = usable ? 0UL : 1UL;
            table->Count++;
            return;
        }
    }
    table->Dropped++;
}

ULONG XhciDescCommit(PXHCI_DESC_STATE state, const XHCI_DESC_ISO_TABLE *table)
{
    if (state == NULL || table == NULL || !table->Valid) {
        return XHCI_DESC_COMMIT_BAD_PARAM;
    }
    /*
     * **A table describing a configuration the device is not running is refused
     * rather than installed.** usbport may read more than one of a
     * multi-configuration device's descriptors and the last one walked is not
     * the one in force; installing it would program one configuration's
     * cadences onto another's endpoints, which is the same confident wrongness
     * as the assumption this file removes.
     *
     * A `SelectedConfig` of 0 is "no SET_CONFIGURATION has been observed", which
     * is the ordinary enumeration order - the descriptors are read before the
     * device is configured - and the selection that follows is what validates
     * what was installed here.
     */
    if (state->SelectedConfig != 0 &&
        state->SelectedConfig != table->ConfigValue) {
        return XHCI_DESC_COMMIT_INACTIVE;
    }
    /*
     * **The one large struct assignment in this driver, and it is deliberate
     * rather than overlooked.** `xhci_caps.c` copies `XHCI_HC_INFO` field-wise
     * precisely to keep MSVC 6.0 from emitting a `memcpy` call, because this
     * driver decides its import list rather than letting codegen decide it.
     * The same hazard exists here - `XHCI_DESC_TABLE` is 184 bytes - and the
     * trade is different: a field-wise copy of this structure would be a second
     * spelling of its layout that a future field could silently fall out of,
     * which is a worse failure than the one being avoided. **The backstop is
     * the import gate**, which fails the build on any `memcpy` this compiler
     * decides to emit, so a regression here is loud rather than a load-time
     * yellow bang. If it ever does fire, copy field-wise and add the assertion
     * that the sizes still agree.
     */
    state->Table = *table;
    return XHCI_DESC_COMMIT_OK;
}

ULONG XhciDescSelectConfig(PXHCI_DESC_STATE state, ULONG configValue)
{
    ULONG dropped;
    ULONG i;

    if (state == NULL) {
        return 0;
    }
    dropped = 0;
    if (state->Table.Valid && state->Table.ConfigValue != configValue) {
        XhciDescTableReset(&state->Table);
        dropped = 1;
    }
    state->SelectedConfig = configValue;
    /*
     * A SET_CONFIGURATION puts every interface back in alternate 0, so a
     * selection carried over from the previous configuration would name an
     * alternate the device has left.
     *
     * **`AltDropped` goes with them**, and review round 4 is why: round 3 made
     * it *state* - an interface absent from the tracked set reads as unknown
     * rather than as alternate 0 while it is nonzero - and an earlier comment
     * here kept it on the grounds that it was "a reading about this driver's
     * table size rather than about the device". That was true while it was only
     * a counter. As state it says "there is a selection this file could not
     * keep", and a configuration reset is exactly the event that makes every
     * such selection irrelevant: every interface is at alternate 0 again and
     * this file knows it. Leaving it set made a device's own valid alternate-0
     * declarations permanently unanswerable. The historical count lives in the
     * caller's `DescInterfacesDropped`, which is where a *reading* belongs.
     */
    state->AltDropped = 0;
    for (i = 0; i < XHCI_DESC_INTERFACES; i++) {
        state->Alt[i].Used = 0;
        state->Alt[i].Interface = 0;
        state->Alt[i].Alternate = 0;
    }
    return dropped;
}

VOID XhciDescSelectInterface(PXHCI_DESC_STATE state,
                             ULONG interfaceNumber,
                             ULONG alternate)
{
    ULONG i;

    if (state == NULL) {
        return;
    }
    for (i = 0; i < XHCI_DESC_INTERFACES; i++) {
        if (state->Alt[i].Used && state->Alt[i].Interface == interfaceNumber) {
            state->Alt[i].Alternate = alternate;
            return;
        }
    }
    for (i = 0; i < XHCI_DESC_INTERFACES; i++) {
        if (!state->Alt[i].Used) {
            state->Alt[i].Used = 1;
            state->Alt[i].Interface = interfaceNumber;
            state->Alt[i].Alternate = alternate;
            return;
        }
    }
    /*
     * More interfaces than this driver tracks. Counted rather than dropped
     * silently: an untracked interface reads as running alternate 0, which is a
     * statement that can be wrong, and nothing else would say so.
     */
    state->AltDropped++;
}

/*
 * The alternate setting an interface is running.
 *
 * 0 for one no SET_INTERFACE has named, which is what a SET_CONFIGURATION
 * leaves every interface in - **unless a selection has been dropped for want of
 * room**, in which case an untracked interface is one this file does not know
 * about rather than one at alternate 0, and `*known` says so. Review round 3
 * found the earlier version answering alternate 0 confidently in exactly that
 * case, which is the shape of wrong answer this whole file is written to avoid.
 *
 * IRQL: any.
 */
static ULONG xhciDescSelectedAlt(const XHCI_DESC_STATE *state,
                                 ULONG interfaceNumber,
                                 ULONG *known)
{
    ULONG i;

    for (i = 0; i < XHCI_DESC_INTERFACES; i++) {
        if (state->Alt[i].Used && state->Alt[i].Interface == interfaceNumber) {
            *known = 1;
            return state->Alt[i].Alternate;
        }
    }
    *known = (state->AltDropped == 0) ? 1UL : 0UL;
    return 0;
}

ULONG XhciDescIsoDeclared(const XHCI_DESC_STATE *state, ULONG endpointAddress)
{
    ULONG i;

    if (state == NULL || endpointAddress == 0 || !state->Table.Valid) {
        return 0;
    }
    for (i = 0; i < XHCI_DESC_ISO_ENDPOINTS; i++) {
        if (state->Table.Endpoint[i].Address == endpointAddress) {
            return 1;
        }
    }
    return 0;
}

ULONG XhciDescIsoInterval(const XHCI_DESC_STATE *state,
                          ULONG endpointAddress,
                          ULONG *bInterval)
{
    const XHCI_DESC_ISO_EP *ep;
    ULONG matched;
    ULONG value;
    ULONG known;
    ULONG i;

    if (state == NULL || bInterval == NULL || endpointAddress == 0) {
        return 0;
    }
    if (!state->Table.Valid) {
        return 0;
    }

    /*
     * Pass 1: the declaration sitting under the alternate setting its interface
     * is actually running. This is the whole reason the entries carry an
     * interface and an alternate at all.
     */
    matched = 0;
    value = 0;
    for (i = 0; i < XHCI_DESC_ISO_ENDPOINTS; i++) {
        ep = &state->Table.Endpoint[i];
        if (ep->Address != endpointAddress) {
            continue;
        }
        /*
         * An interface whose selection this file could not keep makes every
         * declaration under it unanswerable: it may or may not be the running
         * one, and both passes would otherwise treat "we did not record a
         * selection" as "it is at alternate 0".
         */
        if (ep->Alternate != xhciDescSelectedAlt(state, ep->Interface, &known)) {
            if (!known) {
                return 0;
            }
            continue;
        }
        if (!known) {
            return 0;
        }
        /* One interface and setting declared it twice and differently - see
         * XHCI_DESC_ISO_EP.Unusable. */
        if (ep->Unusable) {
            return 0;
        }
        if (!matched) {
            matched = 1;
            value = ep->BInterval;
        } else if (value != ep->BInterval) {
            /* Two interfaces whose selected alternates declare the same
             * endpoint address differently. Nothing here can choose. */
            return 0;
        }
    }
    if (matched) {
        *bInterval = value;
        return 1;
    }

    /*
     * Pass 2: nothing declared it in a selected alternate, which is the shape of
     * an endpoint that exists only in an alternate not yet selected. Whether
     * usbport reopens a pipe before or after its SET_INTERFACE is not
     * established in this repository, and this pass is what makes the answer the
     * same either way whenever the declarations agree - and a refusal, rather
     * than a guess, when they do not.
     *
     * **It requires the table to be complete**, which pass 1 does not: pass 1
     * reads one declaration and is exact, while this pass reasons from the whole
     * set and a set with a declaration missing can agree with itself and still
     * be wrong. Eight alternates asking for 2 and a ninth the table had no room
     * for asking for 4 would otherwise derive 2 for the ninth - a confident
     * wrong cadence where the honest answer is the fallback.
     */
    if (state->Table.Dropped != 0) {
        return 0;
    }
    /*
     * A dropped *selection* needs no gate of its own here, and one stood here
     * until it was shown to be unreachable: pass 1 asks `xhciDescSelectedAlt`
     * about the interface of every declaration of this address, so an address
     * whose interface is untracked-and-dropped has already returned 0 above.
     * Anything reaching this point has known interfaces, and another
     * interface's dropped selection says nothing about this address.
     */
    matched = 0;
    value = 0;
    for (i = 0; i < XHCI_DESC_ISO_ENDPOINTS; i++) {
        ep = &state->Table.Endpoint[i];
        if (ep->Address != endpointAddress) {
            continue;
        }
        if (ep->Unusable) {
            return 0;
        }
        if (!matched) {
            matched = 1;
            value = ep->BInterval;
        } else if (value != ep->BInterval) {
            return 0;
        }
    }
    if (!matched) {
        return 0;
    }
    *bInterval = value;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The walk                                                            */
/* ------------------------------------------------------------------ */

VOID XhciDescParseBegin(PXHCI_DESC_PARSE parse)
{
    ULONG i;

    if (parse == NULL) {
        return;
    }
    parse->Bad = 0;
    parse->Started = 0;
    parse->TotalLength = 0;
    parse->Received = 0;
    parse->Length = 0;
    parse->Pos = 0;
    parse->InInterface = 0;
    parse->Interface = 0;
    parse->Alternate = 0;
    for (i = 0; i < XHCI_DESC_HEAD_BYTES; i++) {
        parse->Head[i] = 0;
    }
    XhciDescTableReset(&parse->Table);
}

/*
 * One complete descriptor has arrived in `Head` (up to XHCI_DESC_HEAD_BYTES of
 * it). IRQL: any.
 */
static VOID xhciDescFoldOne(PXHCI_DESC_PARSE parse)
{
    ULONG type;

    type = (ULONG)parse->Head[XHCI_DESC_OFF_TYPE];

    if (!parse->Started) {
        /*
         * **The first descriptor of the reply must be the configuration
         * itself**, and requiring it is what keeps a walk from wandering
         * through the wrong reply: this is the only descriptor whose
         * `wTotalLength` says where the configuration ends, and without that
         * there is no complete-versus-truncated question to answer.
         */
        if (type != XHCI_DESC_TYPE_CONFIGURATION ||
            parse->Length < XHCI_DESC_CONFIG_BYTES) {
            parse->Bad = 1;
            return;
        }
        parse->TotalLength = (ULONG)parse->Head[XHCI_DESC_OFF_TOTAL_LO] |
                             (((ULONG)parse->Head[XHCI_DESC_OFF_TOTAL_HI]) << 8);
        /* A total that does not even cover the header it was read from is not a
         * short reply, it is a descriptor contradicting itself. */
        if (parse->TotalLength < parse->Length) {
            parse->Bad = 1;
            return;
        }
        parse->Table.ConfigValue =
            (ULONG)parse->Head[XHCI_DESC_OFF_CONFIG_VALUE];
        parse->Started = 1;
        return;
    }

    /*
     * **An interface descriptor is what every endpoint after it belongs to**,
     * and keeping that context is what makes alternate settings resolvable
     * rather than a conflict to be refused. USB 9.6.5: an interface is
     * identified by `bInterfaceNumber` *and* `bAlternateSetting`, and its
     * endpoint descriptors follow it until the next interface descriptor.
     */
    if (type == XHCI_DESC_TYPE_INTERFACE) {
        if (parse->Length < XHCI_DESC_INTERFACE_BYTES) {
            /* Too short to name itself. The walk is still synchronized, but
             * every endpoint until the next interface descriptor now has no
             * context to be filed under - so the context is dropped rather than
             * left describing the *previous* interface. */
            parse->InInterface = 0;
            parse->Table.BadDescriptor++;
            return;
        }
        parse->InInterface = 1;
        parse->Interface = (ULONG)parse->Head[XHCI_DESC_OFF_IF_NUMBER];
        parse->Alternate = (ULONG)parse->Head[XHCI_DESC_OFF_IF_ALTERNATE];
        return;
    }

    if (type != XHCI_DESC_TYPE_ENDPOINT) {
        return;
    }
    /*
     * An endpoint descriptor shorter than seven bytes has no `bInterval` at all.
     * Counted rather than fatal: the walk is still synchronized - `bLength`
     * advanced it - so the rest of the configuration is readable and only this
     * endpoint is lost.
     */
    if (parse->Length < XHCI_DESC_ENDPOINT_BYTES) {
        parse->Table.BadDescriptor++;
        return;
    }
    if ((((ULONG)parse->Head[XHCI_DESC_OFF_EP_ATTRIBUTES]) &
         XHCI_DESC_EP_TYPE_MASK) != XHCI_DESC_EP_TYPE_ISOCH) {
        return;
    }
    /* An endpoint outside any interface belongs to nothing that can be
     * selected. Refused rather than filed under a guess: USB 9.6.6 puts every
     * endpoint descriptor after the interface it belongs to. */
    if (!parse->InInterface) {
        parse->Table.BadDescriptor++;
        return;
    }
    xhciDescRecord(&parse->Table, parse->Interface, parse->Alternate,
                   (ULONG)parse->Head[XHCI_DESC_OFF_EP_ADDRESS],
                   (ULONG)parse->Head[XHCI_DESC_OFF_EP_INTERVAL]);
}

VOID XhciDescParseFeed(PXHCI_DESC_PARSE parse,
                       const UCHAR *data,
                       ULONG length)
{
    ULONG i;
    ULONG b;

    if (parse == NULL || data == NULL) {
        return;
    }

    for (i = 0; i < length; i++) {
        if (parse->Bad) {
            return;
        }
        /*
         * `wTotalLength` bounds the walk, not the transfer's length. usbport
         * asks for whatever the client driver's buffer holds, so a reply may
         * carry trailing bytes that belong to no descriptor of this
         * configuration; walking into them would either invent descriptors or
         * declare the configuration malformed on data it does not own.
         */
        if (parse->Started && parse->Received >= parse->TotalLength) {
            return;
        }

        b = (ULONG)data[i];
        parse->Received++;

        if (parse->Length == 0) {
            /*
             * The first byte of a descriptor is its `bLength`. Below the
             * two-byte minimum the walk cannot advance at all - a zero would
             * loop for ever - so it is the one malformation that stops
             * everything rather than costing one descriptor.
             */
            if (b < XHCI_DESC_MIN_BYTES) {
                parse->Bad = 1;
                return;
            }
            parse->Length = b;
            parse->Pos = 0;
        }

        if (parse->Pos < XHCI_DESC_HEAD_BYTES) {
            parse->Head[parse->Pos] = (UCHAR)b;
        }
        parse->Pos++;

        if (parse->Pos == parse->Length) {
            xhciDescFoldOne(parse);
            parse->Length = 0;
            parse->Pos = 0;
        }
    }
}

ULONG XhciDescParseEnd(PXHCI_DESC_PARSE parse)
{
    if (parse == NULL) {
        return XHCI_DESC_FOLD_MALFORMED;
    }
    if (parse->Bad) {
        return XHCI_DESC_FOLD_MALFORMED;
    }
    /*
     * Not started is the short probe read: usbport asks for the configuration
     * header alone to learn `wTotalLength`, and a reply too short to carry even
     * that produced no reading rather than a bad one.
     */
    if (!parse->Started) {
        return XHCI_DESC_FOLD_PARTIAL;
    }
    if (parse->Received < parse->TotalLength) {
        return XHCI_DESC_FOLD_PARTIAL;
    }
    /*
     * Every byte of the configuration arrived and the last descriptor ended
     * exactly on it. A descriptor still half-collected here claimed a `bLength`
     * running past `wTotalLength`, which is a self-contradiction rather than a
     * short reply - the two are counted apart because one says the device lied
     * and the other says the buffer was small.
     */
    if (parse->Length != 0) {
        return XHCI_DESC_FOLD_MALFORMED;
    }

    parse->Table.Valid = 1;
    return XHCI_DESC_FOLD_COMMIT;
}
