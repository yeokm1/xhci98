#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
"""q9log.py <trace.log> [--all] [--ejects]: Q-9x timeline and ordering check from a usb4-emu trace.

Prints debug-port stage codes (stor98 q9x 0xE0-0xF9, pool9x 0x120-0x132, G9 0x1D0-0x1DF, cm9x 0x1C7-0x1CF and 0x1B5-0x1B6 (int-0.2 numbering)),
tunnel gates, the stage-2 cutoffs and harness events in host ms, then checks every CM
statement (0x1CB) on its own tunnel port and generation, inside its transaction: from the
statement up to the port's next statement or UP (0x1CA), whichever comes first.

  cutoff(tb<port>) < CM DOWN_QUIESCED (0x1CB, port P, generation G)
    < stor98 receipt (0xE9, bus of P, G) < stor98 acknowledgement (0xEA, bus of P, G)
    < CM acknowledgement seen (0x1CC, port P, acked bit set) < removal of P
  removal = Remove_SubTree (0x1CD 0x0D) or retirement (0x1B6 0xC0+port) of port P

Verdicts:
  PASS        acknowledged, every event present, in order, within the transaction.
  TIMEOUT     the CM's wait ended without an acknowledgement (0x1CC, acked bit clear) and a
              removal followed; a receiver acknowledgement before it is accepted only as a
              late one (the CM logged 0x1B6 0xBC00|port between the two).
  AMBIGUOUS   old traces only: the only removal, an early removal or a late marker carries
              no port and cannot be attributed to this statement.
  NO-INSTANCE the port's endpoint devnode was retired (0x1B6 0xC0+port) after its last UP
              and before the statement, and no receipt followed.
  NEGATIVE    a statement without QUIESCED, or before the cutoff (negative tests).
  INCOMPLETE  an event the verdict needs is missing from the transaction.
  FAIL        events present but out of order, a removal inside the transaction before the
              CM's acknowledgement or timeout, or a receiver acknowledgement before a CM
              timeout that the CM did not log as late.
The port's bus is taken as it was when the statement was made.

Traces made before the interface 3 re-review lack port and bus fields in some stage
arguments; such statements are matched by generation and time only and marked "(by gen)".

--ejects also decodes every CM eject (the R11 removal transaction), keyed on the stage
codes of the current tree: cm9x 0x1CE (step << 24: 1 request, 2 vetoed, 3 REMOVE_PENDING
with port << 20 | sequence, 4 acknowledgement taken with outcome << 16, 6 not clean and
not isolated (the CM isolates first), 7 clean, 8 O6), 0x1CD Remove_SubTree (port << 16),
0x1B6 0xC0+port retirement and 0xBD00+port late acknowledgement; stor98 pool9x 0x12A
(request seen), 0x12B (steps), 0x12C (RemoveAck: outcome, not published 0x80, sequence
<< 8, detail << 24), 0x12E (overlap), 0x126 (identity lost) and 0x67 (HwAdapterState,
arg 0 = FALSE). An eject is matched to stor98's events by its sequence number.

Eject verdicts:
  CLEAN        RemoveAck CLEAN taken by the CM, Remove_SubTree succeeded, clean end (step 7).
  ISOLATED     RemoveAck ISOLATED_NOT_CLEAN taken, Remove_SubTree succeeded, devnode retired.
  CM-ISOLATES  FAILED or no valid acknowledgement (FF): the CM isolates first (step 6).
  VETOED       Query_Remove_SubTree refused: no request was published (step 2).
  O6           Remove_SubTree kept failing for 30 s (step 8).
  INCOMPLETE   no end of the transaction in the trace.
  FAIL         events out of order (a successful Remove_SubTree before the CM's
               acknowledgement step included), an outcome the CM took that differs from
               stor98's, a RemoveAck written twice, or a published acknowledgement the CM
               did not see in time and did not log as late.
  AMBIGUOUS    two ports had a transaction open with the same sequence number (cm9x
               counts per port and the events carry no port), so they cannot be judged.

Exit status: 0 when no statement and no eject is FAIL or INCOMPLETE and no eject is
AMBIGUOUS (with --ejects, also 1 when stage lines exist but no eject was found); 1
otherwise; 2 for a usage error.
"""
import re
import sys

