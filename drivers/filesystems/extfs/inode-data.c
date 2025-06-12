#include "extfs.h"

#define EXTFS_INODE_READ_CACHE_SIZE_LIMIT (1 * 1024 * 1024)

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

NTSTATUS ExtfsGenerateExtentListWithPointers(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PEXTFS_EXTENT *ExtentList, PULONG Pointers, ULONG PointersEntries, PULONGLONG FileSizeInBlocks,
    ULONG Level)
{
    NTSTATUS Status = STATUS_SUCCESS;
    LARGE_INTEGER Offset;
    PEXTFS_EXTENT FirstEntry = *ExtentList, CurrentEntry = ExtfsGetFinalExtentEntry(*ExtentList), OldEntry;

    if (Level < 1)
    {
        while (PointersEntries-- && (*FileSizeInBlocks))
        {
            ULONG Block = *Pointers++;
            LONGLONG BlockDiff;
            BOOLEAN IsSparseExtent = !Block;

            if (!FirstEntry)
            {
                FirstEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*FirstEntry), EXTFS_TAG_EXTENT_LIST);
                if (!FirstEntry)
                {
                    Status = STATUS_NO_MEMORY;
                    break;
                }
                RtlZeroMemory(FirstEntry, sizeof(*FirstEntry));
                *ExtentList = CurrentEntry = FirstEntry;

                CurrentEntry->Block = Block;
                CurrentEntry->BlockInBytes = CurrentEntry->Block * VolumeExtension->BlockSize;
                CurrentEntry->IsSparse = IsSparseExtent;
                BlockDiff = 1;
            }
            else
            {
                BlockDiff = Block - (CurrentEntry->Block + (CurrentEntry->Length - 1));

                if (BlockDiff != 1)
                {
                    OldEntry = CurrentEntry;
                    CurrentEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*CurrentEntry), EXTFS_TAG_EXTENT_LIST);
                    if (!CurrentEntry)
                    {
                        Status = STATUS_NO_MEMORY;
                        break;
                    }
                    RtlZeroMemory(CurrentEntry, sizeof(*CurrentEntry));

                    OldEntry->Next = CurrentEntry;
                    CurrentEntry->Prev = OldEntry;

                    CurrentEntry->Block = Block;
                    CurrentEntry->BlockInBytes = CurrentEntry->Block * VolumeExtension->BlockSize;
                    CurrentEntry->IsSparse = IsSparseExtent;
                    BlockDiff = 1;
                }
            }

            CurrentEntry->Length += BlockDiff;
            CurrentEntry->LengthInBytes = CurrentEntry->Length * VolumeExtension->BlockSize;
            (*FileSizeInBlocks) -= 1;
        }
    }
    else
    {
        PVOID TempBlock = ExAllocatePoolWithTag(NonPagedPool, VolumeExtension->BlockSize, EXTFS_TAG_BUFFER);
        if (!TempBlock)
        {
            Status = STATUS_NO_MEMORY;
            goto result;
        }

        while (PointersEntries-- && (*FileSizeInBlocks))
        {
            ULONG Block = *Pointers++;

            Offset.QuadPart = (ULONGLONG)Block * VolumeExtension->BlockSize;
            if (!(ULONGLONG)Offset.QuadPart)
            {
                RtlZeroMemory(TempBlock, VolumeExtension->BlockSize);
            }
            else
            {
                Status = ExtfsDiskRead(VolumeExtension, TempBlock, Offset, VolumeExtension->BlockSize, TRUE);
                if (!NT_SUCCESS(Status))
                {
                    ExFreePoolWithTag(TempBlock, EXTFS_TAG_BUFFER);
                    break;
                }
            }

            Status = ExtfsGenerateExtentListWithPointers(VolumeExtension,
                                                         ExtentList,
                                                         TempBlock,
                                                         VolumeExtension->PointersPerBlock,
                                                         FileSizeInBlocks, Level - 1);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(TempBlock, EXTFS_TAG_BUFFER);
                break;
            }
        }

        ExFreePoolWithTag(TempBlock, EXTFS_TAG_BUFFER);
    }

result:
    return Status;
}

