#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Yeo Kheng Meng
"""
rename-to-stor98.py - the deterministic part of the nvme98 -> stor98 rename.

    python scripts/rename-to-stor98.py            dry run: print the plan
    python scripts/rename-to-stor98.py --apply    git mv + rewrite, then --check
    python scripts/rename-to-stor98.py --check    verify a renamed tree

Run from anywhere; it works on the git work tree that contains it (or
--repo PATH). --apply wants a clean work tree and leaves the changes
unstaged (the renames are staged by git mv); review `git diff` and commit.
The rename is done once: --apply refuses a tree that already passes
--check, and the dry run says so. The tables also keep the deliberate
mentions of the former signature, so a dry run on the renamed tree
rewrites nothing.

Everything the script does is in the tables below (PATH_MAP, EXCLUDE,
KEEP_PATTERNS, LITERAL_RULES, CASE_RULES, LINE_RULES), reviewed as part of
docs/RENAME-STOR98.md. What a script cannot do safely (historical
statements in living docs, migration code, the README notice) is in
hand-written commits that follow the scripted one.

--check fails (exit 1) when
  - a tracked path outside EXCLUDE still contains "nvme98";
  - an included text file still contains "nvme98" (any case) outside a
    KEEP span;
  - a LINE_RULE target does not hold its expected line exactly once;
  - a shipped file name is not 8.3.

Python 3.8+, standard library only.
"""

import argparse
import os
import re
import subprocess
import sys

# ---------------------------------------------------------------------------
# The mapping
# ---------------------------------------------------------------------------

# Tracked paths renamed with git mv. Any other tracked path outside EXCLUDE
# that contains "nvme98" stops the script: add it here after review.
PATH_MAP = {
    "include/nvme98cm.h": "include/stor98cm.h",
    "src/nvme98ver.h": "src/stor98ver.h",
    "inf/nvme98-x86.inf": "inf/stor98-x86.inf",
    "inf/nvme98-x64.inf": "inf/stor98-x64.inf",
}

# Paths never renamed or rewritten (regular expressions on the repository
# path, '/' separators).
EXCLUDE = [
    r"^third_party/",                       # upstream reference copy, unmodified
    r"^releases/\d+\.\d+\.\d+\.\d+/",       # published releases are write-once
    r"^LICENSE$",                           # licence text and copyright lines
    r"^docs/gates/",                        # dated qualification records and evidence
    r"^docs/reviews/",                      # dated reviews
    r"^docs/INTEGRATION-[^/]*\.md$",        # dated integration records
    r"^docs/RENAME-STOR98\.md$",            # names both on purpose
    r"^scripts/rename-to-stor98\.py$",      # this script
    r"^src/README-nvme9x\.txt$",            # upstream readme
]

# Files rewritten only in part: everything from the first line matching the
# pattern on is history and is kept.
PARTIAL = {
    "releases/history.md": r"^## ",
}

# Old versions: "nvme98 <version>" names a release that was published under
# the old name and keeps it. The published release directories are added at
# run time.
OLD_VERSIONS = ["0.1.x", "0.1.0.0", "0.1.1.0", "0.1.1.1", "0.2.0.0"]

# Spans kept as they are (case-insensitive regular expressions). Rules never
# apply inside them, and --check accepts "nvme98" inside them.
KEEP_PATTERNS = [
    r"nvme98-vm",                           # E:\nvme98-vm, the VM folder on the test host
    r"nvme98-wt-[a-z0-9-]+",                # local worktree folder names
    r"(?-i:NVME98\.MPD)",                   # tests/int9x: guest images prepared before the rename
    # Deliberate mentions of the former name written by the hand commits
    # after the scripted one (so that --check also passes on the final tree).
    r"nvme98cm\.h",                         # the header's former name
    r"Nvme98LinkState|Nvme98Cm",            # former key and object names, cited as such
    r"Services\\nvme98",                    # the old locator key, cited in include/stor98cm.h
    r"was named nvme98|the nvme98 interface",
    r"formerly(?: named)? nvme98",          # the rename notice itself
    r"\bnvme98 (?:%s)\b",                   # an old release by version (filled in below)
    r"releases[/\\]\d+\.\d+\.\d+\.\d+[/\\][^\s`'\")\]]*",   # a file of a published release
    # The former signature, cited as such after the rename: the note in
    # include/stor98cm.h and the "old signature refused" case in
    # tests/unit/cmlink_ut.c. LITERAL_RULES must never rewrite these.
    r"(?-i:\(was 'N98L'\))",
    r"(?-i:0x4C38394E;\s*/\* 'N98L' \*/)",
]

