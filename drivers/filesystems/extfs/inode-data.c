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
    PEXTFS_EXTENT *ExtentList, PEXT4_EXTENT_HEADER ExtentHeader, PULONGLONG FileSizeInBlocks)
{
    NTSTATUS Status;
    LARGE_INTEGER Offset;
    PEXTFS_EXTENT FirstEntry = *ExtentList, CurrentEntry = *ExtentList, OldEntry;

    if (ExtentHeader->Depth > EXT4_EXTENT_MAX_LEVEL ||
        ExtentHeader->Magic != EXT4_EXTENT_HEADER_MAGIC)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ULONG Level = ExtentHeader->Depth;
    ULONG Entries = ExtentHeader->Entries;

    DPRINT1("Level: %d\n", Level);
    DPRINT1("Entries: %d\n", Entries);

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
            CurrentEntry->LengthInBytes = CurrentEntry->Length * VolumeExtension->BlockSize;
            (*FileSizeInBlocks) -= Length;

            Extent++;
        }
    }
    else
    {
        PEXT4_EXTENT_IDX Extent = (PVOID)&ExtentHeader[1];
        PVOID TempBuffer = ExAllocatePoolWithTag(NonPagedPool, VolumeExtension->BlockSize, EXTFS_TAG_BUFFER);
        if (!TempBuffer)
        {
            return STATUS_NO_MEMORY;
        }

        while (Entries-- && *FileSizeInBlocks)
        {
            Offset.QuadPart = (((ULONGLONG)Extent->LeafHigh << 32) | (ULONGLONG)Extent->Leaf) * VolumeExtension->BlockSize;
            Status = ExtfsDiskRead(VolumeExtension, TempBuffer, Offset, VolumeExtension->BlockSize);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
                return Status;
            }

            Status = ExtfsGenerateExtentListWithExtents(VolumeExtension, &CurrentEntry, TempBuffer, FileSizeInBlocks);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
                return Status;
            }
            if (!FirstEntry)
                FirstEntry = CurrentEntry, *ExtentList = CurrentEntry;
            CurrentEntry = ExtfsGetFinalExtentEntry(*ExtentList);

            Extent++;
        }

        ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
    }

    return STATUS_SUCCESS;
}

PEXTFS_INODE_CONTEXT
ExtfsPrepareInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Inode)
{
    NTSTATUS Status = STATUS_NO_MEMORY;
    ULONGLONG FileSize = ExtfsGetInodeSize(Inode);
    ULONGLONG FileSizeInBlocks = (FileSize + (VolumeExtension->BlockSize - 1)) / VolumeExtension->BlockSize;

    DPRINT1("ExtfsPrepareInodeContext(0x%p, 0x%p)\n", VolumeExtension, Inode);

    PEXTFS_INODE_CONTEXT InodeContext =
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*InodeContext), EXTFS_TAG_INODE_CONTEXT);
    if (!InodeContext)
    {
        return NULL;
    }
    RtlZeroMemory(InodeContext, sizeof(*InodeContext));

    if (Inode->Flags & EXT4_INODE_FLAG_EXTENTS)
    {
        Status = ExtfsGenerateExtentListWithExtents(VolumeExtension, 
                                                    &InodeContext->ExtentList, &Inode->ExtentHeader, &FileSizeInBlocks);
    }

    if (!NT_SUCCESS(Status))
    {
        ExtfsDestroyExtentList(InodeContext->ExtentList);
        ExFreePoolWithTag(InodeContext, EXTFS_TAG_INODE_CONTEXT);
        return NULL;
    }

    InodeContext->VolumeExtension = VolumeExtension;
    InodeContext->FileSize = FileSize;
    InodeContext->IsDirectory = (Inode->Mode & EXT_S_IFMT) == EXT_S_IFDIR;
    InodeContext->Inode = *Inode;

    return InodeContext;
}

