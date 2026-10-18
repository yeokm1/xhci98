#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
"""ejects.py <trace.log>...: the CM eject transactions (R9-R11) of usb4-emu traces.

The decoder and its stage-code table live in q9log.py (q9log.py <trace> --ejects); this
prints only the eject part, for one or more traces. Exit status: 0 when no eject is FAIL,
INCOMPLETE or AMBIGUOUS and every trace with stage lines has at least one eject; 1 otherwise; 2 for
a usage error.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import q9log  # noqa: E402


def main(argv):
    if len(argv) < 2 or any(a.startswith("-") for a in argv[1:]):
        sys.stderr.write("usage: ejects.py <trace.log>...\n")
        return 2
    bad = 0
    for path in argv[1:]:
        try:
            ev = q9log.parse(path)
        except OSError as e:
            sys.stderr.write("ejects.py: %s\n" % e)
            return 2
        print("== %s" % path)
        results, have_stages = q9log.decode_ejects(ev)
        if any(r in q9log.EJECT_BAD for r in results) or (have_stages and not results):
            bad = 1
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
