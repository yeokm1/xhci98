/*
 * tbt_chain.c - the chained tunnel's structural and occupancy bindings and
 * their transaction (design record 34 section 3.12, roadmap-usb4.md task
 * 48.30). The pure half; the rules are tbt_chain.h's.
 *
 * C89, DDK-free; no kernel service, lock or allocation. IRQL as
 * tbt_chain.h says.
 */

#include "tbt_chain.h"

static VOID xhciChainZero(PVOID p, ULONG bytes)
{
    PUCHAR b;
    ULONG i;

    b = (PUCHAR)p;
    for (i = 0; i < bytes; i++) {
        b[i] = 0;
    }
}

static ULONG xhciChainRd(PXHCI_TBT_CFG io, ULONG bdf, ULONG offset,
                         PULONG value)
{
    *value = 0xFFFFFFFFUL;
    io->Reads++;
    if (!io->Read(io->Context, bdf, offset, value)) {
        *value = 0xFFFFFFFFUL;
        return 0;
    }
    return 1;
}

ULONG XhciChainRead(PXHCI_TBT_CFG io, ULONG memberBdf, ULONG member,
                    PXHCI_CHAIN_BASE out)
{
    XHCI_TBT_CHAIN_SET set;
    ULONG rc;
    ULONG buses;
    ULONG lo;
    ULONG hi;
    ULONG sec;
    ULONG sub;
    ULONG bus;
    ULONG id;
    ULONG vendor;
    ULONG dev;
    ULONG i;

    if (out == NULL) {
        return XHCI_CHAIN_READ_WALK;
    }
    xhciChainZero(out, sizeof(*out));
    if (io == NULL || io->Read == NULL || memberBdf > 0xFFFFUL) {
        return XHCI_CHAIN_READ_WALK;
    }
    rc = XhciTbtChainPorts(io, memberBdf, member, &set);
    if (rc != XHCI_TBT_CHAIN_COMPLETE) {
        /* A read that failed or answered Retry Status may read whole at
         * the next try; anything else is the walk's own refusal. */
        return (rc == XHCI_TBT_CHAIN_INCOMPLETE &&
                (set.Why == XHCI_TBT_CHAIN_WHY_READ ||
                 set.Why == XHCI_TBT_CHAIN_WHY_RETRY))
                   ? XHCI_CHAIN_READ_UNREAD
                   : XHCI_CHAIN_READ_WALK;
    }
    if (set.Over != 0) {
        return XHCI_CHAIN_READ_WALK;
    }
    if (set.Count == 0) {
        return XHCI_CHAIN_READ_NONE;
    }
    out->Walk = set.Count;
    for (i = 0; i < set.Count && i < XHCI_TBT_CHAIN_MAX; i++) {
        out->Bdf[i] = set.Port[i].Bdf;
        out->Buses[i] = XHCI_CHAIN_BUSES(set.Port[i].Secondary,
                                         set.Port[i].Subordinate);
    }
    if (!xhciChainRd(io, memberBdf, XHCI_TBT_CFG_BUSES, &buses) ||
        buses == 0xFFFFFFFFUL) {
        return XHCI_CHAIN_READ_UNREAD;
    }
    lo = (buses >> 8) & 0xFFUL;
    hi = (buses >> 16) & 0xFFUL;
    for (i = 0; i < set.Count && i < XHCI_TBT_CHAIN_MAX; i++) {
        bus = XHCI_TBT_BDF_BUS(set.Port[i].Bdf);
        if (!xhciChainRd(io, set.Port[i].Bdf, XHCI_TBT_CFG_BUSES, &buses) ||
            buses == 0xFFFFFFFFUL) {
            out->Count = i;
            return XHCI_CHAIN_READ_UNREAD;
        }
        sec = (buses >> 8) & 0xFFUL;
        sub = (buses >> 16) & 0xFFUL;
        if (sec <= bus || sec <= lo || sub < sec || sub > hi ||
            XHCI_CHAIN_BUSES(sec, sub) != out->Buses[i]) {
            /* Misnumbered, or numbered otherwise than the walk just read:
             * moving under the reading. */
            return XHCI_CHAIN_READ_NUMBER;
        }
        out->Scanned = i + 1;
        for (dev = 0; dev < 32UL; dev++) {
            if (!xhciChainRd(io, XHCI_TBT_BDF(sec, dev, 0), XHCI_TBT_CFG_ID,
                             &id)) {
                out->Count = i;
                out->PartDev = dev;
                return XHCI_CHAIN_READ_UNREAD;
            }
            vendor = id & 0xFFFFUL;
            if (vendor == 0x0001UL) {
                /* Configuration Request Retry Status: present, not ready;
                 * neither absent nor known. */
                out->Count = i;
                out->PartDev = dev;
                return XHCI_CHAIN_READ_UNREAD;
            }
            if (vendor != 0xFFFFUL && vendor != 0UL) {
                out->Seen[i] |= 1UL << dev;
            }
        }
    }
    out->Count = i;
    return XHCI_CHAIN_READ_OK;
}

