// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2025, Dominik Behr (nvme2k)
// Windows 9x/NT4 port and changes (nvme9x/nvment): Copyright (c) SweetLow
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//
// SCSI Miniport Driver for Windows 2000
//
// Entry points. The request engine (engine.c) and the adapter lifecycle
// (lifecycle.c) do the work; this file keeps discovery, configuration and
// the SCSI dispatch.
//

#include "nvme2k.h"
#include "utils.h"
#include "pciscan.h"

//
// The build's flavour, found in the file by a byte scan (xhci98's
// scripts\check-stor98-build.ps1 in the gated build, and the packager):
// exactly one of these strings is in each image. DriverEntry reads it so
// the linker keeps it.
//
#if defined(STOR98_DBGPORT)
static const char Stor98Flavour[] = "STOR98_FLAVOUR_DBGPORT";
#elif DBG
static const char Stor98Flavour[] = "STOR98_FLAVOUR_CHECKED";
#else
static const char Stor98Flavour[] = "STOR98_FLAVOUR_FREE";
#endif

//
// DriverEntry - Main entry point
//
ULONG DriverEntry(IN PVOID DriverObject, IN PVOID Argument2)
{
    HW_INITIALIZATION_DATA hwInitData;
    ULONG status;
#ifdef NVME2K_NT4
    ULONG HwContext[2];
#endif

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: DriverEntry called\n");
#endif
    // Never true; it keeps the flavour string in the image.
    if (((volatile const char *)Stor98Flavour)[0] == 0) {
        return 0xC0000001UL;
    }
    DBGINIT();
    DBGMARK(STG_DRIVER_ENTRY, 0);
    PlatCmLinkDriverEntry(DriverObject, Argument2);

    // Zero out the initialization data structure
    RtlZeroMemory(&hwInitData, sizeof(HW_INITIALIZATION_DATA));

    // Set size of structure
    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

    // Set entry points
    hwInitData.HwInitialize = HwInitialize;
    hwInitData.HwStartIo = HwStartIo;
    hwInitData.HwInterrupt = HwInterrupt;
    hwInitData.HwFindAdapter = HwFindAdapter;
    hwInitData.HwResetBus = HwResetBus;

#ifndef NVME2K_W9X

#ifndef NVME2K_NT4
    hwInitData.HwAdapterControl = HwAdapterControl;
#endif

#else
    hwInitData.HwAdapterState = HwAdapterState;
#endif

    // Set driver-specific parameters
    hwInitData.AdapterInterfaceType = PCIBus;
    hwInitData.DeviceExtensionSize = sizeof(HW_DEVICE_EXTENSION);
    hwInitData.SpecificLuExtensionSize = 0;
    hwInitData.SrbExtensionSize = sizeof(NVME_SRB_EXTENSION);
    hwInitData.NumberOfAccessRanges = 1;
    hwInitData.MapBuffers = TRUE;               // CPU copies (bounce, log pages)
    hwInitData.NeedPhysicalAddresses = TRUE;
    hwInitData.TaggedQueuing = TRUE;
    hwInitData.AutoRequestSense = TRUE;
    hwInitData.MultipleRequestPerLu = TRUE;

    // Vendor/Device IDs (for PCI devices)
    hwInitData.VendorIdLength = 0;
    hwInitData.VendorId = NULL;
    hwInitData.DeviceIdLength = 0;
    hwInitData.DeviceId = NULL;

#ifdef NVME2K_NT4
    HwContext[0] = 0;
    HwContext[1] = 0;
#endif

    // Call port driver
    status = ScsiPortInitialize(DriverObject, Argument2,
                                &hwInitData,
#ifdef NVME2K_NT4
                                HwContext
#else
                                NULL
#endif
                            );

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: DriverEntry exiting with status 0x%08X\n", status);
#endif
    return status;
}

//
// DMA and transfer negotiation (PLAN 0.8), before ScsiPortGetUncachedExtension
// fixes the DMA adapter from these values. The driver's limit is the
// StorMaxTransferKB / stormaxtransferkb= setting (xfertun.h, parsed by
// EngParseTunables before this), at most STOR98_MAX_TRANSFER. A lower limit
// the port supplied (registry MaximumSGList and the like) is kept;
// SP_UNINITIALIZED_VALUE (and 0) mean the port sets no limit.
//
static VOID NegotiateDma(IN PHW_DEVICE_EXTENSION DevExt, IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo)
{
    ULONG maxTransfer = DevExt->Tun.MaxXferKb * 1024;
    ULONG breaks;

    if (maxTransfer == 0 || maxTransfer > STOR98_MAX_TRANSFER) {
        maxTransfer = STOR98_MAX_TRANSFER;
    }
    // A buffer of N pages at a non-zero offset spans N + 1 pages.
    breaks = maxTransfer / PAGE_SIZE + 1;

    if (ConfigInfo->MaximumTransferLength != SP_UNINITIALIZED_VALUE &&
        ConfigInfo->MaximumTransferLength != 0 &&
        ConfigInfo->MaximumTransferLength < maxTransfer) {
        maxTransfer = ConfigInfo->MaximumTransferLength;
    }
    ConfigInfo->MaximumTransferLength = maxTransfer;
    if (ConfigInfo->NumberOfPhysicalBreaks != SP_UNINITIALIZED_VALUE &&
        ConfigInfo->NumberOfPhysicalBreaks != 0 &&
        ConfigInfo->NumberOfPhysicalBreaks < breaks) {
        breaks = ConfigInfo->NumberOfPhysicalBreaks;
    }
    ConfigInfo->NumberOfPhysicalBreaks = breaks;
    DevExt->PfMaxTransfer = maxTransfer;
    DevExt->PfBreaks = breaks;
    if (ConfigInfo->AlignmentMask < 0x3) {
        ConfigInfo->AlignmentMask = 0x3;        // PRP entries are DWORD aligned
    }
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Master = TRUE;
    ConfigInfo->NeedPhysicalAddresses = TRUE;
    ConfigInfo->Dma32BitAddresses = TRUE;
#ifndef NVME2K_NT4
    // NVMe takes 64-bit PRPs; the port decides whether to grant it.
    ConfigInfo->Dma64BitAddresses = SCSI_DMA64_MINIPORT_SUPPORTED;
#endif
    DBGSTAGE(STG_DMA_NEGOTIATE, ((maxTransfer / 1024) & 0xFFFF) | ((breaks & 0xFFFF) << 16));
}

//
// BAR0, the controller registers (PLAN 3.3, 4.1). NVMe BAR0 is a memory
// BAR, 64-bit when its type bits say so (BAR1 is then the high dword).
// The port's access range is used as given, without sizing, when it
// describes this function: on a PnP start (HwContext NULL, NT 2000 and
// later) and in a dump instance. Otherwise (the NT4-ABI scan, which 9x
// uses, or no range from the port) the BAR is sized here with memory and
// I/O decode off around the write of ones, then restored and read back. A
// dump instance never sizes (PLAN 4.1: there the read-back returns the BAR
// itself). On 9x the registers must lie below 4 GiB (_MapPhysToLinear
// takes a 32-bit address); NT maps the full 64-bit address. Returns
// BAR_OK, or the reason the registers cannot be used; *Flags for the log
// (debug-port stage STG_FIND_BAR).
//
#define BAR_OK              0
#define BAR_UNREADABLE      1   // a config read came back short
#define BAR_NOT_MEMORY      2   // an I/O BAR, or an I/O access range
#define BAR_DUMP_NO_RANGE   3   // a dump instance without an access range
#define BAR_BAD_SIZE        4   // unassigned, or no usable power-of-two size
#define BAR_NOT_RESTORED    5   // the BAR read back differs after sizing
#define BAR_ABOVE_4G        6   // 9x: the registers lie above 4 GiB
#define BAR_CMD_UNREADABLE  7   // the command register could not be read
#define BAR_CMD_NOT_ENABLED 8   // memory decode or bus mastering did not read back on
#define BAR_DECODE_ON       9   // decode did not read back off, so the BAR was not sized

#define BAR_F_PORT          0x01    // the port's access range is used
#define BAR_F_SIZED         0x02    // sized here
#define BAR_F_64            0x04    // a 64-bit BAR
#define BAR_F_HIGH          0x08    // the registers lie at or above 4 GiB
#define BAR_F_SHORT_WRITE   0x10    // a config write reported a short length
#define BAR_F_MISMATCH      0x20    // the port's range does not start at the BAR's address

