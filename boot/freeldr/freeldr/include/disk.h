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

#include <reactos/rosioctl.h>
#include <partition_table/mbr.h>

/* FreeLoader-specific disk geometry structure */
typedef struct _GEOMETRY
{
    ULONG Cylinders;       ///< Number of cylinders on the disk
    ULONG Heads;           ///< Number of heads on the disk
    ULONG SectorsPerTrack; ///< Number of sectors per track
    ULONG BytesPerSector;  ///< Number of bytes per sector
    ULONGLONG Sectors;     ///< Total number of disk sectors/LBA blocks
} GEOMETRY, *PGEOMETRY;

typedef struct GENERIC_DISK
{
    LIST_ENTRY ListEntry;

    GEOMETRY DiskGeometry;
    FILEINFORMATION FileInformation;

    ULONG FileId;
    CHAR ArcPath[MAX_PATH];

    BOOLEAN IsCdrom;

    ULONG DriveNumber;
    PARTITION_STYLE PartitionStyle;

    LIST_ENTRY PartitionList;
} GENERIC_DISK, *PGENERIC_DISK;

typedef struct GENERIC_DISK_PARTITION
{
    LIST_ENTRY ListEntry;

    CHAR ArcPath[MAX_PATH];

    PGENERIC_DISK GenericDisk;

    ULONG DrivePartition;

    BOOLEAN IsBootable;
    LARGE_INTEGER PartitionStart, PartitionEnd;

    LARGE_INTEGER CurrentFileOffset;
} GENERIC_DISK_PARTITION, *PGENERIC_DISK_PARTITION;

/*
 * Partition type defines (of PSDK)
 */
#define PARTITION_ENTRY_UNUSED          0x00      // Entry unused
#define PARTITION_FAT_12                0x01      // 12-bit FAT entries
#define PARTITION_XENIX_1               0x02      // Xenix
#define PARTITION_XENIX_2               0x03      // Xenix
#define PARTITION_FAT_16                0x04      // 16-bit FAT entries
#define PARTITION_EXTENDED              0x05      // Extended partition entry
#define PARTITION_HUGE                  0x06      // Huge partition MS-DOS V4
#define PARTITION_IFS                   0x07      // IFS Partition
#define PARTITION_OS2BOOTMGR            0x0A      // OS/2 Boot Manager/OPUS/Coherent swap
#define PARTITION_FAT32                 0x0B      // FAT32
#define PARTITION_FAT32_XINT13          0x0C      // FAT32 using extended int13 services
#define PARTITION_XINT13                0x0E      // Win95 partition using extended int13 services
#define PARTITION_XINT13_EXTENDED       0x0F      // Same as type 5 but uses extended int13 services
#define PARTITION_NTFS                  0x17      // NTFS
#define PARTITION_PREP                  0x41      // PowerPC Reference Platform (PReP) Boot Partition
#define PARTITION_LDM                   0x42      // Logical Disk Manager partition
#define PARTITION_UNIX                  0x63      // Unix
#define VALID_NTFT                      0xC0      // NTFT uses high order bits
#define PARTITION_NTFT                  0x80      // NTFT partition
#define PARTITION_GPT                   0xEE      // GPT protective partition
#ifdef __REACTOS__
#define PARTITION_OLD_LINUX             0x43
#define PARTITION_LINUX                 0x83
#endif

///////////////////////////////////////////////////////////////////////////////
//
// PC x86/64 BIOS Disk Functions (pcdisk.c)
//
///////////////////////////////////////////////////////////////////////////////
#if defined(__i386__) || defined(_M_AMD64)
VOID __cdecl DiskStopFloppyMotor(VOID);
#endif // defined __i386__ || defined(_M_AMD64)

/* Buffer for disk reads (hwdisk.c) */
extern PVOID DiskReadBuffer;
extern SIZE_T DiskReadBufferSize;


/* ARC path of the boot drive and partition */
extern CCHAR FrLdrBootPath[MAX_PATH];


///////////////////////////////////////////////////////////////////////////////
//
// Disk Management Functions
//
///////////////////////////////////////////////////////////////////////////////

/*
 * Disk devices helpers (disk.c)
 */

LONG
DiskReportError(
    _In_ BOOLEAN bShowError);

VOID
DiskError(
    _In_ PCSTR ErrorString,
    _In_ ULONG ErrorCode);

extern PCSTR
DiskGetErrorCodeString(
    _In_ ULONG ErrorCode);

/* See fs.h */
struct tagDEVVTBL;

ARC_STATUS
DiskInitialize(
    _In_ UCHAR DriveNumber, // FIXME: Arch-specific
    _In_ PCSTR DeviceName,
    _In_ CONFIGURATION_TYPE DeviceType,
    _In_ const struct tagDEVVTBL* FuncTable,
    _Out_opt_ PULONG pChecksum,
    _Out_opt_ PULONG pSignature,
    _Out_opt_ PBOOLEAN pValidPartitionTable);


/*
 * Fixed Disk Partition Management Functions (partition.c)
 */

ARC_STATUS GenericDiskReadRawData(PGENERIC_DISK GenericDisk, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, PULONG ByteCount);
ARC_STATUS 
GenericDiskAddDiskPartition(
    PGENERIC_DISK GenericDisk, ULONG DrivePartition,
    LARGE_INTEGER StartOffset, LARGE_INTEGER EndOffset,
    PGENERIC_DISK_PARTITION *OutDiskPartition);

VOID InitializeGenericDiskList();
ARC_STATUS DiskConfigureGenericDisk(PCHAR ArcPath);

PGENERIC_DISK DiskFindGenericDisk(UCHAR DriveNumber);
PGENERIC_DISK_PARTITION DiskFindGenericPartition(PGENERIC_DISK GenericDisk, ULONG DrivePartition);

PARTITION_STYLE DiskGetDrivePartitionStyle(IN UCHAR DriveNumber);

VOID
DiskDetectPartitionType(
    IN UCHAR DriveNumber);

BOOLEAN
DiskGetBootPartitionNumber(
    IN UCHAR DriveNumber,
    OUT PULONG BootPartition);

BOOLEAN
DiskGetBootPartitionNumberBySize(
    IN UCHAR DriveNumber,
    OUT PULONG BootPartition,
    IN LARGE_INTEGER PartitionSize,
    IN ULONGLONG ToleranceSize);


/*
 * SCSI support (disk/scsiport.c)
 */
ULONG LoadBootDeviceDriver(VOID);

PCCHAR FrLdrGetBootPath(VOID);
UCHAR FrldrGetBootDrive(VOID);
ULONG FrldrGetBootPartition(VOID);