static BOOLEAN xhciChainSame(const XHCI_CHAIN_BASE *a,
                             const XHCI_CHAIN_BASE *b, BOOLEAN seen)
{
    ULONG i;

    if (a->Count != b->Count) {
        return FALSE;
    }
    for (i = 0; i < a->Count; i++) {
        if (a->Bdf[i] != b->Bdf[i] || a->Buses[i] != b->Buses[i] ||
            (seen && a->Seen[i] != b->Seen[i])) {
            return FALSE;
        }
    }
    return TRUE;
}

VOID XhciChainInit(PXHCI_CHAIN c, BOOLEAN keepEpoch)
{
    ULONG epoch;

    if (c == NULL) {
        return;
    }
    epoch = keepEpoch ? c->NextEpoch : 0;
    xhciChainZero(c, sizeof(*c));
    c->NextEpoch = epoch != 0 ? epoch : 1UL;
}

static VOID xhciChainClose(PXHCI_CHAIN c)
{
    c->Txn.Open = 0;
}

/* Another kid than `kid` unsettled. */
static BOOLEAN xhciChainOther(const XHCI_CHAIN *c, ULONG kid)
{
    ULONG i;

    for (i = 0; i < XHCI_CHAIN_KIDS; i++) {
        if (c->Unset[i].Used && c->Unset[i].Kid != kid) {
            return TRUE;
        }
    }
    return FALSE;
}

ULONG XhciChainMayConnect(PXHCI_CHAIN c, ULONG nowMs)
{
    ULONG i;

    if (c == NULL) {
        return 1;
    }
    if (c->Txn.Open && nowMs - c->Txn.OpenMs < XHCI_CHAIN_TXN_MS) {
        c->Held++;
        return 0;
    }
    for (i = 0; i < XHCI_CHAIN_KIDS; i++) {
        if (c->Unset[i].Used &&
            nowMs - c->Unset[i].SinceMs < XHCI_CHAIN_TXN_MS) {
            c->Held++;
            return 0;
        }
    }
    return 1;
}