static ULONG HwLocateRegisters(IN PHW_DEVICE_EXTENSION DevExt, IN OUT PACCESS_RANGE Range, IN BOOLEAN PortOwnRange,
                               IN BOOLEAN Dump, IN USHORT CmdReg, OUT PULONG Flags)
{
    ULONG lo, hi = 0, szLo = 0, szHi = 0xFFFFFFFF, backLo = 0, backHi = 0;
    ULONGLONG addr, mask, size;
    BOOLEAN is64, readOk, restored;
    USHORT cmdOff, cmdBack;

    *Flags = 0;
    if (!ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_0, &lo)) {
        return BAR_UNREADABLE;
    }
    if (lo & 0x1) {
        return BAR_NOT_MEMORY;
    }
    is64 = (BOOLEAN)((lo & 0x6) == 0x4);
    if (is64) {
        *Flags |= BAR_F_64;
        if (!ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_1, &hi)) {
            return BAR_UNREADABLE;
        }
    }
    addr = ((ULONGLONG)hi << 32) | (lo & 0xFFFFFFF0);

    if (Range->RangeLength != 0 && (PortOwnRange || Dump)) {
        // The port's assignment for this function (PnP resources, or the
        // dump port's range).
        if (!Range->RangeInMemory) {
            return BAR_NOT_MEMORY;
        }
        *Flags |= BAR_F_PORT;
        if (Range->RangeStart.LowPart != (ULONG)addr || (ULONG)Range->RangeStart.HighPart != hi) {
            *Flags |= BAR_F_MISMATCH;
        }
    } else if (Dump) {
        return BAR_DUMP_NO_RANGE;
    } else {
        // Size with decode off: while the BAR holds all ones it must not
        // claim addresses (PLAN 3.3). The BAR is probed only once decode
        // reads back off.
        cmdOff = (USHORT)(CmdReg & ~(PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_IO_SPACE));
        if (!WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET, cmdOff)) {
            *Flags |= BAR_F_SHORT_WRITE;
        }
        if (!ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET, &cmdBack) ||
            (cmdBack & (PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_IO_SPACE)) != 0) {
            // Put back what the function had; the BAR was not touched.
            if (!WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET, CmdReg)) {
                *Flags |= BAR_F_SHORT_WRITE;
            }
            return BAR_DECODE_ON;
        }
        if (!WritePciConfigDword(DevExt, PCI_BASE_ADDRESS_0, 0xFFFFFFFF)) {
            *Flags |= BAR_F_SHORT_WRITE;
        }
        readOk = ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_0, &szLo);
        if (is64) {
            if (!WritePciConfigDword(DevExt, PCI_BASE_ADDRESS_1, 0xFFFFFFFF)) {
                *Flags |= BAR_F_SHORT_WRITE;
            }
            if (!ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_1, &szHi)) {
                readOk = FALSE;
            }
            if (!WritePciConfigDword(DevExt, PCI_BASE_ADDRESS_1, hi)) {
                *Flags |= BAR_F_SHORT_WRITE;
            }
        }
        if (!WritePciConfigDword(DevExt, PCI_BASE_ADDRESS_0, lo)) {
            *Flags |= BAR_F_SHORT_WRITE;
        }
        restored = ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_0, &backLo);
        if (is64 && !ReadPciConfigDword(DevExt, PCI_BASE_ADDRESS_1, &backHi)) {
            restored = FALSE;
        }
        if (!restored || backLo != lo || (is64 && backHi != hi)) {
            // Decode stays off: the BAR may not hold the assigned address,
            // and the function must not decode a wrong one.
            return BAR_NOT_RESTORED;
        }
        if (!WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET, CmdReg)) {
            *Flags |= BAR_F_SHORT_WRITE;
        }
        if (!readOk) {
            return BAR_UNREADABLE;
        }
        // A 32-bit BAR has an all-ones high part.
        mask = ((ULONGLONG)szHi << 32) | (szLo & 0xFFFFFFF0);
        size = ~mask + 1;
        if (size < PAGE_SIZE || size > 0xFFFFFFFF || (size & (size - 1)) != 0 ||
            addr == 0 || (addr & (size - 1)) != 0) {
            return BAR_BAD_SIZE;
        }
        Range->RangeStart.LowPart = (ULONG)addr;
        Range->RangeStart.HighPart = (LONG)hi;
        Range->RangeLength = (ULONG)size;
        Range->RangeInMemory = TRUE;
        *Flags |= BAR_F_SIZED;
    }
    if (Range->RangeStart.HighPart != 0) {
        *Flags |= BAR_F_HIGH;
#ifdef NVME2K_W9X
        return BAR_ABOVE_4G;
#endif
    }
    return BAR_OK;
}

ULONG HwFoundAdapter(
    IN PHW_DEVICE_EXTENSION DevExt,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN PUCHAR pciBuffer,
    IN ULONG pciLength,
    IN PCHAR ArgumentString,
    IN BOOLEAN PortOwnRange)
{
    PACCESS_RANGE accessRange;
    USHORT cmdReg;
    ULONG why, barFlags = 0;

#ifdef NVME2K_DBG
    ScsiDebugPrint(0, "nvme2k: HwFoundAdapter - found NVMe device VID=%04X DID=%04X at bus %d slot %d\n",
                   DevExt->VendorId, DevExt->DeviceId, DevExt->BusNumber, DevExt->SlotNumber);
#endif

    // Subsystem IDs from the length-checked header HwFindAdapter read
    if (pciLength >= PCI_SUBSYSTEM_ID_OFFSET + 2) {
        DevExt->SubsystemVendorId = (USHORT)(pciBuffer[PCI_SUBSYSTEM_VENDOR_ID_OFFSET] |
                                             ((USHORT)pciBuffer[PCI_SUBSYSTEM_VENDOR_ID_OFFSET + 1] << 8));
        DevExt->SubsystemId = (USHORT)(pciBuffer[PCI_SUBSYSTEM_ID_OFFSET] |
                                       ((USHORT)pciBuffer[PCI_SUBSYSTEM_ID_OFFSET + 1] << 8));
    }

    // PCI config space is read and written only here (PLAN 3.4); a field
    // that cannot be read fails the find before CC.EN is ever written.
    accessRange = &((*(ConfigInfo->AccessRanges))[0]);
    if (!ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET, &cmdReg)) {
        why = BAR_CMD_UNREADABLE;
    } else {
        // BAR0 before memory decode is enabled below.
        why = HwLocateRegisters(DevExt, accessRange, PortOwnRange, PlatDumpMode(ArgumentString), cmdReg,
                                &barFlags);
    }
    DBGSTAGE(STG_FIND_BAR, barFlags | (why << 8) |
             ((accessRange->RangeLength >> 12) > 0xFFFF ? 0xFFFF0000UL : (accessRange->RangeLength >> 12) << 16));
    if (why != BAR_OK) {
        return SP_RETURN_ERROR;
    }
    DevExt->ControllerRegistersLength = accessRange->RangeLength;

    // Enable Bus Master and Memory Space with INTx disabled; INTx is enabled
    // below once the controller's interrupts are masked. Read back: decode
    // and bus mastering must be on before the registers are touched.
    {
        USHORT back;

#ifdef CHANGE_PCI_COMMAND_REGISTER
        cmdReg |= (PCI_ENABLE_BUS_MASTER | PCI_ENABLE_MEMORY_SPACE | PCI_INTERRUPT_DISABLE);
#else
        cmdReg = PCI_ENABLE_BUS_MASTER | PCI_ENABLE_MEMORY_SPACE | PCI_INTERRUPT_DISABLE;
#endif
        if (!WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET, cmdReg)) {
            barFlags |= BAR_F_SHORT_WRITE;
        }
        if (!ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET, &back) ||
            (back & (PCI_ENABLE_BUS_MASTER | PCI_ENABLE_MEMORY_SPACE)) !=
                (PCI_ENABLE_BUS_MASTER | PCI_ENABLE_MEMORY_SPACE)) {
            DBGSTAGE(STG_FIND_BAR, barFlags | ((ULONG)BAR_CMD_NOT_ENABLED << 8) | ((ULONG)back << 16));
            return SP_RETURN_ERROR;
        }
    }

    // Interrupt configuration and the polling decision (PLAN "Polling
    // mode"). Interrupt Pin and Line come from config space here, the
    // permitted callback (PLAN 3.4); MSI and MSI-X presence from the
    // capability list in the header HwFindAdapter read (report only: no MSI
    // is used). The adapter polls exactly when the interrupt reported to the
    // port would be none, or when PollMode forces it.
    {
        UCHAR interruptLine, interruptPin;
        BOOLEAN portGives;

        DevExt->PortIrqLevel = ConfigInfo->BusInterruptLevel;
        DevExt->PortIrqVector = ConfigInfo->BusInterruptVector;
        // Pin and Line from the length-checked header HwFindAdapter read
        // (ScsiPortGetBusData's returned length); a header too short to
        // hold them decides "configuration unreadable" (polling).
        PciReadIntInfo(pciBuffer, pciLength, &DevExt->IntInfo);
        interruptLine = DevExt->IntInfo.Line;
        interruptPin = DevExt->IntInfo.Pin;
#ifdef NVME2K_NT4
        // 9x (and NT4): ConfigInfo's interrupt is not taken as the OS's
        // resource assignment; the Interrupt Line register decides, as in
        // the base.
        portGives = FALSE;
#else
        // NT 2000 and later: the port fills the interrupt from the PnP
        // resources before calling HwFindAdapter; none means none.
        portGives = TRUE;
#endif
        DevExt->PollWhy = (UCHAR)PollDecide(DevExt->Tun.Poll.Mode,
                                            (BOOLEAN)((DevExt->IntInfo.Flags & S98_PCI_HAS_PIN_LINE) != 0),
                                            interruptPin, interruptLine, portGives,
                                            DevExt->PortIrqLevel, DevExt->PortIrqVector);
        DevExt->PollAlways = (BOOLEAN)(DevExt->PollWhy != POLL_WHY_INTERRUPT);
        // A new start, an NT resume included (the device extension is
        // reused): the missed-interrupt watchdog starts afresh (PLAN
        // "Missed-interrupt watchdog"); controller resets within the start
        // keep it.
        RtlZeroMemory(&DevExt->IntWatch, sizeof(DevExt->IntWatch));
        DevExt->IntClaims = 0;
        DevExt->IntWatchFound = FALSE;
        DBGSTAGE(STG_POLL_PORTIRQ, (DevExt->PortIrqLevel & 0xFFFF) | (DevExt->PortIrqVector << 16));
        DBGMARK(STG_POLL_DECIDE, interruptPin | ((ULONG)interruptLine << 8) | ((ULONG)DevExt->PollWhy << 16) |
                ((ULONG)DevExt->IntInfo.Flags << 24));

        if (!DevExt->PollAlways) {
#ifdef NVME2K_NT4
            // CRITICAL FOR NT4: When manually setting SystemIoBusNumber/SlotNumber,
            // SCSI port doesn't automatically query interrupt configuration from HAL.
            // (9x and NT4: the Interrupt Line, as in the base.)
            ConfigInfo->BusInterruptLevel = interruptLine;
            ConfigInfo->BusInterruptVector = interruptLine;
#else
            // NT 2000 and later: the interrupt the port filled from the PnP
            // resources is kept (PollDecide polls when it gave none). The
            // Interrupt Line decides only polling and the report: on an
            // APIC system it need not be the IRQ the OS assigned.
#endif
            ConfigInfo->InterruptMode = LevelSensitive;
        } else {
            // No interrupt for the port to connect: the engine polls.
            ConfigInfo->BusInterruptLevel = 0;
            ConfigInfo->BusInterruptVector = 0;
        }
    }

    ConfigInfo->NumberOfAccessRanges = 1;
    ConfigInfo->NumberOfBuses = 1;
    // CachesData is a constant (decided 2026-10-15; PLAN 0.11): with TRUE the
    // port passes SRB_FUNCTION_SHUTDOWN and SRB_FUNCTION_FLUSH. Never taken
    // from DriverParameter (NULL at resume, Q-NT).
    ConfigInfo->CachesData = TRUE;
    ConfigInfo->AdapterScansDown = FALSE;
    ConfigInfo->MaximumNumberOfTargets = 2;
    ConfigInfo->TaggedQueuing = TRUE;
    ConfigInfo->MultipleRequestPerLu = TRUE;
    ConfigInfo->AutoRequestSense = TRUE;

    // BAR0 (controller registers): located above (HwLocateRegisters)
    if (!ScsiPortValidateRange(
            (PVOID)DevExt,
            ConfigInfo->AdapterInterfaceType,
            ConfigInfo->SystemIoBusNumber,
            accessRange->RangeStart,
            accessRange->RangeLength,
            (BOOLEAN)!accessRange->RangeInMemory)) {
        return SP_RETURN_ERROR;
    }

