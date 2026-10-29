/*
 * cm_iso.c - the isolation statement's decision (cm_iso.h; task 45.35,
 * design record 22 section 10.5). Pure.
 */

#include "cm_iso.h"

ULONG CmIsoDecide(const CM_ISO_IN *in)
{
    if (in == NULL || !in->Bound) {
        return CM_ISO_UNBOUND;
    }
    if (in->Gone) {
        return CM_ISO_GONE;
    }
    if (!in->DomainListed || !in->StartsSame) {
        return CM_ISO_DOMAIN;
    }
    if (!in->U4On || !in->StageActive) {
        return CM_ISO_PROFILE;
    }
    if (!in->LiveActive || in->LossDrain) {
        return CM_ISO_LOST;
    }
    if (!in->Corresponds) {
        return CM_ISO_MAP;
    }
    if (!in->Isolated) {
        return CM_ISO_OPEN;
    }
    return CM_ISO_YES;
}
