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
#include <partition_table/gpt.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);

#define GPT_PARTITION_LIST_TAG 'GPPL'

static GPT_GUID EFI_NULL_GUID = {0};

static GPT_GUID EFI_SYSTEM_PARTITION_GUID =
{
    0xC12A7328,
    0xF81F,
    0x11D2,
    {0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B}
};

BOOLEAN
GuidEqual(PGPT_GUID a, PGPT_GUID b)
{
    return memcmp(a, b, sizeof(GPT_GUID)) == 0;
}

ARC_STATUS DiskProcessGptHeader(PGENERIC_DISK GenericDisk, LARGE_INTEGER Offset)
{
    ARC_STATUS Status = ESUCCESS;
    GPT_TABLE_HEADER GptHeader;

    LARGE_INTEGER DiskOffset = Offset;
    ULONG ByteCount;

    ULONG PartitionEntrySize = 0;
    PVOID PartitionEntryBuffer = NULL;
    ULONG PartitionEntryBufferSize = 0;

    ULONGLONG SectorSize = GenericDisk->DiskGeometry.BytesPerSector;

    Status = GenericDiskReadRawData(GenericDisk, &GptHeader, DiskOffset, sizeof(GptHeader), &ByteCount);
    if (Status != ESUCCESS)
    {
        ERR("Cannot read GPT Header\n");
        goto result;
    }

    Status = ByteCount >= sizeof(GptHeader) ? ESUCCESS : EINVAL;
    if (Status != ESUCCESS)
    {
        ERR("GptHeader ByteCount is less than expected\n");
        goto result;
    }

    Status = strncmp(GptHeader.Signature, GPT_HEADER_SIGNATURE, 8) ? EINVAL : ESUCCESS;
    if (Status != ESUCCESS)
    {
        ERR("GptHeader Signature is invalid\n");
        goto result;
    }

    PartitionEntrySize = GptHeader.SizeOfPartitionEntry;
    PartitionEntryBufferSize = PartitionEntrySize * GptHeader.NumberOfPartitionEntries;

    PartitionEntryBuffer = FrLdrHeapAlloc(PartitionEntryBufferSize, GPT_PARTITION_LIST_TAG);
    if (!PartitionEntryBuffer)
    {
        ERR("Cannot allocate PartitionEntryBuffer\n");

        Status = ENOMEM;
        goto result;
    }

    DiskOffset.QuadPart = GptHeader.PartitionEntryLba * SectorSize;

    Status = GenericDiskReadRawData(GenericDisk, PartitionEntryBuffer, DiskOffset, PartitionEntryBufferSize, &ByteCount);
    if (Status != ESUCCESS)
    {
        ERR("Cannot read PartitionEntryBuffer\n");
        goto result;
    }

    Status = ByteCount >= PartitionEntryBufferSize ? ESUCCESS : EINVAL;
    if (Status != ESUCCESS)
    {
        ERR("PartitionEntryBuffer ByteCount is less than expected\n");
        goto result;
    }

    for (ULONG i = 0; i < PartitionEntryBufferSize; i += PartitionEntrySize)
    {
        PGPT_PARTITION_ENTRY Entry = (PVOID)((PUCHAR)PartitionEntryBuffer + i);
        ULONG DrivePartition = i + 1;

        if (!GuidEqual(&Entry->PartitionTypeGuid, &EFI_NULL_GUID))
        {
            PGENERIC_DISK_PARTITION DiskPartition;
            LARGE_INTEGER StartOffset;
            LARGE_INTEGER EndOffset;

            StartOffset.QuadPart = Entry->StartingLba * SectorSize;
            EndOffset.QuadPart = Entry->EndingLba * SectorSize;

            Status = GenericDiskAddDiskPartition(GenericDisk, DrivePartition, StartOffset, EndOffset, &DiskPartition);
            if (Status != ESUCCESS)
            {
                ERR("Cannot GPT add partition\n");
                goto result;
            }

            DiskPartition->IsBootable = GuidEqual(&Entry->PartitionTypeGuid, &EFI_SYSTEM_PARTITION_GUID);
        }
    }

    TRACE("Read GPT Done!\n");

result:
    if (PartitionEntryBuffer)
        FrLdrHeapFree(PartitionEntryBuffer, GPT_PARTITION_LIST_TAG);

    return Status;
}

ARC_STATUS DiskProcessGpt(PGENERIC_DISK GenericDisk)
{
    ARC_STATUS Status = ESUCCESS;
    MASTER_BOOT_RECORD MasterBootRecord;

    LARGE_INTEGER DiskOffset;
    ULONG ByteCount;

    Status = GenericDiskReadRawData(GenericDisk, &MasterBootRecord, DiskOffset, sizeof(MasterBootRecord), &ByteCount);
    if (Status != ESUCCESS)
    {
        ERR("Cannot read protective MBR\n");
        goto result;
    }

    Status = ByteCount >= sizeof(MasterBootRecord) ? ESUCCESS : EINVAL;
    if (Status != ESUCCESS)
    {
        ERR("MasterBootRecord ByteCount is less than expected\n");
        goto result;
    }

    Status = MasterBootRecord.MasterBootRecordMagic == 0xAA55 ? ESUCCESS : EINVAL;
    if (Status != ESUCCESS)
    {
        ERR("Invalid MBR signature\n");
        goto result;
    }

    for (ULONG i = 0; i < 4; i++)
    {
        PPARTITION_TABLE_ENTRY Entry = &MasterBootRecord.PartitionTable[i];

        if (Entry->SystemIndicator == PARTITION_GPT)
        {
            LARGE_INTEGER StartOffset;

            StartOffset.QuadPart = Entry->SectorCountBeforePartition * SECTOR_SIZE;

            Status = DiskProcessGptHeader(GenericDisk, StartOffset);
            break;
        }
    }

    TRACE("Read Protective MBR Done!\n");

result:
    return Status;
}
