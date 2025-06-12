#include "extfs.h"

VOID ExtfsDestroyExtentList(PEXTFS_EXTENT ExtentList)
{
    while (ExtentList)
    {
        PEXTFS_EXTENT NextEntry = ExtentList->Next;

        ExFreePoolWithTag(ExtentList, EXTFS_TAG_EXTENT_LIST);

        ExtentList = NextEntry;
    }
}

PEXTFS_EXTENT ExtfsGetFinalExtentEntry(PEXTFS_EXTENT ExtentList)
{
    while (ExtentList && ExtentList->Next)
    {
        ExtentList = ExtentList->Next;
    }

    return ExtentList;
}

NTSTATUS ExtfsGenerateExtentListWithExtents(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PEXTFS_EXTENT *ExtentList, PEXT_INODE Inode, PULONGLONG FileSizeInBlocks)
{
    LARGE_INTEGER Offset;
    PEXT4_EXTENT_HEADER ExtentHeader = &Inode->ExtentHeader;
    PEXTFS_EXTENT FirstEntry = *ExtentList, CurrentEntry = *ExtentList, OldEntry;

    UNREFERENCED_PARAMETER(Offset);

    if (ExtentHeader->Depth > EXT4_EXTENT_MAX_LEVEL ||
        ExtentHeader->Magic != EXT4_EXTENT_HEADER_MAGIC)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ULONG Level = ExtentHeader->Depth;
    ULONG Entries = ExtentHeader->Entries;

    DbgPrint("Level: %d\n", Level);
    DbgPrint("Entries: %d\n", Entries);

    if (Level < 1)
    {
        PEXT4_EXTENT Extent = (PVOID)&ExtentHeader[1];

        while (Entries-- && *FileSizeInBlocks)
        {
            BOOLEAN SparseExtent = (Extent->Length > EXT4_EXTENT_MAX_LENGTH);
            ULONG Length = SparseExtent ? (Extent->Length - EXT4_EXTENT_MAX_LENGTH) : Extent->Length; 
            ULONGLONG CurrentBlock = SparseExtent ? 0 : (((ULONGLONG)Extent->StartHigh << 32) | (ULONGLONG)Extent->Start);

            if (!FirstEntry)
            {
                FirstEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*FirstEntry), EXTFS_TAG_EXTENT_LIST);
                if (!FirstEntry)
                {
                    return STATUS_NO_MEMORY;
                }
                RtlZeroMemory(FirstEntry, sizeof(*FirstEntry));
                *ExtentList = CurrentEntry = FirstEntry;
            }
            else
            {
                OldEntry = CurrentEntry;
                CurrentEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*CurrentEntry), EXTFS_TAG_EXTENT_LIST);
                if (!CurrentEntry)
                {
                    return STATUS_NO_MEMORY;
                }
                RtlZeroMemory(CurrentEntry, sizeof(*CurrentEntry));

                OldEntry->Next = CurrentEntry;
                CurrentEntry->Prev = OldEntry;
            }

            CurrentEntry->Block = CurrentBlock;
            CurrentEntry->Length = Length;
            CurrentEntry->BlockInBytes = CurrentEntry->Block * VolumeExtension->BlockSize;
            CurrentEntry->Length = CurrentEntry->Length * VolumeExtension->BlockSize;
            (*FileSizeInBlocks) -= Length;

            Extent++;
        }
    }
    else
    {
        DbgPrint("More than one level extent not implemented\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    return STATUS_SUCCESS;
}

NTSTATUS 
ExtfsReadInodeData(
    PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Inode,
    PVOID Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    NTSTATUS Status;
    PEXTFS_EXTENT ExtentList = NULL, CurrentEntry;
    ULONGLONG FileSize = ExtfsGetInodeSize(Inode);
    ULONGLONG FileSizeInBlocks = (FileSize + VolumeExtension->BlockSize) / VolumeExtension->BlockSize;
    ULONGLONG CurrentFileSizeInBlocks = FileSizeInBlocks;

    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(CurrentEntry);

    if (Inode->Flags & EXT4_INODE_FLAG_EXTENTS)
    {
        Status = ExtfsGenerateExtentListWithExtents(VolumeExtension, &ExtentList, Inode, &CurrentFileSizeInBlocks);
        if (!NT_SUCCESS(Status))
        {
            ExtfsDestroyExtentList(ExtentList);
            return Status;
        }
    }

    CurrentEntry = ExtentList;

    

    while (TRUE);
    return STATUS_SUCCESS;
}
