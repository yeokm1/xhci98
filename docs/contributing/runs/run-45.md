# Run 45 - Release `3.4.0.0`

Phase 45 of `roadmap-usb4.md`, for release `3.4.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## 45.15 record 34 Part 3, the no-VM part: the design read against the code, the host vectors (2026-10-29)

Design record 34 Part 3 (section 6, the USB4 software connection
manager's minimal chain at depth 2) read against `src/cm/tbt_u4cm.c` and
`tbt_u4cm.h` at `3e314cde`, on development host A; no virtual machine,
no QEMU, no `usb4-emu` run.

**Verdict: the design holds with seven additions (record 34 revision 5);
none of its code exists yet.** `tbt_u4cm.c` has no `TBT_U4_KID` and no
`TBT_U4_VIEW`: below depth 1 it has only the watched `TBT_U4_LINK`
path (`tbtU4DiscoverLinks`, `tbtU4CheckLinks`, `tbtU4RemoveLink`), a
depth-2 router plugged later counted and left locked. The reading
against the code found what Part 3 did not yet say:

1. **Whose sequence a kid's is.** `ActivePort` makes a depth-1
   sequence its port's own (`TbtU4Eject`, `tbtU4Clean`,
   `tbtU4PcieLoss`, since `2f57cad2`); a kid's sequences now run as
   their host port's, so the parent's teardown never nests under them,
   and a kid's own unplug is not nested before 45.18 admits statements
   at depth 2 (section 6.2).
2. **A kid's own unplug hint confirmed by evidence**, as `tbtU4Live`
   confirms the active port's, so a late event never cancels a healthy
   connect (section 6.2).
3. **Which check a notification serves.** `tbtU4TakeQueues` hands every
   device router's ERR_CONN or ERR_LOCK to the host port as its unplug;
   one naming a kid's parent lane is now the kid's, a plain check only
   for the host port (section 6.5).
4. **The parent's Disable Hot Plug** cleared after the parent is UP, so
   depth 1's writes stay byte for byte (section 6.5).
5. **Links below a kid** dropped with it, their routes forgotten
   (section 6.7).
6. **Adoption from a link only when the link carries nothing else**, and
   the kid's teardown followed by `tbtU4RemoveLink`'s sweep as a residual
   pass: a kid closes only its PCIe tunnel, a watched link's removal
   every path (sections 6.7 and 6.9).
7. **The arrays the kids grow**: the obligations, the resume bound's
   count and the carry (section 6.9); and the one vector of today that
   expects a depth-2 plug left uninitialised changes with the code
   (section 9.1).

The revision was reviewed and revised before its commit (`bb3434c7`).

### The fake fabric and the vectors

- `test/u4_fakefab.c`, `.h`: protocol adapters paired below depth 1 (a
  device router's Nth down adapter with the child on its Nth downstream
  port, Spec 5.2.5); a device router's power loss setting Lock on its
  downstream lanes and resetting the routers behind them, still attached
  (Spec 3.3), the host-only reset kept local, no Lock on a Thunderbolt 3
  router's lanes; `FAB_DEV_TBT3_DOCK`, the Thunderbolt 3 dock with a
  downstream port as `usb4-emu`'s `usb4-router-tb3.c` device router has
  it (lanes 1 to 4, PCIe Up 6, PCIe Down 7, the latter's LTSSM field
  fixed at 3 as the emulator reads it); `fabHotPlugChild`,
  `fabHotUnplugChild`, `fabPreosChild`, `fabDownLane0` and
  `fabChainPcieUp`.
- `test/test_u4cm.c`: `test_chain_fabric` runs today (the dock at depth
  1, a hot-plugged child locked and uninitialised, the chained pair
  trained only with both Path Enables, Lock again after the parent's
  power loss, an enumerated child reset by its parent's unplug): 1,373
  checks, 0 failures (1,362 before). Eleven vectors under
  `#ifdef TBT_U4_MAX_KIDS`, written against section 6's names and
  compiled only once `tbt_u4cm.h` defines it: the connect behind a USB4
  parent and behind the dock (sections 6.4 and 6.6, all four entries,
  bonding before Configuration Valid, the child's Path Enable first),
  the refusals (a Thunderbolt 3 child, no PCIe Down, Detect not reached,
  no PCIe tunnel on the parent), a write never answered while the paths
  are written or enabled and a drain that never completes (undone or
  quarantined under the kid, the parent kept), the end device's unplug
  and a quick re-plug, a child's ERR_CONN never the parent's unplug, a
  stale unplug event during a healthy connect, the middle unplug with no
  write even attempted on the departed routers with the kid UP and
  CONNECTING, a kid's unplug at another port's safe point served at the
  next tick and a depth-3 link dropped with its kid, the parent's PCIe
  loss and both ejects, adoption (a preserved chain, a half-built one,
  one with a second path, one with no tunnel, and a preserved chain
  behind the dock), and the resume (verified, also behind the dock,
  rebuilt after the parent, the parent HELD with its kid, and behind the
  dock a power cycle and the dock re-plugged with its child attached).
  Checked to compile under `/Za /W3 /WX` against a copy of the header
  with section 6's fields added; not run.

### Left

- The code (record 34 section 6.10's `tbt_u4cm.c` and `.h` rows, then
  `cm_nhi.c`, `cm_eject.c`, `cm_ejectos.c` and `cm_status.c`, after
  45.12), until the guarded vectors pass with every depth-1 vector
  unchanged; then the gated builds.
- The `usb4-emu` cross-check on `CHAIN=1` (record 34 section 9.2,
  `scripts\usb4-crosscheck\u4cm-crosscheck.c`'s `test_chain`).
- The q35 legs on 98 SE to 10 x64 (record 34 section 9.3).
