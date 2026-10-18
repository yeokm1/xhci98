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
// StorMaxTransferKB / maxtransferkb= setting (xfertun.h, parsed by
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
            if (!ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET, &cmdReg)) {
                return SP_RETURN_ERROR;
            }
            cmdReg &= ~PCI_INTERRUPT_DISABLE;
            WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET, cmdReg);
            DevExt->IntxEnabled = TRUE;
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
    UCHAR baseClass, subClass, progIf;
    ULONG bytesRead;

    UNREFERENCED_PARAMETER(BusInformation);

    DBGINIT();
    // A resume calls HwFindAdapter again on the same, not zeroed, device
    // extension (G7, Q-NT), with no ArgumentString.
    DevExt->Resumed = (BOOLEAN)(DevExt->Magic == STOR98_DEVEXT_MAGIC);
    DevExt->Magic = STOR98_DEVEXT_MAGIC;
    // No sense kept before a stop or power transition survives it.
    ScsiSenseReset(DevExt);
    EngParseTunables(DevExt, ArgumentString);
    PlatWriteThroughSetting(DevExt, ArgumentString);
    DevExt->Clock.CalPct = DevExt->Tun.CalPct;
    ClockCalibrate(&DevExt->Clock);
    DBGMARK(STG_CLOCK_CAL, DevExt->Clock.UnitsPerMs ? DevExt->Clock.TscPerMs : 0);

    if (HwContext) {
        busNumber = ((PULONG)HwContext)[0];
        slotNumber = ((PULONG)HwContext)[1];
    } else {
        // Win2k, PnP gives us this directly
        busNumber = ConfigInfo->SystemIoBusNumber;
        slotNumber = ConfigInfo->SlotNumber;
    }

scanloop:
    // Read PCI configuration space for THIS slot only, into a zeroed buffer
    // (only the bytesRead returned are used).
    RtlZeroMemory(pciBuffer, sizeof(pciBuffer));
    bytesRead = ScsiPortGetBusData(
        DeviceExtension,
        PCIConfiguration,
        busNumber,
        slotNumber,
        pciBuffer,
        256);

    // 0 (no bus), 2 (no function) or too short to hold the class code:
    // nothing that can match here (PLAN 3.4).
    if (bytesRead < PCI_CLASS_CODE_OFFSET + 3) {
        goto notmatched;
    }

    DevExt->VendorId = *(USHORT*)&pciBuffer[PCI_VENDOR_ID_OFFSET];
    DevExt->DeviceId = *(USHORT*)&pciBuffer[PCI_DEVICE_ID_OFFSET];

    if (DevExt->VendorId == 0xFFFF || DevExt->VendorId == 0x0000) {
        goto notmatched;
    }

    DevExt->RevisionId = pciBuffer[PCI_REVISION_ID_OFFSET];
    progIf = pciBuffer[PCI_CLASS_CODE_OFFSET];
    subClass = pciBuffer[PCI_CLASS_CODE_OFFSET + 1];
    baseClass = pciBuffer[PCI_CLASS_CODE_OFFSET + 2];

    if (IsNvmeDevice(baseClass, subClass, progIf)) {
        ULONG rc;

        DevExt->BusNumber = busNumber;
        DevExt->SlotNumber = slotNumber;
        if (HwContext) {
            ((PULONG)HwContext)[0] = DevExt->BusNumber;
            ((PULONG)HwContext)[1] = DevExt->SlotNumber + 1;
            if (((PULONG)HwContext)[1] >= (PCI_MAX_DEVICES*PCI_MAX_FUNCTION)) {
                ((PULONG)HwContext)[1] = 0;
                ((PULONG)HwContext)[0]++;
            }
            *Again = TRUE;
        } else {
            *Again = FALSE;
        }
        DBGSTAGE(STG_FIND_ADAPTER, (busNumber << 16) | slotNumber);
        rc = HwFoundAdapter(DevExt, ConfigInfo, pciBuffer, bytesRead, ArgumentString,
                            (BOOLEAN)(HwContext == NULL));
        if (rc == SP_RETURN_FOUND) {
            // The CM link-state table (G3, MD-6; platform.h): last, once the
            // adapter is found (NT: cmlinknt.c; 9x: pool9x.c).
            PlatCmLinkOpen(DevExt, ArgumentString);
        }
        DBGSTAGE(STG_FIND_RESULT, rc);
        return rc;
    }

notmatched:
    if (HwContext == NULL) {
        // PnP (2000 and later): the port asked about this one function.
        // Another NVMe function elsewhere on the buses is not ours to take
        // while ConfigInfo describes the device PnP started.
        *Again = FALSE;
        return SP_RETURN_NOT_FOUND;
    }

    // NT4 ABI (9x) or install time: Continue scanning the bus
    slotNumber++;
    if (slotNumber == (PCI_MAX_DEVICES*PCI_MAX_FUNCTION)) {
        busNumber++;
        slotNumber = 0;
        if (busNumber == 16) {
            *Again = FALSE;
            return SP_RETURN_NOT_FOUND;
        }
    }
    goto scanloop;
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
// the timer may replay before its hold check (lifecycle.c, EngTimer; review
// round 3, N3). Keep in step with the switch below; anything not listed
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
    case SCSIOP_VERIFY6:
    case SCSIOP_VERIFY:
    case SCSIOP_VERIFY16:
    case SCSIOP_START_STOP_UNIT:
    case SCSIOP_INQUIRY:
    case SCSIOP_REQUEST_SENSE:
    case SCSIOP_READ_CAPACITY:
    case SCSIOP_MODE_SENSE:
    case SCSIOP_MODE_SENSE10:
    case SCSIOP_READ_DEFECT_DATA10:
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
                case SCSIOP_VERIFY6:
                case SCSIOP_VERIFY:
                case SCSIOP_VERIFY16:
                case SCSIOP_START_STOP_UNIT:
                    // Accepted without NVMe work
                    return ScsiSuccess(DevExt, Srb);

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

//
// HwStartIo - Process SCSI request
//
BOOLEAN HwStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;

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
        ScsiPortNotification(RequestComplete, DeviceExtension, Srb);
        ScsiPortNotification(NextRequest, DeviceExtension, NULL);
        return TRUE;
    }

    return EngStartIo(DevExt, Srb);
}

//
// HwInterrupt - ISR for adapter
//
BOOLEAN HwInterrupt(IN PVOID DeviceExtension)
{
    return EngInterrupt((PHW_DEVICE_EXTENSION)DeviceExtension);
}

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
