/*
 *  FreeLoader
 *  Copyright (C) 1998-2003  Brian Palmer  <brianp@sginet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);

#define MaxDriveNumber 0xFF
static PARTITION_STYLE DiskPartitionType[MaxDriveNumber + 1];

/* BRFR signature at disk offset 0x600 */
#define XBOX_SIGNATURE_SECTOR 3
#define XBOX_SIGNATURE        ('B' | ('R' << 8) | ('F' << 16) | ('R' << 24))

VOID DiskSwapMbr(PMASTER_BOOT_RECORD Mbr);

static BOOLEAN
DiskReadBootRecord(
    IN UCHAR DriveNumber,
    IN ULONGLONG LogicalSectorNumber,
    OUT PMASTER_BOOT_RECORD BootRecord)
{
    ULONG Index;

    /* Read master boot record */
    if (!MachDiskReadLogicalSectors(DriveNumber, LogicalSectorNumber, 1, DiskReadBuffer))
    {
        return FALSE;
    }
    RtlCopyMemory(BootRecord, DiskReadBuffer, sizeof(MASTER_BOOT_RECORD));

    TRACE("Dumping partition table for drive 0x%x:\n", DriveNumber);
    TRACE("Boot record logical start sector = %d\n", LogicalSectorNumber);
    TRACE("sizeof(MASTER_BOOT_RECORD) = 0x%x.\n", sizeof(MASTER_BOOT_RECORD));

    for (Index = 0; Index < 4; Index++)
    {
        TRACE("-------------------------------------------\n");
        TRACE("Partition %d\n", (Index + 1));
        TRACE("BootIndicator: 0x%x\n", BootRecord->PartitionTable[Index].BootIndicator);
        TRACE("StartHead: 0x%x\n", BootRecord->PartitionTable[Index].StartHead);
        TRACE("StartSector (Plus 2 cylinder bits): 0x%x\n", BootRecord->PartitionTable[Index].StartSector);
        TRACE("StartCylinder: 0x%x\n", BootRecord->PartitionTable[Index].StartCylinder);
        TRACE("SystemIndicator: 0x%x\n", BootRecord->PartitionTable[Index].SystemIndicator);
        TRACE("EndHead: 0x%x\n", BootRecord->PartitionTable[Index].EndHead);
        TRACE("EndSector (Plus 2 cylinder bits): 0x%x\n", BootRecord->PartitionTable[Index].EndSector);
        TRACE("EndCylinder: 0x%x\n", BootRecord->PartitionTable[Index].EndCylinder);
        TRACE("SectorCountBeforePartition: 0x%x\n", BootRecord->PartitionTable[Index].SectorCountBeforePartition);
        TRACE("PartitionSectorCount: 0x%x\n", BootRecord->PartitionTable[Index].PartitionSectorCount);
    }

    DiskSwapMbr(BootRecord);

    /* Check the partition table magic value */
    return (BootRecord->MasterBootRecordMagic == 0xaa55);
}

static BOOLEAN
DiskDetectBrfrPartitionStyle(
    IN UCHAR DriveNumber)
{
    /*
     * Get partition entry of an Xbox-standard BRFR partitioned disk.
     */
    if (MachDiskReadLogicalSectors(DriveNumber, XBOX_SIGNATURE_SECTOR, 1, DiskReadBuffer))
    {
        if (SWAPD(*((PULONG)DiskReadBuffer)) != XBOX_SIGNATURE)
        {
            /* No magic Xbox partitions */
            return FALSE;
        }

        return TRUE;
    }

    /* Partition does not exist */
    return FALSE;
}