ULONG XhciChainArm(PXHCI_CHAIN c, PXHCI_TBT_CFG io, const XHCI_CHAIN_KID *k,
                   BOOLEAN blocked, ULONG nowMs, ULONG armSeq,
                   PULONG readWhy)
{
    XHCI_CHAIN_BASE a;
    XHCI_CHAIN_BASE b;
    ULONG i;
    ULONG slot;
    ULONG rc;
    BOOLEAN other;

    if (readWhy != NULL) {
        *readWhy = XHCI_CHAIN_READ_OK;
    }
    if (c == NULL || k == NULL || k->Kid == 0 || k->Kid > XHCI_CHAIN_KIDS) {
        return XHCI_CHAIN_ARM_PARAM;
    }
    /* A transaction of this kid's slot from an earlier connection or
     * attempt ends here: this Path Enable is a new one. */
    if (c->Txn.Open && c->Txn.K.Kid == k->Kid) {
        xhciChainClose(c);
        c->Cancelled++;
    }
    other = (BOOLEAN)(blocked || xhciChainOther(c, k->Kid));
    slot = XHCI_CHAIN_KIDS;
    for (i = 0; i < XHCI_CHAIN_KIDS; i++) {
        if (c->Unset[i].Used && c->Unset[i].Kid == k->Kid) {
            slot = i;
        }
    }
    for (i = 0; i < XHCI_CHAIN_KIDS && slot == XHCI_CHAIN_KIDS; i++) {
        if (!c->Unset[i].Used) {
            slot = i;
        }
    }
    if (slot < XHCI_CHAIN_KIDS) {
        c->Unset[slot].Used = 1;
        c->Unset[slot].Kid = k->Kid;
        c->Unset[slot].KidGen = k->KidGen;
        c->Unset[slot].SinceMs = nowMs;
    }
    c->NotArmed++;
    if (c->Txn.Open) {
        return XHCI_CHAIN_ARM_BUSY;
    }
    if (other) {
        return XHCI_CHAIN_ARM_OTHER;
    }
    if (k->Member == XHCI_TBT_NO_MEMBER || k->MemberBdf > 0xFFFFUL ||
        io == NULL) {
        return XHCI_CHAIN_ARM_MEMBER;
    }
    rc = XhciChainRead(io, k->MemberBdf, k->Member, &a);
    if (rc == XHCI_CHAIN_READ_OK) {
        rc = XhciChainRead(io, k->MemberBdf, k->Member, &b);
    }
    if (rc != XHCI_CHAIN_READ_OK) {
        if (readWhy != NULL) {
            *readWhy = rc;
        }
        return XHCI_CHAIN_ARM_READ;
    }
    if (!xhciChainSame(&a, &b, TRUE)) {
        return XHCI_CHAIN_ARM_UNSTABLE;
    }
    /* The link evidence (record 22 section 7.6.4): before the kid's Path
     * Enable no link of its can be up, so an empty candidate's port read
     * down now and up at the gain is that link coming up. */
    for (i = 0; i < a.Count && i < XHCI_TBT_CHAIN_MAX; i++) {
        if (a.Seen[i] == 0 && XhciTbtPortDown(io, a.Bdf[i])) {
            a.Down |= 1UL << i;
        }
    }
    c->NotArmed--;
    c->Armed++;
    c->Txn.Open = 1;
    c->Txn.K = *k;
    c->Txn.Base = a;
    c->Txn.OpenMs = nowMs;
    c->Txn.ArmSeq = armSeq;
    c->Txn.Answered = 0;
    return XHCI_CHAIN_ARM_OPEN;
}

/* The binding of the transaction's parent link, or XHCI_CHAIN_BINDS. */
static ULONG xhciChainLink(const XHCI_CHAIN *c, const XHCI_CHAIN_KID *k)
{
    ULONG i;

    for (i = 0; i < XHCI_CHAIN_BINDS; i++) {
        if (c->Bind[i].Used && c->Bind[i].Port == k->Port &&
            c->Bind[i].ParentGen == k->ParentGen &&
            c->Bind[i].ParentPcieDown == k->ParentPcieDown &&
            c->Bind[i].ParentUuid[0] == k->ParentUuid[0] &&
            c->Bind[i].ParentUuid[1] == k->ParentUuid[1]) {
            return i;
        }
    }
    return XHCI_CHAIN_BINDS;
}

