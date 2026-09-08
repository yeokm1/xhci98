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

- [virtual-hub-per-root-port.md](virtual-hub-per-root-port.md) - Real
  speeds on root ports. The driver reports every root-port device as High
  Speed because usbport bugchecks on anything else (issue 6), at the cost of
  1, 2 and 4 ms interrupt bands, a derived isochronous cadence and a
  Device Manager that says High Speed for everything. The idea: present a
  virtual USB 2.0 hub, High Speed with one port and one transaction
  translator, behind every managed root port, so that usbport finds the hub
  its EHCI model expects and does the truthful thing at every layer, while
  the xHC keeps driving the device on its root port unchanged. Gated
  behind a `REG_DWORD` in the controller's driver key, off by default, and
  with the value absent or 0 the driver behaves exactly as today. Covers
  the switch, the request table the hub answers, the status-change pipe, how the topology
  graph folds the hub out, the permanent and on-demand shapes, what has to
  be measured on each hub driver first, and the owner's decisions.
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
- [superspeed-hcd-reimplementation.md](superspeed-hcd-reimplementation.md) -
  General USB 3.x SuperSpeed support, and why it is a separate driver-stack
  project: the Win2000-era `usbport.sys` has no SuperSpeed concept and no
  newer port driver runs on these targets, so it would first need the
  Option B monolithic host controller driver that re-implements usbport's
  role, and only then the link, descriptor, burst and stream, USB 3.x hub
  and bandwidth work listed there. Not planned; kept so the question is not
  re-derived.
