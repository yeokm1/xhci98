# PRO B650M-CT-CSM (AM5) - XHCIQUAL read-only probe (2026-10-06)

`PROBE.LOG` from `XHCIQUAL --probe-only --no-page --log`, read-only, under
MS-DOS 7.1, on the XHCIQUAL build that prints each Supported Protocol
capability's PSI entries and offset and the raw extended-capability chain,
keeping protocol tables past the dump's bound (`35c5723`). Brought in by
the owner on 2026-10-06, beside the Intel readings behind
[issue 11](../../../docs/issues/11-sunrise-point-ssic-psi-table.md).

Four AMD xHCI controllers, every USB 3 protocol listing ID 4 (5 Gb/s) and
ID 5 (10 Gb/s, Link Protocol SuperSpeedPlus), so issue 11's decoding does
not arise here; every USB 3 header reads Minor Revision `10h`, the BCD the
specification describes (which XHCIQUAL prints as `3.10`), where the Intel
parts read `01h`:

- 1022:43F7 rev 01 (subsystem 1B21:1142), HCIVERSION 1.10,
  18 ports: USB 3.1 on 1-6 (PSIC 2, at 0820h) **ahead of** USB 2.0 on 7-18
  (PSIC 0, at 0860h); the pairing convention matches 1-6 with 13-18 and
  leaves 7-12 USB 2.0-only. PCI power management reports DSI set.
- 1022:15B6 and 1022:15B7 rev 00 (subsystem 1043:8877), HCIVERSION 1.20,
  four ports each: USB 2.0 on 1-2 (PSIC 0) and one USB 3.1 capability per
  port for 3 and 4 (PSIC 2 each, at 04A0h and 04C0h); the convention pairs
  3 with 2 and leaves 4 without a USB 2.0 companion.
- 1022:15B8 rev 00 (subsystem 1043:8877), HCIVERSION 1.20, one USB 2.0 port.

The pairings are the convention's (`docs/usb-xhci-info/xhci-programming.md`,
"Port Topology Classification"), not a reading of the connectors.
