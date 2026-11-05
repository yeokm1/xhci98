# Issue 23 - An update left the UAS disk driver at the earlier version when no UAS disk was plugged in, and on 98 SE an update with one plugged in asked for the Windows CD

Status: **fixed for `3.4.1.0`** (task 46.24; commits `642b6cd2`,
`b73f66b4`, `1111f452`, `7d6203ee`, `5c55782b`, `559cfc7d`, merged as
`e8262104`), reviewed, and read on XP x86, 7 x86, 10 x64 and 98 SE
guests. **Not read on 2000, Vista, XP x64 or ME.** Both problems were in
`3.4.0.0`.

**Scope.** `xhciuas.sys`, the UAS disk driver, on an update over an
earlier release. Part 1 is Windows 2000 to 7 (and 8 to 10 before this
release); part 2 is Windows 98 SE and ME. Found by the project's own
update legs for the `3.4.1.0` cut (task 46.final, 2026-11-04), not by a
user report.

## 1. The update gap (NT)

An update replaces a driver through the devices that are plugged in. A
UAS disk Windows had already set up, but which was not plugged in during
the update, kept its devnode on the earlier `xhciuas.inf`. If no UAS
disk was plugged in at all, `xhciuas.sys` itself stayed at the earlier
version.

Seen on XP x86 and Windows 10 x64. The disk worked when it was plugged in
again, but on the earlier release's driver, so a fix in the new
`xhciuas.sys` did not reach it.

## 2. The 98 SE Insert Disk prompt

On 98 SE, an update with a UAS disk plugged in stopped with Windows'
Insert Disk prompt, asking for "xhci98 xHCI Host Controller Disk" and
looking for `xhciuas.sys` in `C:\WIN98`, Windows' own setup folder.

The cause is the installer's, not the INF's: the UAS disk's install was
the run's second job that copies files, after the controller's, and
Windows 98's setup then looks for files in its own source folder. Task
45.38 hit the same behaviour with a second controller (design record 27).

## 3. The fix

- **Windows 2000 to 7.** After the normal passes, every UAS devnode that
  is not plugged in and runs an older `xhciuas.sys` is updated from the
  staged INF without starting the device. If the file is still older
  after that, it is copied directly, and its version is read back. The
  console and the log list these disks as "(not plugged in)". On XP, each
  such disk adds one Windows driver-signing dialog, which `/yes` cannot
  answer.
- **Windows 8, 8.1 and 10.** Setup no longer installs or stages the UAS
  disk driver: Windows' own UAS driver runs UAS disks, and the UAS box is
  unticked and greyed. A disk an earlier release set up with
  `xhciuas.sys` is left as it is (owner, 2026-11-04).
- **Windows 98 SE and ME.** `xhciuas.sys` and `xhcicm.sys` are put in
  place before the helper's jobs, and those jobs no longer copy the file
  themselves, so no second copying job runs.

## 4. Readings

All on emulated machines with QEMU UAS drives (`docs/contributing/runs/run-46.md`,
"46.24"):

| System | Result |
|---|---|
| XP x86, two known disks, both unplugged during the update | both moved to 3.4.1.0 during the update; the file was 3.4.1.0 after the restart; each disk plugged in with no prompt and read and wrote |
| 7 x86, the same | the same |
| 10 x64, update over a disk bound to `xhciuas` | the binding and the file left at 3.4.0.0; the disk read and wrote |
| 10 x64, fresh install | no `xhciuas.inf` staged and no `xhciuas.sys` |
| 98 SE, disk plugged in during the update | no Insert Disk prompt; both files 3.4.1.0 after the restart; the disk mounted with no wizard and read and wrote |

## 5. Not covered

Windows 2000 (the test harness could not drive its base), Vista, XP x64
and ME were not run. A refused or cancelled signing dialog for a disk
that is not plugged in was checked only in code review.