ULONG XhciChainTry(PXHCI_CHAIN c, PXHCI_TBT_CFG io, BOOLEAN live,
                   BOOLEAN blocked, ULONG nowMs, PULONG bind)
{
    PXHCI_CHAIN_TXN t;
    XHCI_CHAIN_BASE now;
    ULONG rc;
    ULONG i;
    ULONG b;
    ULONG gains;
    ULONG lost;
    ULONG mask;
    ULONG s;
    PXHCI_CHAIN_BIND p;

    if (bind != NULL) {
        *bind = XHCI_CHAIN_BINDS;
    }
    if (c == NULL || !c->Txn.Open) {
        return XHCI_CHAIN_TRY_NONE;
    }
    t = &c->Txn;
    if (!live) {
        xhciChainClose(c);
        c->Cancelled++;
        return XHCI_CHAIN_TRY_CANCELLED;
    }
    if (nowMs - t->OpenMs >= XHCI_CHAIN_TXN_MS) {
        xhciChainClose(c);
        c->Expired++;
        return XHCI_CHAIN_TRY_EXPIRED;
    }
    if (io == NULL) {
        return XHCI_CHAIN_TRY_WAIT;
    }
    rc = XhciChainRead(io, t->K.MemberBdf, t->K.Member, &now);
    if (rc == XHCI_CHAIN_READ_UNREAD) {
        /* A failed read decides nothing, so whatever it did see differ
         * from the baseline spoils the transaction (record 22 section
         * 7.6.4): another candidate set (the walk complete: its count or a
         * place), a candidate's buses, or any device gained or lost where
         * it was read - a whole reading would spoil or decide on each, and
         * none is left to a later reading; nor is a candidate already
         * answered (waiting on its port reading) that it reached, whose
         * gain only a whole reading decides. now.Count is the candidates
         * scanned whole, now.PartDev the devices read of the next,
         * now.Scanned the candidates whose scan began; every candidate's
         * buses are the walk's. */
        if (now.Walk == 0) {
            /* The walk itself failed: what it saw of the parent's switch
             * (a candidate gone, found, renumbered) cannot be told from a
             * read of it that failed, so the set is not known unchanged:
             * spoiled. The walk never descends below a candidate, so the
             * kid's own functions answering Retry Status never fail it. */
            xhciChainClose(c);
            c->Spoiled++;
            return XHCI_CHAIN_TRY_SPOILED;
        }
        if (now.Walk != t->Base.Count) {
            xhciChainClose(c);
            c->Spoiled++;
            return XHCI_CHAIN_TRY_SPOILED;
        }
        for (i = 0; i < now.Walk && i < XHCI_TBT_CHAIN_MAX; i++) {
            if (now.Bdf[i] != t->Base.Bdf[i]) {
                xhciChainClose(c);
                c->Spoiled++;
                return XHCI_CHAIN_TRY_SPOILED;
            }
        }
        for (i = 0; i < t->Base.Count && i < XHCI_TBT_CHAIN_MAX; i++) {
            if (now.Buses[i] != t->Base.Buses[i]) {
                xhciChainClose(c);
                c->Spoiled++;
                return XHCI_CHAIN_TRY_SPOILED;
            }
            if (i >= now.Scanned) {
                continue;
            }
            mask = i < now.Count ? 0xFFFFFFFFUL
                   : now.PartDev >= 32UL ? 0xFFFFFFFFUL
                   : ((1UL << now.PartDev) - 1UL);
            /* An answered candidate reached at all: its gain is still to
             * be decided, and only a whole reading decides it. */
            lost = ((t->Answered >> i) & 1UL) != 0
                       ? 1UL
                       : ((now.Seen[i] ^ t->Base.Seen[i]) & mask);
            if (lost != 0) {
                xhciChainClose(c);
                c->Spoiled++;
                return XHCI_CHAIN_TRY_SPOILED;
            }
        }
        return XHCI_CHAIN_TRY_WAIT;
    }
    if (rc != XHCI_CHAIN_READ_OK || !xhciChainSame(&t->Base, &now, FALSE)) {
        xhciChainClose(c);
        c->Spoiled++;
        return XHCI_CHAIN_TRY_SPOILED;
    }
    gains = 0;
    b = 0;
    for (i = 0; i < now.Count; i++) {
        if ((t->Base.Seen[i] & ~now.Seen[i]) != 0 ||
            (((t->Answered >> i) & 1UL) != 0 && now.Seen[i] == 0)) {
            /* A device lost: something moved below the member. */
            xhciChainClose(c);
            c->Spoiled++;
            return XHCI_CHAIN_TRY_SPOILED;
        }
        if ((now.Seen[i] & ~t->Base.Seen[i]) != 0) {
            gains++;
            b = i;
        }
    }
    if (gains == 0) {
        return XHCI_CHAIN_TRY_WAIT;
    }
    if (gains > 1) {
        xhciChainClose(c);
        c->Spoiled++;
        return XHCI_CHAIN_TRY_SPOILED;
    }
    if (t->Base.Seen[b] != 0 || blocked || xhciChainOther(c, t->K.Kid)) {
        xhciChainClose(c);
        c->Spoiled++;
        return XHCI_CHAIN_TRY_SEVERAL;
    }
    /* What B gained must be a tunnel's far end, or carry the link's
     * evidence: a function of the parent's own that answers late (its
     * xHCI behind a hot-plug capable port of its switch) is never the
     * kid's. A shape or a port not read now is read again. */
    s = XhciTbtGainAdmits(io, now.Bdf[b], now.Buses[b] & 0xFFUL,
                          now.Seen[b], (t->Base.Down >> b) & 1UL);
    if (s == XHCI_TBT_GAIN_UNREAD) {
        /* B's arm-time evidence stands only while B keeps answering. */
        t->Answered |= 1UL << b;
        return XHCI_CHAIN_TRY_WAIT;
    }
    if (s != XHCI_TBT_GAIN_YES) {
        xhciChainClose(c);
        c->Spoiled++;
        return XHCI_CHAIN_TRY_SHAPE;
    }
    /* A chain port is one parent link's: any binding naming it on another
     * link, or this link's naming another port, or this link's still
     * occupied by another kid, is a conflict that withdraws them all. */
    s = xhciChainLink(c, &t->K);
    rc = XHCI_CHAIN_TRY_LEARNED;
    for (i = 0; i < XHCI_CHAIN_BINDS; i++) {
        p = &c->Bind[i];
        if (!p->Used) {
            continue;
        }
        if (i == s) {
            if (p->Bdf != now.Bdf[b] || p->MemberBdf != t->K.MemberBdf ||
                p->Buses != now.Buses[b] ||
                (p->Occupied &&
                 (p->Kid != t->K.Kid || p->KidGen != t->K.KidGen))) {
                rc = XHCI_CHAIN_TRY_CONFLICT;
            }
        } else if (p->Bdf == now.Bdf[b] && p->MemberBdf == t->K.MemberBdf) {
            rc = XHCI_CHAIN_TRY_CONFLICT;
        }
    }
    if (rc == XHCI_CHAIN_TRY_CONFLICT) {
        for (i = 0; i < XHCI_CHAIN_BINDS; i++) {
            p = &c->Bind[i];
            if (p->Used &&
                (i == s || (p->Bdf == now.Bdf[b] &&
                            p->MemberBdf == t->K.MemberBdf))) {
                XhciChainWithdraw(c, i);
            }
        }
        xhciChainClose(c);
        c->Conflicts++;
        c->Spoiled++;
        return XHCI_CHAIN_TRY_CONFLICT;
    }
    if (s == XHCI_CHAIN_BINDS) {
        for (i = 0; i < XHCI_CHAIN_BINDS && s == XHCI_CHAIN_BINDS; i++) {
            if (!c->Bind[i].Used) {
                s = i;
            }
        }
        if (s == XHCI_CHAIN_BINDS) {
            xhciChainClose(c);
            c->Spoiled++;
            return XHCI_CHAIN_TRY_FULL;
        }
        p = &c->Bind[s];
        xhciChainZero(p, sizeof(*p));
        p->Used = 1;
        p->Port = t->K.Port;
        p->ParentGen = t->K.ParentGen;
        p->ParentUuid[0] = t->K.ParentUuid[0];
        p->ParentUuid[1] = t->K.ParentUuid[1];
        p->ParentPcieDown = t->K.ParentPcieDown;
        p->Member = t->K.Member;
        p->MemberBdf = t->K.MemberBdf;
        p->Bdf = now.Bdf[b];
        p->Buses = now.Buses[b];
        p->Epoch = c->NextEpoch;
        c->NextEpoch++;
        if (c->NextEpoch == 0) {
            c->NextEpoch = 1;
        }
    }
    p = &c->Bind[s];
    p->Occupied = 1;
    p->Kid = t->K.Kid;
    p->KidGen = t->K.KidGen;
    p->KidRoute[0] = t->K.KidRoute[0];
    p->KidRoute[1] = t->K.KidRoute[1];
    p->KidUuid[0] = t->K.KidUuid[0];
    p->KidUuid[1] = t->K.KidUuid[1];
    p->ArmSeq = t->ArmSeq;
    p->LearnSeq = 0;
    for (i = 0; i < XHCI_CHAIN_KIDS; i++) {
        if (c->Unset[i].Used && c->Unset[i].Kid == t->K.Kid) {
            c->Unset[i].Used = 0;
        }
    }
    xhciChainClose(c);
    c->Learned++;
    if (bind != NULL) {
        *bind = s;
    }
    return XHCI_CHAIN_TRY_LEARNED;
}

