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

#define GENERIC_DISK_TYPE 'GD  '
#define GENERIC_PARTITION_TYPE 'GP  '

LIST_ENTRY GenericDiskList;

static ARC_STATUS
GenericDiskClose(ULONG FileId)
{
    TRACE("GenericDiskClose\n");
    return ESUCCESS;
}

static ARC_STATUS
GenericDiskGetFileInformation(ULONG FileId, FILEINFORMATION* Information)
{
    PGENERIC_DISK_PARTITION DiskPartition = FsGetDeviceSpecific(FileId);

    Information->StartingAddress.QuadPart = DiskPartition->PartitionStart.QuadPart;
    Information->EndingAddress.QuadPart   = DiskPartition->PartitionEnd.QuadPart;
    Information->CurrentAddress.QuadPart  = DiskPartition->PartitionStart.QuadPart;

    Information->Type = DiskPartition->GenericDisk->FileInformation.Type;

    TRACE("GenericDiskGetFileInformation\n");
    return ESUCCESS;
}

static ARC_STATUS
GenericDiskOpen(CHAR* Path, OPENMODE OpenMode, ULONG* FileId)
{
    UCHAR DriveNumber;
    ULONG DrivePartition;
    ARC_STATUS Status = ESUCCESS;

    Status = DissectArcPath(Path, NULL, &DriveNumber, &DrivePartition) ? ESUCCESS : EINVAL;
    if (Status != ESUCCESS)
    {
        ERR("Cannot dissect ArcPath\n");
        goto result;
    }

    PGENERIC_DISK GenericDisk = DiskFindGenericDisk(DriveNumber);
    Status = GenericDisk ? ESUCCESS : EINVAL;

    if (Status != ESUCCESS)
    {
        ERR("Cannot get GenericDisk\n");
        goto result;
    }

    PGENERIC_DISK_PARTITION DiskPartition = DiskFindGenericPartition(GenericDisk, DrivePartition);
    Status = DiskPartition ? ESUCCESS : EINVAL;

    if (Status != ESUCCESS)
    {
        ERR("Cannot get DiskPartition\n");
        goto result;
    }

    DiskPartition->CurrentFileOffset.QuadPart = 0;

    FsSetDeviceSpecific(*FileId, DiskPartition);

result:
    TRACE("GenericDiskOpen\n");
    return Status;
}

static ARC_STATUS
GenericDiskRead(ULONG FileId, VOID* Buffer, ULONG N, ULONG* Count)
{
    ARC_STATUS Status = ESUCCESS;
    PGENERIC_DISK_PARTITION DiskPartition = FsGetDeviceSpecific(FileId);
    LARGE_INTEGER DiskOffset, DiskOffsetEnd;

    DiskOffset = DiskPartition->PartitionStart;
    DiskOffset.QuadPart += DiskPartition->CurrentFileOffset.QuadPart;

    DiskOffsetEnd = DiskOffset;
    DiskOffsetEnd.QuadPart += N;

    if (!N)
    {
        ERR("Cannot read zero bytes\n");

        Status = EINVAL;
        goto result;
    }

    if (DiskOffset.QuadPart < DiskPartition->PartitionStart.QuadPart ||
        DiskOffset.QuadPart >= DiskPartition->PartitionEnd.QuadPart ||
        DiskOffsetEnd.QuadPart <= DiskOffset.QuadPart ||
        DiskOffsetEnd.QuadPart > DiskPartition->PartitionEnd.QuadPart)
    {
        ERR("Cannot read out of bounds\n");

        Status = EINVAL;
        goto result;
    }

    Status = GenericDiskReadRawData(DiskPartition->GenericDisk, Buffer, DiskOffset, N, Count);

    if (Status == ESUCCESS)
    {
        DiskPartition->CurrentFileOffset.QuadPart += *Count;
    }

result:
    TRACE("GenericDiskRead\n");
    return Status;
}

static ARC_STATUS
GenericDiskSeek(ULONG FileId, LARGE_INTEGER* Position, SEEKMODE SeekMode)
{
    PGENERIC_DISK_PARTITION DiskPartition = FsGetDeviceSpecific(FileId);
    LARGE_INTEGER PartitionOffset = *Position;

    switch (SeekMode)
    {
    case SeekRelative:
        PartitionOffset.QuadPart += DiskPartition->CurrentFileOffset.QuadPart;
        break;

    default:
        break;
    }

    DiskPartition->CurrentFileOffset = PartitionOffset;
    TRACE("GenericDiskSeek\n");
    return ESUCCESS;
}