VOID
ExtfsReleaseInodeContext(PEXTFS_INODE_CONTEXT InodeContext)
{
    if (!InodeContext)
        return;

    ExtfsDestroyExtentList(InodeContext->ExtentList);
    if (InodeContext->CacheBuffer)
        ExFreePoolWithTag(InodeContext->CacheBuffer, EXTFS_TAG_BUFFER);
    ExFreePoolWithTag(InodeContext, EXTFS_TAG_INODE_CONTEXT);
}

PEXTFS_INODE_CONTEXT ExtfsReadInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, ULONG Index)
{
    PEXT_INODE Inode = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Inode), EXTFS_TAG_BUFFER);
    NTSTATUS Status;
    PEXTFS_INODE_CONTEXT InodeContext;

    if (!Inode)
    {
        return NULL;
    }

    Status = ExtfsReadInode(VolumeExtension, Inode, Index);
    if (!NT_SUCCESS(Status))
    {
        return NULL;
    }

    InodeContext = ExtfsPrepareInodeContext(VolumeExtension, Inode);
    if (!InodeContext)
        return NULL;

    InodeContext->InodeNum = Index;
    return InodeContext;
}

ULONG 
ExtfsReadInodeData(
    PEXTFS_INODE_CONTEXT InodeContext,
    PCHAR Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    NTSTATUS Status;
    PEXTFS_EXTENT CurrentEntry = InodeContext->ExtentList;
    ULONGLONG FileSize = InodeContext->FileSize;
    ULONG BytesToRead = (ULONG)min(FileSize - Offset.QuadPart, (ULONGLONG)Length);
    ULONG BytesRead = 0;
    LARGE_INTEGER DiskOffset;

    while (CurrentEntry)
    {
        if (Offset.QuadPart >= CurrentEntry->LengthInBytes)
        {
            Offset.QuadPart -= CurrentEntry->LengthInBytes;
        }
        else
        {
            ULONG SliceLength = (ULONG)min((ULONGLONG)(BytesToRead - BytesRead), CurrentEntry->LengthInBytes - Offset.QuadPart);

            if (CurrentEntry->BlockInBytes)
            {
                PCHAR TempBuffer = InodeContext->CacheBuffer;
                if (!(TempBuffer &&
                      InodeContext->CacheBufferOffset == CurrentEntry->BlockInBytes &&
                      InodeContext->CacheBufferSize == CurrentEntry->LengthInBytes))
                {
                    if (TempBuffer)
                        ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
                    InodeContext->CacheBuffer = NULL;

                    TempBuffer = ExAllocatePoolWithTag(NonPagedPool, CurrentEntry->LengthInBytes, EXTFS_TAG_BUFFER);
                    if (!TempBuffer)
                    {
                        DPRINT1("Cannot allocate TempBuffer %I64u\n", CurrentEntry->LengthInBytes);
                        return BytesRead;
                    }

                    DiskOffset.QuadPart = CurrentEntry->BlockInBytes;
                    Status = ExtfsDiskRead(InodeContext->VolumeExtension, TempBuffer, DiskOffset, CurrentEntry->LengthInBytes);
                    if (!NT_SUCCESS(Status))
                    {
                        ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
                        return BytesRead;
                    }
                }

                RtlCopyMemory(Buffer, TempBuffer + (ULONG_PTR)Offset.QuadPart, SliceLength);

                InodeContext->CacheBuffer = TempBuffer;
                InodeContext->CacheBufferSize = CurrentEntry->LengthInBytes;
                InodeContext->CacheBufferOffset = CurrentEntry->BlockInBytes;
            }
            else
            {
                RtlZeroMemory(Buffer, SliceLength);
            }

            Buffer += SliceLength;
            BytesRead += SliceLength;

            if (BytesRead >= BytesToRead)
                break;

            Offset.QuadPart = 0;
        }

        CurrentEntry = CurrentEntry->Next;
    }

    return BytesRead;
}
