XHCICLN - the devnodes an earlier xhci98.sys left behind
========================================================

What it is for
--------------

Until 2.4.0.0 the driver gave its root hub, the USB hubs it serves and the
composite devices it splits device ids under XHCI98\ (XHCI98\ROOT_HUB,
XHCI98\HUB&VID_...&PID_..., XHCI98\HUB30&..., XHCI98\COMPOSITE&...). From
2.4.0.0 they are under USB\ (USB\XHCI98_ROOT_HUB and the rest). Windows
keeps a devnode per device id, so after an update from 2.3.x or earlier each
root hub, hub and composite device is found once more under its new id, and
its old devnode stays behind, not present.

The old devnodes are inert. Nothing loads for them, Device Manager hides
them, and nothing needs them removed. This tool is for a user who wants
them gone. Nothing runs it, and an install never needs it.

When to run it
--------------

After the update from 2.3.x or earlier AND the restart that loads the new
driver. Before that restart the old driver still runs and its devnodes are
present, and the tool refuses.

Which file
----------

  XHCICLN.EXE   Windows 98 SE, ME, 2000, XP, 32-bit Vista and 7
  XHCICL64.EXE  Windows XP x64, Vista x64 and 7 x64

On 64-bit Windows a 32-bit program cannot remove a device, so XHCICLN.EXE
there lists the old devnodes and refuses to remove any, naming XHCICL64.EXE.

On Windows 2000 and XP run it as an administrator. On Vista and 7 it asks
for elevation itself.

How to run it
-------------

From a command prompt in this directory:

  XHCICLN            lists the old devnodes, removes nothing, and says
                     whether -remove would remove them
  XHCICLN -remove    lists, asks once (type Y and Enter to go on; anything
                     else cancels), then removes them one by one
  XHCICLN -remove -yes   the same without the question

(XHCICL64 in place of XHCICLN on the 64-bit systems.)

It removes only devnodes whose id is one of the four old forms above and
that are conclusively not present. It never touches a present devnode, a
current USB\XHCI98_ one, any USB device's or function's own devnode, the
controller's, or any file, driver package or service.

It removes nothing at all - and says why - when an old devnode is present
(the old driver is running), when no controller of this project is present
and started with its new root hub, or when a check cannot be answered for
certain. One such controller is enough: with two, one of them uninstalled
and the other running, it removes the old entries of both. For its whole
run it keeps each controller's device open, so Windows refuses to stop or
replace the driver while it works, and it checks the controllers again
around each removal.

Windows 98 SE and ME: list only
-------------------------------

On Windows 98 SE and ME this release lists the old entries and removes
nothing. Those systems do not refuse to stop a driver whose device is held
open, so nothing would keep an older driver from loading during a run, and
the system call that would remove an entry there (CM_Uninstall_DevNode)
answers success and removes nothing on both, as read in each system's own
CFGMGR32.DLL.
The entries are inert; leave them.

Exit codes
----------

  0  nothing to do, every old devnode removed, or (98 SE, ME) listed
  1  refused before any removal (a refusal above, rights, a 32-bit run on
     64-bit Windows, a missing system routine, or the question cancelled)
  2  stopped part-way: a removal failed, or a check changed. The lines
     printed say which devnodes were removed and whether a restart is
     pending for any of them
  3  every old devnode removed, a restart pending

Seeing the old devnodes yourself (2000 to 7)
--------------------------------------------

At a command prompt:

  set DEVMGR_SHOW_NONPRESENT_DEVICES=1
  devmgmt.msc

then View, Show hidden devices. The old entries show greyed beside the new
ones under Universal Serial Bus controllers; the old and the new carry the
same names.

Going back to 2.3.x
-------------------

An earlier driver reports the old ids again, and each old devnode comes
back with its driver - unless this tool removed it, in which case it is
found and installed afresh (on XP with the Found New Hardware wizard).

------------------------------------------------------------------------------
Part of the xhci98 3.1.0.0 release. See the readme.txt two directories up
for the driver itself.

This project's own code is under the GNU GPL v2 - see the LICENSE file two
directories up. XHCICLN.EXE and XHCICL64.EXE are not only this project's
code: a C runtime is statically linked into each, which NOTICE.TXT beside
this file records.