#ifdef NVME2K_W9X
// SweetLow: Fix of Q169584, BUG: Win95: ScsiPortGetDeviceBase Does Not Return Linear Address
    if (accessRange->RangeInMemory)
    {
      DevExt->ControllerRegisters = _MapPhysToLinear(
        accessRange->RangeStart.LowPart,
        accessRange->RangeLength,
        MPL_NonCached
      );

      if (((ULONG)DevExt->ControllerRegisters) == 0xFFFFFFFF)
        DevExt->ControllerRegisters = NULL;
    } else
#endif //NVME2K_W9X

    DevExt->ControllerRegisters = ScsiPortGetDeviceBase(
        (PVOID)DevExt,
        ConfigInfo->AdapterInterfaceType,
        ConfigInfo->SystemIoBusNumber,
        accessRange->RangeStart,
        accessRange->RangeLength,
        (BOOLEAN)!accessRange->RangeInMemory);

    if (DevExt->ControllerRegisters == NULL) {
        return SP_RETURN_ERROR;
    }

    // Mask the controller's interrupts, then enable INTx at the PCI level
    // (config writes only from HwFindAdapter, PLAN 3.4). Bring-up polls from
    // the timer until it unmasks vector 0. In the polling mode INTx stays
    // disabled and every vector masked for the life of this start.
    DevExt->IntxEnabled = FALSE;
    if (accessRange->RangeInMemory &&
        NvmeReadReg32(DevExt, NVME_REG_CSTS) != 0xFFFFFFFF) {
        NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
        if (!DevExt->PollAlways) {
            USHORT back;

            if (!ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET, &cmdReg)) {
                return SP_RETURN_ERROR;
            }
            cmdReg &= ~PCI_INTERRUPT_DISABLE;
            // Read back, as the enables above are: an INTx that did not
            // take would leave the engine waiting on interrupts that never
            // come until a bus reset. It polls instead, and the port is
            // given no interrupt to connect.
            if (WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET, cmdReg) &&
                ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET, &back) &&
                (back & PCI_INTERRUPT_DISABLE) == 0) {
                DevExt->IntxEnabled = TRUE;
            } else {
                DevExt->PollWhy = POLL_WHY_INTX_OFF;
                DevExt->PollAlways = TRUE;
                ConfigInfo->BusInterruptLevel = 0;
                ConfigInfo->BusInterruptVector = 0;
                DBGMARK(STG_POLL_DECIDE, ((ULONG)DevExt->PollWhy << 16) |
                        ((ULONG)DevExt->IntInfo.Flags << 24));
            }
        }
    }

    // Limits first, then the DMA memory (PLAN 0.8, 0.5).
    NegotiateDma(DevExt, ConfigInfo);
    if (!PlatDmaAcquire(DevExt, ConfigInfo)) {
        return SP_RETURN_ERROR;
    }

    // The user-readable interrupt report (PLAN "Polling mode").
    PlatIrqReport(DevExt, ArgumentString);

    return SP_RETURN_FOUND;
}

//
// Reads one function's configuration header into PciBuffer (zeroed first;
// only the *BytesRead bytes returned are valid) and records its ids in the
// device extension. TRUE when an NVMe function is there (PLAN 3.4: the
// vendor id alone is probed first, then the header; a return too short to
// hold the class code is "nothing here").
//
static BOOLEAN HwReadNvmeFunction(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG BusNumber, IN ULONG SlotNumber,
                                  OUT PUCHAR PciBuffer, OUT PULONG BytesRead)
{
    UCHAR baseClass, subClass, progIf;
    ULONG bytesRead;

    // The vendor id alone first: on 9x a scan may walk every bus a
    // hot-plug port may have been given, and almost every slot it visits
    // is empty.
    *BytesRead = 0;
    RtlZeroMemory(PciBuffer, 4);
    bytesRead = ScsiPortGetBusData(
        (PVOID)DevExt,
        PCIConfiguration,
        BusNumber,
        SlotNumber,
        PciBuffer,
        4);
    if (PciScanAbsent(bytesRead, *(USHORT*)&PciBuffer[PCI_VENDOR_ID_OFFSET])) {
        return FALSE;
    }

    // Read PCI configuration space for THIS slot only, into a zeroed buffer
    // (only the bytesRead returned are used).
    RtlZeroMemory(PciBuffer, 256);
    bytesRead = ScsiPortGetBusData(
        (PVOID)DevExt,
        PCIConfiguration,
        BusNumber,
        SlotNumber,
        PciBuffer,
        256);
    *BytesRead = bytesRead;

    // 0 (no bus), 2 (no function) or too short to hold the class code:
    // nothing that can match here (PLAN 3.4).
    if (bytesRead < PCI_CLASS_CODE_OFFSET + 3) {
        return FALSE;
    }

    DevExt->VendorId = *(USHORT*)&PciBuffer[PCI_VENDOR_ID_OFFSET];
    DevExt->DeviceId = *(USHORT*)&PciBuffer[PCI_DEVICE_ID_OFFSET];

    if (DevExt->VendorId == 0xFFFF || DevExt->VendorId == 0x0000) {
        return FALSE;
    }

    DevExt->RevisionId = PciBuffer[PCI_REVISION_ID_OFFSET];
    progIf = PciBuffer[PCI_CLASS_CODE_OFFSET];
    subClass = PciBuffer[PCI_CLASS_CODE_OFFSET + 1];
    baseClass = PciBuffer[PCI_CLASS_CODE_OFFSET + 2];

    return IsNvmeDevice(baseClass, subClass, progIf);
}

#ifdef NVME2K_W9X
//
// 9x (PLAN 3.1): TRUE when the function just read by HwReadNvmeFunction is
// the one the port is starting, one of its memory BARs at the start of the
// port's range (pciscan.h, PciScanIsPortFunction).
//
static BOOLEAN HwIsPortFunction(IN PUCHAR PciBuffer, IN ULONG BytesRead, IN ULONG RangeLo, IN ULONG RangeHi)
{
    ULONG bars[PCISCAN_BARS];
    ULONG n = 0;

    while (n < PCISCAN_BARS && BytesRead >= PCI_BASE_ADDRESS_0 + 4 * (n + 1)) {
        bars[n] = *(ULONG*)&PciBuffer[PCI_BASE_ADDRESS_0 + 4 * n];
        n++;
    }
    return (BOOLEAN)PciScanIsPortFunction(1, bars, n, RangeLo, RangeHi);
}