NAMES = {
    0x1B5: "cm TEARDOWN request (port 1)", 0x1B6: "cm diag",
    0x1C7: "cm ERROR", 0x1C8: "cm start", 0x1C9: "cm SHPC slot", 0x1CA: "cm UP", 0x1CB: "cm DOWN published",
    0x1CC: "cm link-down ack/timeout", 0x1CD: "cm CONFIGMG", 0x1CE: "cm remove step", 0x1CF: "cm TEARDOWN request",
    0x1D0: "DriverEntry", 0x1D1: "HwFindAdapter", 0x1D2: "found", 0x1D3: "HwInitialize", 0x1D4: "HwInitialize done",
    0x1D5: "StartIo", 0x1D6: "complete", 0x1D7: "HwResetBus", 0x1D8: "HwAdapterState", 0x1D9: "SHUTDOWN SRB",
    0x1DA: "timer", 0x1DB: "probe", 0x1DC: "g9 gone", 0x1DD: "g9 deadline", 0x1DE: "g9 fastfail", 0x1DF: "summary",
    0xE0: "q9 options", 0xE1: "q9 rendezvous", 0xE2: "q9 pool", 0xE3: "q9 bring-up", 0xE4: "q9 READY",
    0xE5: "q9 hold", 0xE6: "q9 BUSY", 0xE7: "q9 GONE", 0xE8: "q9 MD-1 deadline", 0xE9: "q9 DOWN_QUIESCED seen",
    0xEA: "q9 link-down ack", 0xEB: "q9 REMOVE_PENDING seen", 0xEC: "q9 handshake step", 0xED: "q9 handshake ACK",
    0xF0: "q9 HwResetBus", 0xF1: "q9 late reset ack", 0xF2: "q9 FAILED", 0xF3: "q9 stop", 0xF4: "q9 write-through",
    0xF5: "q9 SHUTDOWN step", 0xF6: "q9 pool check", 0xF7: "q9 tick", 0xF8: "q9 identity",
    0xF9: "q9 refused after ack",
    0x120: "pool9x find", 0x121: "pool9x record", 0x122: "pool9x rendezvous", 0x123: "pool9x reuse check",
    0x124: "pool9x DOWN_QUIESCED seen", 0x125: "pool9x link-down ack", 0x126: "pool9x identity", 0x127: "pool9x budget",
    0x128: "pool9x token", 0x129: "pool9x deferred", 0x12A: "pool9x REMOVE_PENDING seen", 0x12B: "pool9x removal step",
    0x12C: "pool9x RemoveAck", 0x12D: "pool9x refused after ack", 0x12E: "pool9x overlapping request",
    0x12F: "pool9x ISR probe", 0x130: "pool9x removal BUSY", 0x131: "pool9x test options",
    0x132: "pool9x late lookup",
    0x43: "bring-up done", 0x44: "bring-up failed", 0x67: "HwAdapterState", 0x81: "gone entered",
    0x83: "MD-1 gone deadline", 0x84: "held SRB completed", 0xD1: "isolation evidence",
    0xFA: "ctl bench start", 0xFB: "ctl bench writes done", 0xFC: "ctl bench committed", 0xFD: "ctl bench read done",
}
CMCALL = {0x0C: "Query_Remove_SubTree", 0x0D: "Remove_SubTree", 0x33: "Reenumerate_DevNode", 0xEE: "(no devnode)"}
QUIET = {0x1D5, 0x1D6, 0x1DA, 0x1DF, 0xF7}

rx_ns = re.compile(r"host_ns=(\d+)")
rx_tb = re.compile(r"\btb(\d+):")
rx_code = re.compile(r"code=0x([0-9a-fA-F]+)")
rx_arg = re.compile(r"arg=0x([0-9a-fA-F]+)")
USAGE = "usage: q9log.py <trace.log> [--all] [--ejects]"
# Eject verdicts that fail a run (q9log.py --ejects and ejects.py).
EJECT_BAD = ("FAIL", "INCOMPLETE", "AMBIGUOUS")


