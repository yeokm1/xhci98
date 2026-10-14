/*
 * cm_reenum_stub.c - a stand-in for task 39-A.5's cm_reenum.c, so that
 * the NHI driver of task 39-A.4 builds and runs against the seam of
 * cm_reenum.h before that implementation is merged. Each call the NHI
 * makes is counted and logged and nothing else happens: no tunnel port is
 * discovered, nothing is re-enumerated, and a tunnel change reaches the
 * operating system only through a Device Manager refresh or "Scan for
 * hardware changes" (record 21 section 6.4). The registration list's
 * calls (cm_filt.c's) and CmReenumSetEject / CmReenumPorts are not
 * provided: nothing in this branch calls them.
 *
 * REMOVED, with its line in sources, when task 39-A.5's cm_reenum.c is
 * merged.
 *
 * IRQL: PASSIVE_LEVEL, on the connection-manager thread.
 */

#include "cm.h"

VOID CmReenumStart(PCM_NHI nhi)
{
    nhi->Reenum.Starts++;
    nhi->Reenum.Reenum = nhi->Values.Value[CM_VAL_REENUM];
    nhi->Reenum.SafeEject = nhi->Values.Value[CM_VAL_SAFE_EJECT];
    nhi->Reenum.Route = CM_ROUTE_NONE;
    CmLogNote(nhi, "cm.reenum.start", nhi->Reenum.Starts);
}

VOID CmReenumAction(PCM_NHI nhi, const TBT_ICM_ACTION *action)
{
    if (action->Kind == TBT_ICM_ACT_TUNNEL_UP) {
        nhi->Reenum.Ups++;
    } else if (action->Kind == TBT_ICM_ACT_TUNNEL_DOWN) {
        nhi->Reenum.Downs++;
    } else {
        nhi->Reenum.Resyncs++;
    }
    nhi->Reenum.NotDone++;
    CmLogNote(nhi, "cm.reenum.not-done", (action->Kind << 8) | action->Why);
}

VOID CmReenumService(PCM_NHI nhi)
{
    UNREFERENCED_PARAMETER(nhi);
}

VOID CmReenumSuspend(PCM_NHI nhi)
{
    nhi->Reenum.Suspends++;
    CmLogNote(nhi, "cm.reenum.suspend", nhi->Reenum.Suspends);
}

VOID CmReenumResumed(PCM_NHI nhi, BOOLEAN ok)
{
    if (ok) {
        nhi->Reenum.Resumes++;
    } else {
        nhi->Reenum.ResumeFailures++;
    }
    CmLogNote(nhi, "cm.reenum.resumed", ok ? 1UL : 0UL);
}

VOID CmReenumStop(PCM_NHI nhi)
{
    nhi->Reenum.Stops++;
    CmLogNote(nhi, "cm.reenum.stop", nhi->Reenum.Stops);
}
