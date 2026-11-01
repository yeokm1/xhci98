/*
 * cm_iso.h - the isolation statement's decision (task 45.35; design record
 * 22 section 10.5, record 17 section 4.13.6): pure, DDK-free, host-tested
 * by test\test_cm_iso.c. The glue that gathers its inputs under cmRegLock
 * and answers xhci98.sys's interface is cm_reenum.c's (CmRegIsoBind,
 * CmRegIsoAsk) and cm_filt.c's.
 *
 * IRQL: any; no lock, no allocation.
 */

#ifndef CM_ISO_H
#define CM_ISO_H

#include "../xhci_compat.h"

/* CmIsoDecide's verdict: CM_ISO_YES, else the first condition that
 * failed, in the order record 22 section 10.5 lists them. */
#define CM_ISO_YES          0UL
#define CM_ISO_UNBOUND      1UL /* no binding was taken                 */
#define CM_ISO_GONE         2UL /* the child is being removed           */
#define CM_ISO_DOMAIN       3UL /* the bound domain is not listed, or
                                   restarted since (Reenum.Starts)       */
#define CM_ISO_PROFILE      4UL /* not this connection manager, or the
                                   domain not started                   */
#define CM_ISO_LOST         5UL /* the host interface lost, or its loss
                                   still draining                       */
#define CM_ISO_MAP          6UL /* the bound port no longer corresponds
                                   to the bound member                  */
#define CM_ISO_OPEN         7UL /* the bound port's path not read back
                                   closed for the bound generation      */
#define CM_ISO_REASONS      8UL

typedef struct _CM_ISO_IN {
    ULONG Bound;            /* the interface took a binding              */
    ULONG Gone;             /* the child's removal has begun             */
    ULONG DomainListed;     /* the bound NHI object is in the list       */
    ULONG StartsSame;       /* its Reenum.Starts equals the bound one    */
    ULONG U4On;             /* it runs the USB4 software CM              */
    ULONG StageActive;      /* CM_STAGE_ACTIVE                           */
    ULONG LiveActive;       /* Live.State is CM_LIVE_ACTIVE              */
    ULONG LossDrain;        /* Reenum.LossDrain                          */
    ULONG Corresponds;      /* XhciTbtPortMember(port) is the bound member,
                               read now                                  */
    ULONG Isolated;         /* TbtU4PortIsolated(port, generation)       */
} CM_ISO_IN, *PCM_ISO_IN;

ULONG CmIsoDecide(const CM_ISO_IN *in);

#endif /* CM_ISO_H */