VOID XhciChainWithdraw(PXHCI_CHAIN c, ULONG i)
{
    if (c == NULL || i >= XHCI_CHAIN_BINDS || !c->Bind[i].Used) {
        return;
    }
    xhciChainZero(&c->Bind[i], sizeof(c->Bind[i]));
    c->Withdrawn++;
}

VOID XhciChainWithdrawAll(PXHCI_CHAIN c)
{
    ULONG i;

    if (c == NULL) {
        return;
    }
    for (i = 0; i < XHCI_CHAIN_BINDS; i++) {
        XhciChainWithdraw(c, i);
    }
    if (c->Txn.Open) {
        xhciChainClose(c);
        c->Cancelled++;
    }
    for (i = 0; i < XHCI_CHAIN_KIDS; i++) {
        c->Unset[i].Used = 0;
    }
}

ULONG XhciChainKidGone(PXHCI_CHAIN c, ULONG kid, ULONG kidGen)
{
    ULONG i;
    ULONG gone;

    gone = 0;
    if (c == NULL || kid == 0) {
        return 0;
    }
    for (i = 0; i < XHCI_CHAIN_KIDS; i++) {
        if (c->Unset[i].Used && c->Unset[i].Kid == kid &&
            (kidGen == 0 || c->Unset[i].KidGen == kidGen)) {
            c->Unset[i].Used = 0;
        }
    }
    if (c->Txn.Open && c->Txn.K.Kid == kid &&
        (kidGen == 0 || c->Txn.K.KidGen == kidGen)) {
        xhciChainClose(c);
        c->Cancelled++;
    }
    for (i = 0; i < XHCI_CHAIN_BINDS; i++) {
        if (c->Bind[i].Used && c->Bind[i].Occupied &&
            c->Bind[i].Kid == kid &&
            (kidGen == 0 || c->Bind[i].KidGen == kidGen)) {
            c->Bind[i].Occupied = 0;
            c->Bind[i].Kid = 0;
            c->Bind[i].KidGen = 0;
            c->Bind[i].ArmSeq = 0;
            c->Bind[i].LearnSeq = 0;
            c->Withdrawn++;
            gone = 1;
        }
    }
    return gone;
}