VOID
DiskDetectPartitionType(
    IN UCHAR DriveNumber)
{
    MASTER_BOOT_RECORD MasterBootRecord;
    ULONG Index;
    ULONG PartitionCount = 0;
    PPARTITION_TABLE_ENTRY ThisPartitionTableEntry;
    BOOLEAN GPTProtect = FALSE;

    BOOLEAN ReadBootRecordResult = DiskReadBootRecord(DriveNumber, 0, &MasterBootRecord);

    /* Probe for Master Boot Record */
    if (ReadBootRecordResult &&
        MasterBootRecord.MasterBootRecordMagic == 0xAA55)
    {
        DiskPartitionType[DriveNumber] = PARTITION_STYLE_MBR;

        /* Check for GUID Partition Table */
        for (Index = 0; Index < 4; Index++)
        {
            ThisPartitionTableEntry = &MasterBootRecord.PartitionTable[Index];

            if (ThisPartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED)
            {
                PartitionCount++;

                if (Index == 0 && ThisPartitionTableEntry->SystemIndicator == PARTITION_GPT)
                {
                    GPTProtect = TRUE;
                }
            }
        }

        if (PartitionCount == 1 && GPTProtect)
        {
            DiskPartitionType[DriveNumber] = PARTITION_STYLE_GPT;
        }
        TRACE("Drive 0x%X partition type %s\n", DriveNumber, DiskPartitionType[DriveNumber] == PARTITION_STYLE_MBR ? "MBR" : "GPT");
        return;
    }

    /* Probe for Xbox-BRFR partitioning */
    if (DiskDetectBrfrPartitionStyle(DriveNumber))
    {
        DiskPartitionType[DriveNumber] = PARTITION_STYLE_BRFR;
        TRACE("Drive 0x%X partition type Xbox-BRFR\n", DriveNumber);
        return;
    }

    /* Failed to detect partitions, assume partitionless disk */
    DiskPartitionType[DriveNumber] = PARTITION_STYLE_RAW;
    TRACE("Drive 0x%X partition type unknown\n", DriveNumber);
}

PARTITION_STYLE DiskGetDrivePartitionStyle(IN UCHAR DriveNumber)
{
    return DiskPartitionType[DriveNumber];
}

BOOLEAN
DiskGetBootPartitionNumber(
    IN UCHAR DriveNumber,
    OUT PULONG BootPartition)
{
    PGENERIC_DISK GenericDisk = DiskFindGenericDisk(DriveNumber);
    if (!GenericDisk)
        return FALSE;

    PLIST_ENTRY End = &GenericDisk->PartitionList;
    PLIST_ENTRY Current = End->Flink;

    while (Current != End)
    {
        PGENERIC_DISK_PARTITION GenericPartition = CONTAINING_RECORD(Current, GENERIC_DISK_PARTITION, ListEntry);

        if (GenericPartition->IsBootable)
        {
            (*BootPartition) = GenericPartition->DrivePartition;
            return TRUE;
        }

        Current = Current->Flink;
    }

    return FALSE;
}

BOOLEAN
DiskGetBootPartitionNumberBySize(
    IN UCHAR DriveNumber,
    OUT PULONG BootPartition,
    IN LARGE_INTEGER PartitionSize,
    IN ULONGLONG ToleranceSize)
{
    PGENERIC_DISK GenericDisk = DiskFindGenericDisk(DriveNumber);
    if (!GenericDisk)
        return FALSE;

    PLIST_ENTRY End = &GenericDisk->PartitionList;
    PLIST_ENTRY Current = End->Flink;

    while (Current != End)
    {
        PGENERIC_DISK_PARTITION GenericPartition = CONTAINING_RECORD(Current, GENERIC_DISK_PARTITION, ListEntry);
        LARGE_INTEGER CurrentSize;
        LARGE_INTEGER DifferenceSize;

        CurrentSize.QuadPart = GenericPartition->PartitionEnd.QuadPart - GenericPartition->PartitionStart.QuadPart;
        DifferenceSize.QuadPart = abs(CurrentSize.QuadPart - PartitionSize.QuadPart);

        if (DifferenceSize.QuadPart <= ToleranceSize)
        {
            (*BootPartition) = GenericPartition->DrivePartition;
            return TRUE;
        }

        Current = Current->Flink;
    }

    return FALSE;
}
