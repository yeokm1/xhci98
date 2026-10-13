/*
 * xhci_own.h - who holds an endpoint's ring: the pure half of design record
 * 20 section 5 (roadmap-hcd.md task 38.10).
 *
 * A pool ring, and a stream block, go back only once the controller is
 * proven done with them - Linux's rule (xhci.c, xhci_check_bandwidth and
 * xhci_reset_bandwidth): a ring an Endpoint Context may still name is never
 * handed to another endpoint. Each allocation is new (no command naming it
 * issued), owned (installed, or possibly installed), or superseded (a
 * successful Configure Endpoint dropped it, a Reset Device, a Disable Slot,
 * an HCRST or the slot's going). A command has four outcomes: not issued,
 * success, refused (a completion with an error code: the Output Endpoint
 * Contexts the command names "shall be unchanged", xHCI 1.2c 4.6.6 p.106)
 * and unknown (issued, no completion seen) - after which nothing it named
 * is released until the recovery's HCRST.
 *
 * What lives here is the decision of a SELECT_INTERFACE (section 5.3) and
 * of a function's select (section 5.4) at each step - which allocations
 * are freed, put back, held or published, and what is issued next - so
 * hcd_cfg.c, which carries the steps out, decides nothing of its own, and
 * test_own.c drives every outcome through a modelled controller and pool.
 * And whether an interface has one alternate setting, which decides a
 * STALLed SET_INTERFACE (owner, 2026-10-14; section 5.3, step 4).
 *
 * DDK-free: part of the pure core. C89. IRQL: any; nothing here takes a
 * lock or touches the controller.
 */

#ifndef XHCI_OWN_H
#define XHCI_OWN_H

#include "xhci_compat.h"

/* A Configure Endpoint built but never issued, as a completion code: the
 * value hcd_cfg.c returns for it (HCD_CFG_NOT_ISSUED). 0 is a command
 * issued whose completion was never seen. */
#define XHCI_OWN_NOT_ISSUED_CODE    0xFFFFFFFFUL

/* What happened at a step, the event XhciOwnSelectStep takes. */
#define XHCI_OWN_EV_CMD_NOT_ISSUED  1UL /* nothing reached the controller   */
#define XHCI_OWN_EV_CMD_SUCCESS     2UL
#define XHCI_OWN_EV_CMD_REFUSED     3UL /* completed with an error code     */
#define XHCI_OWN_EV_CMD_UNKNOWN     4UL /* issued, no completion seen       */
#define XHCI_OWN_EV_SETIF_DONE      5UL /* SET_INTERFACE completed           */
#define XHCI_OWN_EV_SETIF_STALLED   6UL /* the device STALLed it            */
#define XHCI_OWN_EV_SETIF_FAILED    7UL /* not established: no status stage
                                         * seen, a transaction error, a
                                         * timeout, never sent             */
#define XHCI_OWN_EV_CLEARS_DONE     8UL /* every CLEAR_FEATURE completed   */
#define XHCI_OWN_EV_CLEARS_FAILED   9UL /* one did not, however it failed  */

/* Where a select stands: what it waits for next. */
#define XHCI_OWN_PH_END             0UL
#define XHCI_OWN_PH_CONFIGURE       1UL /* the Configure Endpoint (step 3)  */
#define XHCI_OWN_PH_SETIF           2UL /* SET_INTERFACE (step 4)           */
#define XHCI_OWN_PH_CLEARS          3UL /* one setting's STALL: the clears
                                         * that carry the request out      */
#define XHCI_OWN_PH_ROLLBACK        4UL /* the Configure Endpoint that
                                         * re-instates the old setting     */
#define XHCI_OWN_PH_RECLEARS        5UL /* the rollback's clears           */

/*
 * What the caller does at a step (bits), in this order: the frees and the
 * holds first, then what is published or put back, then what is issued.
 * "Old" is the pipes the select detached, "new" the pipes it made.
 */
#define XHCI_OWN_A_FREE_NEW         0x0001UL /* new pipes and rings freed   */
#define XHCI_OWN_A_FREE_OLD         0x0002UL /* old pipe objects and their
                                              * superseded rings freed    */
#define XHCI_OWN_A_FREE_OLD_STREAMS 0x0004UL /* the old pipes' stream
                                              * blocks freed: a successful
                                              * drop superseded them, and
                                              * no rollback reuses one    */
#define XHCI_OWN_A_RETAIN_NEW       0x0008UL /* new pipes held closed, their
                                              * endpoints stale: owned
                                              * until a proof drops them  */
#define XHCI_OWN_A_RETAIN_OLD       0x0010UL /* the same for the old        */
#define XHCI_OWN_A_PIN_NEW          0x0020UL /* new pipes held closed until
                                              * the slot goes (an unknown
                                              * outcome; the HCRST the
                                              * timeout requested)        */
