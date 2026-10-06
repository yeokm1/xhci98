# HP EliteBook 850 G5 - XHCIQUAL read-only probe (2026-10-06)

`PROBE.LOG` from `XHCIQUAL --probe-only --no-page --log`, read-only, under
MS-DOS 7.1, on the XHCIQUAL build that prints each Supported Protocol
capability's PSI entries and the raw extended-capability chain (`f17b43e`,
`6df116b`; its banner predates `dfbac0c`). Brought in by the owner on
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
  HCIVERSION 1.10, four ports: USB 2.0 on 1-2 (PSIC 3), USB 3.1 on 3-4,
  PSIC 2: ID 4 at 5 Gb/s and ID 5 at 10 Gb/s, Link Protocol SuperSpeedPlus.
  Its raw dump reached XHCIQUAL's recording bound (256 DWORDs) before the
  USB 3.1 capability, so that capability's words are in the decoded lines
  only; the protocol-shaped words inside the first capability's span at
  8020h are skipped by the chain and are not that controller's table.