//
// 9x (PLAN 3.1, "A controller already set up"): the found function's
// command register (from the header HwReadNvmeFunction read) and CC and
// CSTS through its BAR0, before HwLocateRegisters, which turns decode off
// to size the BAR. With decode on nothing is written; with decode off,
// decode is turned on for the two reads where BAR0 is shown to be this
// devnode's, and put back (pciscan.h: PciScanFirmwareReach, then
// PciScanReadFirmware over the config helpers, which address DevExt's bus
// and slot: the caller sets them to the found function first). *Reach:
// PCISCAN_REACH_*; *ReadHow: PCISCAN_READ_* (READ_NONE when not reached);
// *TurnedOn: decode turned on for the read. The page is mapped uncached as
// HwFoundAdapter maps the registers (_MapPhysToLinear, Q169584).
//
typedef struct _HW_FW_CTX {
    PHW_DEVICE_EXTENSION DevExt;
    PUCHAR Regs;
} HW_FW_CTX;

static int HwFwReadCmd(void *Ctx, unsigned short *Value)
{
    return ReadPciConfigWord(((HW_FW_CTX *)Ctx)->DevExt, PCI_COMMAND_OFFSET, (PUSHORT)Value);
}

static int HwFwWriteCmd(void *Ctx, unsigned short Value)
{
    return WritePciConfigWord(((HW_FW_CTX *)Ctx)->DevExt, PCI_COMMAND_OFFSET, (USHORT)Value);
}

static unsigned long HwFwReadReg(void *Ctx, unsigned long Offset)
{
    return ScsiPortReadRegisterUlong((PULONG)(((HW_FW_CTX *)Ctx)->Regs + Offset));
}

static VOID HwReadFirmwareState(IN PHW_DEVICE_EXTENSION DevExt, IN PUCHAR PciBuffer, IN ULONG BytesRead,
                                IN ULONG RangeLo, IN ULONG RangeHi, IN ULONG RangeLen, OUT PUSHORT Cmd,
                                OUT PULONG Reach, OUT PULONG ReadHow, OUT PBOOLEAN TurnedOn, OUT PULONG Cc,
                                OUT PULONG Csts)
{
    HW_FW_CTX ctx;
    PCISCAN_FW_IO io;
    ULONG bar0, bar1;
    int on = 0;

    *Reach = PCISCAN_REACH_SHORT;
    *ReadHow = PCISCAN_READ_NONE;
    *TurnedOn = FALSE;
    *Cc = 0;
    *Csts = 0;
    if (BytesRead < PCI_BASE_ADDRESS_1 + 4) {
        // Decode unknown: the header did not hold the command register's
        // neighbours; refused as unreadable, nothing written.
        *Cmd = PCI_ENABLE_MEMORY_SPACE;
        return;
    }
    *Cmd = *(USHORT*)&PciBuffer[PCI_COMMAND_OFFSET];
    bar0 = *(ULONG*)&PciBuffer[PCI_BASE_ADDRESS_0];
    bar1 = *(ULONG*)&PciBuffer[PCI_BASE_ADDRESS_1];
    *Reach = (ULONG)PciScanFirmwareReach(BytesRead, *Cmd, bar0, bar1, RangeLo, RangeHi, RangeLen);
    if (*Reach != PCISCAN_REACH_OK) {
        return;
    }
    ctx.DevExt = DevExt;
    ctx.Regs = (PUCHAR)_MapPhysToLinear(bar0 & 0xFFFFFFF0, PAGE_SIZE, MPL_NonCached);
    if (ctx.Regs == NULL || (ULONG)ctx.Regs == 0xFFFFFFFF) {
        *Reach = PCISCAN_REACH_NO_MAP;
        return;
    }
    io.Ctx = &ctx;
    io.ReadCmd = HwFwReadCmd;
    io.WriteCmd = HwFwWriteCmd;
    io.ReadReg = HwFwReadReg;
    *ReadHow = (ULONG)PciScanReadFirmware(&io, *Cmd, Cc, Csts, &on);
    *TurnedOn = (BOOLEAN)(on != 0);
}
#endif

//
// HwFindAdapter - Locate and configure the adapter
//
ULONG HwFindAdapter(
    IN PVOID DeviceExtension,
    IN PVOID HwContext,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    UCHAR pciBuffer[256];
    ULONG slotNumber;
    ULONG busNumber;
    ULONG bytesRead;
    ULONG rc;
#ifdef NVME2K_W9X
    ULONG rangeLo = 0, rangeHi = 0;
    BOOLEAN scanned = FALSE;
    BOOLEAN nvme, fwOn;
    USHORT fwCmd;
    ULONG fwCc, fwCsts, fwReach, fwHow;
    PLAT_FIND_INFO fi;
#endif

    UNREFERENCED_PARAMETER(BusInformation);

    DBGINIT();
    // A resume calls HwFindAdapter again on the same, not zeroed, device
    // extension (G7, Q-NT), with no ArgumentString.
    DevExt->Resumed = (BOOLEAN)(DevExt->Magic == STOR98_DEVEXT_MAGIC);
    DevExt->Magic = STOR98_DEVEXT_MAGIC;
    // The counter answer's totals live as long as this epoch (MD-8): a
    // resume keeps them, every other start begins a new one.
    if (!DevExt->Resumed) {
        ULONG lo, hi;

        RtlZeroMemory(&DevExt->Pf, sizeof(DevExt->Pf));
        DevExt->PfDcbSeen = 0;
        ClockReadTsc(&lo, &hi);
        DevExt->PfEpoch = (lo ^ hi) | 1;
    }
    // No sense kept before a stop or power transition survives it.
    ScsiSenseReset(DevExt);
    EngParseTunables(DevExt, ArgumentString);
    PlatWriteThroughSetting(DevExt, ArgumentString);
    DevExt->Clock.CalPct = DevExt->Tun.CalPct;
    ClockCalibrate(&DevExt->Clock);
    DBGMARK(STG_CLOCK_CAL, DevExt->Clock.UnitsPerMs ? DevExt->Clock.TscPerMs : 0);

    // One adapter per call, on every target: the next call, if any, is the
    // port's for another device.
    *Again = FALSE;

#ifdef NVME2K_W9X
    // 9x (PLAN 3.1): only the function whose devnode the port is starting,
    // never every NVMe function in the machine. The NT4-style HwContext
    // scan from bus 0 took the first NVMe function it met, so a tunnelled
    // drive's load brought up (and reset) an internal drive the BIOS was
    // serving the system disk from. The port's first access range is the
    // devnode's memory resource and names the function by BAR0's address;
    // ConfigInfo's bus and slot (G9) are where it is looked for first, and
    // a walk of every bus finds it when they do not hold it. HwContext is
    // not used.
    UNREFERENCED_PARAMETER(HwContext);
    RtlZeroMemory(&fi, sizeof(fi));
    // RangeInMemory is not relied on (not read on 9x): an I/O range's start
    // cannot equal a memory BAR's address, so it names no function.
    if (ConfigInfo->NumberOfAccessRanges != 0 && ConfigInfo->AccessRanges != NULL) {
        rangeLo = (*(ConfigInfo->AccessRanges))[0].RangeStart.LowPart;
        rangeHi = (ULONG)(*(ConfigInfo->AccessRanges))[0].RangeStart.HighPart;
        fi.RangeLen = (*(ConfigInfo->AccessRanges))[0].RangeLength;
    }
#ifdef STOR98_TEST_FINDNONE
    // Test only (46.20): the port's range moved 16 KiB past BAR0, as the
    // board's refused start read it, so no function matches and the find
    // returns NOT_FOUND (the refused plug-in path).
    if (rangeLo != 0) {
        rangeLo += 0x4000;
    }
#endif
    busNumber = ConfigInfo->SystemIoBusNumber;
    slotNumber = ConfigInfo->SlotNumber;
    fi.PortBus = busNumber;
    fi.PortSlot = slotNumber;
    fi.RangeLo = rangeLo;
    fi.RangeHi = rangeHi;
    fi.Result = SP_RETURN_NOT_FOUND;
    DBGSTAGE(STG_FIND_PORT, (busNumber << 16) | (slotNumber & 0xFFFF));
    DBGSTAGE(STG_FIND_PORT_RANGE, rangeLo);
    if (busNumber > PCISCAN_LAST_BUS || slotNumber >= PCISCAN_SLOTS) {
        busNumber = 0;
        slotNumber = 0;
        scanned = TRUE;
    }
    for (;;) {
        nvme = HwReadNvmeFunction(DevExt, busNumber, slotNumber, pciBuffer, &bytesRead);
        if (!scanned) {
            // What the port's own slot holds, for the find report.
            fi.SlotBytes = bytesRead;
            if (bytesRead >= PCI_CLASS_CODE_OFFSET + 3) {
                fi.SlotClass = (ULONG)pciBuffer[PCI_CLASS_CODE_OFFSET] |
                               ((ULONG)pciBuffer[PCI_CLASS_CODE_OFFSET + 1] << 8) |
                               ((ULONG)pciBuffer[PCI_CLASS_CODE_OFFSET + 2] << 16);
            }
            // Every BAR dword the match compares (a 64-bit BAR's high
            // half included), as read.
            {
                ULONG b;

                for (b = 0; b < 6; b++) {
                    if (bytesRead >= PCI_BASE_ADDRESS_0 + 4 * b + 4) {
                        fi.SlotBar[b] = *(ULONG*)&pciBuffer[PCI_BASE_ADDRESS_0 + 4 * b];
                    }
                }
            }
            if (rangeLo == 0 && rangeHi == 0) {
                // No memory range: nothing names a function, and none is
                // taken.
                break;
            }
        }
        if (nvme && HwIsPortFunction(pciBuffer, bytesRead, rangeLo, rangeHi)) {
            fi.Found = 1;
            break;
        }
        if (!scanned) {
            scanned = TRUE;
            busNumber = 0;
            slotNumber = 0;
            continue;
        }
        if (!PciScanNext(&busNumber, &slotNumber)) {
            break;
        }
    }
#ifdef STOR98_TEST_FINDNONE
    // Test only: never taken, whatever the shifted range matched.
    fi.Found = 0;
#endif
    if (!fi.Found) {
        PlatFindReport(&fi);
        DBGSTAGE(STG_FIND_RESULT, SP_RETURN_NOT_FOUND);
        return SP_RETURN_NOT_FOUND;
    }
    fi.FoundBus = busNumber;
    fi.FoundSlot = slotNumber;
    // A controller already set up by another owner (the BIOS serving it
    // through INT 13h) is left to it: nothing has been written to this
    // function yet, and the check writes only its command register, turning
    // decode on for the read and back when it found decode off (PLAN 3.1, "A
    // controller already set up"). The config helpers address DevExt's bus
    // and slot.
    DevExt->BusNumber = busNumber;
    DevExt->SlotNumber = slotNumber;
#ifdef STOR98_TEST_FWDECODEOFF
    // Test only (47.9): memory decode turned off before the check, as a
    // firmware that left its controller enabled with decode off would hand
    // it over; the command word read right after the check (the check must
    // have put back the decode-off word), then the word as found written
    // back, so a drive the BIOS goes on serving keeps working. Stage 0x0B:
    // 1 << 24 the injection took (written and read back with decode off),
    // or 8 << 24 | the word put back when it failed (the check not run,
    // NOT_FOUND; 16 << 24 added when the word did not go back); then the
    // word after the check (15:0) | 2 << 24 when it equals the injected word;
    // then 4 << 24 | the word after the restore, 16 << 24 added and the find
    // NOT_FOUND when it did not read back as found.
    {
        USHORT testCmd = *(USHORT*)&pciBuffer[PCI_COMMAND_OFFSET];
        USHORT testOff = (USHORT)(testCmd & ~PCI_ENABLE_MEMORY_SPACE);
        USHORT testBack = 0;
        BOOLEAN testSet = FALSE, testOk;

        if (bytesRead >= PCI_BASE_ADDRESS_1 + 4 && (testCmd & PCI_ENABLE_MEMORY_SPACE) != 0) {
            if (WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET, testOff) &&
                ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET, &testBack) && testBack == testOff) {
                testSet = TRUE;
                *(USHORT*)&pciBuffer[PCI_COMMAND_OFFSET] = testOff;
                DBGSTAGE(STG_FIND_FWTEST, 0x01000000UL | testBack);
            } else {
                // A short write may still have changed the register: the
                // word as found goes back, and the check is not run (no
                // register read with decode perhaps off); 8 << 24.
                testBack = 0;
                testOk = (BOOLEAN)(WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET, testCmd) &&
                                   ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET, &testBack) && testBack == testCmd);
                DBGSTAGE(STG_FIND_FWTEST, 0x08000000UL | (testOk ? 0 : 0x10000000UL) | testBack);
                PlatFindReport(&fi);
                DBGSTAGE(STG_FIND_RESULT, SP_RETURN_NOT_FOUND);
                return SP_RETURN_NOT_FOUND;
            }
        } else {
            DBGSTAGE(STG_FIND_FWTEST, testCmd);
        }