ULONG XhciChainKidCheck(PXHCI_CHAIN c, ULONG kid, BOOLEAN used, ULONG gen)
{
    ULONG i;
    ULONG gone;

    if (c == NULL || kid == 0) {
        return 0;
    }
    if (!used) {
        return XhciChainKidGone(c, kid, 0);
    }
    gone = 0;
    for (i = 0; i < XHCI_CHAIN_KIDS; i++) {
        if (c->Unset[i].Used && c->Unset[i].Kid == kid &&
            c->Unset[i].KidGen != gen) {
            c->Unset[i].Used = 0;
        }
    }
    if (c->Txn.Open && c->Txn.K.Kid == kid && c->Txn.K.KidGen != gen) {
        xhciChainClose(c);
        c->Cancelled++;
    }
    for (i = 0; i < XHCI_CHAIN_BINDS; i++) {
        if (c->Bind[i].Used && c->Bind[i].Occupied &&
            c->Bind[i].Kid == kid && c->Bind[i].KidGen != gen) {
            c->Bind[i].Occupied = 0;
            c->Bind[i].Kid = 0;
            c->Bind[i].KidGen = 0;
            c->Bind[i].ArmSeq = 0;
            c->Bind[i].LearnSeq = 0;
            c->Withdrawn++;
            gone = 1;
        }
    }
    return gone;
}