static const DEVVTBL GenericDiskVtbl =
{
    GenericDiskClose,
    GenericDiskGetFileInformation,
    GenericDiskOpen,
    GenericDiskRead,
    GenericDiskSeek,
};

VOID InitializeGenericDiskList()
{
    InitializeListHead(&GenericDiskList);
}

ARC_STATUS GenericDiskReadRawData(PGENERIC_DISK GenericDisk, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, PULONG ByteCount)
{
    ARC_STATUS Status = ESUCCESS;
    LARGE_INTEGER NewPosition = Offset;

    Status = ArcSeek(GenericDisk->FileId, &NewPosition, SeekAbsolute);
    if (Status != ESUCCESS)
        return Status;

    return ArcRead(GenericDisk->FileId, Buffer, Length, ByteCount);
}

ARC_STATUS 
GenericDiskAddDiskPartition(
    PGENERIC_DISK GenericDisk, ULONG DrivePartition,
    LARGE_INTEGER StartOffset, LARGE_INTEGER EndOffset,
    PGENERIC_DISK_PARTITION *OutDiskPartition)
{
    ARC_STATUS Status = ESUCCESS;

    PGENERIC_DISK_PARTITION DiskPartition = FrLdrHeapAlloc(sizeof(*DiskPartition), GENERIC_PARTITION_TYPE);
    if (!DiskPartition)
    {
        Status = ENOMEM;
        goto result;
    }

    RtlZeroMemory(DiskPartition, sizeof(*DiskPartition));

    InitializeListHead(&DiskPartition->ListEntry);

    DiskPartition->GenericDisk = GenericDisk;

    DiskPartition->DrivePartition = DrivePartition;

    DiskPartition->PartitionStart = StartOffset;
    DiskPartition->PartitionEnd = EndOffset;

    InsertTailList(&GenericDisk->PartitionList, &DiskPartition->ListEntry);

    if (GenericDisk->IsCdrom)
    {
        sprintf(DiskPartition->ArcPath, "%s", GenericDisk->ArcPath);
    }
    else
    {
        sprintf(DiskPartition->ArcPath, "%spartition(%u)", GenericDisk->ArcPath, DiskPartition->DrivePartition);
    }

    TRACE("%s\n", DiskPartition->ArcPath);

    FsRegisterDevice(DiskPartition->ArcPath, &GenericDiskVtbl);

    *OutDiskPartition = DiskPartition;

result:
    return Status;
}

PGENERIC_DISK DiskFindGenericDisk(UCHAR DriveNumber)
{
    PLIST_ENTRY End = &GenericDiskList;
    PLIST_ENTRY Current = End->Flink;

    while (Current != End)
    {
        PGENERIC_DISK GenericDisk = CONTAINING_RECORD(Current, GENERIC_DISK, ListEntry);

        if (GenericDisk->DriveNumber == DriveNumber)
            return GenericDisk;

        Current = Current->Flink;
    }
    
    return NULL;
}

PGENERIC_DISK_PARTITION DiskFindGenericPartition(PGENERIC_DISK GenericDisk, ULONG DrivePartition)
{
    PLIST_ENTRY End = &GenericDisk->PartitionList;
    PLIST_ENTRY Current = End->Flink;

    while (Current != End)
    {
        PGENERIC_DISK_PARTITION GenericDiskPartition = CONTAINING_RECORD(Current, GENERIC_DISK_PARTITION, ListEntry);

        if (GenericDiskPartition->DrivePartition == DrivePartition)
            return GenericDiskPartition;

        Current = Current->Flink;
    }
    
    return NULL;
}