#endif
    HwReadFirmwareState(DevExt, pciBuffer, bytesRead, rangeLo, rangeHi, fi.RangeLen, &fwCmd, &fwReach, &fwHow,
                        &fwOn, &fwCc, &fwCsts);
#ifdef STOR98_TEST_FWDECODEOFF
        if (testSet) {
            testBack = 0;
            if (ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET, &testBack)) {
                DBGSTAGE(STG_FIND_FWTEST, (testBack == fwCmd ? 0x02000000UL : 0) | testBack);
            } else {
                DBGSTAGE(STG_FIND_FWTEST, 0x00FF0000UL);
            }
            testBack = 0;
            if (!WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET, testCmd) ||
                !ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET, &testBack) || testBack != testCmd) {
                // Not put back: never taken (16 << 24).
                DBGSTAGE(STG_FIND_FWTEST, 0x14000000UL | testBack);
                PlatFindReport(&fi);
                DBGSTAGE(STG_FIND_RESULT, SP_RETURN_NOT_FOUND);
                return SP_RETURN_NOT_FOUND;
            }
            *(USHORT*)&pciBuffer[PCI_COMMAND_OFFSET] = testCmd;
            DBGSTAGE(STG_FIND_FWTEST, 0x04000000UL | testBack);
        }
    }
#endif
    fi.FwChecked = 1;
    fi.FwCmd = fwCmd;
    fi.FwCc = fwCc;
    fi.FwCsts = fwCsts;
    fi.FwReach = fwReach;
    fi.FwTurnedOn = fwOn;
    fi.FwState = (ULONG)PciScanFirmwareState((int)fwHow, fwCc, fwCsts);
    DBGSTAGE(STG_FIND_FIRMWARE, fi.FwState | ((fwCc & 0xFF) << 8) | ((fwCsts & 0xFF) << 16) |
             ((ULONG)(fwHow == PCISCAN_READ_OK) << 24) | ((ULONG)fwOn << 25) |
             ((ULONG)(fwHow == PCISCAN_READ_NOT_RESTORED) << 26) | ((fwReach & 0x7) << 27));
    if (fi.FwState != PCISCAN_FW_TAKE) {
        PlatFindReport(&fi);
        DBGSTAGE(STG_FIND_RESULT, SP_RETURN_NOT_FOUND);
        return SP_RETURN_NOT_FOUND;
    }
#else
    if (HwContext) {
        busNumber = ((PULONG)HwContext)[0];
        slotNumber = ((PULONG)HwContext)[1];
    } else {
        // Win2k, PnP gives us this directly
        busNumber = ConfigInfo->SystemIoBusNumber;
        slotNumber = ConfigInfo->SlotNumber;
    }

    for (;;) {
        if (HwReadNvmeFunction(DevExt, busNumber, slotNumber, pciBuffer, &bytesRead)) {
            break;
        }
        if (HwContext == NULL) {
            // PnP (2000 and later): the port asked about this one function.
            // Another NVMe function elsewhere on the buses is not ours to
            // take while ConfigInfo describes the device PnP started.
            return SP_RETURN_NOT_FOUND;
        }
        // NT4 ABI: continue scanning, to the last bus (pciscan.h).
        if (!PciScanNext(&busNumber, &slotNumber)) {
            return SP_RETURN_NOT_FOUND;
        }
    }
    if (HwContext) {
        ULONG nextBus = busNumber;
        ULONG nextSlot = slotNumber;

        // The next call resumes after this slot; after the last slot of
        // the last bus there is no next call (pciscan.h).
        *Again = (BOOLEAN)PciScanNext(&nextBus, &nextSlot);
        ((PULONG)HwContext)[0] = nextBus;
        ((PULONG)HwContext)[1] = nextSlot;
    }
#endif

    DevExt->BusNumber = busNumber;
    DevExt->SlotNumber = slotNumber;
    DBGSTAGE(STG_FIND_ADAPTER, (busNumber << 16) | slotNumber);
    rc = HwFoundAdapter(DevExt, ConfigInfo, pciBuffer, bytesRead, ArgumentString,
                        (BOOLEAN)(HwContext == NULL));
    if (rc == SP_RETURN_FOUND) {
        // The CM link-state table (G3, MD-6; platform.h): last, once the
        // adapter is found (NT: cmlinknt.c; 9x: pool9x.c).
        PlatCmLinkOpen(DevExt, ArgumentString);
    }
#ifdef NVME2K_W9X
    fi.Result = rc;
    PlatFindReport(&fi);
#endif
    DBGSTAGE(STG_FIND_RESULT, rc);
    return rc;
}

//
// HwInitialize - deferred bring-up (G1); see lifecycle.c
//
BOOLEAN HwInitialize(IN PVOID DeviceExtension)
{
    return EngInitialize((PHW_DEVICE_EXTENSION)DeviceExtension);
}

//
// TRUE if ScsiDispatch answers this SRB in software, without admitting it to
// the engine or starting any hardware work (no child, no DMA): the requests
// the timer may replay before its hold check (lifecycle.c, EngTimer;
// engine.c, SoftwareOnly). Keep in step with the switch below; anything not listed
// (READ/WRITE, SYNCHRONIZE CACHE, LOG SENSE, ATA pass-through, FLUSH,
// SHUTDOWN, IO_CONTROL) may start hardware work.
//
BOOLEAN ScsiCompletesInSoftware(IN PSCSI_REQUEST_BLOCK Srb)
{
    if (Srb->Function == SRB_FUNCTION_FLUSH_QUEUE) {
        return TRUE;
    }
    if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI) {
        return FALSE;
    }
    switch (Srb->Cdb[0]) {
    case SCSIOP_TEST_UNIT_READY:
    case SCSIOP_VERIFY6:            // refused in software (47.22)
    case SCSIOP_START_STOP_UNIT:
    case SCSIOP_INQUIRY:
    case SCSIOP_REQUEST_SENSE:
    case SCSIOP_READ_CAPACITY:
    case SCSIOP_MODE_SENSE:
    case SCSIOP_MODE_SENSE10:
    case SCSIOP_READ_DEFECT_DATA10:
    case STOR98PF_OPCODE:
        return TRUE;
    case SCSI_SRV_ACT_IN:
        return (BOOLEAN)(Srb->Cdb[1] == SCSI_RD_CAPAC16);
    default:
        return FALSE;
    }
}