def parse(path):
    ev = []
    for line in open(path, errors="replace"):
        m = rx_ns.search(line)
        if not m:
            continue
        t = int(m.group(1)) / 1e6
        if line.startswith("usb4_dbg_stage"):
            mc = rx_code.search(line)
            ma = rx_arg.search(line)
            if not mc or not ma:
                continue                            # a truncated line (the trace was still being written)
            ev.append((t, "stage", int(mc.group(1), 16), int(ma.group(1), 16), line.strip()))
        else:
            mt = rx_tb.search(line)
            tb = int(mt.group(1)) if mt else -1
            if line.startswith("usb4_tp_gate "):
                ev.append((t, "gate", 1 if "closed=1" in line else 0, tb, line.strip()))
            elif line.startswith("usb4_tp_cutoff"):
                ev.append((t, "cutoff", 0, tb, line.strip()))
            elif line.startswith("usb4_h_fire"):
                ev.append((t, "harness", 0, tb, line.strip()))
            elif line.startswith("qemu_system_"):
                ev.append((t, "system", 0, -1, line.strip()))
    return ev


def timeline(ev, show_all):
    t_base = ev[0][0] if ev else 0.0
    for t, kind, code, arg, raw in ev:
        rel = "%12.1f" % (t - t_base)
        if kind == "stage":
            if code in QUIET and not show_all:
                continue
            extra = ""
            if code == 0x1CD:
                extra = " %s port=%d cr=%X" % (CMCALL.get(arg >> 24, hex(arg >> 24)), (arg >> 16) & 0xFF,
                                                arg & 0xFFFF)
            print("%s  %03X %-26s arg=%08X%s" % (rel, code, NAMES.get(code, "?"), arg, extra))
        elif kind in ("gate", "cutoff", "harness", "system"):
            print("%s  -- %s" % (rel, raw[:150]))
    print()


