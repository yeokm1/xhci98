# HP EliteBook 850 G5 - XHCIQUAL read-only probe (2026-10-06)

`PROBE.LOG` from `XHCIQUAL --probe-only --no-page --log`, read-only, under
MS-DOS 7.1, on the XHCIQUAL build that prints each Supported Protocol
capability's PSI entries and offset and the raw extended-capability chain,
keeping protocol tables past the dump's bound (`35c5723`). It replaces a
first run the same evening on the build before that (`f17b43e`, `6df116b`),
whose dump stopped short of the second controller's USB 3.1 table. Brought in by the owner on
2026-10-06 for [issue 11](../../../docs/issues/11-sunrise-point-ssic-psi-table.md):
the machine whose tester reported SuperSpeed devices failing.

Two controllers:

- Intel Sunrise Point-LP, xHCI 8086:9D2F rev 21, subsystem 103C:83B2. Its
  USB 3 protocol (ports 13-18, PSIC 3) lists only the SSIC rates 1248, 2496
  and 4992 Mb/s at IDs 1-3, and the raw chain from 8020h to 806Fh is word
  for word the E460's (`../e460-2026-10-06/PROBE.LOG`), the uncounted words
  after the three entries included. The USB 2.0 capability differs from the
  E460's only in DWORD 2's protocol-defined field (`30110C01` here,
  `30190C01` there).
- Intel 8086:15DB rev 02 (subsystem 8086:0000), a second xHCI controller,
  HCIVERSION 1.10, four ports: USB 2.0 on 1-2 (PSIC 3, at 8000h), USB 3.1 on
  3-4 (PSIC 2, at 8E28h, header `03010C02`: Minor Revision `01h`): ID 4 at
  5 Gb/s (`00050134`) and ID 5 at 10 Gb/s, Link Protocol SuperSpeedPlus
  (`000A4135`). Its chain reaches the dump's 256-DWORD bound on vendor
  capabilities first, and the USB 3.1 capability is recorded past it. The
  protocol-shaped words inside the first capability's span at 8020h are
  skipped by the chain and are not that controller's table.