//
// ScsiDispatch - an SRB for our LUN in the running state (also the replay of
// a held SRB). Requests answered by the driver complete here; NVMe requests
// are admitted to the engine (SrbStatus PENDING).
//
BOOLEAN ScsiDispatch(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    switch (Srb->Function) {
        case SRB_FUNCTION_EXECUTE_SCSI:
            switch (Srb->Cdb[0]) {
                case SCSIOP_READ6:
                case SCSIOP_READ:
                case SCSIOP_READ16:
                case SCSIOP_WRITE6:
                case SCSIOP_WRITE:
                case SCSIOP_WRITE16:
                    return EngSubmitReadWrite(DevExt, Srb);

                case SCSIOP_TEST_UNIT_READY:
                case SCSIOP_START_STOP_UNIT:
                    // Accepted without NVMe work
                    return ScsiSuccess(DevExt, Srb);

                case SCSIOP_VERIFY6:
                    // A sequential-access command (SSC): for a disk an
                    // unsupported operation code (47.22)
                    return ScsiIllegalRequest(DevExt, Srb, SCSI_ADSENSE_ILLEGAL_COMMAND);

                case SCSIOP_VERIFY:
                case SCSIOP_VERIFY16:
                    // Fields, range and size checked, then the medium read
                    // (NVMe Verify or reads into the bounce pages)
                    return ScsiHandleVerify(DevExt, Srb);

                case SCSIOP_INQUIRY:
                    return ScsiHandleInquiry(DevExt, Srb);

                case SCSIOP_REQUEST_SENSE:
                    return ScsiHandleRequestSense(DevExt, Srb);

                case SCSIOP_READ_CAPACITY:
                    return ScsiHandleReadCapacity(DevExt, Srb);

                case SCSI_SRV_ACT_IN:
                    if (Srb->Cdb[1] == SCSI_RD_CAPAC16) {
                        return ScsiHandleReadCapacity64(DevExt, Srb);
                    }
                    return ScsiIllegalRequest(DevExt, Srb, SCSI_ADSENSE_INVALID_CDB);

                case SCSIOP_LOG_SENSE:
                    return ScsiHandleLogSense(DevExt, Srb);

                case SCSIOP_MODE_SENSE:
                case SCSIOP_MODE_SENSE10:
                    return ScsiHandleModeSense(DevExt, Srb);

                case SCSIOP_SYNCHRONIZE_CACHE:
                case SCSIOP_SYNCHRONIZE_CACHE16:
                    DBGSTAGE(STG_SYNC_CACHE, Srb->Cdb[0] | (DevExt->ShutdownCount << 8));
                    return EngSubmitFlush(DevExt, Srb);

                case SCSIOP_ATA_PASSTHROUGH16:
                case SCSIOP_ATA_PASSTHROUGH12:
                    return ScsiHandleSatPassthrough(DevExt, Srb);

                case SCSIOP_READ_DEFECT_DATA10:
                    return ScsiHandleReadDefectData10(DevExt, Srb);

                case STOR98PF_OPCODE:
                    // READ BUFFER: the counter answer (MD-8), from memory
                    return ScsiHandleReadBuffer(DevExt, Srb);

                default:
                    // Not implemented (READ/WRITE(12), REPORT LUNS, MODE
                    // SELECT, UNMAP, WRITE AND VERIFY, ...): CHECK
                    // CONDITION, INVALID COMMAND OPERATION CODE.
                    return ScsiIllegalRequest(DevExt, Srb, SCSI_ADSENSE_ILLEGAL_COMMAND);
            }

        case SRB_FUNCTION_FLUSH:
            // Flush barrier (PLAN 0.11 class 1)
            DBGSTAGE(STG_SRB_FLUSH, (DevExt->ShutdownCount << 8) | DevExt->State);
            return EngSubmitFlush(DevExt, Srb);

        case SRB_FUNCTION_SHUTDOWN:
            // Restartable shutdown notification (PLAN 0.11 class 2)
            DevExt->ShutdownCount++;
            DBGSTAGE(STG_SRB_SHUTDOWN, (DevExt->ShutdownCount << 8) | DevExt->State);
            return EngSubmitFlush(DevExt, Srb);

        case SRB_FUNCTION_FLUSH_QUEUE:
            // The engine holds no queue the port could flush
            return ScsiSuccess(DevExt, Srb);

        case SRB_FUNCTION_IO_CONTROL:
            {
            // Admission is decided by the engine's own counters, never by
            // the SrbStatus the SRB arrived with (the port may hand it in
            // as PENDING). A handler that returns TRUE without admitting
            // the SRB has set its status; PENDING left over means it did
            // not, which is answered as an error, never left uncompleted.
            ULONG completed = DevExt->SrbsCompleted;
            ULONG admitted = DevExt->ParentsAdmitted;

            Srb->SrbStatus = SRB_STATUS_PENDING;
            if (HandleIO_NVME2KDB(DevExt, Srb) || HandleIO_SCSIDISK(DevExt, Srb)) {
                if (DevExt->SrbsCompleted != completed || DevExt->ParentsAdmitted != admitted) {
                    return TRUE;    // admitted to (or completed by) the engine
                }
                if (Srb->SrbStatus == SRB_STATUS_PENDING) {
                    Srb->SrbStatus = SRB_STATUS_ERROR;
                }
                EngCompleteSrb(DevExt, Srb);
                return TRUE;
            }
            return ScsiError(DevExt, Srb, SRB_STATUS_INVALID_REQUEST);
            }

        default:
            return ScsiError(DevExt, Srb, SRB_STATUS_INVALID_REQUEST);
    }
}

#ifdef STOR98_TEST_VMMPROBE
//
// Test only (vmmprobe.h): the probe's state, for every adapter together.
//
static volatile LONG VpIn[VP_KINDS];
static ULONG VpEntries[VP_KINDS];
static ULONG VpIfSet[VP_KINDS];
static ULONG VpIfSeen;                  // bit Kind * 2 + IF: STG_TEST_VMM_IF given
static ULONG VpNestSeen[VP_KINDS];      // bit = set in progress: STG_TEST_VMM_NEST given
static ULONG VpNested;
static ULONG VpLeast[VP_KINDS];         // least headroom so far (0 = none measured yet)
static ULONG VpTotal;
static ULONG VpArenaSeen;               // bit What * 3 + arena: STG_TEST_VMM_ARENA given
static ULONG VpArenaOdd;
static ULONG VpTimerSeen;               // bit set * 2 + cancel: STG_TEST_VMM_TIMER given
static ULONG VpTimers[16];

// _CopyPageTable (Windows 98 DDK VMM.H, VMM service 0x61): nPages page-table
// entries from linear page LinPgNum into PageBuf; nonzero on success.
static ULONG __declspec(naked) _cdecl VpCopyPageTable(ULONG LinPgNum, ULONG nPages, PULONG PageBuf, ULONG flags)
{
    __asm int 0x20
    __asm _emit 0x61
    __asm _emit 0x80
    __asm _emit (VMM_DEVICE_ID >> 0) & 0xFF
    __asm _emit (VMM_DEVICE_ID >> 8) & 0xFF
}

#define VP_LOOK_PAGES   8

// Bytes from Sp down to the first page below it that is not present;
// VP_LOOK_PAGES pages' worth when all of them are (or the table cannot be
// read).
static ULONG VpHeadroom(IN ULONG Sp)
{
    ULONG ptes[VP_LOOK_PAGES];
    ULONG page = Sp >> PAGE_SHIFT;
    ULONG i;

    if (page < VP_LOOK_PAGES || !VpCopyPageTable(page - VP_LOOK_PAGES, VP_LOOK_PAGES, ptes, 0)) {
        return VP_LOOK_PAGES * PAGE_SIZE;
    }
    for (i = VP_LOOK_PAGES; i > 0; i--) {
        if (!(ptes[i - 1] & 1)) {
            // Page (page - VP_LOOK_PAGES + i - 1) is not present: the room
            // ends at its top.
            return Sp - ((page - VP_LOOK_PAGES + i) << PAGE_SHIFT);
        }
    }
    return VP_LOOK_PAGES * PAGE_SIZE;
}

ULONG VpMask(VOID)
{
    return (VpIn[VP_STARTIO] != 0 ? 1 : 0) | (VpIn[VP_TIMER] != 0 ? 2 : 0) | (VpIn[VP_INTERRUPT] != 0 ? 4 : 0);
}

