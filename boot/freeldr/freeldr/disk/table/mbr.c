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

BOOLEAN IsExtended(UCHAR type)
{
    return type == PARTITION_EXTENDED ||
           type == PARTITION_XINT13_EXTENDED;
}

ARC_STATUS DiskProcessExtendedMbr(PGENERIC_DISK GenericDisk, LARGE_INTEGER Offset)
{
    ARC_STATUS Status = ESUCCESS;
    ULONG DrivePartition = 5;
    EXTENDED_BOOT_RECORD ExtendedBootRecord;

    LARGE_INTEGER DiskOffset = Offset;
    ULONG ByteCount;

    ULONGLONG SectorSize = SECTOR_SIZE;

    Status = GenericDiskReadRawData(GenericDisk, &ExtendedBootRecord, DiskOffset, sizeof(ExtendedBootRecord), &ByteCount);
    if (Status != ESUCCESS)
    {
        ERR("Cannot read EBR\n");
        goto result;
    }

    if (ByteCount < sizeof(ExtendedBootRecord))
    {
        WARN("ByteCount for ExtendedBootRecord is lower than expected\n");
        goto result;
    }

    if (ExtendedBootRecord.ExtendedBootRecordMagic != 0xAA55)
    {
        WARN("Invalid ExtendedBootRecord Magic signature\n");
        goto result;
    }

    while (ExtendedBootRecord.ExtendedBootRecordMagic == 0xAA55)
    {
        PPARTITION_TABLE_ENTRY Logical = &ExtendedBootRecord.PartitionTable[0];
        PPARTITION_TABLE_ENTRY Next = &ExtendedBootRecord.PartitionTable[0];
        ULONGLONG RelativeOffset = Next->SectorCountBeforePartition * SectorSize;

        if (Logical->SystemIndicator != PARTITION_ENTRY_UNUSED)
        {
            PGENERIC_DISK_PARTITION DiskPartition;
            LARGE_INTEGER StartOffset;
            LARGE_INTEGER EndOffset;

            StartOffset.QuadPart = DiskOffset.QuadPart + (Logical->SectorCountBeforePartition * SectorSize);
            EndOffset.QuadPart = StartOffset.QuadPart + (Logical->PartitionSectorCount * SectorSize);

            Status = GenericDiskAddDiskPartition(GenericDisk, DrivePartition, StartOffset, EndOffset, &DiskPartition);
            if (Status != ESUCCESS)
            {
                ERR("Cannot add disk partition\n");
                goto result;
            }

            DiskPartition->IsBootable = Logical->BootIndicator == 0x80;
        }

        DrivePartition++;

        if (Next->SystemIndicator == PARTITION_ENTRY_UNUSED)
            break;

        if (!RelativeOffset)
            break;

        DiskOffset.QuadPart += RelativeOffset;

        Status = GenericDiskReadRawData(GenericDisk, &ExtendedBootRecord, DiskOffset, sizeof(ExtendedBootRecord), &ByteCount);
        if (Status != ESUCCESS)
        {
            ERR("Cannot read EBR\n");
            goto result;
        }

        if (ByteCount < sizeof(ExtendedBootRecord))
        {
            WARN("ByteCount for ExtendedBootRecord is lower than expected\n");
            goto result;
        }
    }

result:
    return Status;
}

ARC_STATUS DiskProcessMbr(PGENERIC_DISK GenericDisk)
{
    ARC_STATUS Status = ESUCCESS;
    MASTER_BOOT_RECORD MasterBootRecord;

    LARGE_INTEGER DiskOffset;
    ULONG ByteCount;

    ULONGLONG SectorSize = SECTOR_SIZE;
    BOOLEAN ProcessedEBR = FALSE;

    Status = GenericDiskReadRawData(GenericDisk, &MasterBootRecord, DiskOffset, sizeof(MasterBootRecord), &ByteCount);
    if (Status != ESUCCESS)
    {
        ERR("Cannot read MBR\n");
        goto result;
    }

    Status = ByteCount == sizeof(MasterBootRecord) ? ESUCCESS : EINVAL;
    if (Status != ESUCCESS)
    {
        ERR("ByteCount for MasterBootRecord is lower than expected\n");
        goto result;
    }

    Status = MasterBootRecord.MasterBootRecordMagic == 0xAA55 ? ESUCCESS : EINVAL;
    if (Status != ESUCCESS)
    {
        ERR("Invalid MasterBootRecord Magic signature\n");
        goto result;
    }

    for (ULONG i = 0; i < 4; i++)
    {
        ULONG DrivePartition = 1 + i;
        PPARTITION_TABLE_ENTRY Entry = &MasterBootRecord.PartitionTable[i];

        if (Entry->SystemIndicator != PARTITION_ENTRY_UNUSED)
        {
            PGENERIC_DISK_PARTITION DiskPartition = NULL;
            LARGE_INTEGER StartOffset;
            LARGE_INTEGER EndOffset;

            StartOffset.QuadPart = Entry->SectorCountBeforePartition * SectorSize;
            EndOffset.QuadPart = StartOffset.QuadPart + (Entry->PartitionSectorCount * SectorSize);

            if (IsExtended(Entry->SystemIndicator))
            {
                if (!ProcessedEBR)
                    Status = DiskProcessExtendedMbr(GenericDisk, StartOffset);

                ProcessedEBR = TRUE;
            }
            else
            {
                Status = GenericDiskAddDiskPartition(GenericDisk, DrivePartition, StartOffset, EndOffset, &DiskPartition);
            }

            if (Status != ESUCCESS)
            {
                ERR("Cannot process MBR partition\n");
                goto result;
            }

            if (DiskPartition)
                DiskPartition->IsBootable = Entry->BootIndicator == 0x80;
        }
    }

    TRACE("Read MBR Done!\n");

result:
    return Status;
}