def check_statements(ev):
    stages = [(t, c, a) for t, k, c, a, _ in ev if k == "stage"]
    bus_of = {}                                     # port -> bus (from 0x1CA, new traces)
    statements = []
    last_up = {}
    for t, c, a in stages:
        if c == 0x1CA:
            port = a >> 24
            bus = (a >> 16) & 0xFF
            if bus:
                bus_of[port] = bus
            last_up[port] = t
        if c == 0x1CB and a != 0xEA:
            # the bus mapping as it was when the statement was made (a later UP may change it)
            statements.append((t, a >> 24, (a >> 16) & 0xFF, a & 0xFFFF, dict(last_up), a >> 24 in bus_of,
                               bus_of.get(a >> 24, 0)))
    results = []
    for n, (td, port, flags, gen, ups, new_fmt, bus) in enumerate(statements):
        tag = "" if new_fmt else " (by gen)"
        gates = [t for t, k, c, a, _ in ev if k == "gate" and c == 1 and a == port and t <= td]
        t0 = gates[-1] if gates else None
        cut = [t for t, k, c, a, _ in ev if k == "cutoff" and a == port and t0 is not None and t0 <= t]
        cutoff = cut[0] if cut else None

        # transaction end: the port's next statement or UP (any port for old traces)
        end = None
        for t, c, a in stages:
            if t > td and c in (0x1CA, 0x1CB) and a != 0xEA and (not new_fmt or (a >> 24) == port):
                end = t
                break

        def first(code, pred, after=td):
            for t, c, a in stages:
                if end is not None and t >= end:
                    break
                if t >= after and c in (code if isinstance(code, tuple) else (code,)) and pred(a):
                    return t, a
            return None, None

        rx, _ = first((0xE9, 0x124), lambda a: (a & 0xFF) == (gen & 0xFF) and (not new_fmt or (a >> 24) == bus))
        ack, _ = first((0xEA, 0x125), lambda a: (a & 0xFF) == (gen & 0xFF) and ((a >> 16) & 1) and
                       (not new_fmt or (a >> 24) == bus))
        cma, cma_arg = first(0x1CC, lambda a: (not new_fmt) or (a >> 28) == port)
        acked = cma_arg is not None and (cma_arg & 1)

        # every removal event inside the transaction: a successful Remove_SubTree of the
        # port (new traces) or the port's devnode retirement
        def is_removal(c, a):
            if c == 0x1B6:
                return a == (0xC0 | port)
            return new_fmt and c == 0x1CD and (a >> 24) == 0x0D and ((a >> 16) & 0xFF) == port and \
                (a & 0xFFFF) == 0
        inside = lambda t: t >= td and (end is None or t < end)
        removals = [t for t, c, a in stages if inside(t) and is_removal(c, a)]
        # old traces: a successful Remove_SubTree carries no port and cannot be attributed
        legacy = [] if new_fmt else [t for t, c, a in stages if inside(t) and c == 0x1CD and
                                     (a >> 24) == 0x0D and (a & 0xFFFF) == 0]
        early = [t for t in removals if cma is None or t < cma]
        early_legacy = [t for t in legacy if cma is None or t < cma]
        rem = next((t for t in removals if cma is not None and t >= cma), None)
        rem_legacy = next((t for t in legacy if cma is not None and t >= cma), None)
        # a late acknowledgement: the CM logged 0x1B6 0xBC for this port (0xBC00 | port)
        # between the receiver's acknowledgement and its own timeout; an old portless 0xBC
        # marker cannot be attributed
        late = cma is not None and ack is not None and any(
            c == 0x1B6 and a == (0xBC00 | port) and ack <= t <= cma for t, c, a in stages)
        late_legacy = cma is not None and ack is not None and any(
            c == 0x1B6 and a == 0xBC and ack <= t <= cma for t, c, a in stages)
        retired = [t for t, c, a in stages if c == 0x1B6 and a == (0xC0 | port) and
                   ups.get(port, -1.0) <= t <= td]
        rel = lambda x: "-" if x is None or t0 is None else "%.1f" % (x - t0)
        line = "statement #%d port %d gen %d flags %02X: cutoff=%s cm_down=%s rx=%s ack=%s cm_%s=%s removal=%s" % (
            n + 1, port, gen, flags, rel(cutoff), rel(td), rel(rx), rel(ack),
            "ack" if acked else ("timeout" if cma is not None else "none"), rel(cma), rel(rem))
        if not (flags & 0x04):
            verdict = "NEGATIVE (statement without QUIESCED)"
        elif cutoff is None or cutoff > td:
            verdict = "NEGATIVE (statement before the stage-2 cutoff)"
        elif retired and rx is None:
            verdict = "NO-INSTANCE (devnode retired before the statement)"
        elif early:
            verdict = "FAIL (removal before the CM's acknowledgement or timeout)"
        elif early_legacy:
            verdict = "AMBIGUOUS (an unattributable Remove_SubTree before the CM's result)"
        elif cma is None:
            verdict = "INCOMPLETE (no CM acknowledgement or timeout event in the transaction)"
        elif rem is None and rem_legacy is not None:
            verdict = "AMBIGUOUS (the only removal found carries no port)"
        elif acked:
            order = [cutoff, td, rx, ack, cma, rem]
            if None in order:
                verdict = "INCOMPLETE (missing: %s)" % ", ".join(
                    nm for nm, x in zip(("cutoff", "statement", "receipt", "stor98 ack", "cm ack", "removal"), order)
                    if x is None)
            elif all(order[i] <= order[i + 1] for i in range(len(order) - 1)):
                verdict = "PASS"
            else:
                verdict = "FAIL (out of order)"
        else:
            if ack is not None and ack <= cma and not late and late_legacy:
                verdict = "AMBIGUOUS (a portless late-acknowledgement marker)"
            elif ack is not None and ack <= cma and not late:
                verdict = "FAIL (receiver acknowledged before the CM timed out)"
            elif rem is None:
                verdict = "INCOMPLETE (timeout without a removal in the transaction)"
            elif cutoff <= td <= cma <= rem:
                verdict = ("TIMEOUT (late acknowledgement: first seen by the CM after its deadline)" if late
                           else "TIMEOUT (no acknowledgement within the CM's wait)")
            else:
                verdict = "FAIL (out of order)"
        results.append(verdict.split()[0])
        print("%s  -> %s%s" % (line, verdict, tag))
    print()
    print("summary: " + ", ".join("%s %d" % (k, results.count(k)) for k in sorted(set(results))))
    return results