# Literal replacements (case-sensitive, applied first).
LITERAL_RULES = [
    # Interface signature 'N98L' (include/stor98cm.h). The ASCII bytes
    # 'N','9','8','L' read as a little-endian ULONG; the new value is
    # 'S','9','8','L'.
    ("0x4C38394E", "0x4C383953"),
    ("0x4c38394e", "0x4c383953"),
    ("'N98L'", "'S98L'"),
]

# Case-preserving name replacements (applied after LITERAL_RULES). Every
# spelling is the same length, so fixed-size buffers and string lengths in
# the code (for example tests/cm9x/cm9xctl.c) stay right.
CASE_RULES = [
    ("NVME98", "STOR98"),   # macros, include guards, "NVME98CM" signature
    ("NVMe98", "Stor98"),   # INF section and string names
    ("Nvme98", "Stor98"),   # "Nvme98LinkState" (HKEY_DYN_DATA, \Callback)
    ("nvme98", "stor98"),   # file, service, key, binary names and prose
]

# Whole-line rules on renamed files: (path, regex for the line, new line).
# Each must match exactly one line before --apply (or already equal the new
# line); --check requires the new line exactly once.
INF_STRINGS = [
    # [Strings] values only (quoted), not the %Provider% reference in [Version].
    (r'^Provider=".*"$', 'Provider="Yeo Kheng Meng"'),
    (r'^Mfg=".*"$', 'Mfg="Stor98 Project"'),
    (r'^Stor98\.DeviceDesc=".*"$', 'Stor98.DeviceDesc="Stor98 Storage Controller for NVMe SSDs"'),
    (r'^Stor98\.ServiceDesc=".*"$', 'Stor98.ServiceDesc="Stor98 Storage Controller Driver (stor98)"'),
    (r'^DiskName=".*"$', 'DiskName="Stor98 Storage Controller Disk"'),
]
LINE_RULES = [(p, rx, new) for p in ("inf/stor98-x86.inf", "inf/stor98-x64.inf") for (rx, new) in INF_STRINGS] + [
    ("src/nvme2k.rc", r'^#define VER_PRODUCTNAME_STR\s.*$', '#define VER_PRODUCTNAME_STR         "Stor98"'),
    ("src/nvme2k.rc", r'^#define VER_COMPANYNAME_STR\s.*$', '#define VER_COMPANYNAME_STR         "Yeo Kheng Meng"'),
    ("src/nvme2k.rc", r'^#define VER_FILEDESCRIPTION_STR\s+"stor98 (NVMe SCSI miniport for Windows 98 SE/ME)"$',
     '#define VER_FILEDESCRIPTION_STR     "Stor98 SCSI miniport for NVMe SSDs (Windows 98 SE/ME)"'),
    ("src/nvme2k.rc", r'^#define VER_FILEDESCRIPTION_STR\s+"stor98 (NVMe SCSIport miniport for Windows 2000 and later)"$',
     '#define VER_FILEDESCRIPTION_STR     "Stor98 SCSIport miniport for NVMe SSDs (Windows 2000 and later)"'),
]

# Shipped names that must be 8.3 (release folders, IOSUBSYS, system32\drivers).
SHIPPED_83 = ["stor98.inf", "stor98.mpd", "stor98.sys", "stor98.pdb"]

BINARY_EXT = {".png", ".jpg", ".sys", ".mpd", ".cer", ".pfx", ".pvk", ".pdb", ".exe", ".dll", ".zip", ".bin"}

# ---------------------------------------------------------------------------

