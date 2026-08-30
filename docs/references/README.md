# Reference PDFs - fetch these yourself; they are not tracked

This directory is where the external PDF references this project cites are
kept on your own machine. Everything in it except this README is git-ignored
(`.gitignore`: `docs/references/*` with the README un-ignored, so the rule
holds for the next document whatever its extension). These are third-party
documents that this project has no documented permission to redistribute, so
the repository records how to identify and obtain each one, and nothing else.
See `docs/contributing/legal-provenance.md` for the reasoning and for the wider
provenance record.

Read the table below with the note under it. Both recorded URLs served the
recorded hash when this was last checked (August 2026), but Intel's landing
page serves whichever revision is current and carries no version-pinned link,
so the day it moves past 1.2c the xHCI row stops matching. "When Intel moves
on again" below says what that costs and what to do about it.

The two documents in this directory are not every third-party document this
tree cites by page. A third, Oney's WDM book, is a purchased book that belongs
nowhere in this repository. It is identified under "A third document, cited by
page and kept nowhere in this tree" below rather than in the table.

Keep derived implementation guidance in the Markdown files under `docs/`;
these PDFs are supporting source material, not the day-to-day checklist. The
project's own bit-exact transcription lives in
`docs/usb-xhci-info/xhci-data-structures.md` and is what `src/` is written
against. A clone with no PDFs here still has every rule; it just cannot
re-verify a citation without downloading the source.

## What to fetch

| Save as | Version | SHA-256 | Official source |
|---|---|---|---|
| `xHCI__Rev1.2c.pdf` (Intel's download name) | 1.2c (600 pages, October 2025) | `0b06318005c3e0c8b896f2a002c2a3c78426b5fdacac4ad1cc02ffec15835190` | Intel, [xHCI for USB - requirements specification][intel-xhci]. Every `p.N` in this tree is a page this copy prints |
| `extensible-host-controller-interface-usb-xhci.pdf` | 1.2 (645 pages, May 2019) | `9257ca24f34bff08e6020623f707c788ca0f0df657aa713309ff489a68b1731e` | The revision the citations were first made against, before Phase 15 moved them to 1.2c. Intel no longer serves it and nothing here needs it; the row stays so the migration below can be re-derived by anyone who still has a copy |
| `xhci-backwards-compatibility-testing-v1-7.pdf` | 1.7 | `6e6464b293f10600ae208c72c7c6796162f75430841c901265db8e18fe37fcc2` | USB-IF, [xHCI Backwards Compatibility Testing][usb-bct] |

[intel-xhci]: https://www.intel.com/content/www/us/en/content-details/868295/extensible-host-controller-interface-for-universal-serial-bus-xhci-requirements-specification-r1-2c.html
[usb-bct]: https://www.usb.org/sites/default/files/xHCI_Backwards_Compatibility_Testing_v1_7.pdf

Check the download before citing from it:

```sh
sha256sum docs/references/*.pdf     # or: certutil -hashfile <file> SHA256
```

## When Intel moves on again

Every `p.N` citation of the specification in this tree names the page the
1.2c copy above prints. A later revision repaginates the whole document (1.2c
holds more text per page than 1.2 did, so the same passage moved up to 46
pages between them), and Intel publishes no version-pinned URL, so a reader
who fetches the next revision gets a document on which none of those numbers
land. If that has happened to you, there are two positions:

- You have a 1.2c copy that matches the hash. Every page number in the tree
  is good.
- You fetched something later. Treat the `p.N` citations as approximate
  locators, not addresses: find the quoted phrase with the `check()` recipe
  below and cite the page your copy prints. If you do that work
  systematically, do it the way Phase 15 did (next section), record the new
  revision and hash in the table above, and say which citations you
  re-verified.

Do not silently cite a later revision's page number as if it were one of this
tree's existing citations. The two are then indistinguishable, and the next
reader cannot tell which document either refers to.

## How the citations were moved from 1.2 to 1.2c

Recorded so the next move can repeat it rather than reinvent it. The sweep
(`git ls-files '*.c' '*.h' '*.md' | xargs grep -n '\bp\.[0-9]'`) found 1,286
`p.N` and `p.N-M` matches. Of those, 56 in the three Oney-citing files are the
book's pages and did not move (`grep -l Oney` derives the files; within
`build-and-test.md` and `implementation-invariants.md` the specification and
the book share a file, so those sites were classified line by line), 7 are
this README's own examples, and 4 in the roadmap's Phase 15 text already
cited 1.2c. The remaining 1,219 were rewritten, and the work was mechanical
except for the residue named at the end:

1. Dump both PDFs with the recipe below into `spec-1.2-dump.txt` and
   `spec-1.2c-dump.txt` (both git-ignored in this directory), and confirm for
   every sheet that the printed number equals the dump's `===PAGE N===`.
2. Map each cited 1.2 page to 1.2c by five-word shingle overlap between the
   normalised page texts. A 1.2 page lands on one 1.2c page or straddles two;
   169 distinct pages were cited, and 157 of them had a top candidate holding
   half or more of the page's shingles.