ULONG XhciChainPortCheck(PXHCI_CHAIN c, ULONG port, BOOLEAN up, ULONG gen)
{
    ULONG i;
    ULONG mask;

    mask = 0;
    if (c == NULL) {
        return 0;
    }
    for (i = 0; i < XHCI_CHAIN_BINDS; i++) {
        if (c->Bind[i].Used && c->Bind[i].Port == port &&
            (!up || c->Bind[i].ParentGen != gen)) {
            XhciChainWithdraw(c, i);
            mask |= 1UL << i;
        }
    }
    if (c->Txn.Open && c->Txn.K.Port == port &&
        (!up || c->Txn.K.ParentGen != gen)) {
        xhciChainClose(c);
        c->Cancelled++;
    }
    return mask;
}

ULONG XhciChainOfKid(const XHCI_CHAIN *c, ULONG kid, ULONG kidGen)
{
    ULONG i;

    if (c == NULL || kid == 0) {
        return XHCI_CHAIN_BINDS;
    }
    for (i = 0; i < XHCI_CHAIN_BINDS; i++) {
        if (c->Bind[i].Used && c->Bind[i].Occupied &&
            c->Bind[i].Kid == kid &&
            (kidGen == 0 || c->Bind[i].KidGen == kidGen)) {
            return i;
        }
    }
    return XHCI_CHAIN_BINDS;
}

ULONG XhciChainOfPort(const XHCI_CHAIN *c, ULONG memberBdf, ULONG bdf)
{
    ULONG i;

    if (c == NULL) {
        return XHCI_CHAIN_BINDS;
    }
    for (i = 0; i < XHCI_CHAIN_BINDS; i++) {
        if (c->Bind[i].Used && c->Bind[i].Bdf == bdf &&
            c->Bind[i].MemberBdf == memberBdf) {
            return i;
        }
    }
    return XHCI_CHAIN_BINDS;
}

ULONG XhciChainIncarnation(PXHCI_CHAIN c, ULONG i, ULONG inc)
{
    if (c == NULL || i >= XHCI_CHAIN_BINDS || !c->Bind[i].Used || inc == 0) {
        return 0;
    }
    if (c->Bind[i].Inc == 0) {
        c->Bind[i].Inc = inc;
        return 1;
    }
    if (c->Bind[i].Inc == inc) {
        return 1;
    }
    XhciChainWithdraw(c, i);
    return 0;
}

VOID XhciChainToken(const XHCI_CHAIN *c, ULONG i, PULONG tok)
{
    const XHCI_CHAIN_BIND *p;

    tok[0] = 0;
    tok[1] = 0;
    tok[2] = 0;
    if (c == NULL || i >= XHCI_CHAIN_BINDS) {
        return;
    }
    p = &c->Bind[i];
    if (!p->Used || !p->Occupied) {
        return;
    }
    tok[0] = XHCI_CHAIN_TOK_VALID | (p->Port & 0xFFUL) |
             ((p->Kid & 0xFFUL) << 8);
    tok[1] = p->KidGen;
    tok[2] = p->Epoch;
}

ULONG XhciChainTokenLive(const XHCI_CHAIN *c, ULONG i, const ULONG *tok)
{
    ULONG now[XHCI_CHAIN_TOK_WORDS];

    if (tok == NULL || (tok[0] & XHCI_CHAIN_TOK_VALID) == 0) {
        return 0;
    }
    XhciChainToken(c, i, now);
    return (ULONG)(now[0] == tok[0] && now[1] == tok[1] && now[2] == tok[2]);
}
