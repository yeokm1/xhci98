# external/ - Local reference source mirror

Read-only copies of the relevant upstream source files this project uses as
references (see `AGENTS.md` "Reference Implementations" and the per-doc
citations). Only the files this project actually consults are mirrored here,
not the full upstream repositories.

Everything under `external/` except this README is git-ignored. It is a local
convenience cache; nothing here is part of this project's source or is
redistributed by it. Re-fetch it with the commands below on any machine.

## What is here, and why

| Path | Upstream | Role in this project |
|---|---|---|
| `reactos/usbmport.h` | `sdk/include/reactos/drivers/usbport/usbmport.h` | The miniport-facing ABI header: `USBPORT_REGISTRATION_PACKET`, callback typedefs, version/flag constants. The single most important reference for Phase 3. |
| `reactos/usbport/` | `drivers/usb/usbport/` | The port driver `xhci98.sys` plugs into: `usbport.c` (registration/takeover), `endpoint.c`, `roothub.c`, `device.c` (the SET_ADDRESS flow to intercept), `queue.c`, `iface.c`, `pnp.c`, `power.c`, plus `usbport.spec` (export list). |
| `reactos/usbehci/` | `drivers/usb/usbehci/` | A complete USB2 miniport against that ABI; the structural template for `src/xhci_dispatch.c`. |
| `reactos/usbohci/`, `reactos/usbuhci/` | `drivers/usb/usbohci/`, `usbuhci/` | USB 1.1 miniports; secondary templates showing which parts vary by controller type. |
| `linux/` | `drivers/usb/host/` | xHCI hardware programming: `xhci-ring.c`, `xhci-mem.c`, `xhci-hub.c`, ext-caps; and the quirk references `xhci-pci.c`, `xhci-pci-renesas.c` (Renesas uPD720201/202 firmware upload), `pci-quirks.c` (BIOS handoff + Intel port switchover). |
| `freebsd/` | `sys/dev/usb/controller/` | Secondary. Clean xHCI register-sequence reference: `xhci.c`, `xhci.h`, `xhcireg.h`, `xhci_pci.c`. |
| `haiku/` | `src/add-ons/kernel/busses/usb/` | Secondary. Readable xHCI implementation: `xhci.cpp`, `xhci.h`, `xhci_hardware.h`, `xhci_rh.cpp`. |
| `qemu/` | `hw/usb/` | The emulated controller this project's guests actually run against, read to settle what the model does and does not implement: `hcd-xhci.c`, `hcd-xhci.h`. Added 2026-09-20 for roadmap task 23.2, whose first question is whether QEMU models interrupt moderation at all. It does not - `imod` is stored, returned and migrated, and no code path consults it - so no interrupt-rate reading can come out of a guest. **This tree documents the emulator, not the hardware**: where it and `xhci-data-structures.md` disagree the spec wins, and the disagreements are themselves findings (the model resets IMOD to 0 where hardware resets it to 4000). |

Split by layer (per `AGENTS.md`): ReactOS documents the upward usbport
miniport interface; Linux documents the downward xHCI hardware. FreeBSD and
Haiku are secondary, kept for a second opinion on a hardware detail. Nothing
currently rests on them: the Route String tier order they were cited for was
settled by the controller itself in batch 7b-A (see
`docs/usb-xhci-info/xhci-data-structures.md`, "Route String tier order").

Bit positions still come from `docs/usb-xhci-info/xhci-data-structures.md`
(verified against the spec PDF), and the usbport ABI is still validated against
the NUSB-installed binary. These mirrors are readable cross-references, not the
source of truth.

## Licensing

These are third-party sources under their own licenses. They are references
only and are not to be copied into `src/`:

- ReactOS, GPL 2.0. Use strictly as documentation of the undocumented usbport
  ABI (struct layouts, function signatures for interoperability). Write this
  project's code independently; do not paste function bodies into `src/`
  (`docs/usb-xhci-info/usbport-miniport-interface.md`, license note).
