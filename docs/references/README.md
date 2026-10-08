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
| `usb_32_202206/USB 3.2 Revision 1.1.pdf` (the zip's own folder and name) | USB 3.2 revision 1.1 (569 pages, June 2022), with its ECNs in `usb_32_202206/USB 3.2 ECNs/` | `26e025a56ce0352f4f91fcaaca67b622d8e9e030ca429804bf7da98f52e674fd` | USB-IF document library, ["USB 3.2 Revision 1.1 - June 2022"](https://www.usb.org/document-library/usb-32-revision-11-june-2022), delivered as `usb_32_202206.zip`. Cited as `USB 3.2 p.N`; see "USB 3.2 revision 1.1" below |

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

## USB 3.2 revision 1.1

The USB 3.2 specification, revision 1.1 (June 2022), is the source for the
USB 3.2 side of roadmap tasks 29-0 and 30-0: the BOS, device capability and
endpoint companion descriptors, the link states and eSS.Disabled rules of
chapter 7, and the hub class of chapter 10. `xhci-data-structures.md`
sections 10 and 11 were verified against it on 2026-10-04.

- Keep the zip's layout: `usb_32_202206/USB 3.2 Revision 1.1.pdf`, SHA-256
  `26e025a56ce0352f4f91fcaaca67b622d8e9e030ca429804bf7da98f52e674fd`
  (569 pages). The same folder carries a redline against revision 1.0 and
  `USB 3.2 ECNs/`. Revision 1.1 already contains the ECNs that touch what
  this tree cites: the "UFP Exit Condition Clarification" ECN's text is the
  1.1 text of 7.5.1.1.2 and 7.5.1.2.3, and the "USB FW Update" ECN's
  FWStatus capability (11h) is in 1.1's Table 9-14. The others are physical
  layer and re-timer changes.
- Licence: the document carries the USB 3.0 Promoter Group's copyright and
  "All rights reserved", and states that providing it grants no licence to any
  intellectual-property rights. It is fetched, never tracked, like the other
  rows.
- Text dump: `usb32-r1.1-dump.txt` beside it (git-ignored), made with
  `pdftotext` (poppler) rather than pypdf, in the same `===PAGE N===` format
  as the xHCI dump, N being the PDF page index plus 1. The shape of the
  recipe, one `pdftotext` run whose form feeds become the markers:

  ```sh
  cd docs/references
  pdftotext "usb_32_202206/USB 3.2 Revision 1.1.pdf" - |
    gawk 'BEGIN { RS = "\f" } RT != "" { printf "\n===PAGE %d===\n%s", NR, $0 }' \
    > usb32-r1.1-dump.txt
  grep -c '^===PAGE' usb32-r1.1-dump.txt       # 569
  ```

  This recipe reproduces the dump's format and page markers; it has not been
  re-run to compare the two byte for byte.
  pdftotext keeps a table's cells in reading order but not in rows, so read a
  table from the rendered page when the dump scrambles it.
- **The printed page is not N.** The front matter is unnumbered or roman, and
  the printed number is **N - 31**: Table 9-12 (BOS) is on dump page 381 and
  prints 350, Table 10-5 (the hub descriptor) is on dump page 468 and prints
  437, and dump page 377 prints `- 346 -`, all as the table of contents says.
  Cite the printed number, as `USB 3.2 p.N`, and check it the way the xHCI
  section above checks its own: the number sits in the running head as
  `- N -`, after "Revision 1.1 June 2022".

## Intel PCH datasheets (issue 12, the `D20IR` routing quirk)

These are fetched for issue 12 and roadmap task 36.3, the
`XhciIntelIrqRoute` quirk. They are the authority for:
- the RCBA register (LPC `D31:F0`, offset `F0h`);
- `D20IP` (RCBA+`3128h`, bits 3:0 = the xHCI's pin) and `D20IR`
  (RCBA+`3160h`, 16-bit, bits 2:0 = the INTA route, 0-7 = PIRQA#-H#);
- the APIC-mode mapping of PIRQA#-H# to IRQ16-23.

Intel's servers refuse scripted downloads (HTTP 403), so fetch these in a
browser. The owner fetched them on 2026-10-08. Their text dumps
(`<name>-dump.txt`, made with `pdftotext -layout`) sit beside them, and the
dumps are not authoritative.

| Save as | Version | SHA-256 | What it settles |
|---|---|---|---|
| `7-series-chipset-pch-datasheet.pdf` | 7 Series / C216 PCH datasheet, order no. 326776-003, June 2012 | `558cbf7e82b9f5ae2dfd366ce9e26b1edb646bc93e3a102be4a6d3799e60fded` | `D20IP` 10.1.25 p.406, `D20IR` 10.1.33 p.414, RCBA 13.1.40 p.493. The device IDs are not in it; it defers to its specification update |
| `8-series-chipset-pch-datasheet.pdf` | 8 Series / C220 PCH datasheet, order no. 328904-003, May 2014 | `1239037e57bcfa9b753c0e0c799a32f09e9705670cd2890f183c0bed7f19ec98` | `D20IP` 10.1.25 p.343, `D20IR` 10.1.34 p.350, RCBA 12.1.40 p.400. LPC device IDs `8C44`-`8C5C` in Table 1-2 |
| `4th-gen-core-family-mobile-i-o-datasheet.pdf` | Mobile 4th Gen Core Processor Family I/O datasheet (Lynx Point-LP), document no. 329003-003, February 2015 | `74ee3eb507145bd47fa173221580d405455364f0c62cf5b5e1432051ff8f595a` | `D20IP` 8.1.23 p.335, `D20IR` 8.1.31 p.342 (its summary table labels the `3160h` row "Device 22"; the register page is right), RCBA 10.1.40 p.403. xHCI `9C31`, LPC `9C41` (engineering sample), `9C43` (Premium) and `9C45` (Base) in the ID table, p.63 |
| `9-series-chipset-pch-datasheet.pdf` | 9 Series PCH datasheet, order no. 330550-002, June 2015 | `45ea509e826e451fe289e7f4c03eb28c4c28fb3a7f69dc1131266a547d3ec50e` | `D20IP` 10.1.25 p.336, `D20IR` 10.1.34 p.343, RCBA 12.1.40 p.393. xHCI `8CB1`, LPC `8CC1`/`8CC2` (engineering samples), `8CC3`, `8CC4` and `8CC6` |
| `core-m-processor-platform-i-o-datasheet.pdf` | Intel Core M Processor Family I/O datasheet (Wildcat Point-LP), document no. 330837-002, October 2014 | `99fd244d6830a1d6cc54c4d7e7ed34dafc1184d3695765bb335fdf06f1281c51` | `D20IP` 8.1.23 p.337, `D20IR` 8.1.31 p.344 (its summary table labels the `3160h` row "Device 22"; the register page is right), RCBA 10.1.40 p.405. xHCI `9CB1` at revision 03, matching the E7250, in Table 1-4 p.61. Its LPC rows are for the Y-processor line (`9CC1`, `9CC6`, `9CC7`, `9CC9`). The E7250's U-line LPC `9CC3` is not in it; it was read from the machine itself (issue 12, `lpc.jpg`) |
| `x99-chipset-pch-datasheet.pdf` | C610 Series / X99 PCH datasheet, order no. 330788-003, October 2015 | `7168d555962ad39a74fd780cb0cf1500427c023bc185b4ce922819dc9e9f7860` | `D20IP` 10.1.34 p.288, `D20IR` 10.1.43 p.294, RCBA 12.1.40 p.350. xHCI `8D31` in Table 1-3. **Erratum:** Table 10-1's rows from `3110h` are misaligned (it lists `D20IP` at `3140h` and `D20IR` at `31FEh`). The register pages give `3128h` and `3160h`, as the other datasheets do |
| `pci_ids.h` | Linux `include/linux/pci_ids.h`, master as fetched 2026-10-08 ([GitHub](https://github.com/torvalds/linux/blob/master/include/linux/pci_ids.h)) | `020fac001871c0f8441c1294f787231fb6cfbf2c55915ca8f918262e2db4487c` | `PANTHERPOINT_XHCI` `1E31` and the 7-series LPC range `1E40`-`1E5F`. It has no entries for the 8/9-series or C610 xHCI. GPL-2.0; kept here as a reference copy, not used by the build |

Every chipset the quirk lists is covered above. Only the 7 Series
specification update is not fetched; it would give the `1E31` LPC device
IDs, which the 7 Series datasheet leaves to it. Linux's `pci_ids.h` range
stands in for it.

The Licence limits section above applies to these as well: Intel's
documents are fetched per machine, never tracked.