#define XHCI_OWN_A_PIN_OLD          0x0040UL /* the same for the old        */
#define XHCI_OWN_A_PUBLISH_NEW      0x0080UL /* new pipes published, the
                                              * URB's entries filled      */
#define XHCI_OWN_A_REATTACH_OLD     0x0100UL /* old pipes republished with
                                              * their own handles, this
                                              * select's pause released  */
#define XHCI_OWN_A_STALE_CLEARED    0x0200UL /* the command dropped every
                                              * stale endpoint: no DCI is
                                              * stale before what follows
                                              * marks one                 */
#define XHCI_OWN_A_SET_INTERFACE    0x0400UL /* issue SET_INTERFACE         */
#define XHCI_OWN_A_CLEAR_NEW        0x0800UL /* CLEAR_FEATURE(ENDPOINT_
                                              * HALT) to each new non-
                                              * isochronous endpoint      */
#define XHCI_OWN_A_ROLLBACK         0x1000UL /* the old rings restarted,
                                              * then the Configure Endpoint
                                              * dropping the new endpoints
                                              * and adding the old        */
#define XHCI_OWN_A_CLEAR_OLD        0x2000UL /* CLEAR_FEATURE(ENDPOINT_
                                              * HALT) to each old non-
                                              * isochronous endpoint      */

/* The answer a select ends with (XhciOwnSelectStep's *answer), which the
 * caller turns into a USBD status. */
#define XHCI_OWN_ANS_PENDING        0UL /* not ended                        */
#define XHCI_OWN_ANS_SUCCESS        1UL
#define XHCI_OWN_ANS_COMMAND        2UL /* the refusal's own status: the
                                         * command's, or the pool's or the
                                         * parameters' before any command */
#define XHCI_OWN_ANS_STALL          3UL /* the device STALLed SET_INTERFACE */
#define XHCI_OWN_ANS_FAILED         4UL /* not established                  */

/*
 * One select's place, kept by the caller between steps. Single: the
 * interface has one alternate setting (XhciOwnSingleSetting). Streams: an
 * old pipe had streams open, so the old setting cannot be re-instated (a
 * fresh, streamless context cannot honour its stream handles). Rollback:
 * the select may re-instate - a SELECT_INTERFACE; a function's select
 * sends SET_INTERFACE to several interfaces and re-instates nothing
 * (section 5.4), so it never sets it.
 */
typedef struct _XHCI_OWN_SELECT {
    ULONG Phase;
    ULONG Single;
    ULONG Streams;
    ULONG Rollback;
} XHCI_OWN_SELECT, *PXHCI_OWN_SELECT;

/* A select begins, waiting for its Configure Endpoint. */
VOID XhciOwnSelectBegin(PXHCI_OWN_SELECT s, ULONG rollback, ULONG single,
                        ULONG streams);

/*
 * One step: `event` is what the phase waited for. Returns the actions
 * (XHCI_OWN_A_*), moves s->Phase on, and sets *answer once the select has
 * ended (XHCI_OWN_PH_END), XHCI_OWN_ANS_PENDING until then. An event the
 * phase does not wait for, or a step after the end, ends the select as an
 * unknown outcome would: every allocation it holds pinned, failed - the
 * conservative answer to a caller's slip. IRQL: any.
 */
ULONG XhciOwnSelectStep(PXHCI_OWN_SELECT s, ULONG event, PULONG answer);

/*
 * A Configure Endpoint that only drops - a function's unconfigure or a
 * removed function's release (section 5.4) - as actions on the pipes it
 * detached ("old"): a success drops every stale endpoint too, and frees
 * the old with their stream blocks; a refusal, or nothing issued, leaves
 * their endpoints enabled, so the old are held stale for the next command
 * to drop; an unknown outcome pins them. IRQL: any.
 */
ULONG XhciOwnDropStep(ULONG event);

/* A Configure Endpoint's completion code as an event: XHCI_OWN_NOT_ISSUED_
 * CODE not issued, 0 unknown, 1 (Success) success, any other refused. */
ULONG XhciOwnCommandEvent(ULONG code);

/*
 * Whether interface `number` has exactly one alternate setting in the
 * configuration descriptor `config` (`length` bytes, walked to its
 * wTotalLength, which `length` must reach): 1 when exactly one interface descriptor
 * carries that bInterfaceNumber and the walk reached its end cleanly. A
 * descriptor shorter than its kind, a zero or overlong bLength, a header
 * too short - any walk that cannot finish - answers 0, so a malformed
 * configuration never turns a STALL into a success. IRQL: any.
 */
ULONG XhciOwnSingleSetting(const UCHAR *config, ULONG length, ULONG number);

#endif /* XHCI_OWN_H */