def git(repo, *args):
    return subprocess.run(["git", "-C", repo] + list(args), check=True, capture_output=True,
                          text=True, encoding="utf-8").stdout


def tracked(repo):
    out = subprocess.run(["git", "-C", repo, "ls-files", "-z"], check=True, capture_output=True).stdout
    return sorted(p for p in out.decode("utf-8").split("\0") if p)


def excluded(path):
    return any(re.search(rx, path) for rx in EXCLUDE)


def is_binary(repo, path):
    if os.path.splitext(path)[1].lower() in BINARY_EXT:
        return True
    with open(os.path.join(repo, path), "rb") as f:
        return b"\0" in f.read(8192)


def keep_regex(repo, files):
    versions = set(OLD_VERSIONS)
    for p in files:
        m = re.match(r"^releases/(\d+\.\d+\.\d+\.\d+)/", p)
        if m:
            versions.add(m.group(1))
    alt = "|".join(re.escape(v) for v in sorted(versions, key=len, reverse=True))
    pats = [p % alt if "%s" in p else p for p in KEEP_PATTERNS]
    # Basenames of excluded (historical) files that carry the old name, other
    # than the product's own file names, so that links to them keep working.
    product = re.compile(r"^nvme98(?:\.(?:sys|mpd|inf|pdb)|-test\.(?:cer|pfx|pvk))$", re.I)
    for p in files:
        base = p.rsplit("/", 1)[-1]
        if excluded(p) and "nvme98" in base.lower() and not product.match(base):
            pats.append(re.escape(base))
    return re.compile("|".join("(?:%s)" % p for p in pats), re.I)


def split_keep(text, keep):
    """[(is_kept, chunk)] covering text."""
    out, pos = [], 0
    for m in keep.finditer(text):
        if m.start() > pos:
            out.append((False, text[pos:m.start()]))
        out.append((True, m.group(0)))
        pos = m.end()
    if pos < len(text):
        out.append((False, text[pos:]))
    return out


def rewrite_text(text, keep):
    parts = []
    for kept, chunk in split_keep(text, keep):
        if not kept:
            for a, b in LITERAL_RULES:
                chunk = chunk.replace(a, b)
            for a, b in CASE_RULES:
                chunk = chunk.replace(a, b)
        parts.append(chunk)
    return "".join(parts)


def leftovers(text, keep):
    """Line numbers and lines with 'nvme98' outside KEEP spans."""
    bad = []
    for kept, chunk in split_keep(text, keep):
        if not kept and re.search("nvme98", chunk, re.I):
            bad.append(chunk)
    if not bad:
        return []
    res = []
    for n, line in enumerate(text.splitlines(), 1):
        rest = keep.sub("", line)
        if re.search("nvme98", rest, re.I):
            res.append((n, line.strip()[:160]))
    return res


def read(repo, path):
    with open(os.path.join(repo, path), "rb") as f:
        return f.read().decode("latin-1")     # byte-preserving; the tree is ASCII


def write(repo, path, text):
    with open(os.path.join(repo, path), "wb") as f:
        f.write(text.encode("latin-1"))


def split_partial(path, text):
    rx = PARTIAL.get(path)
    if not rx:
        return text, ""
    m = re.search(rx, text, re.M)
    return (text, "") if not m else (text[:m.start()], text[m.start():])


def apply_line_rules(repo, path, text, check_only):
    errors = []
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(nl)
    for (p, rx, new) in LINE_RULES:
        if p != path:
            continue
        hits = [i for i, l in enumerate(lines) if re.match(rx, l)]
        exact = [i for i, l in enumerate(lines) if l == new]
        if check_only:
            if len(exact) != 1:
                errors.append("%s: expected exactly one line %r, found %d" % (path, new, len(exact)))
            continue
        if len(hits) == 1:
            lines[hits[0]] = new
        elif len(exact) == 1 and not [i for i in hits if i not in exact]:
            pass
        else:
            errors.append("%s: rule %r matched %d lines (want 1)" % (path, rx, len(hits)))
    return nl.join(lines), errors