NTSTATUS ExtfsGenerateExtentListWithExtents(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PEXTFS_EXTENT *ExtentList, PEXT4_EXTENT_HEADER ExtentHeader, PULONGLONG FileSizeInBlocks)
{
    NTSTATUS Status;
    LARGE_INTEGER Offset;
    PEXTFS_EXTENT FirstEntry = *ExtentList, CurrentEntry = ExtfsGetFinalExtentEntry(*ExtentList), OldEntry;

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

            CurrentEntry->IsSparse = SparseExtent;
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
            Status = ExtfsDiskRead(VolumeExtension, TempBuffer, Offset, VolumeExtension->BlockSize, TRUE);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
                return Status;
            }

            Status = ExtfsGenerateExtentListWithExtents(VolumeExtension, ExtentList, TempBuffer, FileSizeInBlocks);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
                return Status;
            }

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
    LARGE_INTEGER DiskOffset;
    ULONG Index;

    DPRINT1("ExtfsPrepareInodeContext(0x%p, 0x%p)\n", VolumeExtension, Inode);

    PEXTFS_INODE_CONTEXT InodeContext =
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*InodeContext), EXTFS_TAG_INODE_CONTEXT);
    if (!InodeContext)
    {
        return NULL;
    }
    RtlZeroMemory(InodeContext, sizeof(*InodeContext));

    PVOID TempBuffer = ExAllocatePoolWithTag(NonPagedPool, VolumeExtension->BlockSize, EXTFS_TAG_BUFFER);
    if (!TempBuffer)
    {
        ExFreePoolWithTag(InodeContext, EXTFS_TAG_INODE_CONTEXT);
        return NULL;
    }

    if (Inode->Flags & EXT4_INODE_FLAG_EXTENTS)
    {
        Status = ExtfsGenerateExtentListWithExtents(VolumeExtension, 
                                                    &InodeContext->ExtentList,
                                                    &Inode->ExtentHeader,
                                                    &FileSizeInBlocks);
    }
    else
    {
        ULONG DirectBlocks = sizeof(Inode->Blocks.DirectBlocks) / sizeof(ULONG);
        ULONG TotalBlocks = sizeof(Inode->TotalBlocks) / sizeof(ULONG);
        Status = ExtfsGenerateExtentListWithPointers(VolumeExtension,
                                                     &InodeContext->ExtentList,
                                                     Inode->Blocks.DirectBlocks,
                                                     DirectBlocks,
                                                     &FileSizeInBlocks, 0);
        if (!NT_SUCCESS(Status))
        {
            goto skip;
        }

        for (Index = DirectBlocks; Index < TotalBlocks; Index++)
        {
            DiskOffset.QuadPart = (ULONGLONG)Inode->TotalBlocks[Index] * VolumeExtension->BlockSize;

            if (!(ULONGLONG)DiskOffset.QuadPart)
            {
                RtlZeroMemory(TempBuffer, VolumeExtension->BlockSize);
            }
            else
            {
                Status = ExtfsDiskRead(VolumeExtension, TempBuffer, DiskOffset, VolumeExtension->BlockSize, TRUE);
                if (!NT_SUCCESS(Status))
                {
                    goto skip;
                }
            }

            Status = ExtfsGenerateExtentListWithPointers(VolumeExtension,
                                                         &InodeContext->ExtentList,
                                                         TempBuffer,
                                                         VolumeExtension->PointersPerBlock,
                                                         &FileSizeInBlocks, Index - DirectBlocks);
            if (!NT_SUCCESS(Status))
            {
                goto skip;
            }
        }
    }