VOID VpEnter(IN ULONG Kind)
{
    ULONG fl, stk, mask, ifs, room, k;

    __asm {
        pushfd
        pop eax
        mov fl, eax
        mov stk, esp
    }
    mask = VpMask();
    ifs = (fl >> 9) & 1;
    VpIn[Kind]++;
    VpEntries[Kind]++;
    if (ifs) {
        VpIfSet[Kind]++;
    }
    if (!(VpIfSeen & (1UL << (Kind * 2 + ifs)))) {
        VpIfSeen |= 1UL << (Kind * 2 + ifs);
        DBGMARK(STG_TEST_VMM_IF, Kind | (ifs << 4) | (mask << 8));
    }
    if (mask != 0) {
        VpNested++;
        if (!(VpNestSeen[Kind] & (1UL << mask)) || (VpNested & 0xFF) == 0) {
            VpNestSeen[Kind] |= 1UL << mask;
            DBGMARK(STG_TEST_VMM_NEST, Kind | (mask << 4) | (ifs << 7) |
                    ((VpNested > 0xFFFFFF ? 0xFFFFFF : VpNested) << 8));
        }
    }
    room = VpHeadroom(stk);
    if (VpLeast[Kind] == 0 || room < VpLeast[Kind]) {
        VpLeast[Kind] = room;
        DBGMARK(STG_TEST_VMM_STACK, (room & 0xFFFFFF) | (Kind << 24) | (mask << 28));
    }
    if ((++VpTotal & 0xFFFF) == 0) {
        for (k = 0; k < VP_KINDS; k++) {
            DBGSTAGE(STG_TEST_VMM_SUM, k | (((VpIfSet[k] >> 4) > 0xFFFFF ? 0xFFFFF : (VpIfSet[k] >> 4)) << 4) |
                     (((VpEntries[k] >> 16) > 0xFF ? 0xFF : (VpEntries[k] >> 16)) << 24));
        }
    }
}

VOID VpExit(IN ULONG Kind)
{
    VpIn[Kind]--;
}

VOID VpArena(IN ULONG What, IN ULONG Linear, IN ULONG Bytes, IN ULONG Func, IN ULONG Cdb0)
{
    ULONG arena = Linear < 0x80000000UL ? 0 : (Linear < 0xC0000000UL ? 1 : 2);
    ULONG kib = Bytes >> 10;
    BOOLEAN first = (BOOLEAN)!(VpArenaSeen & (1UL << (What * 3 + arena)));

    if (arena != 2) {
        VpArenaOdd++;
    }
    if (first || (arena != 2 && (VpArenaOdd & 0xFFF) == 0)) {
        VpArenaSeen |= 1UL << (What * 3 + arena);
        DBGMARK(STG_TEST_VMM_ARENA, What | (arena << 2) | (VpMask() << 4) | ((Func > 0xFF ? 0xFF : Func) << 8) |
                ((Cdb0 & 0xFF) << 16) | ((kib > 0xFF ? 0xFF : kib) << 24));
    }
}

VOID VpCopy(IN ULONG Linear, IN ULONG Bytes, IN BOOLEAN ToCaller)
{
    ULONG mask = VpMask();

    if (Linear < 0xC0000000UL && (mask & 6) != 0) {
        DBGMARK(STG_TEST_VMM_COPY, Linear);
        DBGSTAGE(STG_TEST_VMM_COPY2, (Bytes & 0xFFFFFF) | ((ULONG)(ToCaller ? 1 : 0) << 24) | (mask << 28));
    }
}

VOID VpTimerRequest(IN ULONG PeriodUs)
{
    ULONG mask = VpMask();
    ULONG cancel = PeriodUs == 0 ? 1 : 0;
    ULONG key = mask * 2 + cancel;
    ULONG n = ++VpTimers[key];

    if (!(VpTimerSeen & (1UL << key)) || (n & 0xFFF) == 0) {
        VpTimerSeen |= 1UL << key;
        DBGMARK(STG_TEST_VMM_TIMER, mask | (cancel << 4) | ((n > 0xFFFFFF ? 0xFFFFFF : n) << 8));
    }
}

#ifdef STOR98_TEST_VMMMAP
// Test only (46.20): one deliberate fault inside a VMM service at the
// 3000th HwStartIo, so that the STOP screen names the object and offset
// of that service's code. STOR98_TEST_VMMMAP selects it: 1 _CopyPageTable
// storing into a page that was never committed, 2 Cancel_Time_Out on a
// handle in such a page, 3 _HeapFree of a block in such a page; 4
// Cancel_Global_Event, 5 Cancel_Priority_VM_Event and 6 Signal_Semaphore
// on a handle in such a page, 7 List_Remove of a node from a list whose
// handle is in such a page, 8 _HeapFree of a block of its own whose 16
// bytes before it were overwritten with addresses in such a page.
static ULONG VpStarts;

// Cancel_Time_Out (VMM service 0x3E): ESI = the time-out handle.
static VOID __declspec(naked) _cdecl VpCancelTimeOut(ULONG Handle)
{
    __asm {
        push esi
        mov esi, [esp + 8]
        int 0x20
        _emit 0x3E
        _emit 0x00
        _emit (VMM_DEVICE_ID >> 0) & 0xFF
        _emit (VMM_DEVICE_ID >> 8) & 0xFF
        pop esi
        ret
    }
}

// _HeapFree (VMM service 0x51), C-callable.
static ULONG __declspec(naked) _cdecl VpHeapFree(ULONG Block, ULONG Flags)
{
    __asm int 0x20
    __asm _emit 0x51
    __asm _emit 0x80
    __asm _emit (VMM_DEVICE_ID >> 0) & 0xFF
    __asm _emit (VMM_DEVICE_ID >> 8) & 0xFF
}

// A VMM service with its handle in ESI (Cancel_Global_Event 0x12,
// Cancel_Priority_VM_Event 0x15): VpEsiCall(service, handle).
#define VP_ESI_SERVICE(name, svc)                       \
static VOID __declspec(naked) _cdecl name(ULONG Handle) \
{                                                       \
    __asm push esi                                      \
    __asm mov esi, [esp + 8]                            \
    __asm int 0x20                                      \
    __asm _emit svc                                     \
    __asm _emit 0x00                                    \
    __asm _emit (VMM_DEVICE_ID >> 0) & 0xFF             \
    __asm _emit (VMM_DEVICE_ID >> 8) & 0xFF             \
    __asm pop esi                                       \
    __asm ret                                           \
}
VP_ESI_SERVICE(VpCancelGlobalEvent, 0x12)
VP_ESI_SERVICE(VpCancelPriorityVmEvent, 0x15)

// Signal_Semaphore (VMM service 0x28): EAX = the semaphore handle.
static VOID __declspec(naked) _cdecl VpSignalSemaphore(ULONG Handle)
{
    __asm {
        mov eax, [esp + 4]
        int 0x20
        _emit 0x28
        _emit 0x00
        _emit (VMM_DEVICE_ID >> 0) & 0xFF
        _emit (VMM_DEVICE_ID >> 8) & 0xFF
        ret
    }
}

// List_Remove (VMM service 0xA1): ESI = the list handle, EAX = the node.
static VOID __declspec(naked) _cdecl VpListRemove(ULONG List, ULONG Node)
{
    __asm {
        push esi
        mov esi, [esp + 8]
        mov eax, [esp + 12]
        int 0x20
        _emit 0xA1
        _emit 0x00
        _emit (VMM_DEVICE_ID >> 0) & 0xFF
        _emit (VMM_DEVICE_ID >> 8) & 0xFF
        pop esi
        ret
    }
}

// _HeapAllocate (VMM service 0x4F), C-callable: nbytes, flags.
static ULONG __declspec(naked) _cdecl VpHeapAllocate(ULONG Bytes, ULONG Flags)
{
    __asm int 0x20
    __asm _emit 0x4F
    __asm _emit 0x80
    __asm _emit (VMM_DEVICE_ID >> 0) & 0xFF
    __asm _emit (VMM_DEVICE_ID >> 8) & 0xFF
}

// A system-arena page whose page-table entry is 0 (never committed), or 0.
static ULONG VpUncommitted(VOID)
{
    ULONG lin, pte;

    for (lin = 0xC1000000UL; lin < 0xFF000000UL; lin += 0x100000UL) {
        if (VpCopyPageTable(lin >> PAGE_SHIFT, 1, &pte, 0) && pte == 0) {
            return lin;
        }
    }
    return 0;
}

static VOID VpMapFault(VOID)
{
    ULONG bad;

    if (++VpStarts != 3000) {
        return;
    }
    bad = VpUncommitted();
    DBGMARK(STG_TEST_VMM_MAP, STOR98_TEST_VMMMAP | (bad & 0xFFFFF000UL));
    if (bad == 0) {
        return;
    }
#if STOR98_TEST_VMMMAP == 1
    (VOID)VpCopyPageTable(0xC0001, 1, (PULONG)(bad + 0x10), 0);
#elif STOR98_TEST_VMMMAP == 2
    VpCancelTimeOut(bad + 0x10);
#elif STOR98_TEST_VMMMAP == 3
    (VOID)VpHeapFree(bad + 0x10, 0);
#elif STOR98_TEST_VMMMAP == 4
    VpCancelGlobalEvent(bad + 0x10);
#elif STOR98_TEST_VMMMAP == 5
    VpCancelPriorityVmEvent(bad + 0x10);
#elif STOR98_TEST_VMMMAP == 6
    VpSignalSemaphore(bad + 0x10);
#elif STOR98_TEST_VMMMAP == 7
    VpListRemove(bad + 0x10, bad + 0x40);
#elif STOR98_TEST_VMMMAP == 8
    {
        PULONG blk;
        ULONG i;

        blk = (PULONG)VpHeapAllocate(64, 0);
        DBGMARK(STG_TEST_VMM_MAP, 0xE0 | ((ULONG)blk & 0xFFFFFF00UL));
        if (blk != NULL) {
            for (i = 1; i <= 4; i++) {
                blk[-(LONG)i] = bad + 0x10 * i;
            }
            (VOID)VpHeapFree((ULONG)blk, 0);
        }
    }
#endif
    DBGMARK(STG_TEST_VMM_MAP, 0xF0 | STOR98_TEST_VMMMAP);
}
#endif

