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
      "The list shows each device this package has a driver for, its "
      "current driver, and what Setup will do. The note under it says what "
      "to do first if needed, such as finishing a hardware wizard Windows "
      "has open.\n\n"
      "Nothing is chosen on this page. Next continues; Cancel exits with "
      "nothing changed.\n\n"
      "The log file named above records what Setup reads and does. Keep it "
      "when you report a problem." },
    { INST_HELP_UWELCOME, "Uninstall",
      "Setup will remove all of this package's drivers, tools and "
      "records.\n\n"
      "The box: tick it once you have a keyboard/mouse that does not use "
      "these drivers (PS/2, a laptop's built-in one, or a USB controller "
      "run by another driver). When only the connection manager is "
      "removed: once all programs using Thunderbolt / USB4 devices are "
      "closed.\n\n"
      "Next shows what will be removed. Nothing is changed before Remove." },
    { INST_HELP_COMP, "Components",
      "xHCI98 USB 3 driver: runs the USB 3 controllers listed on the "
      "Welcome page. Untick it to install only the Thunderbolt / USB4 "
      "support.\n\n"
      "UAS disk driver: the faster protocol for USB disks that support it. "
      "Untick only if you use another UAS driver.\n\n"
      "Storage drivers for Win 98 SE: the USB disk drivers 98 SE lacks. "
      "Untick if you already have NUSB installed.\n\n"
      "Thunderbolt / USB4 support: the connection manager. Tick it if "
      "this computer has such a port; the next page has its options.\n\n"
      "Start xHCI98 USB Tree (tray icon) at login: the tool that lists USB "
      "devices and stops them for removal. It is installed either way.\n\n"
      "Notify me when a USB device can run faster: the USB Tree's "
      "notification for a device on a slower port than it supports.\n\n"
      "Replace the driver files Windows has stored for this package: "
      "available when Windows' driver store holds this version with "
      "different files. Ticked, that copy is replaced and the devices are "
      "installed again.\n\n"
      "Settings...: the USB settings.\n\n"
      "Change folder...: where the tools and Setup go." },
    { INST_HELP_TBT, "Thunderbolt and USB4",
      "Find Thunderbolt / USB4 controllers and devices placed above 4 GB: "
      "XHCIPFW.VXD on 98 SE and ME, XHCIPFW.SYS on 2000 to the 32-bit 8.1. "
      "At each start it closes the PCI bridge windows above "
      "4 GB this Windows cannot use, so the controllers and devices behind "
      "them are found. It changes nothing where there are none. Leave it "
      "ticked.\n\n"
      "Stor98: the driver for NVMe drives in Thunderbolt / USB4 "
      "enclosures. Untick only if another driver runs those drives.\n\n"
      "Allow Stor98 transfer chunks up to 2MiB (98 SE and ME): reads and "
      "writes of up to 2 MiB at once, instead of 63 KiB pieces.\n\n"
      "Connect PCIe devices automatically: each device plugged in is "
      "connected and can read and write this computer's memory. Only for "
      "devices you trust. Some controller firmware can control this "
      "instead.\n\n"
      "Do not connect PCIe devices: no new PCIe connection is made; a "
      "device may still work over USB. Connections made before Windows "
      "starts are kept unless XhciCmPreOsCleanup is set.\n\n"
      "Change only takes effect after restart.\n\n"
      "Thunderbolt networking: network with another computer over a TB / "
      "USB4 cable, file sharing included. Ticked, the network adapter "
      "appears after restart; unticked later, it is removed after the next "
      "restart. Greyed when this package has no network driver for this "
      "Windows.\n\n"
      "Settings...: the Thunderbolt / USB4 and Stor98 settings." },
    { INST_HELP_FREE, "Free USB drivers",
      "The box: copies the free USB drivers (usb-wdm-free) to the install "
      "folder, for USB devices this Windows has no driver for.\n\n"
      "The list: a ticked package is staged, so Windows installs it "
      "automatically when a matching device is plugged in. Unticked, point "
      "Windows' hardware wizard at its folder when you need it. The text "
      "under the list says which are only copied on this Windows.\n\n"
      "Trust usb-wdm-free's 64-bit test certificate (64-bit Windows): adds "
      "the certificate its 64-bit drivers are signed with to this "
      "computer's trusted certificates. Uninstalling will remove this "
      "certificate if Setup added it.\n\n"
      "The line under the box says where the drivers are, or where to "
      "download them." },
    { INST_HELP_NONFREE, "Non-free USB drivers",
      "The box: copies all non-free USB drivers (USB Ethernet, serial, game "
      "pads and audio) to the install folder. Copying installs "
      "nothing.\n\n"
      "The list: a ticked package is staged, so Windows installs it "
      "automatically when a matching device is plugged in. Unticked, point "
      "Windows' hardware wizard at its folder when you need it. The text "
      "under the list says which are only copied on this Windows.\n\n"
      "The line under the box says where the drivers are, or where to "
      "download them." },
    { INST_HELP_SET_USB, "USB settings",
      "XhciSilentInstall (98 SE and ME): installs hubs, keyboards, mice and "
      "Bulk-Only disks without the Add New Hardware wizard.\n\n"
      "XhciIntelPortSwitch (Intel controllers with shared ports): moves the "
      "shared ports from EHCI controller to xHCI. Use 2 only if the release "
      "notes advise it.\n\n"
      "A greyed row is for another system or controller and cannot be "
      "changed here.\n\n"
      "More >: the next rows, when they do not all fit.\n\n"
      "Advanced...: the other USB settings, each explained.\n\n"
      "Only the settings you change are saved for all controllers, when "
      "you click Install. They take effect after restart.\n\n"
      "OK keeps your choices; Cancel discards them." },
    { INST_HELP_SET_TB, "Thunderbolt / USB4 settings",
      "XhciCmMode: Thunderbolt / USB4 support, 1 automatic (default) or 0 "
      "off. Use 2 and 3 only if the release notes advise it. Change only "
      "takes effect after restart.\n\n"
      "XhciCmSafeEject: 1 offers Safely Remove for devices behind a dock; 0 "
      "does not, and also removes the safe-removal mark service and its "
      "marks.\n\n"
      "XhciCmAsm4242Structural (98 SE and ME, shown when Setup finds an "
      "ASMedia ASM4242 whose firmware it does not know): 1 sends the USB4 "
      "handover if the firmware's handover code matches a known build's. "
      "Without the handover the TB / USB4 ports do not work.\n\n"
      "Whether PCIe devices connect automatically is on the Thunderbolt / "
      "USB4 page.\n\n"
      "More >, when shown: the next rows.\n\n"
      "Advanced...: the other Thunderbolt / USB4 settings.\n\n"
      "Only the settings you change are saved for all controllers, when "
      "you click Install.\n\n"
      "OK keeps your choices; Cancel discards them." },
    { INST_HELP_SET_S98, "Stor98 settings",
      "StorWriteThrough: how writes reach the drive. auto (cacheoff on ME, "
      "off elsewhere), off (drive left as it is), cacheoff (the drive's "
      "write cache off) or fua (every write marked Force Unit Access).\n\n"
      "StorPollMode: 0 polls only if there is no interrupt, 1 always "
      "polls.\n\n"
      "Only the settings you change are saved. Unchanged ones keep Stor98's "
      "current setting, or its default if none is set. Change one only if "
      "Stor98's readme or a problem report advises it.\n\n"
      "Advanced...: Stor98's other settings.\n\n"
      "OK keeps your choices; Cancel discards them." },
    { INST_HELP_ADV, "Advanced settings",
      "The settings not on the tab, one row each with its current value. "
      "Select a row to read what it does under the list, then choose its "
      "value under Setting.\n\n"
      "Only the settings you change are saved for all controllers, when "
      "you click Install. Change one only if the release notes or a "
      "problem report advise it.\n\n"
      "OK keeps your choices; Cancel discards them." },
    { INST_HELP_ADV_S98, "Stor98's advanced settings",
      "Stor98's settings not on the tab, one row each. Select a row to read "
      "what it does, then type its value under Value.\n\n"
      "Only the settings you change are saved. Unchanged ones keep Stor98's "
      "current setting, or its default if none is set. Change one only if "
      "Stor98's readme or a problem report advises it.\n\n"
      "OK keeps your choices; Cancel discards them." },
    { INST_HELP_SUMMARY, "Summary",
      "What Install will do: the devices, components, settings saved and "
      "install folder, and what to know before you continue. Nothing has "
      "been changed yet.\n\n"
      "Back: change a choice.\n\n"
      "Install: make the changes.\n\n"
      "USB keyboards and mice on a controller whose driver changes stop "
      "working for a moment. Have a keyboard/mouse that uses alternative "
      "connections like PS/2." },
    { INST_HELP_USUMMARY, "Summary",
      "What Remove will remove: all of this package's drivers, tools and "
      "records. Nothing has been changed yet.\n\n"
      "Remove: make the changes.\n\n"
      "Cancel: exit with nothing changed." },
    { INST_HELP_PROGRESS, "Progress",
      "The steps as they run. Windows may open its own windows, such as "
      "Found New Hardware or a driver-signing question: answer them (Next, "
      "Yes or Continue, then Finish). Setup waits for them.\n\n"
      "Cancel stops at the next step; what was done is kept. While Setup "
      "waits for a Thunderbolt drive to reconnect, Cancel skips only that "
      "wait and Setup continues." },
    { INST_HELP_STEPS, "Steps left for you",
      "Devices Setup could not install by itself, each with the steps to "
      "follow after the restart.\n\n"
      "Save as text file...: saves the steps to read after the "
      "restart.\n\n"
      "Open Device Manager: where the steps are done." },
    { INST_HELP_FINISH, "Finish",
      "What was done, what is left, and where the log file is.\n\n"
      "Restart Windows now: Finish restarts Windows, and what was "
      "installed starts.\n\n"
      "Restart Windows later: to save your work or follow the steps first. "
      "Restart before using the devices." },
    { INST_HELP_FINISH_SHUT, "Finish",
      "What was done, what is left, and where the log file is.\n\n"
      "Shut down now, then start Windows again: Finish shuts Windows down; "
      "start it again yourself. Stor98's update, or the storage drivers' "
      "copy, needs a fresh start rather than a restart.\n\n"
      "Shut down later: to save your work first." },
    { INST_HELP_FINISH_NONE, "Finish",
      "No changes were made, so no restart is needed. The last line above "
      "says why. Finish closes Setup." },
    { INST_HELP_UFINISH, "Finish",
      "What was removed, what is left, and where the log file is.\n\n"
      "Restart Windows now: Finish restarts Windows, and what was removed "
      "is gone.\n\n"
      "Restart Windows later: to save your work or follow the steps left "
      "first." },
    { INST_HELP_PROGRESS_9X, "Progress",
      "The steps as they run. Windows may open its own windows, such as the "
      "Add New Hardware Wizard: answer them (Next, then Finish). Setup "
      "waits for them.\n\n"
      "Cancel stops at the next step; what was done is kept. While Setup "
      "waits for a Thunderbolt drive to reconnect, Cancel skips only that "
      "wait and Setup continues." }
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