skip:

    ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);

    if (!NT_SUCCESS(Status))
    {
        ExtfsDestroyExtentList(InodeContext->ExtentList);
        ExFreePoolWithTag(InodeContext, EXTFS_TAG_INODE_CONTEXT);
        return NULL;
    }

    InodeContext->VolumeExtension = VolumeExtension;
    InodeContext->FileSize = FileSize;
    InodeContext->IsDirectory = (Inode->Mode & EXT_S_IFMT) == EXT_S_IFDIR;
    InodeContext->IsReparsePoint = (Inode->Mode & EXT_S_IFMT) == EXT_S_IFLNK;
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
    ULONG BytesToRead = (ULONG)min(FileSize - min(Offset.QuadPart, FileSize), (ULONGLONG)Length);
    ULONG BytesRead = 0;
    LARGE_INTEGER DiskOffset;

    DPRINT("ExtfsReadInodeData(0x%p, 0x%p, %I64u, %u)\n", InodeContext, Buffer, Offset.QuadPart, Length);

    if (InodeContext->IsReparsePoint && InodeContext->FileSize <= sizeof(InodeContext->Inode.SymLink))
    {
        RtlCopyMemory(Buffer, &InodeContext->Inode.SymLink[(ULONG)Offset.QuadPart], BytesToRead);
        return BytesToRead;
    }

    while (CurrentEntry)
    {
        if (Offset.QuadPart >= CurrentEntry->LengthInBytes)
        {
            Offset.QuadPart -= CurrentEntry->LengthInBytes;
        }
        else
        {
            ULONGLONG RemainderBytes = BytesToRead - BytesRead;
            ULONGLONG RemainderLengthInBytes = CurrentEntry->LengthInBytes - Offset.QuadPart;
            ULONG SliceLength = (ULONG)min(RemainderBytes, RemainderLengthInBytes);

            if (!CurrentEntry->IsSparse)
            {
                LONGLONG CacheIterations = (SliceLength - 1) / EXTFS_INODE_READ_CACHE_SIZE_LIMIT;
                ULONGLONG InitialOffset =
                    (Offset.QuadPart / InodeContext->VolumeExtension->BlockSize) * InodeContext->VolumeExtension->BlockSize;
                ULONGLONG CacheCurrentOffset = 0;

                Offset.QuadPart -= InitialOffset;

                while (CacheIterations >= 0)
                {
                    ULONG CacheSizeLimited =
                        (ULONG)min((CurrentEntry->LengthInBytes - InitialOffset) - CacheCurrentOffset, EXTFS_INODE_READ_CACHE_SIZE_LIMIT);
                    ULONGLONG CacheAbsoluteOffset =
                        CurrentEntry->BlockInBytes + InitialOffset + CacheCurrentOffset;
                    ULONG CacheSliceLength =
                        (ULONG)min(BytesToRead - BytesRead, CacheSizeLimited - (ULONG)Offset.QuadPart);

                    PCHAR TempBuffer = InodeContext->CacheBuffer;
                    if (!(TempBuffer &&
                          InodeContext->CacheBufferOffset == CacheAbsoluteOffset))
                    {
                        if (!TempBuffer ||
                            InodeContext->CacheBufferSize != CacheSizeLimited)
                        {
                            if (TempBuffer)
                                ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
                            InodeContext->CacheBuffer = NULL;

                            TempBuffer = ExAllocatePoolWithTag(NonPagedPool, CacheSizeLimited, EXTFS_TAG_BUFFER);
                            if (!TempBuffer)
                            {
                                DPRINT1("Cannot allocate TempBuffer %u\n", CacheSizeLimited);
                                return BytesRead;
                            }
                        }

                        DiskOffset.QuadPart = CacheAbsoluteOffset;

                        Status = ExtfsDiskRead(InodeContext->VolumeExtension, TempBuffer, DiskOffset, CacheSizeLimited, TRUE);
                        if (!NT_SUCCESS(Status))
                        {
                            InodeContext->CacheBuffer = NULL;
                            ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
                            return BytesRead;
                        }
                    }

                    RtlCopyMemory(Buffer, TempBuffer + (ULONG_PTR)Offset.QuadPart, CacheSliceLength);

                    InodeContext->CacheBuffer = TempBuffer;
                    InodeContext->CacheBufferSize = CacheSizeLimited;
                    InodeContext->CacheBufferOffset = CacheAbsoluteOffset;

                    Buffer += CacheSliceLength;
                    BytesRead += CacheSliceLength;

                    if (BytesRead >= BytesToRead)
                        break;

                    Offset.QuadPart = 0;

                    CacheCurrentOffset += EXTFS_INODE_READ_CACHE_SIZE_LIMIT;
                    CacheIterations--;
                }
            }
            else
            {
                RtlZeroMemory(Buffer, SliceLength);
                Buffer += SliceLength;
                BytesRead += SliceLength;
            }

            if (BytesRead >= BytesToRead)
                break;

            Offset.QuadPart = 0;
        }

        CurrentEntry = CurrentEntry->Next;
    }

    return BytesRead;
}
