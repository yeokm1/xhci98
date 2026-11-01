/*
 * inst_help.c - the windowed installer's Help texts, one a page, and which
 * page opens which (design record 26 section 5.10; roadmap-usb4 task
 * 45.66; owner, 2026-10-31). Pure: no Windows call, so the host suite
 * test\test_inst.c links it with inst_core.c and checks every text and
 * route; the window (xhciwiz.c) links it too. The console front end does
 * not: its questions carry their own explanations.
 *
 * A file of its own, not in inst_core.c, which the tray tool, the mark
 * service and several host suites link: those images have no use for the
 * texts. C89, ASCII.
 */

#include <stddef.h>
#include "inst_core.h"


/* One entry a topic, in INST_HELP_* order: the title after "Help: ", and
 * the text: a short paragraph a choice, starting with the page's label,
 * then what it does, then when to change it, the paragraphs (an intro
 * among them) apart by one empty line (owner, 2026-10-31). A text longer
 * than the window scrolls there. */
static const struct {
    int Topic;
    const char *Title;
    const char *Text;
} instHelpTexts[INST_HELP_COUNT] = {
    { INST_HELP_WELCOME, "Welcome",
      "The list: each controller this package has a driver for, what runs "
      "it now, and what this install does to it. The note under it says "
      "what to do first when something needs it, such as a hardware "
      "wizard Windows has open.\n\n"
      "Nothing is chosen on this page. Next goes on to the choices; Cancel "
      "leaves with nothing changed.\n\n"
      "The log named above records what the installer reads and does: "
      "keep it when you report a problem." },
    { INST_HELP_UWELCOME, "Uninstall",
      "Remove takes away every driver of this package, its tools and its "
      "records.\n\n"
      "The box: tick it once you have a keyboard and mouse that do not go "
      "through these drivers (PS/2, a laptop's built-in ones, or a USB "
      "controller another driver runs). Where only the connection manager "
      "is removed: once every program that uses a Thunderbolt or USB4 "
      "device is closed.\n\n"
      "Next shows what will be removed; nothing is changed before Remove." },
    { INST_HELP_COMP, "Components",
      "xHCI98 USB 3 host controller driver: runs the USB 3 controllers on "
      "the Welcome page. Untick it to install the Thunderbolt support "
      "alone.\n\n"
      "UAS disk driver: the faster protocol of USB disks that offer it. "
      "Untick it only if another UAS driver is installed.\n\n"
      "USB storage drivers for Windows 98 SE: the disk drivers 98 SE "
      "lacks. Leave it unticked if NUSB is installed.\n\n"
      "Thunderbolt 3 / USB4 support: the connection manager. Tick it if "
      "this computer has such a port; the next page holds its choices.\n\n"
      "Start the xHCI98 USB Tree (tray icon) at login: the tool that lists "
      "USB devices and stops them for removal; installed either way.\n\n"
      "Tell me when a USB device can run faster: the USB Tree's notice for "
      "a device on a slower port than it supports.\n\n"
      "Replace the stored build of this package's drivers: available where "
      "Windows' driver store holds this version with other bytes; ticked, "
      "that copy is replaced and the devices installed again.\n\n"
      "Settings...: the drivers' settings.\n\n"
      "Change folder...: where the tools and this installer go." },
    { INST_HELP_TBT, "Thunderbolt and USB4",
      "Find Thunderbolt and USB4 devices placed above 4 GB: XHCIPFW.VXD on 98 SE and "
      "ME, XHCIPFW.SYS on 2000 to the 32-bit 8.1. At each start it closes "
      "the PCI bridge windows above 4 GB this Windows cannot use, so the "
      "devices behind them are found; it writes nothing where there are "
      "none. Leave it ticked.\n\n"
      "Stor98: the driver for NVMe drives behind a Thunderbolt or USB4 "
      "tunnel. Untick it only if another driver runs those drives.\n\n"
      "Large transfers for Stor98 (98 SE and ME): reads and writes of up "
      "to 2 MiB whole, instead of 63 KiB pieces.\n\n"
      "Give it a PCI Express connection at once: each device plugged in is "
      "connected and can read and write this computer's memory; for "
      "devices you trust. Some controllers' firmware decides this itself.\n\n"
      "Give it none: no new PCI Express connection; a device may still "
      "work over USB. Connections made before Windows starts stay, unless "
      "XhciCmPreOsCleanup ends them.\n\n"
      "The choice applies from the next restart; on a new install unplug "
      "the dock until then if you choose none.\n\n"
      "Thunderbolt networking: a network connection to another computer "
      "joined by a Thunderbolt or USB4 cable, which then reaches this one "
      "as over any network, file sharing included. Ticked, the network "
      "adapter appears after the restart; unticked later, it goes after "
      "the next one. Greyed where this package has no network driver for "
      "this Windows." },
    { INST_HELP_FREE, "Free USB drivers",
      "The box: copies the free USB class drivers (usb-wdm-free) to the "
      "install folder, for USB devices this Windows has no driver for.\n\n"
      "The list under it: a package ticked there is staged, so Windows "
      "uses it by itself when a matching device is plugged in; unticked, "
      "point the hardware wizard at its folder when you want it. The text "
      "under the list says which are copied only on this Windows.\n\n"
      "Trust usb-wdm-free's test certificate on this computer (64-bit "
      "Windows): adds the certificate its 64-bit drivers are signed with "
      "to the trusted roots and publishers; the uninstall removes the "
      "trust this installer added.\n\n"
      "The line under the box says where the drivers are, or where to "
      "download them." },
    { INST_HELP_NONFREE, "Non-free USB drivers",
      "The box: copies other manufacturers' drivers (USB Ethernet, serial "
      "and game pads) to the install folder. The copy installs nothing.\n\n"
      "The list under it: a package ticked there is staged, so Windows "
      "uses it by itself when a matching device is plugged in; unticked, "
      "point the hardware wizard at its folder when you want it. The text "
      "under the list says which are copied only on this Windows.\n\n"
      "The line under the box says where the drivers are, or where to "
      "download them." },
    { INST_HELP_SET_USB, "USB settings",
      "XhciSilentInstall (98 SE and ME): installs hubs, boot keyboards and "
      "mice and Bulk-Only storage with no Add New Hardware wizard.\n\n"
      "XhciIntelPortSwitch (Intel controllers with shared connectors): "
      "moves those connectors from the EHCI controller to this driver; 2 "
      "only on the release notes' advice.\n\n"
      "A greyed row is for another system or controller and cannot be "
      "changed here.\n\n"
      "More >: the tab's next rows, where they do not all fit.\n\n"
      "Advanced...: the other USB settings, each explained.\n\n"
      "A row left as it is writes nothing. A change is written "
      "machine-wide when you click Install and is read from the next "
      "restart.\n\n"
      "OK keeps your choices for Install; Cancel drops them." },
    { INST_HELP_SET_TB, "Thunderbolt settings",
      "XhciCmMode: the Thunderbolt and USB4 support, 1 automatic (the "
      "default) or 0 off; 2 and 3 only on the release notes' advice. A new "
      "install starts with the default until the restart: unplug the dock "
      "until then if you choose 0.\n\n"
      "XhciCmSafeEject: 1 offers Safely Remove for the devices behind a "
      "dock; 0 does not, and the install also removes the safe-removal "
      "mark service with its marks.\n\n"
      "The approval of new devices is on the Thunderbolt and USB4 page.\n\n"
      "More >, where shown: the next rows.\n\n"
      "Advanced...: the other Thunderbolt settings.\n\n"
      "A row left as it is writes nothing; a change is written "
      "machine-wide when you click Install.\n\n"
      "OK keeps your choices for Install; Cancel drops them." },
    { INST_HELP_SET_S98, "Stor98 settings",
      "StorWriteThrough: how writes reach the drive's media: auto "
      "(cacheoff on ME, off elsewhere), off (the drive as it is), cacheoff "
      "(its write cache off) or fua (every write marked Force Unit "
      "Access).\n\n"
      "StorPollMode: 0 polls only without an interrupt, 1 always polls.\n\n"
      "A row left as it is keeps Stor98's current setting; where nothing "
      "is set, its built-in default applies. Change one when Stor98's "
      "readme or a problem report points to it.\n\n"
      "Advanced...: Stor98's other settings.\n\n"
      "OK keeps your choices for Install; Cancel drops them." },
    { INST_HELP_ADV, "Advanced settings",
      "The settings not on the tab, one row each with its value now. "
      "Select a row to read what it does under the list, then choose its "
      "value under Setting.\n\n"
      "A value left as it is is not written; a change is written "
      "machine-wide when you click Install. Change one only when the "
      "release notes or a problem report advise it.\n\n"
      "OK keeps your choices for Install; Cancel drops them." },
    { INST_HELP_ADV_S98, "Stor98's advanced settings",
      "Stor98's settings not on the tab, one row each. Select a row to "
      "read what it does, then give its value under The value.\n\n"
      "A row left as it is keeps Stor98's current setting; where nothing "
      "is set, its built-in default applies. Change one only when Stor98's "
      "readme or a problem report advises it.\n\n"
      "OK keeps your choices for Install; Cancel drops them." },
    { INST_HELP_SUMMARY, "Summary",
      "What Install will do: the devices, the components, the settings "
      "written and the install folder, and what to know before going on. "
      "Nothing has been changed yet.\n\n"
      "Back: to change a choice.\n\n"
      "Install: goes on.\n\n"
      "The USB keyboard and mouse on a controller whose driver changes "
      "stop for a moment: have a keyboard and mouse that do not go "
      "through it." },
    { INST_HELP_USUMMARY, "Summary",
      "What Remove will take away: each driver of this package, its tools "
      "and its records. Nothing has been changed yet.\n\n"
      "Remove: goes on.\n\n"
      "Cancel: leaves with nothing changed." },
    { INST_HELP_PROGRESS, "Progress",
      "The steps as they run. Windows may open windows of its own, such as "
      "Found New Hardware or a driver-signing question: answer them (Next, "
      "Yes or Continue, then Finish); this waits for them.\n\n"
      "Cancel stops at the next step; what was done stays. While a "
      "Thunderbolt drive's reconnection is awaited, Cancel skips that wait "
      "alone and the run goes on." },
    { INST_HELP_STEPS, "Steps left for you",
      "Devices the installer could not install by itself, each with the "
      "steps to do after the restart.\n\n"
      "Save as text file...: keeps the steps to read after the restart.\n\n"
      "Open Device Manager: where the steps are done." },
    { INST_HELP_FINISH, "Finish",
      "What was done and what is left, and the log's place.\n\n"
      "Restart Windows now: Finish restarts Windows, and what was "
      "installed starts.\n\n"
      "Restart Windows later: to save your work, or to do the steps "
      "first; restart before using the devices." },
    { INST_HELP_FINISH_SHUT, "Finish",
      "What was done and what is left, and the log's place.\n\n"
      "Shut down now, then start Windows again: Finish shuts Windows down; "
      "start it again yourself. Stor98's update, or the USB storage "
      "drivers' copy, needs a fresh start rather than a restart.\n\n"
      "Shut down later: to save your work first." },
    { INST_HELP_FINISH_NONE, "Finish",
      "This run changed nothing on this computer, so no restart is asked; "
      "the last line above says why. Finish closes this window." },
    { INST_HELP_UFINISH, "Finish",
      "What was removed and what is left, and the log's place.\n\n"
      "Restart Windows now: Finish restarts Windows, so what was removed "
      "is gone from it.\n\n"
      "Restart Windows later: to save your work first, or to do what the "
      "page says is left." },
    { INST_HELP_PROGRESS_9X, "Progress",
      "The steps as they run. Windows may open windows of its own, such as "
      "the Add New Hardware Wizard: answer them (Next, then Finish); this "
      "waits for them.\n\n"
      "Cancel stops at the next step; what was done stays. While a "
      "Thunderbolt drive's reconnection is awaited, Cancel skips that wait "
      "alone and the run goes on." }
};