- Linux, GPL 2.0. Reference for hardware behaviour only.
- FreeBSD, BSD 2-Clause.
- Haiku, MIT.

## Source revisions (as mirrored)

Fetched at these upstream commits:

| Repo | Commit | Date |
|---|---|---|
| reactos/reactos | `0298e10d5d904a0230868be8f7bdf6436d589c62` | 2026-07-18 |
| torvalds/linux | `c6859eed755df351a3978b33cb92365f9b3e8f06` | 2026-07-18 |
| freebsd/freebsd-src | `cb325dcedfa291c9bfe350a513694df3776a17a4` | 2026-07-18 |
| haiku/haiku | `618e42b86da9d041c995efe240af0a11329751fe` | 2026-07-18 |
| qemu/qemu | tag `v11.1.0`, cross-checked against master `c1c18d1e640b64292859ce9f30f3c344edfb0294` | 2026-09-20 |

The QEMU row names a tag rather than only a commit because the question asked
of that tree is version-specific. It was read at `v11.1.0` - the version this
host's `qemu-system-x86_64` reports - and again at the master commit above,
which is a day older than the reading; the `imod` sites are identical in both,
to the line number. The installed binary reports the build id
`v11.1.0-12130-ge470268ff4`, which is **not** a commit in `qemu/qemu` and could
not be fetched: it is the Windows packaging build's own descriptor. So the
reading rests on the released tag matching that binary's version, not on
having read the exact tree it was built from.

## Refreshing the mirror

Run from the repo root. This pulls only the relevant subtrees (blobless sparse
checkout), copies out the files listed above, and discards the clones, so no
git repositories are left behind under `external/`. The block is a skeleton:
the copy step between the clones and the `rm -rf` is the table above, and it
is spelled out there rather than duplicated here. Run as written, with the
copy step skipped, it leaves `external/` empty.

```sh
cd external
tmp="$(mktemp -d)"

# ReactOS: usbport ABI header + port driver + template miniports
git clone --depth 1 --filter=blob:none --sparse https://github.com/reactos/reactos "$tmp/reactos"
git -C "$tmp/reactos" sparse-checkout set \
  drivers/usb/usbport drivers/usb/usbehci drivers/usb/usbohci drivers/usb/usbuhci \
  sdk/include/reactos/drivers/usbport

# Linux: core xHCI hardware + PCI quirk/handoff + Renesas firmware
git clone --depth 1 --filter=tree:0 --sparse https://github.com/torvalds/linux "$tmp/linux"
git -C "$tmp/linux" sparse-checkout set drivers/usb/host

# FreeBSD: register-sequence reference
git clone --depth 1 --filter=blob:none --sparse https://github.com/freebsd/freebsd-src "$tmp/freebsd"
git -C "$tmp/freebsd" sparse-checkout set sys/dev/usb/controller

# Haiku: readable xHCI implementation
git clone --depth 1 --filter=blob:none --sparse https://github.com/haiku/haiku "$tmp/haiku"
git -C "$tmp/haiku" sparse-checkout set src/add-ons/kernel/busses/usb

# QEMU: the emulated xHC the guests run against. Fetch the TAG matching the
# installed qemu-system-x86_64, not master - what the model implements is a
# per-version fact and the whole point of reading it is to know what a guest
# reading can and cannot mean.
git clone --depth 1 --filter=blob:none --sparse https://github.com/qemu/qemu "$tmp/qemu"
git -C "$tmp/qemu" sparse-checkout set hw/usb
git -C "$tmp/qemu" fetch --depth 1 origin tag v11.1.0
git -C "$tmp/qemu" checkout v11.1.0

# Copy out just the relevant files (see the table above), then drop the clones.
# For example, the ABI header and the port driver:
cp "$tmp/reactos/sdk/include/reactos/drivers/usbport/usbmport.h" reactos/
cp -r "$tmp/reactos/drivers/usb/usbport" reactos/
# ...and so on for each row of the table, then:
rm -rf "$tmp"
```

For a from-scratch re-copy of the exact file set, follow the table above: the
per-directory file lists are small and stable.