def decode_ejects(ev):
    """The CM's eject transactions with stor98's side of each (R9-R11); see the docstring."""
    stages = [(t, c, a) for t, k, c, a, _ in ev if k == "stage"]
    txs = []
    req_t = None

    def open_tx(port=None):
        for x in reversed(txs):
            if x["end"] is None and (port is None or x["port"] == port):
                return x
        return None

    def by_seq(seq):
        # cm9x numbers requests per port and these events carry no port: two open
        # transactions with the same sequence cannot be told apart.
        match = [x for x in txs if x["seq"] is not None and (x["seq"] & 0xFFFF) == (seq & 0xFFFF)]
        live = [x for x in match if x["end"] is None]
        if len(live) > 1:
            for x in live:
                x["ambiguous"] = seq & 0xFFFF
        if live:
            return live[-1]
        return match[-1] if match else None

    for t, c, a in stages:
        if c == 0x1CE:
            step = a >> 24
            if step == 1:
                req_t = t
            elif step == 2:
                txs.append(dict(port=None, seq=None, req=req_t, set=t, end=("VETOED", t), ev={}, steps=[],
                                rmacks=[], removes=[], notes=[]))
                req_t = None
            elif step == 3:
                port = (a >> 20) & 0xF
                prev = open_tx(port)
                if prev is not None:
                    if "retired" in prev["ev"]:
                        prev["end"] = ("ISOLATED", t)   # D2 recovery: no step 7, the devnode is retired
                    else:
                        prev["notes"].append("superseded by the next request")
                        prev["end"] = ("INCOMPLETE", t)
                txs.append(dict(port=port, seq=a & 0xFFFFF, req=req_t, set=t, end=None, ev={}, steps=[],
                                rmacks=[], removes=[], notes=[]))
                req_t = None
            elif step in (4, 6, 7, 8):
                x = by_seq(a & 0xFFFF)
                if x is None or x["end"] is not None:
                    continue
                if step == 4:
                    x["ev"].setdefault("cmack", (t, (a >> 16) & 0xFF))
                else:
                    x["end"] = ({6: "CM-ISOLATES", 7: "CLEAN", 8: "O6"}[step], t)
        elif c == 0x12A:
            x = by_seq(a & 0xFFFF)
            if x is not None and x["end"] is None:
                x["ev"].setdefault("seen", t)
        elif c == 0x12B:
            x = open_tx()
            if x is not None and "seen" in x["ev"]:
                x["steps"].append((a & 0xFF, t))
        elif c == 0x12C:
            x = by_seq((a >> 8) & 0xFFFF)
            if x is not None:
                x["rmacks"].append((t, a & 0x7F, not (a & 0x80), a >> 24))
        elif c == 0x12E:
            x = open_tx()
            if x is not None:
                x["notes"].append("overlap %X ignored" % a)
        elif c == 0x126:
            x = open_tx()
            if x is not None:
                x["ev"].setdefault("identity", (t, a))
        elif c == 0x1CD and (a >> 24) == 0x0D:
            x = open_tx((a >> 16) & 0xFF)
            if x is not None and "retired" not in x["ev"]:
                # Every removal of the transaction, also one before the CM's
                # acknowledgement step, which is then a FAIL below.
                x["removes"].append((t, a & 0xFFFF))
        elif c == 0x67 and a == 0:
            x = open_tx()
            if x is not None and "retired" not in x["ev"]:
                x["ev"].setdefault("stop", t)
        elif c == 0x1B6 and (a & 0xFFF0) == 0xBD00:
            x = open_tx(a & 0xF)
            if x is not None:
                x["ev"].setdefault("late", t)
        elif c == 0x1B6 and (a & 0xFFF0) == 0xC0:
            x = open_tx(a & 0xF)
            if x is not None and "cmack" in x["ev"]:
                x["ev"].setdefault("retired", t)

    results = []
    for n, x in enumerate(txs):
        e = x["ev"]
        s = x["set"]
        rel = lambda v: "-" if v is None else "+%.0f" % (v - s)
        problems = []
        kind = x["end"][0] if x["end"] else None
        if kind is None and "retired" in e:
            kind = "ISOLATED"                       # D2 recovery: no step 7, the devnode is retired
        cm = e.get("cmack")
        rm = x["rmacks"][0] if x["rmacks"] else None
        if len(x["rmacks"]) > 1:
            problems.append("RemoveAck written %d times" % len(x["rmacks"]))
        if kind not in (None, "VETOED", "INCOMPLETE"):
            if "seen" not in e:
                problems.append("request never seen by stor98")
            if cm is None:
                problems.append("no acknowledgement step of the CM")
            if rm is not None and cm is not None:
                if rm[0] < s or ("seen" in e and rm[0] < e["seen"]):
                    problems.append("RemoveAck before the request")
                if rm[2] and cm[1] != 0xFF and cm[1] != rm[1]:
                    problems.append("the CM took outcome %d, stor98 wrote %d" % (cm[1], rm[1]))
                if not rm[2] and cm[1] != 0xFF:
                    problems.append("the CM took an outcome stor98 did not publish")
                if rm[2] and cm[1] == 0xFF and rm[0] <= cm[0] and "late" not in e and "identity" not in e:
                    problems.append("published before the CM's deadline but not taken")
                if cm[1] != 0xFF and cm[0] < rm[0]:
                    problems.append("the CM acted before the RemoveAck")
            if cm is not None and cm[1] != 0xFF and rm is None:
                problems.append("the CM took outcome %d with no RemoveAck" % cm[1])
            early = [r for r in x["removes"] if r[1] == 0 and (cm is None or r[0] < cm[0])]
            if early:
                problems.append("Remove_SubTree before the CM's acknowledgement step")
            if kind in ("CLEAN", "ISOLATED"):
                ok = [r for r in x["removes"] if r[1] == 0]
                if not ok:
                    problems.append("no successful Remove_SubTree")
                # ISOLATED: ISOLATED_NOT_CLEAN, or CLEAN that the CM treats as cancelled (CancelAfterAck)
                want = (1,) if kind == "CLEAN" else (1, 2)
                if rm is None or rm[1] not in want:
                    problems.append("RemoveAck outcome %s" % ("missing" if rm is None else rm[1]))
        if "ambiguous" in x:
            verdict = ("AMBIGUOUS (another port's open transaction has sequence %d: their events "
                       "cannot be told apart)" % x["ambiguous"])
        elif problems:
            verdict = "FAIL (%s)" % "; ".join(problems)
        elif kind is None:
            verdict = "INCOMPLETE (no end of the transaction in the trace)"
        elif kind == "INCOMPLETE":
            verdict = "INCOMPLETE (%s)" % "; ".join(x["notes"])
        else:
            verdict = kind
        results.append(verdict.split()[0])
        steps = " ".join("s%d%s" % (st, rel(tt)) for st, tt in x["steps"])
        ack = "-" if rm is None else "%d%s detail %02X %s" % (
            rm[1], "" if rm[2] else " (not published)", rm[3], rel(rm[0]))
        rem = ",".join("%s cr %X" % (rel(tt), cr) for tt, cr in x["removes"][:3]) or "-"
        notes = "; ".join(n2 for n2 in x["notes"] if not n2.startswith("superseded"))
        print("eject #%d port %s seq %s: seen %s, steps %s, RemoveAck %s, CM outcome %s %s, Remove_SubTree %s, "
              "HwAdapterState(FALSE) %s%s%s  -> %s" % (
                  n + 1, "-" if x["port"] is None else x["port"], "-" if x["seq"] is None else x["seq"],
                  rel(e.get("seen")), steps or "-", ack, "-" if cm is None else "%02X" % cm[1],
                  rel(cm[0]) if cm else "", rem, rel(e.get("stop")),
                  ", identity lost %X" % e["identity"][1] if "identity" in e else "",
                  ", " + notes if notes else "", verdict))
    print()
    print("ejects: " + (", ".join("%s %d" % (k, results.count(k)) for k in sorted(set(results)))
                        if results else "none found"))
    return results, bool(stages)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opts = set(a for a in argv[1:] if a.startswith("--"))
    if len(args) != 1 or opts - {"--all", "--ejects"}:
        sys.stderr.write(USAGE + "\n")
        return 2
    try:
        ev = parse(args[0])
    except OSError as e:
        sys.stderr.write("q9log.py: %s\n" % e)
        return 2
    timeline(ev, "--all" in opts)
    bad = [r for r in check_statements(ev) if r in ("FAIL", "INCOMPLETE")]
    if "--ejects" in opts:
        print()
        results, have_stages = decode_ejects(ev)
        bad += [r for r in results if r in EJECT_BAD]
        if have_stages and not results:
            bad.append("none")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
