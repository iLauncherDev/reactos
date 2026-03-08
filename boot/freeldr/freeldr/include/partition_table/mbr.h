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

#pragma once

#include <pshpack1.h>

/*
 * Define the structure of a partition table entry
 */
typedef struct _PARTITION_TABLE_ENTRY
{
    UCHAR   BootIndicator;              // 0x00 - non-bootable partition,
                                        // 0x80 - bootable partition (one partition only)
    UCHAR   StartHead;                  // Beginning head number
    UCHAR   StartSector;                // Beginning sector (2 high bits of cylinder #)
    UCHAR   StartCylinder;              // Beginning cylinder# (low order bits of cylinder #)
    UCHAR   SystemIndicator;            // System indicator
    UCHAR   EndHead;                    // Ending head number
    UCHAR   EndSector;                  // Ending sector (2 high bits of cylinder #)
    UCHAR   EndCylinder;                // Ending cylinder# (low order bits of cylinder #)
    ULONG   SectorCountBeforePartition; // Number of sectors preceding the partition
    ULONG   PartitionSectorCount;       // Number of sectors in the partition
} PARTITION_TABLE_ENTRY, *PPARTITION_TABLE_ENTRY;

/*
 * Define the structure of the master boot record
 */
typedef struct _MASTER_BOOT_RECORD
{
    UCHAR   MasterBootRecordCodeAndData[0x1b8]; /* 0x000 */
    ULONG   Signature;                          /* 0x1B8 */
    USHORT  Reserved;                           /* 0x1BC */
    PARTITION_TABLE_ENTRY   PartitionTable[4];  /* 0x1BE */
    USHORT  MasterBootRecordMagic;              /* 0x1FE */
} MASTER_BOOT_RECORD, *PMASTER_BOOT_RECORD;

#include <poppack.h>
