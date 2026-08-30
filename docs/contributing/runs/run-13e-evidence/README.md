# 13-E bench evidence - the decoded reads `run-13e.md` cites

These are the decoded companion reports the operator took off the E460 in bench
session 2, the batch 13-R boots, bench session 3 and stage L3 of batch 13-L.
They are tracked because [`run-13e.md`](../run-13e.md) cites them by name and
they cannot be regenerated: the machine is the operator's and the sessions are
over.

The raw `.BIN` dumps and `.PSC` blobs they were decoded from are not here. They
were removed on purpose: 23 files, 1,148,788 bytes, against the 42 KB of text
that is left.

## What the raw dumps were, and why keeping them was worse than not

Nothing could read them. A dump decodes only against an `offsets.txt` generated
from the tree that built the instrument that took it, and decoding against the
wrong table produces plausible, wrong numbers rather than an error. Three things
were needed to read one, and all three were outside this repository: the commit
that built each generation, the `offsets.txt` regenerated from that checkout,
and `scripts\local\readsnap.py`, which is git-ignored per-host tooling that
exists on one machine. The published repository never carried any of them.

What they held has also moved. Task 13-L.2 retired the throwaway
`XHCI_OBS_SNAPSHOT` instrument these came from, rebuilt the `PassThru` channel
into every shipping flavour, and changed the counter set underneath:
`XhciLogSnapshot` retired, `SwitchStatusSnapshot` gone with schema 3,
`DescSelectionsOffAddress` added by a later review. A successful decode would
have landed partly on fields the driver no longer has.

What they decoded to is already written down. The counter blocks are
transcribed in full into `run-13e.md`'s finding tables, in a form needing no
tooling at all, so that document, and not a re-decode, is the citable record
for Findings R through V.

The precedent was already in the sheet. Stage L3's own dumps, `l3h.BIN` and
`l3a2.BIN`, are cited in its decode tables and were never tracked: that session
decoded them, wrote the numbers into the sheet, and kept no bytes. Its companion
reports and probe reads (`l3*.txt`) joined this directory later, on the same
terms as the rest.

## What survived the logging change, and what did not

A companion report has two halves and they aged differently. Read the first;
be careful with the second.

Durable: the PORTSC table. `PORTSC` is an xHCI hardware register: `PP`,
`CCS`, `PED`, `PR`, `PLS`, the speed field and the change bits are spec-defined
rather than this project's format. Today's `XHCISNAP` prints the same table in
the same shape, so a fresh dump can be compared against these directly. Eleven
of the files here carry the full 18-port table, where `run-13e.md` quotes only
the interesting line or an abbreviated four-port form, so the healthy-against-
wedged pair across every port exists here and nowhere else. "What does a wedged
port look like" is a question that recurs, and these answer it without decoding
anything.

Not durable: the instrument header, and the counter set behind it. The
`ExtensionBytes` and `build flags` lines describe a build the tree no longer
contains, and each report opens by naming the `.BIN` and `.PSC` it was read
from, `c:\p11h.BIN` and the like. Those are paths on the E460's own disk at
capture time, printed by the tool, not paths into this repository. They are
left as the tool wrote them, because a report edited after the fact is not a
reading.

Two files go further and must not be read as current: `p11w-notering.txt` and
`wedge2-notering.txt` open with `log.file.status`, `log.file.requested` and
`log.debugview`. Those are the ring-0 file sink task 13-L.2 removed from the
driver entirely; there is no such mechanism in any shipping build. What the two
files are for is their note-ring records, described below, which are ordinary
text.

## Which instrument a companion came from

The `ExtensionBytes` line in a companion's header says which generation took it.
The generations are listed so that a header is interpretable, not so that a dump
can be re-read; there are none left to re-read.

| extension | reads | companions here |
|---|---|---|
| 87,592 | Findings R, S | `wedge2.txt`, `wedge2-notering.txt`, `probe4.txt`, `probe5.txt` |
| 87,636 | Finding T | `p11w.txt`, `p11h.txt`, `p11w-notering.txt`, `p11p1.txt`, `p11p2.txt` |
| 87,644 | Finding U | `p12h.txt`, `p12a3.txt`, `p12a5.txt`, `p12a10.txt`, `p12a15.txt`, `p12p1.txt` |
| 90,600 | Finding V | `p13h.txt`, `p13a3.txt`, `p13a5.txt`, `p13p1.txt`, `p13p2.txt` |
| 90,272 | stage L3 (`xhcisnap` 0.0.0.5, schema 3, the shipping channel) | `l3h.txt`, `l3a1.txt`, `l3a2.txt`, `l3d2.txt`, `l3p0.txt`-`l3p5.txt`, `l3v0.txt`-`l3v2.txt` |

A size is not an identity. Two different builds carried 83,867 bytes of binary
and different extension sizes, and two different trees have carried extension
90,280: the 13-L.2 build and a later head that added
`DescSelectionsOffAddress` back. The table above is a reading aid for headers
that already exist, not a key.

`docs/contributing/passthru-snapshot-instrument.md` is the record for the
shipping channel that replaced all of this, and `releases\<version>\xhcisnap\`
is where a user gets the reader.

## Naming

| suffix | what it is |
|---|---|
| `.txt` | the decoded companion report: identity header, then the PORTSC table |
| `-notering.txt` | the decoded note ring, where one was dumped separately |
| `p<N>p<M>.txt` | a probe read, 663 bytes: whether the route answered, and what the four controls returned |

Within a session, `h` is the healthy control, `w` is the wedged capture, `a<N>`
is an interim read after recipe cycle N, and `p<N>`/`probe<N>` are the probe
reads. So a name is `p` + the session number + the role: `p11h`, `p12a3`,
`p13p2`. Stage L3's files are `l3` + the role: `l3h`, `l3a1`, `l3a2`, `l3d2`
(the DEBUG candidate's read), `l3p0`-`l3p5` (probe reads, 1,093 or 679 bytes),
and `l3v0`-`l3v2`, which are not reads at all but `-verbosity <N>` transcripts:
what the tool printed when it set the registry ladder. `l3a1.txt` opens with a
`cannot create c:\l3a1.TXT` line because the report was redirected by hand after
the tool could not write it; the reading below it is intact. `p11p2.txt` is
the file `run-13e.md`'s P11-BENCH step B4 writes as `C:\P11P2.TXT`.

Not every file here is a report. Finding V's set is three reads and two probes,
not five reads.

## What is worth knowing before reading them

- `p12a3` is the only `p12` read with an intact note ring (`Log.Used 12,560`,
  `Log.BytesDropped` 0). By `p12a15` the ring had dropped 46,308 bytes and
  turned over about four times, because thirty-three recoveries each rewrite
  the whole initialization note block. Take ordering from `p12a3`; the later
  reads' counters are exact and their rings are not.
- `p11w-notering.txt` is 7,532 bytes, 323 records, no wrap: the whole of
  Finding T's mechanism, in order, and the most useful single file here.
- `wedge2-notering.txt` is 89 records, 2,070 bytes, no wrap, and reproduced the
  first run's wedge identically, which is what made Finding S a mechanism
  rather than an incident.
