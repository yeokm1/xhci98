# Future plans

Ideas and proposals for work the project has not taken up. Nothing here has
a phase or task id, nothing here has been built, and a page here binds no
code change; the rules a change must preserve stay in
`docs/contributing/implementation-invariants.md` and the design records that
own them. A page moves out of this folder when its work is scheduled: it
becomes a numbered design record under `docs/contributing/design/` and the
roadmap gets the tasks.

Each page says when it was written, what it rests on, what it would cost,
what would have to be measured before it could be trusted, and which
decisions are the project owner's. Where a page and the tree disagree, the
tree wins.

## Index

The virtual-hub page, the one entry here that ever carried a task id, left
on 2026-09-25 to become design record 12
(`docs/contributing/design/12-virtual-hub-on-root-ports.md`) when roadmap
task 24.3 took it up. The SuperSpeed HCD page left on 2026-10-02 to become
design record 13 (`docs/contributing/design/13-superspeed-hcd.md`) when the
owner froze the miniport at `1.2.0.0` and scheduled its successor as roadmap
Phases 25 onward (`docs/contributing/roadmap-hcd.md`).

- [superspeed-storage-behind-a-switch.md](superspeed-storage-behind-a-switch.md) -
  Proposal, written 2026-09-04 and not yet built (it was design record 10
  until 2026-09-07): SuperSpeed mass storage on root ports without leaving
  Option A, by driving the SuperSpeed link in the miniport and reporting the
  device to `usbport.sys` as High-Speed, the lie the root hub already tells
  for Low and Full Speed. Gated behind a `REG_DWORD` in the controller's
  driver key, off by default, and with the value absent or 0 the driver
  behaves as today in every reading the previous release produces. Covers
  the port translation table, the descriptor rewrite in the completion
  drain, the burst and 1024-byte endpoint policy, the refusals that keep it
  narrow (SuperSpeed hubs and isochronous devices sent back to USB 2.0, UAS
  never selected), the port-reset policy, what QEMU can and cannot show,
  the batches, the owner's five decisions, and the two reviews' corrections.
  With the miniport frozen at `1.2.0.0` (owner, 2026-10-02) it stays here as
  the record of what SuperSpeed storage inside the miniport would have taken;
  the successor takes SuperSpeed storage in Phase 29 of `roadmap-hcd.md`.