int inst_help_topic(int kind, int uninstall, int variant)
{
    switch (kind) {
    case INST_HK_WELCOME:
        return uninstall ? INST_HELP_UWELCOME : INST_HELP_WELCOME;
    case INST_HK_COMP:
        return INST_HELP_COMP;
    case INST_HK_TBT:
        return INST_HELP_TBT;
    case INST_HK_FREE:
        return INST_HELP_FREE;
    case INST_HK_NONFREE:
        return INST_HELP_NONFREE;
    case INST_HK_SET_USB:
        return INST_HELP_SET_USB;
    case INST_HK_SET_TB:
        return INST_HELP_SET_TB;
    case INST_HK_SET_S98:
        return INST_HELP_SET_S98;
    case INST_HK_ADV:
        return INST_HELP_ADV;
    case INST_HK_ADV_S98:
        return INST_HELP_ADV_S98;
    case INST_HK_SUMMARY:
        return uninstall ? INST_HELP_USUMMARY : INST_HELP_SUMMARY;
    case INST_HK_PROGRESS:
        /* Each system's own wizard named (98 SE and ME: the Add New
         * Hardware Wizard). */
        return variant == INST_PV_9X ? INST_HELP_PROGRESS_9X
                                     : INST_HELP_PROGRESS;
    case INST_HK_STEPS:
        return INST_HELP_STEPS;
    case INST_HK_FINISH:
        if (variant == INST_FV_NONE) {
            return INST_HELP_FINISH_NONE;
        }
        if (uninstall) {
            return INST_HELP_UFINISH;
        }
        return variant == INST_FV_SHUT ? INST_HELP_FINISH_SHUT
                                       : INST_HELP_FINISH;
    default:
        return -1;
    }
}

const char *inst_help_text(int topic)
{
    return topic >= 0 && topic < INST_HELP_COUNT ?
           instHelpTexts[topic].Text : "";
}

const char *inst_help_title(int topic)
{
    return topic >= 0 && topic < INST_HELP_COUNT ?
           instHelpTexts[topic].Title : "";
}