static VOID DiskDestroyGenericDisk(PGENERIC_DISK GenericDisk)
{
    PLIST_ENTRY End = &GenericDisk->PartitionList;
    PLIST_ENTRY Current = End->Flink;

    while (Current != End)
    {
        PLIST_ENTRY Next = Current->Flink;
        PGENERIC_DISK_PARTITION GenericDiskPartition = CONTAINING_RECORD(Current, GENERIC_DISK_PARTITION, ListEntry);

        FsUnregisterDevice(GenericDiskPartition->ArcPath);
        RemoveEntryList(&GenericDiskPartition->ListEntry);
        FrLdrHeapFree(GenericDiskPartition, GENERIC_PARTITION_TYPE);

        Current = Next;
    }

    RemoveEntryList(&GenericDisk->ListEntry);
    FrLdrHeapFree(GenericDisk, GENERIC_DISK_TYPE);
}

static VOID RemovePartitionPart(PCHAR ArcPath)
{
    PCHAR Offset = strstr(ArcPath, "partition");
    if (Offset != NULL)
    {
        *Offset = '\0';
    }
}

static BOOLEAN IsCdrom(PCHAR ArcPath)
{
    return strstr(ArcPath, "cdrom") != NULL;
}

ARC_STATUS DiskProcessMbr(PGENERIC_DISK GenericDisk);
ARC_STATUS DiskProcessGpt(PGENERIC_DISK GenericDisk);
ARC_STATUS DiskProcessBrfr(PGENERIC_DISK GenericDisk);

ARC_STATUS DiskConfigureGenericDisk(PCHAR ArcPath)
{
    UCHAR DriveNumber;
    ULONG DrivePartition;

    FILEINFORMATION FileInformation;

    GEOMETRY DiskGeometry;
    PARTITION_STYLE PartitionStyle;
    PGENERIC_DISK GenericDisk = NULL;

    ULONG FileId;
    ARC_STATUS Status = ESUCCESS;

    Status = ArcOpen(ArcPath, OpenReadOnly, &FileId);
    if (Status != ESUCCESS)
    {
        ERR("Cannot open Arc disk\n");
        goto result;
    }

    Status = ArcGetFileInformation(FileId, &FileInformation);
    if (Status != ESUCCESS)
    {
        ERR("Cannot get Arc disk information\n");
        goto result;
    }

    Status = DissectArcPath(ArcPath, NULL, &DriveNumber, &DrivePartition) ? ESUCCESS : EINVAL;
    if (Status != ESUCCESS)
    {
        ERR("Cannot dissect Arc disk path\n");
        goto result;
    }

    Status = MachDiskGetDriveGeometry(DriveNumber, &DiskGeometry) ? ESUCCESS : EINVAL;
    if (Status != ESUCCESS)
    {
        ERR("Cannot get Arc disk geometry\n");
        goto result;
    }

    PartitionStyle = DiskGetDrivePartitionStyle(DriveNumber);

    GenericDisk = FrLdrHeapAlloc(sizeof(*GenericDisk), GENERIC_DISK_TYPE);
    if (!GenericDisk)
    {
        ERR("Cannot allocate GenericDisk\n");

        Status = ENOMEM;
        goto result;
    }

    RtlZeroMemory(GenericDisk, sizeof(*GenericDisk));

    InitializeListHead(&GenericDisk->ListEntry);

    GenericDisk->DiskGeometry = DiskGeometry;
    GenericDisk->FileInformation = FileInformation;

    sprintf(GenericDisk->ArcPath, "%s", ArcPath);
    RemovePartitionPart(GenericDisk->ArcPath);

    GenericDisk->FileId = FileId;

    GenericDisk->IsCdrom = IsCdrom(GenericDisk->ArcPath);

    GenericDisk->DriveNumber = DriveNumber;

    GenericDisk->PartitionStyle = PartitionStyle;

    InitializeListHead(&GenericDisk->PartitionList);

    switch (PartitionStyle)
    {
    case PARTITION_STYLE_MBR:
        Status = DiskProcessMbr(GenericDisk);
        break;

    case PARTITION_STYLE_GPT:
        Status = DiskProcessGpt(GenericDisk);
        break;

    case PARTITION_STYLE_BRFR:
        Status = DiskProcessBrfr(GenericDisk);
        break;

    default:
        ERR("PartitionStyle (%u) is not implemented\n", PartitionStyle);

        Status = EINVAL;
        break;
    }

result:
    if (Status == ESUCCESS)
    {
        InsertTailList(&GenericDiskList, &GenericDisk->ListEntry);
    }
    else
    {
        if (GenericDisk)
            DiskDestroyGenericDisk(GenericDisk);
    }

    return Status;
}