3. Per citation, take the quotation that ends within a few characters before
   it (a quotation with another `p.N` between it and the citation belongs to
   that other citation, not this one), and check it first against the 1.2
   page cited: 22 citations were already a page off and were corrected rather
   than carried across. Then keep the candidate 1.2c page that prints the
   quotation, matched on three-word runs with whitespace removed, because
   extraction splits words. Failing a quotation, keep the candidate on which
   the section or table number nearest before the citation on the same line
   appears. Failing that, take the map's top candidate.
4. Read the residue. 19 citations whose map was split nearly evenly between
   two 1.2c pages and had no quotation or anchor to settle them (every 4.17.5
   site, `p.296` in 1.2, is the IPE/IP rules on 1.2c p.270 and not the 4.18
   heading on p.271; the Configure Endpoint precondition of 4.6.6 is p.104;
   the 4.23.2 notes are p.315); the sites whose nearest quotation belonged to
   a neighbouring citation; and the Oney classification above.
5. Verify the result against 1.2c alone: of the 591 rewritten citations that
   sit within four lines of a quotation, 544 have that quotation printed on
   the page now cited, and the other 47 are the neighbouring-quotation sites,
   which were settled by section anchor or map and read by hand.

Two traps. Appendix I (1.2c p.595-600) and p.513 reprint errata passages
verbatim, so a quotation search that lands there has found the excerpt and not
the body; exclude them. And 1.2c's page header is not always the first token
of the sheet (see `printed_of` below), so a recipe written for 1.2 mislabels
half the pages.

## Licence limits on these two documents

Neither document is this project's to pass on, so they are fetched rather
than tracked:

- Intel xHCI specification. Carries Intel's copyright notice and "All rights
  reserved", and states that the document grants no express or implied
  intellectual-property licence. Intel publishing it for download is not, by
  itself, permission for anyone else to rehost it.
- USB-IF xHCI Backwards Compatibility Testing. Grants a copyright licence to
  reproduce and distribute the document "FOR INTERNAL USE ONLY", and states
  that no other licence is granted. USB-IF hosting the source copy publicly
  does not widen that limit.

Do not add a third-party document to this directory as a tracked file. Add a
row to the table above instead: filename, version, SHA-256, official URL, and
the document's own licence limit if it states one.

## A third document, cited by page and kept nowhere in this tree

The two rows above are the documents that live in this directory. They are
not the whole set this tree cites by printed page number. Walter Oney's book
is a third, and it is not here and never should be: it is a purchased book,
not a downloadable specification, so the "fetch it yourself" instruction above
has no URL to give, and the row below says so rather than leaving the column
blank.

It is recorded here for the same reason the two rows above exist. A reader
who meets `Oney p.253` in `docs/usb-xhci-info/win98-wdm.md` cannot check it
without knowing which edition, and cannot find the page without knowing the
offset.

| Document | Version | SHA-256 | Source |
|---|---|---|---|
| Walter Oney, *Programming the Microsoft Windows Driver Model* | 2nd edition, 2003, Microsoft Press; the PDF the citations were made against is 467 pages | `985161e306d4c5934b0536cf93fc4d141eb5be0f59cdceed5fbefef2e01ab6da` | No fetch URL; it is a purchased book. Buy or borrow a copy. The hash identifies the file this tree's page numbers were read from. What is cited throughout is the printed page, so another 2nd-edition copy should resolve every citation whatever its hash and whatever its own index offset. That has not been checked against a second copy, because this project has only ever had one |

The offset is what makes a citation checkable. In this PDF the printed page
number is the pypdf index minus 17, so printed page `p` is `r.pages[p + 17]`.
That was derived rather than assumed: reading the footer number off every
sheet, 439 of the 467 pages agree and 20 carry no readable footer at all.

The `check()` recipe below does not port to this book unchanged. Its
`printed_of` takes the first token of the page as the page number, which is
right for the specification and wrong here. Oney's sheets open with a running
head (`8.4 Windows 98/Me Compatibility Notes - 253 - Programming The Microsoft
...`), so the number sits mid-line between hyphens and the first token is a
section number. Match the footer instead, then the rest of the recipe applies:

```python
def printed_of(i):                      # Oney, not the xHCI specification
    t = re.sub(r"\s+", " ", r.pages[i].extract_text() or "")
    m = re.search(r"-\s*(\d{1,3})\s*-", t)
    return int(m.group(1)) if m else None
```

Two cautions carry over from the specification, and both bite here:

- Extraction inserts stray spaces, inside the very words you would search
  for. `PASSIVE_LEVEL` on p.253 extracts as `P ASSIVE_LEVEL` and
  `DO_POWER_PAGABLE` as `DO_POWER_P AGABLE`, so a needle that looks right will
  miss a page that says what you cited. Normalise whitespace, keep the needle
  short, and read the sheet before concluding a citation is wrong.
