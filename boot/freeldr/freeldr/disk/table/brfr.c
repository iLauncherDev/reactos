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

/* Default hardcoded partition number to boot from Xbox disk */
#define FATX_DATA_PARTITION 1

typedef struct BRFR_PARTITION_ENTRY
{
    ULONG SectorCountBeforePartition;
    ULONG PartitionSectorCount;
    UCHAR SystemIndicator;
} BRFR_PARTITION_ENTRY, *PBRFR_PARTITION_ENTRY;

static BRFR_PARTITION_ENTRY XboxPartitions[] =
{
    /* This is in the \Device\Harddisk0\Partition.. order used by the Xbox kernel */
    { 0x0055F400, 0x0098F800, PARTITION_FAT32  }, /* Store , E: */
    { 0x00465400, 0x000FA000, PARTITION_FAT_16 }, /* System, C: */
    { 0x00000400, 0x00177000, PARTITION_FAT_16 }, /* Cache1, X: */
    { 0x00177400, 0x00177000, PARTITION_FAT_16 }, /* Cache2, Y: */
    { 0x002EE400, 0x00177000, PARTITION_FAT_16 }  /* Cache3, Z: */
};

static ULONG XboxPartitionsEntries = sizeof(XboxPartitions) / sizeof(*XboxPartitions);

ARC_STATUS DiskProcessBrfr(PGENERIC_DISK GenericDisk)
{
    ARC_STATUS Status = ESUCCESS;
    ULONGLONG SectorSize = SECTOR_SIZE;

    for (ULONG i = 0; i < XboxPartitionsEntries; i++)
    {
        ULONG DrivePartition = i + 1;
        PBRFR_PARTITION_ENTRY Entry = &XboxPartitions[i];

        PGENERIC_DISK_PARTITION DiskPartition;
        LARGE_INTEGER StartOffset;
        LARGE_INTEGER EndOffset;

        StartOffset.QuadPart = Entry->SectorCountBeforePartition * SectorSize;
        EndOffset.QuadPart = StartOffset.QuadPart + (Entry->PartitionSectorCount * SectorSize);

        Status = GenericDiskAddDiskPartition(GenericDisk, DrivePartition, StartOffset, EndOffset, &DiskPartition);
        if (Status != ESUCCESS)
        {
            ERR("Cannot add disk partition\n");
            goto result;
        }

        DiskPartition->IsBootable = DrivePartition == FATX_DATA_PARTITION;
    }

result:
    return Status;
}