def plan_paths(files):
    moves, unmapped = [], []
    for p in files:
        if excluded(p):
            continue
        if p in PATH_MAP:
            moves.append((p, PATH_MAP[p]))
        elif "nvme98" in p.lower():
            unmapped.append(p)
    return moves, unmapped


def check(repo, quiet=False):
    files = tracked(repo)
    keep = keep_regex(repo, files)
    errors = []
    for p in files:
        if not excluded(p) and "nvme98" in p.lower():
            errors.append("path still has the old name: %s" % p)
    for p in files:
        if excluded(p) or is_binary(repo, p):
            continue
        head, _ = split_partial(p, read(repo, p))
        for n, line in leftovers(head, keep):
            errors.append("%s:%d: %s" % (p, n, line))
        _, errs = apply_line_rules(repo, p, head, True)
        errors += errs
    for name in SHIPPED_83:
        stem, _, ext = name.partition(".")
        if not (1 <= len(stem) <= 8 and len(ext) <= 3 and re.match(r"^[a-z0-9_-]+$", stem)):
            errors.append("not an 8.3 name: %s" % name)
    for p in files:
        if p.startswith("inf/") and p.endswith(".inf") and not excluded(p):
            for n, line in enumerate(read(repo, p).splitlines(), 1):
                m = re.match(r"^\s*([A-Za-z0-9_.-]+\.(?:sys|mpd))\s*(=|$)", line)
                if m:
                    stem, _, ext = m.group(1).partition(".")
                    if len(stem) > 8 or len(ext) > 3:
                        errors.append("%s:%d: not an 8.3 file name: %s" % (p, n, m.group(1)))
    if not quiet:
        for e in errors:
            print("CHECK: " + e)
        print("check: %s" % ("FAILED, %d problem(s)" % len(errors) if errors else "ok"))
    return not errors


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--apply", action="store_true")
    g.add_argument("--check", action="store_true")
    a = ap.parse_args()
    repo = os.path.abspath(a.repo)

    if a.check:
        return 0 if check(repo) else 1

    files = tracked(repo)
    moves, unmapped = plan_paths(files)
    if unmapped:
        for p in unmapped:
            print("UNMAPPED: %s (add it to PATH_MAP or EXCLUDE after review)" % p)
        return 1
    # The rename is a one-shot. On a tree that already passes --check there is
    # nothing left to do, and the rules must not run again over text written
    # after the rename.
    done = check(repo, quiet=True)
    if a.apply and done:
        print("the tree already passes --check: the rename is done; --apply refuses to run again")
        return 1
    if a.apply and git(repo, "status", "--porcelain", "--untracked-files=no").strip():
        print("the work tree has changes; commit or stash them first")
        return 1

    for src, dst in moves:
        print("rename %s -> %s" % (src, dst))
        if a.apply:
            git(repo, "mv", src, dst)
    files = tracked(repo) if a.apply else [PATH_MAP.get(p, p) for p in files]
    keep = keep_regex(repo, files)

    changed, errors = 0, []
    for p in files:
        if excluded(p):
            continue
        src_path = p
        if not a.apply:
            inv = {v: k for k, v in PATH_MAP.items()}
            src_path = inv.get(p, p) if not os.path.exists(os.path.join(repo, p)) else p
        if is_binary(repo, src_path):
            continue
        old = read(repo, src_path)
        head, tail = split_partial(p, old)
        new_head = rewrite_text(head, keep)
        new_head, errs = apply_line_rules(repo, p, new_head, False)
        errors += errs
        new = new_head + tail
        if new != old:
            changed += 1
            n = sum(1 for x, y in zip(old.splitlines(), new.splitlines()) if x != y)
            print("rewrite %s (%d line(s))" % (p, n))
            if a.apply:
                write(repo, p, new)
    for e in errors:
        print("ERROR: " + e)
    print("%d rename(s), %d file(s) rewritten%s" % (len(moves), changed, "" if a.apply else " (dry run)"))
    if not a.apply and done:
        print("the tree already passes --check: the rename is done and --apply would refuse")
    if errors:
        return 1
    if a.apply:
        return 0 if check(repo) else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