// Test only: HwStartIo and HwInterrupt counted around their bodies.
static BOOLEAN HwStartIoBody(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb);
static BOOLEAN HwInterruptBody(IN PVOID DeviceExtension);

BOOLEAN HwStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb)
{
    BOOLEAN r;

    VpEnter(VP_STARTIO);
#ifdef STOR98_TEST_VMMMAP
    VpMapFault();
#endif
    r = HwStartIoBody(DeviceExtension, Srb);
    VpExit(VP_STARTIO);
    return r;
}

BOOLEAN HwInterrupt(IN PVOID DeviceExtension)
{
    BOOLEAN r;

    VpEnter(VP_INTERRUPT);
    r = HwInterruptBody(DeviceExtension);
    VpExit(VP_INTERRUPT);
    return r;
}
#define HwStartIo HwStartIoBody
#define HwInterrupt HwInterruptBody
#endif

//
// HwStartIo - Process SCSI request
//
#ifdef STOR98_TEST_REENTRY
#ifndef STOR98_DBGPORT
#error STOR98_TEST_REENTRY is test-only: build it with STOR98_DBGPORT=1 (out\<flavor>-dbgport, never staged)
#endif
// Test only: marks HwStartIo's run, so HwInterrupt can count an entry
// inside it (STG_TEST_REENTRY).
static BOOLEAN HwStartIoBody(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb);

BOOLEAN HwStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    BOOLEAN r;

    DevExt->TestInStartIo++;
    r = HwStartIoBody(DeviceExtension, Srb);
    DevExt->TestInStartIo--;
    return r;
}
#define HwStartIo HwStartIoBody
#endif

BOOLEAN HwStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

    // The SCSI status is an output only: it starts as GOOD, whatever the
    // port's SRB held (PLAN "SCSI status of a completion"); a path that
    // fails with CHECK CONDITION sets it.
    Srb->ScsiStatus = SCSISTAT_GOOD;

    // Check if the request is for our device (PathId=0, TargetId=0, Lun=0)
    if (Srb->PathId != 0 || Srb->TargetId != 0 || Srb->Lun != 0) {
        if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI) {
            if (Srb->PathId == 0 && Srb->TargetId == 0 && Srb->Lun != 0) {
                // Invalid LUN on our target
                Srb->SrbStatus = SRB_STATUS_ERROR;
                Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
                // Not our LU: no sense is retained for it.
                ScsiSetSense(NULL, Srb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_LUN, 0x00);
            } else {
                Srb->SrbStatus = SRB_STATUS_SELECTION_TIMEOUT;
            }
        } else {
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        }
        ScsiCompletionStatus(Srb);
        ScsiPortNotification(RequestComplete, DeviceExtension, Srb);
        // NextRequest through the engine's debt (EngStartIoTail), so it is
        // withheld while the engine cannot accept, as for any other SRB;
        // never NextLuRequest, which is LU 0's and owed by none of these.
        EngStartIoTail(DevExt, FALSE);
        return TRUE;
    }

#if defined(NVME2K_W9X) && defined(STOR98_DBGPORT)
    EngProbeSg9x(Srb);
#endif
    EngPfSrbIn(DevExt, Srb);
#ifdef STOR98_TEST_NOAUTOSENSE
#ifndef STOR98_DBGPORT
#error STOR98_TEST_NOAUTOSENSE is test-only: build it with STOR98_DBGPORT=1 (out\<flavor>-dbgport, never staged)
#endif
    // Test build (xhci98 record 30 section 9.2 item 10): every N-th READ(10)
    // fails with CHECK CONDITION, its sense kept and not given as autosense,
    // which is when SCSIPORT.PDR fetches the sense itself with a REQUEST
    // SENSE of its own; that one is answered from the kept entry (sense.c).
    if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI && Srb->Cdb[0] == SCSIOP_REQUEST_SENSE) {
        DBGSTAGE(STG_TEST_REQSENSE, (ULONG)(ULONG_PTR)Srb->DataBuffer);
    }
    if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI && Srb->Cdb[0] == SCSIOP_READ &&
        ++DevExt->TestNoAsReads % STOR98_TEST_NOAUTOSENSE == 0) {
        UCHAR senseLen = Srb->SenseInfoBufferLength;
        BOOLEAN tagged = (BOOLEAN)((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) &&
                                   Srb->QueueTag != SP_UNTAGGED);

        DBGSTAGE(STG_TEST_NOAS, (ULONG)(ULONG_PTR)Srb->DataBuffer);
        // A new READ ends the sense kept for earlier commands (sense.c), as
        // EngStartIo would have done.
        ScsiSenseNewCommand(DevExt, Srb);
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        // No room for autosense: the kept path of ScsiSetSense.
        Srb->SenseInfoBufferLength = 0;
        ScsiSetSense(DevExt, Srb, SCSI_SENSE_HARDWARE_ERROR, 0x44, 0);
        Srb->SenseInfoBufferLength = senseLen;
        Srb->DataTransferLength = 0;
        ScsiError(DevExt, Srb, SRB_STATUS_ERROR);
        // The engine's own queue-readiness accounting (NextRequest, or the
        // NextLuRequest a tagged request owes).
        EngStartIoTail(DevExt, tagged);
        return TRUE;
    }
#endif
    return EngStartIo(DevExt, Srb);
}

//
// HwInterrupt - ISR for adapter
//
#ifdef STOR98_TEST_REENTRY
#undef HwStartIo
#endif

BOOLEAN HwInterrupt(IN PVOID DeviceExtension)
{
#ifdef STOR98_TEST_REENTRY
    {
        PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

        if (DevExt->TestInStartIo != 0 || DevExt->TestInTimer != 0) {
            if ((DevExt->TestReentries++ & 0xFF) == 0) {
                DBGMARK(STG_TEST_REENTRY, (DevExt->TestReentries & 0xFFFFFF) |
                        ((ULONG)((DevExt->TestInStartIo != 0 ? 1 : 0) | (DevExt->TestInTimer != 0 ? 2 : 0)) << 24));
            }
        }
    }
#endif
    return EngInterrupt((PHW_DEVICE_EXTENSION)DeviceExtension);
}
#ifdef STOR98_TEST_VMMPROBE
#undef HwStartIo
#undef HwInterrupt
#endif

//
// HwResetBus - truthful reset (PLAN 0.10, G4); see lifecycle.c
//
BOOLEAN HwResetBus(IN PVOID DeviceExtension, IN ULONG PathId)
{
    return EngResetBus((PHW_DEVICE_EXTENSION)DeviceExtension, PathId);
}

#ifndef NVME2K_W9X

#ifndef NVME2K_NT4
//
// HwAdapterControl - Handle adapter power and PnP events (Windows 2000+)
//
SCSI_ADAPTER_CONTROL_STATUS HwAdapterControl(
    IN PVOID DeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    SCSI_ADAPTER_CONTROL_STATUS status = ScsiAdapterControlSuccess;

    DBGMARK(STG_ADAPTER_CONTROL, ControlType);
    switch (ControlType) {
        case ScsiQuerySupportedControlTypes:
            {
                PSCSI_SUPPORTED_CONTROL_TYPE_LIST list =
                    (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;

                // ScsiRestartAdapter is NOT advertised (PLAN 0.11, G7): a
                // power-up goes through HwFindAdapter/HwInitialize with the
                // bring-up failure semantics of 0.10.
                if (list->MaxControlType > ScsiStopAdapter) {
                    list->SupportedTypeList[ScsiStopAdapter] = TRUE;
                }
                if (list->MaxControlType > ScsiSetRunningConfig) {
                    list->SupportedTypeList[ScsiSetRunningConfig] = TRUE;
                }
            }
            break;

        case ScsiStopAdapter:
            // Class 3 stop (PLAN 0.11)
            EngStop(DevExt);
            break;

        case ScsiSetRunningConfig:
            // Advertised, never called on the measured ports (G7); the PCI
            // command register is restored in HwFindAdapter.
            break;

        default:
            status = ScsiAdapterControlUnsuccessful;
            break;
    }

    return status;
}
#endif

#else //NVME2K_W9X

//
// HwAdapterState - 9x: TRUE once per load before HwInitialize; FALSE is the
// stop at removal and shutdown (G8, G9), after which no timer runs. Windows
// Me never calls FALSE; there the CM's removal transaction (MD-5, pool9x.c)
// and write-through (PLAN D3, platform.c) stand in for the stop.
//
BOOLEAN HwAdapterState(
    IN PVOID DeviceExtension,
    IN PVOID Context,
    IN BOOLEAN SaveState)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

    UNREFERENCED_PARAMETER(Context);

    DBGMARK(STG_ADAPTER_STATE, SaveState);
    if (!SaveState) {
        EngStop(DevExt);
    }
    return TRUE;
}
#endif //NVME2K_W9X