- The off-by-one is the same off-by-one. A citation to this book can land a
  page early just as easily as one to the specification; confirm the sentence
  is printed on the page you name. Every page citation to this book has been
  checked against the page it names, and lands.

Where it is cited: `docs/usb-xhci-info/win98-wdm.md` (the most of any file;
its "Windows 98/Me Compatibility Notes" chapters are what the book was read
for), `docs/contributing/build-and-test.md` and
`docs/contributing/implementation-invariants.md` cite it by page;
`docs/contributing/lessons.md`, `docs/contributing/failure-diagnosis.md` and
`src/xhci_evt.c` cite it by name. Every one of those sites labels it a 2003
secondary source, which is the standing it has here: it describes the
platform, it does not specify it, and where it disagrees with what a target
machine does, the machine wins.

## Usage Notes

- Treat the xHCI specification as authoritative for hardware programming.
- Treat the backwards compatibility PDF as validation guidance. It is not Win98-specific, so adapt its device and hub coverage to this project.
- Keep Markdown docs ASCII-only when summarising content from these PDFs.

## Extracting Text from the xHCI Spec PDF

This applies to your own downloaded copy, obtained from the official source
above. The 1.2c PDF (600 pages) is not encrypted; the 1.2 PDF was, with an
empty owner password, so the recipe keeps the `decrypt("")` line for anyone
dumping that copy. The working recipe (any OS with Python 3):

```sh
python3 -m venv venv && ./venv/bin/pip install pypdf
./venv/bin/python - <<'EOF'
from pypdf import PdfReader
r = PdfReader("xHCI__Rev1.2c.pdf")
if r.is_encrypted:
    r.decrypt("")            # 1.2 only; the empty password succeeds
with open("spec-1.2c-dump.txt", "w", encoding="utf-8") as f:
    for i, p in enumerate(r.pages):
        f.write(f"\n===PAGE {i+1}===\n")
        f.write(p.extract_text() or "")
EOF
grep -n "4.6.1.1" spec-1.2c-dump.txt   # then search the dump
```

Dump once, grep many times; per-claim page extraction is slower and misses
cross-references. Section anchors in this PDF (1.2c), each verified against
the sheet that prints the number: section 4.2 (Host Controller Initialization)
p.68; section 4.5.4.1 (software SET_ADDRESS prohibition) p.90; sections
4.6.1.1/4.6.1.2 (command ring stop/abort) p.93; section 4.9.4 (Event Ring
Management, ERSTSZ and ERDP before ERSTBA) p.162; section 5.4.2 (USB Status
Register) p.363.

## Citing a page number: `pages[i]` is not page i

In this PDF the printed page number is the pypdf index + 1, because the cover
sheet is not numbered. The dump recipe above already writes `===PAGE {i+1}===`
for that reason, but a one-off `r.pages[i]` loop that labels its output with
`i` does not, and a citation taken from such a label is off by one, landing
the next reader a page early on a 600-page document.

Do not trust the offset either. Read the number off the sheet itself and
confirm the quotation is on the page you are about to cite. In 1.2c the
number is the first token only on even pages; odd pages carry the running
header first (`Document Number: 868295, Revision: 1.2c  N`), so `printed_of`
accepts either form:

```python
import pypdf, re
r = pypdf.PdfReader("xHCI__Rev1.2c.pdf")

def printed_of(i):
    t = re.sub(r"\s+", " ", r.pages[i].extract_text() or "").strip()
    m = (re.match(r"(\d{1,4})\b", t) or
         re.match(r"Document Number: 868295, Revision: 1\.2c (\d{1,4})\b", t))
    return int(m.group(1)) if m else None

def check(page, phrase):                       # page = what you intend to cite
    i = page - 1
    t = re.sub(r"\s+", " ", r.pages[i].extract_text() or "")
    print(page, printed_of(i) == page and re.sub(r"\s+", " ", phrase) in t)

check(214, "shall not interpret an error Event")
```

Quote verbatim and keep the phrase short. Extraction inserts stray spaces
inside words (`multi -TRB`, `Deque ue`) and turns quotation marks into
replacement characters, so a long exact-match needle will fail even when the
text is right there. Normalise whitespace before matching, as above.

When correcting citations, sweep the whole tree rather than a list of files
you wrote down; a hand-written list misses files. Enumerate the sources
instead:

```
git ls-files '*.c' '*.h' '*.md' | xargs grep -n '\bp\.[0-9]'
```

Two things that sweep must account for. A build tag like `xpsp.080413-2108`
matches a naive `p\.[0-9]`, so require a word boundary (`\bp\.`). And three
files cite the Oney book, a different PDF with its own page numbering and its
own offset: `docs/usb-xhci-info/win98-wdm.md`,
`docs/contributing/build-and-test.md` and
`docs/contributing/implementation-invariants.md`. Derive that split rather
than reading it here (`grep -l Oney` over the same file set), for the same
reason hand-written lists are not trusted above.
