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

static BOOLEAN IsExtended(UCHAR type)
{
    return type == PARTITION_EXTENDED ||
           type == PARTITION_XINT13_EXTENDED;
}

static VOID DiskSwapMbrPte(PPARTITION_TABLE_ENTRY Entry)
{
    SD(Entry, SectorCountBeforePartition);
    SD(Entry, PartitionSectorCount);
}

VOID DiskSwapMbr(PMASTER_BOOT_RECORD Mbr)
{
    SD(Mbr, Signature);
    SW(Mbr, Reserved);
    for (ULONG i = 0; i < sizeof(Mbr->PartitionTable) / sizeof(*Mbr->PartitionTable); i++)
        DiskSwapMbrPte(&Mbr->PartitionTable[i]);
    SW(Mbr, MasterBootRecordMagic);
}

ARC_STATUS DiskGenericReadMbr(PGENERIC_DISK GenericDisk, LARGE_INTEGER DiskOffset, PMASTER_BOOT_RECORD OutMbr)
{
    ARC_STATUS Status = ESUCCESS;
    MASTER_BOOT_RECORD MasterBootRecord;
    ULONG ByteCount;

    Status = GenericDiskReadRawData(GenericDisk, &MasterBootRecord, DiskOffset, sizeof(MasterBootRecord), &ByteCount);
    if (Status != ESUCCESS)
    {
        ERR("Cannot read MBR\n");
        goto result;
    }

    DiskSwapMbr(&MasterBootRecord);

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

    RtlCopyMemory(OutMbr, &MasterBootRecord, sizeof(MasterBootRecord));

result:
    return Status;
}

ARC_STATUS DiskProcessExtendedMbr(PGENERIC_DISK GenericDisk, LARGE_INTEGER Offset)
{
    ARC_STATUS Status = ESUCCESS;
    ULONG DrivePartition = 5;
    MASTER_BOOT_RECORD ExtendedBootRecord;

    LARGE_INTEGER DiskOffset = Offset;

    ULONGLONG SectorSize = SECTOR_SIZE;

    Status = DiskGenericReadMbr(GenericDisk, DiskOffset, &ExtendedBootRecord);
    if (Status != ESUCCESS)
        goto result;

    while (ExtendedBootRecord.MasterBootRecordMagic == 0xAA55)
    {
        PPARTITION_TABLE_ENTRY Logical = &ExtendedBootRecord.PartitionTable[0];
        PPARTITION_TABLE_ENTRY Next = &ExtendedBootRecord.PartitionTable[1];
        ULONGLONG RelativeOffset = Next->SectorCountBeforePartition * SectorSize;

        if (Logical->SystemIndicator != PARTITION_ENTRY_UNUSED)
        {
            PGENERIC_DISK_PARTITION DiskPartition;
            LARGE_INTEGER StartOffset;
            LARGE_INTEGER EndOffset;

            StartOffset.QuadPart = (ULONGLONG)Logical->SectorCountBeforePartition * SectorSize;
            EndOffset.QuadPart = StartOffset.QuadPart + ((ULONGLONG)Logical->PartitionSectorCount * SectorSize);

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

        Status = DiskGenericReadMbr(GenericDisk, DiskOffset, &ExtendedBootRecord);
        if (Status != ESUCCESS)
            goto result;
    }

result:
    return Status;
}

ARC_STATUS DiskProcessMbr(PGENERIC_DISK GenericDisk)
{
    ARC_STATUS Status = ESUCCESS;
    MASTER_BOOT_RECORD MasterBootRecord;

    LARGE_INTEGER DiskOffset;

    ULONGLONG SectorSize = SECTOR_SIZE;
    BOOLEAN ProcessedEBR = FALSE;

    DiskOffset.QuadPart = 0;

    Status = DiskGenericReadMbr(GenericDisk, DiskOffset, &MasterBootRecord);
    if (Status != ESUCCESS)
        goto result;

    for (ULONG i = 0; i < 4; i++)
    {
        ULONG DrivePartition = 1 + i;
        PPARTITION_TABLE_ENTRY Entry = &MasterBootRecord.PartitionTable[i];

        if (Entry->SystemIndicator != PARTITION_ENTRY_UNUSED)
        {
            PGENERIC_DISK_PARTITION DiskPartition = NULL;
            LARGE_INTEGER StartOffset;
            LARGE_INTEGER EndOffset;

            StartOffset.QuadPart = (ULONGLONG)Entry->SectorCountBeforePartition * SectorSize;
            EndOffset.QuadPart = StartOffset.QuadPart + ((ULONGLONG)Entry->PartitionSectorCount * SectorSize);

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
