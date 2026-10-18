/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * cmnt.sys - test-only NT stand-in for a connection manager's link-state
 * table (include/stor98cm.h, interface 2), for the receiver cases xhci98's
 * CM cannot produce on demand: a table of another interface version, a
 * table with a malformed entry, and an entry published for the function
 * stor98 binds. Never shipped. A legacy driver started by hand before the
 * NVMe function appears (net start cmnt); it never unloads.
 *
 * Parameters (REG_DWORD, HKLM\System\CurrentControlSet\Services\cmnt\Parameters):
 *   Version     the table's Version field (default 1, the version-1 table
 *               header's; 4 is this interface's)
 *   Bus, Slot   the entry published (default none: Bus 0xFFFFFFFF)
 *   Flags       the entry's Flags (default IN_USE | UP)
 *   Gen         the entry's LinkGeneration (default 1)
 * The table is never written after DriverEntry: a frozen table, as a CM
 * leaves it at its unload.
 */

#include <ntddk.h>
#include "..\..\include\stor98cm.h"

static PSTOR98LS_TABLE table;
static PCALLBACK_OBJECT object;
static PVOID registration;

static VOID Answer(PVOID Context, PVOID Argument1, PVOID Argument2)
{
    UNREFERENCED_PARAMETER(Context);
    if (Argument1 == STOR98LS_QUERY && Argument2 != NULL) {
        *(PSTOR98LS_TABLE *)Argument2 = table;
    }
}

static ULONG Param(PUNICODE_STRING RegistryPath, PCWSTR Name, ULONG Default)
{
    RTL_QUERY_REGISTRY_TABLE q[2];
    ULONG value = Default;
    ULONG def = Default;
    WCHAR path[256];
    UNICODE_STRING p;

    RtlZeroMemory(q, sizeof(q));
    p.Buffer = path;
    p.Length = 0;
    p.MaximumLength = sizeof(path);
    RtlAppendUnicodeStringToString(&p, RegistryPath);
    RtlAppendUnicodeToString(&p, L"\\Parameters");
    q[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
    q[0].Name = (PWSTR)Name;
    q[0].EntryContext = &value;
    q[0].DefaultType = REG_DWORD;
    q[0].DefaultData = &def;
    q[0].DefaultLength = sizeof(def);
    if (!NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, path, q, NULL, NULL))) {
        return Default;
    }
    return value;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;
    ULONG bus;

    UNREFERENCED_PARAMETER(DriverObject);
    table = (PSTOR98LS_TABLE)ExAllocatePoolWithTag(NonPagedPool, sizeof(*table), 'tnmc');
    if (table == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(table, sizeof(*table));
    table->Signature = STOR98LS_SIGNATURE;
    table->Version = Param(RegistryPath, L"Version", 1);
    table->Count = STOR98LS_ENTRIES;
    bus = Param(RegistryPath, L"Bus", 0xFFFFFFFF);
    if (bus != 0xFFFFFFFF) {
        table->Entry[0].Sequence = 1;
        table->Entry[0].BusNumber = bus;
        table->Entry[0].SlotNumber = Param(RegistryPath, L"Slot", 0);
        table->Entry[0].LinkGeneration = Param(RegistryPath, L"Gen", 1);
        table->Entry[0].Flags = Param(RegistryPath, L"Flags", STOR98LS_IN_USE | STOR98LS_UP);
        table->Entry[0].Sequence = 2;
    }
    RtlInitUnicodeString(&name, STOR98LS_CALLBACK_NAME);
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE | OBJ_PERMANENT, NULL, NULL);
    /* Only on a fresh rendezvous: an object already there belongs to a CM
     * (live, or unloaded and leaving its permanent object), and a second
     * responder or a second table in that boot would break the contract
     * the receiver is tested against (Codex round 2). Load the stand-in
     * only on a system with no CM since boot. */
    status = ExCreateCallback(&object, &attributes, FALSE, TRUE);
    if (NT_SUCCESS(status)) {
        ObDereferenceObject(object);
        object = NULL;
        ExFreePool(table);
        table = NULL;
        return STATUS_OBJECT_NAME_COLLISION;
    }
    status = ExCreateCallback(&object, &attributes, TRUE, TRUE);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    registration = ExRegisterCallback(object, Answer, NULL);
    if (registration == NULL) {
        return STATUS_UNSUCCESSFUL;
    }
    /* No DriverUnload: the callback and the table stay for the boot. */
    return STATUS_SUCCESS;
}
