#include <extfs.h>

#define EXTFS_INODE_READ_CACHE_SIZE_LIMIT (16 * 1024)
#define EXTFS_INODE_CONTEXT_COUNT_LIMIT (8)

VOID
ExtfsAddAllocationEntryToList(
    PEXTFS_INODE_CONTEXT InodeContext,
    PEXTFS_ALLOCATION_ENTRY AllocationEntry);
VOID
ExtfsReleaseAllocationEntriesFromList(PEXTFS_INODE_CONTEXT InodeContext);
VOID ExtfsWriteInodeContext(PEXTFS_INODE_CONTEXT InodeContext);
VOID
ExtfsDestroyInodeContext(PEXTFS_INODE_CONTEXT InodeContext);

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

VOID ExtfsDumpExtentList(PEXTFS_EXTENT ExtentList)
{
    while (ExtentList)
    {
        DPRINT1("ExtentList->Length = %I64u\n", ExtentList->Length);

        ExtentList = ExtentList->Next;
    }
}

VOID ExtfsTruncateExtents(PEXTFS_INODE_CONTEXT InodeContext, ULONGLONG TruncateSizeInBlocks)
{
    ULONGLONG BlockSize = InodeContext->VolumeExtension->BlockSize;
    PEXTFS_EXTENT CurrentEntry = InodeContext->ExtentList;

    while (CurrentEntry)
    {
        if (TruncateSizeInBlocks >= CurrentEntry->Length)
        {
            TruncateSizeInBlocks -= CurrentEntry->Length;
        }
        else
        {
            PEXTFS_EXTENT NextEntry = CurrentEntry->Next;
            PEXTFS_EXTENT PrevEntry = CurrentEntry->Prev;

            CurrentEntry->Length = TruncateSizeInBlocks;
            CurrentEntry->LengthInBytes = CurrentEntry->Length * BlockSize;

            if (!CurrentEntry->Length)
            {
                if (PrevEntry)
                    PrevEntry->Next = NULL;
                else
                    InodeContext->ExtentList = NULL;

                ExtfsDestroyExtentList(CurrentEntry);
            }
            else
            {
                CurrentEntry->Next = NULL;
                ExtfsDestroyExtentList(NextEntry);
            }
            break;
        }

        CurrentEntry = CurrentEntry->Next;
    }
}

NTSTATUS ExtfsGenerateExtentListWithPointers(
    PEXTFS_INODE_CONTEXT InodeContext,
    PEXTFS_EXTENT *ExtentList, PULONG Pointers, ULONG PointersEntries, PULONGLONG FileSizeInBlocks,
    ULONG Level)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = InodeContext->VolumeExtension;
    PEXTFS_ALLOCATION_MANAGER AllocationManager = &VolumeExtension->AllocationManager;
    LARGE_INTEGER Offset;
    PEXTFS_EXTENT FirstEntry = *ExtentList, CurrentEntry = ExtfsGetFinalExtentEntry(*ExtentList), OldEntry;

    UNREFERENCED_PARAMETER(AllocationManager);

    if (Level < 1)
    {
        while (PointersEntries-- && (*FileSizeInBlocks))
        {
            ULONG Block = ReadFieldLE((Pointers++)[0]);
            LONGLONG BlockDiff;
            BOOLEAN IsSparseExtent = !Block;

            // ExtfsAddAllocationEntryToList(InodeContext,
            //                               ExtfsAllocationManagerAcquireAllocationEntryByBlock(AllocationManager, Block));
            // IsSparseExtent |= !ExtfsAllocationManagerCheckBlock(AllocationManager, Block);

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
                if (CurrentEntry->IsSparse && IsSparseExtent)
                    BlockDiff = 1;

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

                    CurrentEntry->LogicalBlock = OldEntry->LogicalBlock + OldEntry->Length;
                    CurrentEntry->LogicalBlockInBytes = CurrentEntry->LogicalBlock * VolumeExtension->BlockSize;

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
            ULONG Block = ReadFieldLE((Pointers++)[0]);
            BOOLEAN IsValid = TRUE;

            // ExtfsAddAllocationEntryToList(InodeContext,
            //                               ExtfsAllocationManagerAcquireAllocationEntryByBlock(AllocationManager, Block));
            // IsValid = ExtfsAllocationManagerCheckBlock(AllocationManager, Block);

            Offset.QuadPart = (ULONGLONG)Block * VolumeExtension->BlockSize;
            if (!(ULONGLONG)Offset.QuadPart || !IsValid)
            {
                RtlZeroMemory(TempBlock, VolumeExtension->BlockSize);
            }
            else
            {
                Status = ExtfsFastDiskRead(VolumeExtension, TempBlock, Offset, VolumeExtension->BlockSize, 0);
                if (!NT_SUCCESS(Status))
                {
                    break;
                }
            }

            Status = ExtfsGenerateExtentListWithPointers(InodeContext,
                                                         ExtentList,
                                                         TempBlock,
                                                         VolumeExtension->PointersPerBlock,
                                                         FileSizeInBlocks, Level - 1);
            if (!NT_SUCCESS(Status))
            {
                break;
            }

            InodeContext->AdditionalBlocksCount++;
        }

        ExFreePoolWithTag(TempBlock, EXTFS_TAG_BUFFER);
    }

result:
    return Status;
}

NTSTATUS ExtfsGenerateExtentListWithExtents(
    PEXTFS_INODE_CONTEXT InodeContext,
    PEXTFS_EXTENT *ExtentList, PEXT4_EXTENT_HEADER ExtentHeader, PULONGLONG FileSizeInBlocks)
{
    NTSTATUS Status;
    LARGE_INTEGER Offset;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = InodeContext->VolumeExtension;
    PEXTFS_EXTENT FirstEntry = *ExtentList, CurrentEntry = ExtfsGetFinalExtentEntry(*ExtentList), OldEntry;

    if (ExtentHeader->Depth > EXT4_EXTENT_MAX_LEVEL ||
        ExtentHeader->Magic != EXT4_EXTENT_HEADER_MAGIC)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ULONG Level = ReadFieldLE(ExtentHeader->Depth);
    ULONG Entries = ReadFieldLE(ExtentHeader->Entries);

    DPRINT1("Level: %d\n", Level);
    DPRINT1("Entries: %d\n", Entries);

    if (Level < 1)
    {
        PEXT4_EXTENT Extent = (PVOID)&ExtentHeader[1];

        while (Entries-- && *FileSizeInBlocks)
        {
            BOOLEAN SparseExtent = (ReadFieldLE(Extent->Length) > EXT4_EXTENT_MAX_LENGTH);
            ULONG Length = SparseExtent ? (ReadFieldLE(Extent->Length) - EXT4_EXTENT_MAX_LENGTH) : ReadFieldLE(Extent->Length); 
            ULONGLONG CurrentBlock = SparseExtent ? 0 : ((ReadFieldLE(Extent->StartHigh) << 32) | ReadFieldLE(Extent->Start));

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
            Offset.QuadPart = ((ReadFieldLE(Extent->LeafHigh) << 32) | ReadFieldLE(Extent->Leaf)) * VolumeExtension->BlockSize;
            Status = ExtfsFastDiskRead(VolumeExtension, TempBuffer, Offset, VolumeExtension->BlockSize, 0);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
                return Status;
            }

            Status = ExtfsGenerateExtentListWithExtents(InodeContext, ExtentList, TempBuffer, FileSizeInBlocks);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
                return Status;
            }

            Extent++, InodeContext->AdditionalBlocksCount++;
        }

        ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
    }

    return STATUS_SUCCESS;
}

NTSTATUS ExtfsAllocPointer(PEXTFS_INODE_CONTEXT InodeContext, PVOID BlockBuffer, PULONGLONG Block, BOOLEAN AvoidAllocation)
{
    NTSTATUS Status = STATUS_SUCCESS;
    LARGE_INTEGER DiskOffset;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = InodeContext->VolumeExtension;
    PEXTFS_ALLOCATION_MANAGER AllocationManager = &VolumeExtension->AllocationManager;
    ULONGLONG OutputBlock;
    BOOLEAN IsValid = ExtfsAllocationManagerCheckBlock(AllocationManager, *Block);

    if ((!*Block || !IsValid) && !AvoidAllocation)
    {
        if (!InodeContext->CacheSecondAllocationEntry)
        {
            if (!InodeContext->CacheSecondAllocationGroup)
            {
                InodeContext->CacheSecondAllocationEntry = ExtfsAllocationManagerAcquireAllocationEntryByFreeBlock(&VolumeExtension->AllocationManager);
                ExtfsAddAllocationEntryToList(InodeContext, InodeContext->CacheSecondAllocationEntry);

                if (InodeContext->CacheSecondAllocationEntry)
                    InodeContext->CacheSecondAllocationGroup = InodeContext->CacheSecondAllocationEntry->Group;
            }
            else
            {
                InodeContext->CacheSecondAllocationEntry = ExtfsAllocationManagerAcquireAllocationEntryByGroup(&VolumeExtension->AllocationManager,
                                                                                                               InodeContext->CacheSecondAllocationGroup);
                ExtfsAddAllocationEntryToList(InodeContext, InodeContext->CacheSecondAllocationEntry);
            }
        }

        if (InodeContext->CacheSecondAllocationEntry)
        {
            OutputBlock = ExtfsAllocationManagerGetAllocationEntryFreeBlock(InodeContext->CacheSecondAllocationEntry, TRUE);
            if (!OutputBlock)
            {
                InodeContext->CacheSecondAllocationEntry = ExtfsAllocationManagerAcquireAllocationEntryByFreeBlock(&VolumeExtension->AllocationManager);
                ExtfsAddAllocationEntryToList(InodeContext, InodeContext->CacheSecondAllocationEntry);

                InodeContext->CacheSecondAllocationGroup = 0;
                if (InodeContext->CacheSecondAllocationEntry)
                {
                    OutputBlock = ExtfsAllocationManagerGetAllocationEntryFreeBlock(InodeContext->CacheSecondAllocationEntry, TRUE);
                    InodeContext->CacheSecondAllocationGroup = InodeContext->CacheSecondAllocationEntry->Group;
                }
            }
        }

        if (InodeContext->CacheSecondAllocationEntry)
            *Block = OutputBlock, InodeContext->AdditionalBlocksCount++;
        else
            *Block = 0, Status = STATUS_DISK_FULL;

        RtlZeroMemory(BlockBuffer, VolumeExtension->BlockSize);
    }
    else
    {
        DiskOffset.QuadPart = *Block * VolumeExtension->BlockSize;
        if (!DiskOffset.QuadPart)
        {
            Status = STATUS_UNSUCCESSFUL;
            RtlZeroMemory(BlockBuffer, VolumeExtension->BlockSize);
        }
        else
        {
            if (!IsValid)
            {
                *Block = 0;
                Status = STATUS_UNSUCCESSFUL;
            }
            else
            {
                Status = ExtfsFastDiskRead(VolumeExtension, BlockBuffer, DiskOffset, VolumeExtension->BlockSize, 0);
            }
        }
    }

    return Status;
}

NTSTATUS ExtfsFlushPointer(PEXTFS_INODE_CONTEXT InodeContext, PVOID BlockBuffer, PULONGLONG Block)
{
    NTSTATUS Status = STATUS_SUCCESS;
    LARGE_INTEGER DiskOffset;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = InodeContext->VolumeExtension;
    PEXTFS_ALLOCATION_MANAGER AllocationManager = &VolumeExtension->AllocationManager;
    BOOLEAN IsValid = ExtfsAllocationManagerCheckBlock(AllocationManager, *Block);

    DiskOffset.QuadPart = *Block * VolumeExtension->BlockSize;
    if (!DiskOffset.QuadPart || !IsValid)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        Status = ExtfsFastDiskWrite(VolumeExtension, BlockBuffer, DiskOffset, VolumeExtension->BlockSize, 0);
    }

    return Status;
}

NTSTATUS ExtfsFreePointer(PEXTFS_INODE_CONTEXT InodeContext, PULONGLONG Block)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = InodeContext->VolumeExtension;
    PEXTFS_ALLOCATION_MANAGER AllocationManager = &VolumeExtension->AllocationManager;
    PEXTFS_ALLOCATION_ENTRY AllocationEntry = ExtfsAllocationManagerAcquireAllocationEntryByBlock(AllocationManager, *Block);
    ExtfsAddAllocationEntryToList(InodeContext, AllocationEntry);

    if (!*Block)
    {
        DPRINT1("Warning null block\n");
        goto result;
    }

    if (!AllocationEntry)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

result:
    if (AllocationEntry)
        ExtfsAllocationManagerClearAllocationEntryBlock(AllocationEntry, *Block);
    return Status;
}

BOOLEAN ExtfsGetInodeDataPhysicalBlock(PEXTFS_INODE_CONTEXT InodeContext, ULONGLONG LogicalBlock, PULONGLONG OutputBlock)
{
    PEXTFS_EXTENT CurrentEntry = InodeContext->ExtentList;
    ULONGLONG CurrentBlock = 0;

    while (CurrentEntry)
    {
        ULONGLONG CurrentLength = CurrentEntry->Length;

        if (LogicalBlock >= CurrentBlock &&
            LogicalBlock < CurrentBlock + CurrentLength)
        {
            if (CurrentEntry->IsSparse)
                *OutputBlock = 0;
            else
                *OutputBlock = CurrentEntry->Block + (LogicalBlock - CurrentBlock);

            return TRUE;
        }

        CurrentBlock += CurrentLength;
        CurrentEntry = CurrentEntry->Next;
    }

    return FALSE;
}

NTSTATUS ExtfsFreeInodeDataPointers(
    PEXTFS_INODE_CONTEXT InodeContext,
    ULONG Level, PULONG Pointers, ULONG PointersEntries,
    PULONGLONG CurrentOffset, ULONGLONG StartOffset, ULONGLONG EndOffset,
    PULONGLONG ReleasedPointers)
{
    ULONG Index = 0;
    ULONG PointersPerBlock = PointersEntries;
    ULONG NewPointersPerBlock = 0;
    PULONG Buffer = NULL;
    ULONG BufferEntries = InodeContext->VolumeExtension->PointersPerBlock;
    NTSTATUS Status = STATUS_SUCCESS;

    for (Index = 1; Index < Level; Index++)
    {
        PointersPerBlock *= PointersEntries;
    }

    if (!Level)
    {
        Index = 0;
        while (*CurrentOffset < EndOffset &&
               Index < PointersPerBlock)
        {
            ULONGLONG PhysicalBlock = ReadFieldLE(Pointers[Index]);
            ULONGLONG NextEntry = *CurrentOffset + PointersPerBlock;
            if (NextEntry <= StartOffset)
            {
                *CurrentOffset = NextEntry;
                break;
            }

            if (*CurrentOffset < StartOffset)
            {
                Index++, (*CurrentOffset)++;
                continue;
            }

            Status = ExtfsFreePointer(InodeContext, &PhysicalBlock);
            if (!NT_SUCCESS(Status))
            {
                goto result;
            }
            Index++, (*CurrentOffset)++, (*ReleasedPointers)++;
        }
    }
    else
    {
        NewPointersPerBlock = BufferEntries;
        for (Index = 2; Index < Level; Index++)
        {
            NewPointersPerBlock *= BufferEntries;
        }

        Index = 0;
        Buffer = ExAllocatePoolWithTag(NonPagedPool, InodeContext->VolumeExtension->BlockSize, EXTFS_TAG_BUFFER);
        if (!Buffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto result;
        }

        while (*CurrentOffset < EndOffset &&
               Index < PointersPerBlock)
        {
            ULONGLONG CurrentReleasedPointers = 0;
            ULONGLONG PhysicalBlock = ReadFieldLE(Pointers[Index]);
            ULONGLONG NextEntry1 = *CurrentOffset + PointersPerBlock;
            ULONGLONG NextEntry2 = *CurrentOffset + NewPointersPerBlock;

            if (NextEntry1 <= StartOffset)
            {
                *CurrentOffset = NextEntry1;
                break;
            }

            if (NextEntry2 <= StartOffset)
            {
                *CurrentOffset = NextEntry2;
                Index++;
                continue;
            }

            Status = ExtfsAllocPointer(InodeContext, Buffer, &PhysicalBlock, TRUE);
            if (!NT_SUCCESS(Status))
            {
                goto result;
            }

            if (PhysicalBlock)
            {
                Status = ExtfsFreeInodeDataPointers(
                    InodeContext,
                    Level - 1, Buffer, BufferEntries,
                    CurrentOffset, StartOffset, EndOffset,
                    &CurrentReleasedPointers
                );
                if (!NT_SUCCESS(Status))
                {
                    goto result;
                }

                if (CurrentReleasedPointers == BufferEntries)
                {
                    ExtfsFreePointer(InodeContext, &PhysicalBlock);
                    (*ReleasedPointers)++;
                }
            }
            else
            {
                (*ReleasedPointers)++;
            }

            Index++;
        }
    }

result:
    if (Buffer)
        ExFreePoolWithTag(Buffer, EXTFS_TAG_BUFFER);
    return Status;
}

NTSTATUS ExtfsGenerateInodeDataPointers(
    PEXTFS_INODE_CONTEXT InodeContext,
    ULONG Level, PULONG Pointers, ULONG PointersEntries,
    PULONGLONG CurrentOffset, ULONGLONG StartOffset, ULONGLONG EndOffset,
    BOOLEAN IsSparse)
{
    ULONG Index = 0;
    ULONG PointersPerBlock = PointersEntries;
    ULONG NewPointersPerBlock = 0;
    PULONG Buffer = NULL;
    ULONG BufferEntries = InodeContext->VolumeExtension->PointersPerBlock;
    NTSTATUS Status = STATUS_SUCCESS;

    for (Index = 1; Index < Level; Index++)
    {
        PointersPerBlock *= PointersEntries;
    }

    if (!Level)
    {
        Index = 0;
        while (*CurrentOffset < EndOffset &&
               Index < PointersPerBlock)
        {
            ULONGLONG PhysicalBlock = 0;
            ULONGLONG NextEntry = *CurrentOffset + PointersPerBlock;
            if (NextEntry <= StartOffset)
            {
                *CurrentOffset = NextEntry;
                break;
            }

            if (*CurrentOffset < StartOffset)
            {
                Index++, (*CurrentOffset)++;
                continue;
            }

            if (!IsSparse)
                ASSERT(ExtfsGetInodeDataPhysicalBlock(InodeContext, *CurrentOffset, &PhysicalBlock));

            WriteFieldLE(Pointers[Index], (ULONG)PhysicalBlock);
            Index++, (*CurrentOffset)++;
        }
    }
    else
    {
        NewPointersPerBlock = BufferEntries;
        for (Index = 2; Index < Level; Index++)
        {
            NewPointersPerBlock *= BufferEntries;
        }

        Index = 0;
        Buffer = ExAllocatePoolWithTag(NonPagedPool, InodeContext->VolumeExtension->BlockSize, EXTFS_TAG_BUFFER);
        if (!Buffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto result;
        }

        while (*CurrentOffset < EndOffset &&
               Index < PointersPerBlock)
        {
            ULONGLONG PhysicalBlock = 0;
            ULONGLONG NextEntry1 = *CurrentOffset + PointersPerBlock;
            ULONGLONG NextEntry2 = *CurrentOffset + NewPointersPerBlock;

            if (NextEntry1 <= StartOffset)
            {
                *CurrentOffset = NextEntry1;
                break;
            }

            if (NextEntry2 <= StartOffset)
            {
                *CurrentOffset = NextEntry2;
                Index++;
                continue;
            }

            if (!IsSparse)
            {
                Status = ExtfsAllocPointer(InodeContext, Buffer, &PhysicalBlock, FALSE);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("Cannot allocate pointer\n");
                    goto result;
                }

                Status = ExtfsGenerateInodeDataPointers(
                    InodeContext,
                    Level - 1, Buffer, BufferEntries,
                    CurrentOffset, StartOffset, EndOffset,
                    IsSparse
                );
                ASSERT(NT_SUCCESS(Status));

                ExtfsFlushPointer(InodeContext, Buffer, &PhysicalBlock);
            }
            else
            {
                (*CurrentOffset) += NewPointersPerBlock;
            }

            WriteFieldLE(Pointers[Index], (ULONG)PhysicalBlock);
            Index++;
        }
    }

result:
    if (Buffer)
        ExFreePoolWithTag(Buffer, EXTFS_TAG_BUFFER);
    return Status;
}

NTSTATUS ExtfsTruncatePointersWithFileSize(
    PEXTFS_INODE_CONTEXT InodeContext)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = InodeContext->VolumeExtension;
    ULONG Index;

    ULONGLONG PointersPerBlock = VolumeExtension->PointersPerBlock;
    ULONGLONG PointersPerBlockPowered = 1;

    ULONG BlockSize = VolumeExtension->BlockSize;

    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);

    ULONG DirectBlocks = sizeof(InodeContext->Inode.Blocks.DirectBlocks) / sizeof(ULONG);
    ULONG TotalBlocks = sizeof(InodeContext->Inode.TotalBlocks) / sizeof(ULONG);

    ULONGLONG CurrentOffset = 0, ReleasedPointers;
    ULONGLONG StartBlock = (InodeContext->FileSize + (BlockSize - 1)) / BlockSize;
    ULONGLONG EndBlock = (InodeContext->OldFileSize + (BlockSize - 1)) / BlockSize;
    ULONG MaxDirectBlocksEntries;
    PULONG Level1Buffer = ExAllocatePoolWithTag(NonPagedPool, BlockSize, EXTFS_TAG_BUFFER);

    MaxDirectBlocksEntries = sizeof(InodeContext->Inode.Blocks.DirectBlocks) / sizeof(ULONG);

    if (!Level1Buffer)
        goto result;

    if (CurrentOffset < EndBlock)
    {
        ReleasedPointers = 0;
        ExtfsFreeInodeDataPointers(
            InodeContext,
            0, (PULONG)&InodeContext->Inode.Blocks.DirectBlocks, MaxDirectBlocksEntries,
            &CurrentOffset, StartBlock, EndBlock,
            &ReleasedPointers
        );
    }

    for (Index = DirectBlocks; Index < TotalBlocks; Index++)
    {
        ULONG Level = Index - DirectBlocks;

        PointersPerBlockPowered *= PointersPerBlock;

        if (CurrentOffset < EndBlock &&
            CurrentOffset + PointersPerBlockPowered >= StartBlock)
        {
            ULONGLONG TempBlock = ReadFieldLE(InodeContext->Inode.TotalBlocks[Index]);
            ExtfsAllocPointer(InodeContext, Level1Buffer, &TempBlock, TRUE);

            if (TempBlock)
            {
                ReleasedPointers = 0;
                ExtfsFreeInodeDataPointers(
                    InodeContext,
                    Level, Level1Buffer, PointersPerBlock,
                    &CurrentOffset, StartBlock, EndBlock,
                    &ReleasedPointers
                );

                if (ReleasedPointers == PointersPerBlock)
                {
                    ExtfsFreePointer(InodeContext, &TempBlock);
                }
            }
        }
        else
        {
            CurrentOffset += PointersPerBlockPowered;
        }
    }

    goto result;
result:
    if (Level1Buffer)
        ExFreePoolWithTag(Level1Buffer, EXTFS_TAG_BUFFER);
    ExReleaseResourceLite(&InodeContext->InodeLock);
    return Status;
}

NTSTATUS ExtfsGeneratePointersWithExtentList(
    PEXTFS_INODE_CONTEXT InodeContext, ULONGLONG StartOffset, ULONGLONG EndOffset, BOOLEAN IsSparse)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = InodeContext->VolumeExtension;
    ULONG Index;

    ULONGLONG PointersPerBlock = VolumeExtension->PointersPerBlock;
    ULONGLONG PointersPerBlockPowered = 1;

    ULONG BlockSize = VolumeExtension->BlockSize;

    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);

    ULONG DirectBlocks = sizeof(InodeContext->Inode.Blocks.DirectBlocks) / sizeof(ULONG);
    ULONG TotalBlocks = sizeof(InodeContext->Inode.TotalBlocks) / sizeof(ULONG);

    ULONGLONG StartBlock = StartOffset / BlockSize;
    ULONGLONG EndBlock = (EndOffset + (BlockSize - 1)) / BlockSize;

    ULONGLONG CurrentOffset = 0;
    ULONG MaxDirectBlocksEntries;
    PULONG Level1Buffer = ExAllocatePoolWithTag(NonPagedPool, BlockSize, EXTFS_TAG_BUFFER);

    MaxDirectBlocksEntries = sizeof(InodeContext->Inode.Blocks.DirectBlocks) / sizeof(ULONG);

    if (!Level1Buffer)
        goto result;

    if (CurrentOffset < EndBlock)
    {
        ExtfsGenerateInodeDataPointers(
            InodeContext,
            0, (PULONG)&InodeContext->Inode.Blocks.DirectBlocks, MaxDirectBlocksEntries,
            &CurrentOffset, StartBlock, EndBlock,
            IsSparse
        );
    }

    for (Index = DirectBlocks; Index < TotalBlocks; Index++)
    {
        ULONG Level = Index - DirectBlocks;

        PointersPerBlockPowered *= PointersPerBlock;

        if (CurrentOffset < EndBlock &&
            CurrentOffset + PointersPerBlockPowered >= StartBlock)
        {
            ULONGLONG TempBlock = InodeContext->Inode.TotalBlocks[Index];

            ExtfsAllocPointer(InodeContext, Level1Buffer, &TempBlock, FALSE);
            WriteFieldLE(InodeContext->Inode.TotalBlocks[Index], TempBlock);

            ExtfsGenerateInodeDataPointers(
                InodeContext,
                Level, Level1Buffer, PointersPerBlock,
                &CurrentOffset, StartBlock, EndBlock,
                IsSparse
            );

            ExtfsFlushPointer(InodeContext, Level1Buffer, &TempBlock);
        }
        else
        {
            CurrentOffset += PointersPerBlockPowered;
        }
    }

    goto result;
result:
    if (Level1Buffer)
        ExFreePoolWithTag(Level1Buffer, EXTFS_TAG_BUFFER);
    ExReleaseResourceLite(&InodeContext->InodeLock);
    return Status;
}

ULONGLONG
ExtfsGetAllocationSizeByExtents(PEXTFS_EXTENT ExtentList)
{
    ULONGLONG AllocationSize = 0;

    while (ExtentList)
    {
        if (!ExtentList->IsSparse)
            AllocationSize += ExtentList->LengthInBytes;
        ExtentList = ExtentList->Next;
    }

    return AllocationSize;
}

ULONGLONG
ExtfsGetFileSizeByExtents(PEXTFS_EXTENT ExtentList)
{
    ULONGLONG FileSize = 0;

    while (ExtentList)
    {
        FileSize += ExtentList->LengthInBytes;
        ExtentList = ExtentList->Next;
    }

    return FileSize;
}

NTSTATUS
ExtfsUpdateInodeContextExtents(PEXTFS_INODE_CONTEXT InodeContext)
{
    ULONG Index;
    LARGE_INTEGER DiskOffset;
    NTSTATUS Status = STATUS_SUCCESS;
    PEXT_INODE Inode = &InodeContext->Inode;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = InodeContext->VolumeExtension;
    ULONGLONG FileSizeInBlocks = (InodeContext->FileSize + (VolumeExtension->BlockSize - 1)) / VolumeExtension->BlockSize;
    PVOID TempBuffer = ExAllocatePoolWithTag(NonPagedPool, VolumeExtension->BlockSize, EXTFS_TAG_BUFFER);
    if (!TempBuffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    ExtfsDestroyExtentList(InodeContext->ExtentList);
    InodeContext->ExtentList = NULL;
    InodeContext->AdditionalBlocksCount = 0;

    if (InodeContext->IsUsingExtents)
    {
        Status = ExtfsGenerateExtentListWithExtents(InodeContext, 
                                                    &InodeContext->ExtentList,
                                                    &Inode->ExtentHeader,
                                                    &FileSizeInBlocks);
    }
    else
    {
        ULONG DirectBlocks = sizeof(Inode->Blocks.DirectBlocks) / sizeof(ULONG);
        ULONG TotalBlocks = sizeof(Inode->TotalBlocks) / sizeof(ULONG);
        Status = ExtfsGenerateExtentListWithPointers(InodeContext,
                                                     &InodeContext->ExtentList,
                                                     Inode->Blocks.DirectBlocks,
                                                     DirectBlocks,
                                                     &FileSizeInBlocks, 0);
        if (!NT_SUCCESS(Status))
        {
            goto result;
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
                Status = ExtfsFastDiskRead(VolumeExtension, TempBuffer, DiskOffset, VolumeExtension->BlockSize, 0);
                if (!NT_SUCCESS(Status))
                {
                    goto result;
                }
            }

            Status = ExtfsGenerateExtentListWithPointers(InodeContext,
                                                         &InodeContext->ExtentList,
                                                         TempBuffer,
                                                         VolumeExtension->PointersPerBlock,
                                                         &FileSizeInBlocks, Index - DirectBlocks);
            if (!NT_SUCCESS(Status))
            {
                goto result;
            }
        }
    }

result:
    if (TempBuffer)
        ExFreePoolWithTag(TempBuffer, EXTFS_TAG_BUFFER);
    return Status;
}

PEXTFS_INODE_CONTEXT
ExtfsPrepareInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Inode)
{
    NTSTATUS Status = STATUS_NO_MEMORY;
    ULONGLONG FileSize = ExtfsGetInodeSize(Inode);

    LONGLONG CreationTime = !Inode->ExtraInodeSize ? -1 : UnixTimeToWindowsTime(ReadFieldLE(Inode->CRtime));
    LONGLONG LastAccessTime = UnixTimeToWindowsTime(ReadFieldLE(Inode->Atime));
    LONGLONG LastWriteTime = UnixTimeToWindowsTime(ReadFieldLE(Inode->Mtime));
    LONGLONG ChangeTime = UnixTimeToWindowsTime(ReadFieldLE(Inode->Ctime));

    DPRINT("ExtfsPrepareInodeContext(0x%p, 0x%p)\n", VolumeExtension, Inode);

    PEXTFS_INODE_CONTEXT InodeContext =
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*InodeContext), EXTFS_TAG_INODE_CONTEXT);
    if (!InodeContext)
    {
        goto error;
    }
    RtlZeroMemory(InodeContext, sizeof(*InodeContext));

    ExtfsInitListEntry(&InodeContext->ListEntry, InodeContext);
    ExtfsInitListEntry(&InodeContext->CacheAllocationEntries, InodeContext);

    InodeContext->ReferenceCount = 1;

    InodeContext->CreationTime = CreationTime;
    InodeContext->LastAccessTime = LastAccessTime;
    InodeContext->LastWriteTime = LastWriteTime;
    InodeContext->ChangeTime = ChangeTime;

    InodeContext->VolumeExtension = VolumeExtension;
    InodeContext->FileSize = InodeContext->OldFileSize = FileSize;
    InodeContext->IsDirectory = (ReadFieldLE(Inode->Mode) & EXT_S_IFMT) == EXT_S_IFDIR;
    InodeContext->IsReparsePoint = (ReadFieldLE(Inode->Mode) & EXT_S_IFMT) == EXT_S_IFLNK;
    InodeContext->Inode = *Inode;

    InodeContext->IsUsingExtents = !!(ReadFieldLE(Inode->Flags) & EXT4_INODE_FLAG_EXTENTS);

    ExInitializeResourceLite(&InodeContext->CacheAllocationEntriesLock);
    ExInitializeResourceLite(&InodeContext->InodeLock);
    ExInitializeResourceLite(&InodeContext->InodeDataLock);
    ExInitializeResourceLite(&InodeContext->DirectoryDataLock);
    ExInitializeResourceLite(&InodeContext->ReferenceCountLock);

    if (!InodeContext->IsReparsePoint || InodeContext->FileSize > sizeof(InodeContext->Inode.SymLink))
    {
        Status = ExtfsUpdateInodeContextExtents(InodeContext);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Cannot get extent list\n");

            goto error;
        }
    }

    return InodeContext;

error:
    if (InodeContext)
    {
        ExtfsDestroyInodeContext(InodeContext);
    }

    return NULL;
}

VOID
ExtfsFlushInodeContext(PEXTFS_INODE_CONTEXT InodeContext)
{
    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);
    ExAcquireResourceExclusiveLite(&InodeContext->InodeDataLock, TRUE);
    ExAcquireResourceExclusiveLite(&InodeContext->DirectoryDataLock, TRUE);

    if (!InodeContext->AvoidInodeFlush)
        ExtfsWriteInodeContext(InodeContext);
    ExtfsReleaseAllocationEntriesFromList(InodeContext);

    ExReleaseResourceLite(&InodeContext->DirectoryDataLock);
    ExReleaseResourceLite(&InodeContext->InodeDataLock);
    ExReleaseResourceLite(&InodeContext->InodeLock);
}

PEXTFS_INODE_CONTEXT ExtfsFindExistingInodeContextByInodeNum(PEXTFS_VOLUME_EXTENSION VolumeExtension, ULONGLONG InodeNum)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;
    PEXTFS_INODE_CONTEXT OutputInodeContext = NULL;

    ExAcquireResourceExclusiveLite(&VolumeExtension->InodeContextListLock, TRUE);

    EndListEntry = &VolumeExtension->InodeContextList;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_INODE_CONTEXT CurrentInodeContext = ExtfsGetListEntryStructure(CurrentListEntry);

        if (CurrentInodeContext->InodeNum == InodeNum)
        {
            CurrentInodeContext->ReferenceCount++;
            CurrentInodeContext->AvoidInodeFlush = FALSE;
            OutputInodeContext = CurrentInodeContext;
            goto result;
        }

        CurrentListEntry = CurrentListEntry->Next;
    }

result:
    ExReleaseResourceLite(&VolumeExtension->InodeContextListLock);
    return OutputInodeContext;
}

VOID ExtfsAddInodeContextToList(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_INODE_CONTEXT InodeContext)
{
    ExAcquireResourceExclusiveLite(&VolumeExtension->InodeContextListLock, TRUE);

    ExtfsInsertTailList(&VolumeExtension->InodeContextList, &InodeContext->ListEntry);

    VolumeExtension->InodeContextCount++;
    ExReleaseResourceLite(&VolumeExtension->InodeContextListLock);
}

VOID ExtfsRemoveInodeContextFromList(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_INODE_CONTEXT InodeContext)
{
    ExAcquireResourceExclusiveLite(&VolumeExtension->InodeContextListLock, TRUE);

    ExtfsRemoveEntryList(&InodeContext->ListEntry);

    VolumeExtension->InodeContextCount--;
    ExReleaseResourceLite(&VolumeExtension->InodeContextListLock);
}

VOID
ExtfsDestroyInodeContext(PEXTFS_INODE_CONTEXT InodeContext)
{
    DPRINT("ExtfsDestroyInodeContext(0x%p)\n", InodeContext);

    if (!InodeContext)
        return;

    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);
    ExtfsWriteInode(InodeContext->VolumeExtension, &InodeContext->Inode, InodeContext->InodeNum);
    ExReleaseResourceLite(&InodeContext->InodeLock);

    ExtfsRemoveInodeContextFromList(InodeContext->VolumeExtension, InodeContext);
    ExtfsFlushInodeContext(InodeContext);

    ExDeleteResourceLite(&InodeContext->CacheAllocationEntriesLock);
    ExDeleteResourceLite(&InodeContext->InodeLock);
    ExDeleteResourceLite(&InodeContext->InodeDataLock);
    ExDeleteResourceLite(&InodeContext->DirectoryDataLock);
    ExDeleteResourceLite(&InodeContext->ReferenceCountLock);
    ExtfsDestroyExtentList(InodeContext->ExtentList);
    ExFreePoolWithTag(InodeContext, EXTFS_TAG_INODE_CONTEXT);
}

VOID
ExtfsReleaseInodeContext(PEXTFS_INODE_CONTEXT InodeContext)
{
    PEXTFS_VOLUME_EXTENSION VolumeExtension;

    DPRINT("ExtfsReleaseInodeContext(0x%p)\n", InodeContext);

    if (!InodeContext)
        return;

    VolumeExtension = InodeContext->VolumeExtension;
    ExAcquireResourceExclusiveLite(&VolumeExtension->InodeContextListLock, TRUE);

    ASSERT(InodeContext->ReferenceCount > 0);

    if (InodeContext->ReferenceCount > 0)
    {
        InodeContext->ReferenceCount--;
    }

    if (InodeContext->ReferenceCount < 1 &&
        (VolumeExtension->InodeContextCount >= EXTFS_INODE_CONTEXT_COUNT_LIMIT ||
         !InodeContext->IsDirectory))
    {
        ExtfsDestroyInodeContext(InodeContext);
    }

    ExReleaseResourceLite(&VolumeExtension->InodeContextListLock);
}

VOID
ExtfsAutoReleaseInodeContextThread(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;

    ExAcquireResourceExclusiveLite(&VolumeExtension->InodeContextListLock, TRUE);

    EndListEntry = &VolumeExtension->InodeContextList;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_INODE_CONTEXT CurrentInodeContext = ExtfsGetListEntryStructure(CurrentListEntry);
        PEXTFS_LIST_ENTRY NextEntry = CurrentListEntry->Next;

        if (CurrentInodeContext->ReferenceCount < 1)
        {
            DPRINT1("Destroying unreferenced InodeContext (%I64u)\n", CurrentInodeContext->InodeNum);

            ExtfsDestroyInodeContext(CurrentInodeContext);
        }

        CurrentListEntry = NextEntry;
    }

    ExReleaseResourceLite(&VolumeExtension->InodeContextListLock);
}

PEXTFS_INODE_CONTEXT ExtfsReadInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, ULONGLONG Index)
{
    PEXT_INODE Inode = NULL;
    PEXTFS_INODE_CONTEXT InodeContext = NULL;

    DPRINT("ExtfsReadInodeContext(0x%p, %u)\n", VolumeExtension, Index);

    ExAcquireResourceExclusiveLite(&VolumeExtension->InodeContextListLock, TRUE);

    InodeContext = ExtfsFindExistingInodeContextByInodeNum(VolumeExtension, Index);
    if (InodeContext)
        goto result;

    Inode = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Inode), EXTFS_TAG_BUFFER);
    if (!Inode)
        goto result;

    if (!NT_SUCCESS(ExtfsReadInode(VolumeExtension, Inode, Index)))
        goto result;

    InodeContext = ExtfsPrepareInodeContext(VolumeExtension, Inode);
    if (!InodeContext)
    {
        goto result;
    }

    InodeContext->InodeNum = Index;
    ExtfsAddInodeContextToList(VolumeExtension, InodeContext);

result:
    if (Inode)
        ExFreePoolWithTag(Inode, EXTFS_TAG_BUFFER);

    ExReleaseResourceLite(&VolumeExtension->InodeContextListLock);
    return InodeContext;
}

VOID ExtfsResetInodeContextLinkCount(PEXTFS_INODE_CONTEXT InodeContext)
{
    if (!InodeContext)
        return;

    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);

    WriteFieldLE(InodeContext->Inode.LinksCount, 0);

    ExReleaseResourceLite(&InodeContext->InodeLock);
}

VOID ExtfsDecrementInodeContextLinkCount(PEXTFS_INODE_CONTEXT InodeContext)
{
    if (!InodeContext)
        return;

    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);

    ULONGLONG LinksCount = ReadFieldLE(InodeContext->Inode.LinksCount);
    if (LinksCount > 0)
        LinksCount--;

    WriteFieldLE(InodeContext->Inode.LinksCount, LinksCount);

    ExReleaseResourceLite(&InodeContext->InodeLock);
}

VOID ExtfsIncrementInodeContextLinkCount(PEXTFS_INODE_CONTEXT InodeContext)
{
    if (!InodeContext)
        return;

    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);

    ULONGLONG LinksCount = ReadFieldLE(InodeContext->Inode.LinksCount);
    LinksCount++;

    WriteFieldLE(InodeContext->Inode.LinksCount, LinksCount);

    ExReleaseResourceLite(&InodeContext->InodeLock);
}

VOID ExtfsSetInodeContextType(PEXTFS_INODE_CONTEXT InodeContext, UCHAR FileType)
{
    USHORT Imode;
    USHORT Type = 0;

    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);
    Imode = (USHORT)ReadFieldLE(InodeContext->Inode.Mode);

    switch (FileType)
    {
    case EXT_DIR_ENTRY_TYPE_REGULAR:
        Type = EXT_S_IFREG;
        break;
    case EXT_DIR_ENTRY_TYPE_DIRECTORY:
        Type = EXT_S_IFDIR;
        break;
    case EXT_DIR_ENTRY_TYPE_CHARACTER_DEVICE:
        Type = EXT_S_IFCHR;
        break;
    case EXT_DIR_ENTRY_TYPE_BLOCK_DEVICE:
        Type = EXT_S_IFBLK;
        break;
    case EXT_DIR_ENTRY_TYPE_FIFO:
        Type = EXT_S_IFIFO;
        break;
    case EXT_DIR_ENTRY_TYPE_SOCKET:
        Type = EXT_S_IFSOCK;
        break;
    case EXT_DIR_ENTRY_TYPE_SYMBOLIC_LINK:
        Type = EXT_S_IFLNK;
        break;
    }

    Imode &= ~EXT_S_IFMT;
    Imode |= Type;

    InodeContext->IsDirectory = (Imode & EXT_S_IFMT) == EXT_S_IFDIR;
    InodeContext->IsReparsePoint = (Imode & EXT_S_IFMT) == EXT_S_IFLNK;

    WriteFieldLE(InodeContext->Inode.Mode, Imode);
    ExReleaseResourceLite(&InodeContext->InodeLock);
}

VOID ExtfsRemoveInodeContext(PEXTFS_INODE_CONTEXT InodeContext)
{
    if (!InodeContext)
        return;

    PEXTFS_VOLUME_EXTENSION VolumeExtension = InodeContext->VolumeExtension;
    PEXTFS_ALLOCATION_MANAGER AllocationManager = &VolumeExtension->AllocationManager;

    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);
    ExAcquireResourceExclusiveLite(&InodeContext->InodeDataLock, TRUE);

    ULONGLONG LinksCount = ReadFieldLE(InodeContext->Inode.LinksCount);
    if (LinksCount > 0)
        LinksCount--;
    WriteFieldLE(InodeContext->Inode.LinksCount, LinksCount);

    if (LinksCount < 1)
    {
        PEXTFS_ALLOCATION_ENTRY AllocationEntry = ExtfsAllocationManagerAcquireAllocationEntryByInode(AllocationManager, InodeContext->InodeNum);

        ExtfsFlushInodeContext(InodeContext);

        if (AllocationEntry)
        {
            ExtfsAllocationManagerClearAllocationEntryInode(AllocationEntry, InodeContext->InodeNum);
            ExtfsAllocationManagerReleaseAllocationEntry(AllocationEntry);
        }

        ExtfsChangeInodeSize(InodeContext, 0, FALSE);

        InodeContext->AvoidInodeFlush = TRUE;
    }

    ExReleaseResourceLite(&InodeContext->InodeDataLock);
    ExReleaseResourceLite(&InodeContext->InodeLock);
}

PEXTFS_INODE_CONTEXT ExtfsCreateInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    if (VolumeExtension->ReadOnly)
    {
        DPRINT1("Volume is read only\n");
        return NULL;
    }

    PEXTFS_ALLOCATION_MANAGER AllocationManager = &VolumeExtension->AllocationManager;
    PEXTFS_ALLOCATION_ENTRY AllocationEntry = ExtfsAllocationManagerAcquireAllocationEntryByFreeInode(AllocationManager);
    ULONGLONG InodeNum;
    PEXTFS_INODE_CONTEXT InodeContext = NULL;
    if (!AllocationEntry)
        goto result;

    InodeNum = ExtfsAllocationManagerGetAllocationEntryFreeInode(AllocationEntry, TRUE);
    InodeContext = ExtfsReadInodeContext(VolumeExtension, InodeNum);
    if (InodeContext)
    {
        RtlZeroMemory(&InodeContext->Inode, sizeof(InodeContext->Inode));
        InodeContext->Inode.ExtraInodeSize = VolumeExtension->InodeSizeInBytes - EXT_DEFAULT_INODE_SIZE;

        InodeContext->Inode.UID = 1000;
        InodeContext->Inode.GID = 1000;

        ExtfsChangeTime(InodeContext,
                        ExtfsGetSystemTime(),
                        ExtfsGetSystemTime(),
                        0,
                        0);
    }
    else
    {
        ExtfsAllocationManagerClearAllocationEntryInode(AllocationEntry, InodeNum);
    }

result:
    if (AllocationEntry)
        ExtfsAllocationManagerReleaseAllocationEntry(AllocationEntry);
    return InodeContext;
}

ULONGLONG ExtfsGetInodeContextAllocationSize(PEXTFS_INODE_CONTEXT InodeContext)
{
    ULONGLONG Value;
    ExAcquireResourceExclusiveLite(&InodeContext->InodeDataLock, TRUE);

    Value = ExtfsGetAllocationSizeByExtents(InodeContext->ExtentList);

    ExReleaseResourceLite(&InodeContext->InodeDataLock);
    return Value;
}

ULONGLONG ExtfsGetInodeContextFileSize(PEXTFS_INODE_CONTEXT InodeContext)
{
    ULONGLONG Value;
    ExAcquireResourceSharedLite(&InodeContext->InodeLock, TRUE);

    Value = InodeContext->FileSize;

    ExReleaseResourceLite(&InodeContext->InodeLock);
    return Value;
}

ULONG ExtfsGetFileTypeAttribute(PEXTFS_INODE_CONTEXT InodeContext)
{
    ULONG Attribute = 0;

    if (InodeContext->IsDirectory)
    {
        Attribute = FILE_ATTRIBUTE_DIRECTORY;
    }
    else
    {
        if (InodeContext->IsReparsePoint)
        {
            Attribute = FILE_ATTRIBUTE_REPARSE_POINT;
        }
        else
        {
            Attribute = FILE_ATTRIBUTE_ARCHIVE;
        }
    }
    return Attribute;
}

ULONG ExtfsGetFileTypeReparseTag(PEXTFS_INODE_CONTEXT InodeContext)
{
    ULONG ReparseTag = 0;

    if (InodeContext->IsReparsePoint)
    {
        ReparseTag = IO_REPARSE_TAG_SYMLINK;
    }
    return ReparseTag;
}

UCHAR ExtfsGetFileTypeDirEntry(PEXTFS_INODE_CONTEXT InodeContext)
{
    UCHAR FileType = 0;

    if (InodeContext->IsDirectory)
    {
        FileType = EXT_DIR_ENTRY_TYPE_DIRECTORY;
    }
    else
    {
        if (InodeContext->IsReparsePoint)
        {
            FileType = EXT_DIR_ENTRY_TYPE_SYMBOLIC_LINK;
        }
        else
        {
            FileType = EXT_DIR_ENTRY_TYPE_REGULAR;
        }
    }
    return FileType;
}

VOID ExtfsWriteInodeContext(PEXTFS_INODE_CONTEXT InodeContext)
{
    // ExtfsWriteInode(InodeContext->VolumeExtension, &InodeContext->Inode, InodeContext->InodeNum);
}

VOID
ExtfsChangeInodeSize(
    PEXTFS_INODE_CONTEXT InodeContext,
    ULONGLONG InodeSize,
    BOOLEAN IgnoreBlockChange)
{
    DPRINT1("ExtfsChangeInodeSize(0x%p, %I64u)\n", InodeContext, InodeSize);

    if (InodeContext->VolumeExtension->ReadOnly)
    {
        DPRINT1("Volume is read only\n");
        return;
    }

    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);
    ExAcquireResourceExclusiveLite(&InodeContext->InodeDataLock, TRUE);

    ULONGLONG BlockSize = InodeContext->VolumeExtension->BlockSize;
    ULONGLONG OldFileSizeInBlocks = (InodeContext->FileSize + (BlockSize - 1)) / BlockSize;
    ULONGLONG NewFileSizeInBlocks = (InodeSize + (BlockSize - 1)) / BlockSize;
    ULONGLONG BlocksCount;

    InodeContext->OldFileSize = InodeContext->FileSize;
    InodeContext->FileSize = InodeSize;
    ExtfsSetInodeSize(&InodeContext->Inode, InodeSize);

    if (!IgnoreBlockChange)
    {
        if (NewFileSizeInBlocks < OldFileSizeInBlocks)
        {
            InodeContext->AdditionalBlocksCount = 0;
            if (InodeContext->IsUsingExtents)
            {
                DPRINT1("There's no support for truncating extents yet!\n");
                ASSERT(FALSE);
            }
            else
            {
                ExtfsTruncatePointersWithFileSize(InodeContext);
                DPRINT1("Truncated\n");
            }
            ExtfsTruncateExtents(InodeContext, NewFileSizeInBlocks);
        }
        else if (NewFileSizeInBlocks > OldFileSizeInBlocks)
        {
            InodeContext->AdditionalBlocksCount = 0;
            if (ReadFieldLE(InodeContext->Inode.Flags) & EXT4_INODE_FLAG_EXTENTS)
            {
                DPRINT1("Unimplemented extents generation\n");
                ASSERT(FALSE);
            }
            else
            {
                ExtfsGeneratePointersWithExtentList(InodeContext, OldFileSizeInBlocks * BlockSize, NewFileSizeInBlocks * BlockSize, TRUE);
            }
            ExtfsWriteInodeContext(InodeContext);
        }
    }

    BlocksCount = ExtfsGetAllocationSizeByExtents(InodeContext->ExtentList) / EXT_HALF_BLOCK_SIZE;
    BlocksCount += (InodeContext->AdditionalBlocksCount * BlockSize) / EXT_HALF_BLOCK_SIZE;

    WriteFieldLE(InodeContext->Inode.BlocksCount, BlocksCount);
    ExtfsWriteInodeContext(InodeContext);

    ExReleaseResourceLite(&InodeContext->InodeDataLock);
    ExReleaseResourceLite(&InodeContext->InodeLock);
}

VOID ExtfsConvertTime(
    PEXTFS_INODE_CONTEXT InodeContext)
{
    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);

    if (InodeContext->Inode.ExtraInodeSize)
        WriteFieldLE(InodeContext->Inode.CRtime, WindowsTimeToUnixTime(InodeContext->CreationTime));
    WriteFieldLE(InodeContext->Inode.Atime, WindowsTimeToUnixTime(InodeContext->LastAccessTime));
    WriteFieldLE(InodeContext->Inode.Mtime, WindowsTimeToUnixTime(InodeContext->LastWriteTime));
    WriteFieldLE(InodeContext->Inode.Ctime, WindowsTimeToUnixTime(InodeContext->ChangeTime));

    ExtfsWriteInodeContext(InodeContext);
    ExReleaseResourceLite(&InodeContext->InodeLock);
}

VOID
ExtfsGetTime(
    PEXTFS_INODE_CONTEXT InodeContext,
    PLONGLONG CreationTime,
    PLONGLONG LastAccessTime,
    PLONGLONG LastWriteTime,
    PLONGLONG ChangeTime)
{
    ExAcquireResourceSharedLite(&InodeContext->InodeLock, TRUE);

    if (CreationTime)
        *CreationTime = InodeContext->CreationTime;
    if (LastAccessTime)
        *LastAccessTime = InodeContext->LastAccessTime;
    if (LastWriteTime)
        *LastWriteTime = InodeContext->LastWriteTime;
    if (ChangeTime)
        *ChangeTime = InodeContext->ChangeTime;

    ExReleaseResourceLite(&InodeContext->InodeLock);
}

VOID
ExtfsChangeTime(
    PEXTFS_INODE_CONTEXT InodeContext,
    LONGLONG CreationTime,
    LONGLONG LastAccessTime,
    LONGLONG LastWriteTime,
    LONGLONG ChangeTime)
{
    ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);

    if (CreationTime)
        InodeContext->CreationTime = CreationTime;
    if (LastAccessTime)
        InodeContext->LastAccessTime = LastAccessTime;
    if (LastWriteTime)
        InodeContext->LastWriteTime = LastWriteTime;
    if (ChangeTime)
        InodeContext->ChangeTime = ChangeTime;

    ExtfsConvertTime(InodeContext);

    ExReleaseResourceLite(&InodeContext->InodeLock);
}

PEXTFS_CACHE_ALLOCATION_ENTRY
ExtfsFindAllocationEntryFromList(
    PEXTFS_INODE_CONTEXT InodeContext,
    PEXTFS_ALLOCATION_ENTRY AllocationEntry)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;
    PEXTFS_CACHE_ALLOCATION_ENTRY OutputCacheAllocationEntry = NULL;

    ExAcquireResourceExclusiveLite(&InodeContext->CacheAllocationEntriesLock, TRUE);

    EndListEntry = &InodeContext->CacheAllocationEntries;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_CACHE_ALLOCATION_ENTRY CurrentCacheAllocationEntry = ExtfsGetListEntryStructure(CurrentListEntry);

        if (CurrentCacheAllocationEntry->AllocationEntry == AllocationEntry)
        {
            CurrentCacheAllocationEntry->ReferenceCount++;
            OutputCacheAllocationEntry = CurrentCacheAllocationEntry;
            break;
        }

        CurrentListEntry = CurrentListEntry->Next;
    }

    ExReleaseResourceLite(&InodeContext->CacheAllocationEntriesLock);

    return OutputCacheAllocationEntry;
}

VOID
ExtfsAddAllocationEntryToList(
    PEXTFS_INODE_CONTEXT InodeContext,
    PEXTFS_ALLOCATION_ENTRY AllocationEntry)
{
    ExAcquireResourceExclusiveLite(&InodeContext->CacheAllocationEntriesLock, TRUE);

    if (!AllocationEntry)
        goto result;

    PEXTFS_CACHE_ALLOCATION_ENTRY CacheAllocationEntry = ExtfsFindAllocationEntryFromList(InodeContext, AllocationEntry);
    if (CacheAllocationEntry)
        goto result;

    CacheAllocationEntry = ExAllocatePoolWithTag(NonPagedPool,
                                                 sizeof(*CacheAllocationEntry),
                                                 EXTFS_TAG_ALLOCATION_ENTRY);
    if (!CacheAllocationEntry)
    {
        DPRINT1("Cannot allocate CacheAllocationEntry\n");
        goto result;
    }
    RtlZeroMemory(CacheAllocationEntry, sizeof(*CacheAllocationEntry));

    ExtfsInitListEntry(&CacheAllocationEntry->ListEntry, CacheAllocationEntry);

    CacheAllocationEntry->AllocationEntry = AllocationEntry;
    CacheAllocationEntry->ReferenceCount = 1;

    ExtfsInsertTailList(&InodeContext->CacheAllocationEntries, &CacheAllocationEntry->ListEntry);

result:
    ExReleaseResourceLite(&InodeContext->CacheAllocationEntriesLock);
}

VOID
ExtfsReleaseAllocationEntriesFromList(PEXTFS_INODE_CONTEXT InodeContext)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;

    ExAcquireResourceExclusiveLite(&InodeContext->CacheAllocationEntriesLock, TRUE);

    EndListEntry = &InodeContext->CacheAllocationEntries;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_LIST_ENTRY NextEntry = CurrentListEntry->Next;
        PEXTFS_CACHE_ALLOCATION_ENTRY CurrentCacheAllocationEntry = ExtfsGetListEntryStructure(CurrentListEntry);

        ExtfsAllocationManagerReleaseAllocationEntryWithReferenceCount(CurrentCacheAllocationEntry->AllocationEntry,
                                                                       CurrentCacheAllocationEntry->ReferenceCount);
        ExFreePoolWithTag(CurrentCacheAllocationEntry, EXTFS_TAG_ALLOCATION_ENTRY);

        CurrentListEntry = NextEntry;
    }

    ExtfsInitListEntry(&InodeContext->CacheAllocationEntries, InodeContext);

    InodeContext->CacheAllocationEntry = NULL;
    InodeContext->CacheSecondAllocationEntry = NULL;

    ExReleaseResourceLite(&InodeContext->CacheAllocationEntriesLock);
}

ULONG
ExtfsReadInodeData(
    PEXTFS_INODE_CONTEXT InodeContext,
    PCHAR Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTFS_EXTENT CurrentEntry;
    ULONGLONG FileSize, AlignedFileSize;
    ULONGLONG CurrentOffset = Offset.QuadPart;
    ULONG BytesToRead = 0;
    ULONG BytesRead = 0;
    ULONG BlockSize = InodeContext->VolumeExtension->BlockSize;
    LARGE_INTEGER DiskOffset;

    UNREFERENCED_PARAMETER(BlockSize);

    DPRINT("ExtfsReadInodeData(0x%p, 0x%p, %I64u, %u)\n", InodeContext, Buffer, Offset.QuadPart, Length);

    ExAcquireResourceSharedLite(&InodeContext->InodeDataLock, TRUE);

    FileSize = ExtfsGetInodeContextFileSize(InodeContext);
    AlignedFileSize = (FileSize + (ULONGLONG)(BlockSize - 1)) & ~(ULONGLONG)(BlockSize - 1);

    BytesToRead = (ULONG)min(FileSize - min(Offset.QuadPart, FileSize), (ULONGLONG)Length);
    CurrentEntry = InodeContext->ExtentList;

    if (!BytesToRead)
        goto result;

    if (InodeContext->IsReparsePoint && FileSize <= sizeof(InodeContext->Inode.SymLink))
    {
        RtlCopyMemory(Buffer, &InodeContext->Inode.SymLink[(ULONG)Offset.QuadPart], BytesToRead);
        goto result;
    }

    while (BytesRead < BytesToRead)
    {
        ULONGLONG AlignedOffset = CurrentOffset & ~(ULONGLONG)(BlockSize - 1);
        ULONGLONG AlignedOffsetInBlocks = AlignedOffset / (ULONGLONG)BlockSize;
        ULONGLONG DifferenceOffset = CurrentOffset - AlignedOffset;

        ULONG SliceLength = (ULONG)min(BytesToRead - BytesRead, (ULONGLONG)BlockSize - DifferenceOffset);
        ULONGLONG Index = 0;

        ULONGLONG PhysicalBlock = 0;

        BOOLEAN State = ExtfsGetInodeDataPhysicalBlock(InodeContext, AlignedOffsetInBlocks, &PhysicalBlock);
        if (!State)
        {
            DPRINT1("WARNING: Cannot get physical block!\n");
            break;
        }

        DiskOffset.QuadPart = (PhysicalBlock * (ULONGLONG)BlockSize) + DifferenceOffset;

        if (DiskOffset.QuadPart)
        {
            Status = ExtfsFastDiskRead(InodeContext->VolumeExtension, Buffer + BytesRead, DiskOffset, SliceLength, 0);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Bad block??\n");
                break;
            }
        }
        else
        {
            DPRINT1("ERROR: Cannot write on sparse block!\n");
            break;
        }

skip:
        CurrentOffset += SliceLength;
        BytesRead += SliceLength;
    }

    if (BytesRead)
    {
        ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);

        InodeContext->LastAccessTime = ExtfsGetSystemTime();
        ExtfsConvertTime(InodeContext);

        ExReleaseResourceLite(&InodeContext->InodeLock);
    }

result:
    RtlZeroMemory(Buffer + BytesRead, Length - BytesRead);

    InodeContext->OperationStatus = Status;
    ExReleaseResourceLite(&InodeContext->InodeDataLock);
    return BytesRead;
}

BOOLEAN
ExtfsCreateExtent(PEXTFS_INODE_CONTEXT InodeContext, PEXTFS_EXTENT CurrentExtent, ULONGLONG SizeInBytes, BOOLEAN IsSparse)
{
    ULONGLONG BlockSize = InodeContext->VolumeExtension->BlockSize;
    ULONGLONG SizeInBlocks = (SizeInBytes + (BlockSize - 1)) / BlockSize;

    PEXTFS_EXTENT NewExtent = ExAllocatePoolWithTag(NonPagedPool, sizeof(*NewExtent), EXTFS_TAG_EXTENT_LIST);
    ASSERT(NewExtent);
    RtlZeroMemory(NewExtent, sizeof(*NewExtent));

    NewExtent->IsSparse = IsSparse;
    NewExtent->Length = SizeInBlocks;
    NewExtent->LengthInBytes = SizeInBlocks * BlockSize;

    if (CurrentExtent)
    {
        NewExtent->Prev = CurrentExtent;
        NewExtent->Next = CurrentExtent->Next;

        if (NewExtent->Next)
        {
            NewExtent->Next->Prev = NewExtent;
        }

        CurrentExtent->Next = NewExtent;
    }
    else
    {
        ASSERT(!InodeContext->ExtentList);
        InodeContext->ExtentList = NewExtent;
    }

    return TRUE;
}

BOOLEAN
ExtfsDeleteExtent(PEXTFS_EXTENT CurrentExtent)
{
    PEXTFS_EXTENT NextExtent, PrevExtent;
    if (!CurrentExtent)
        return FALSE;

    NextExtent = CurrentExtent->Next;
    PrevExtent = CurrentExtent->Prev;

    if (NextExtent)
        NextExtent->Prev = PrevExtent;

    if (PrevExtent)
        PrevExtent->Next = NextExtent;

    ExFreePoolWithTag(CurrentExtent, EXTFS_TAG_EXTENT_LIST);
    return TRUE;
}

BOOLEAN
ExtfsDivideExtent(PEXTFS_INODE_CONTEXT InodeContext, PEXTFS_EXTENT *OutputExtent, PEXTFS_EXTENT *OutputLastExtent, ULONGLONG OffsetInBlocks)
{
    ULONGLONG RemainingBlocks, BlockSize = InodeContext->VolumeExtension->BlockSize;
    PEXTFS_EXTENT CurrentExtent;
    EXTFS_EXTENT CurrentExtentBackup;

    ASSERT(OutputExtent);

    CurrentExtent = *OutputExtent;
    ASSERT(CurrentExtent);

    ASSERT(OffsetInBlocks <= CurrentExtent->Length);

    RemainingBlocks = CurrentExtent->Length - OffsetInBlocks;

    CurrentExtentBackup = *CurrentExtent;
    CurrentExtent->Length = OffsetInBlocks;
    CurrentExtent->LengthInBytes = CurrentExtent->Length * BlockSize;

    if (RemainingBlocks)
    {
        PEXTFS_EXTENT NewExtent;

        if (!ExtfsCreateExtent(InodeContext, CurrentExtent, RemainingBlocks * BlockSize, CurrentExtent->IsSparse))
        {
            *CurrentExtent = CurrentExtentBackup;
            return FALSE;
        }

        NewExtent = CurrentExtent->Next;

        NewExtent->Block = CurrentExtent->Block + CurrentExtent->Length;
        NewExtent->BlockInBytes = NewExtent->Block * BlockSize;

        if (OutputLastExtent)
            *OutputLastExtent = NewExtent;
    }
    else if (OutputLastExtent)
    {
        *OutputLastExtent = NULL;
    }

    return TRUE;
}

BOOLEAN
ExtfsAppendExtentWithBlock(PEXTFS_INODE_CONTEXT InodeContext, PEXTFS_EXTENT *OutputExtent, PEXTFS_EXTENT *OutputLastExtent, ULONGLONG Block)
{
    ULONGLONG BlockSize = InodeContext->VolumeExtension->BlockSize;
    PEXTFS_EXTENT CurrentExtent;

    ASSERT(OutputExtent);

    CurrentExtent = *OutputExtent;
    ASSERT(CurrentExtent);

    if (CurrentExtent->IsSparse && CurrentExtent->Length)
    {
        PEXTFS_EXTENT NewExtent;

        if (!ExtfsCreateExtent(InodeContext, CurrentExtent, BlockSize, FALSE))
        {
            return FALSE;
        }

        NewExtent = CurrentExtent->Next;

        NewExtent->Block = Block;
        NewExtent->BlockInBytes = NewExtent->Block * BlockSize;

        *OutputExtent = NewExtent;
    }
    else
    {
        ULONGLONG BlockStart = CurrentExtent->Block;
        ULONGLONG BlockEnd = BlockStart + CurrentExtent->Length;

        if (CurrentExtent->IsSparse)
        {
            CurrentExtent->Block = Block;
            CurrentExtent->BlockInBytes = CurrentExtent->Block * BlockSize;

            CurrentExtent->Length = 1;
            CurrentExtent->LengthInBytes = CurrentExtent->Length * BlockSize;

            CurrentExtent->IsSparse = FALSE;
        }
        else
        {
            if (BlockEnd == Block)
            {
                CurrentExtent->Length++;
                CurrentExtent->LengthInBytes = CurrentExtent->Length * BlockSize;
            }
            else
            {
                PEXTFS_EXTENT NewExtent;

                if (!ExtfsCreateExtent(InodeContext, CurrentExtent, BlockSize, FALSE))
                {
                    return FALSE;
                }
                NewExtent = CurrentExtent->Next;

                NewExtent->Block = Block;
                NewExtent->BlockInBytes = NewExtent->Block * BlockSize;

                *OutputExtent = NewExtent;
            }
        }
    }

    if (OutputLastExtent && *OutputLastExtent)
    {
        PEXTFS_EXTENT LastExtent = *OutputLastExtent;

        LastExtent->Length--;
        LastExtent->LengthInBytes = LastExtent->Length * BlockSize;

        if (!LastExtent->Length)
        {
            ExtfsDeleteExtent(LastExtent);
            *OutputLastExtent = NULL;
        }
    }

    return TRUE;
}

ULONG
ExtfsAllocateInodeData(
    PEXTFS_INODE_CONTEXT InodeContext,
    LARGE_INTEGER Offset, ULONG Length)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONGLONG BlockSize = InodeContext->VolumeExtension->BlockSize;
    PEXTFS_EXTENT CurrentEntry = NULL, LastEntry = NULL;
    ULONG BytesToAllocate = Length;
    ULONG BytesAllocated = 0, BytesSparse = 0;
    ULONGLONG BytesTotal = 0;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = InodeContext->VolumeExtension;
    PEXTFS_ALLOCATION_MANAGER AllocationManager = &VolumeExtension->AllocationManager;
    ULONGLONG FirstDataBlock = VolumeExtension->FirstDataBlock;
    ULONGLONG BlocksPerGroup = VolumeExtension->BlocksPerGroup;
    ULONGLONG AlignedBytesTotal, AlignedFileSize;
    ULONGLONG LastGroup = 0;
    ULONGLONG LastFreeBlocks = 0;
    LARGE_INTEGER CurrentOffset = Offset;

    DPRINT("ExtfsAllocateInodeData(0x%p, %I64u, %u)\n", InodeContext, Offset.QuadPart, Length);

    ExAcquireResourceExclusiveLite(&InodeContext->InodeDataLock, TRUE);

    if (InodeContext->AvoidInodeFlush)
    {
        goto result;
    }

    CurrentEntry = InodeContext->ExtentList;
    if (InodeContext->IsReparsePoint && Offset.QuadPart + (ULONGLONG)BytesToAllocate <= sizeof(InodeContext->Inode.SymLink))
    {
        goto result;
    }

    if (!CurrentEntry)
    {
        ULONGLONG RemainingBytes = Offset.QuadPart + (ULONGLONG)BytesToAllocate;

        ExtfsCreateExtent(InodeContext, NULL, RemainingBytes, TRUE);
        CurrentEntry = InodeContext->ExtentList;
    }

    while (CurrentEntry)
    {
        PEXTFS_EXTENT NextEntry = CurrentEntry->Next;
        PEXTFS_EXTENT PrevEntry = CurrentEntry->Prev;

        if (!CurrentEntry->IsSparse)
        {
            ULONGLONG CurrentBlock = CurrentEntry->Block;
            ULONGLONG Blocks = CurrentEntry->Length;

            while (Blocks--)
            {
                PEXTFS_ALLOCATION_ENTRY CurrentGroupAllocationEntry;
                ULONGLONG CurrentFreeBlocks;
                ULONGLONG CurrentGroup;

                CurrentGroup = ((CurrentBlock - FirstDataBlock) / BlocksPerGroup) + 1;
                CurrentGroupAllocationEntry = ExtfsAllocationManagerAcquireAllocationEntryByGroup(AllocationManager, CurrentGroup);

                if (CurrentGroupAllocationEntry)
                {
                    CurrentFreeBlocks = ExtfsAllocationManagerGetAllocationEntryFreeBlocks(CurrentGroupAllocationEntry);
                    if (CurrentFreeBlocks > LastFreeBlocks)
                    {
                        LastGroup = CurrentGroup;
                        LastFreeBlocks = CurrentFreeBlocks;
                    }

                    ExtfsAllocationManagerReleaseAllocationEntry(CurrentGroupAllocationEntry);
                }

                CurrentBlock++;
            }
        }

        if (CurrentOffset.QuadPart >= CurrentEntry->LengthInBytes)
        {
            CurrentOffset.QuadPart -= CurrentEntry->LengthInBytes;
            BytesTotal += CurrentEntry->LengthInBytes;
        }
        else
        {
            PEXTFS_EXTENT LastExtent = NULL;
            ULONGLONG RemainderBytes = BytesToAllocate - BytesAllocated;
            ULONGLONG RemainderLengthInBytes = CurrentEntry->LengthInBytes - CurrentOffset.QuadPart;
            ULONG SliceLength = (ULONG)min(RemainderBytes, RemainderLengthInBytes);
            ULONG SliceBlocks = (SliceLength + (ULONG)(BlockSize - 1)) / (ULONG)BlockSize;

            if (!CurrentEntry->IsSparse)
            {
                BytesAllocated += SliceLength;
                BytesTotal += SliceLength;
                BytesSparse += SliceLength;

                if (BytesAllocated >= BytesToAllocate)
                    break;

                CurrentOffset.QuadPart = 0;
                goto skip_entry;
            }

            DPRINT1("Found perfect sparse extent\n");

            ULONGLONG BlockOffsetStart = CurrentOffset.QuadPart / BlockSize;

            if (LastEntry &&
                CurrentOffset.QuadPart < BlockSize)
            {
                LastExtent = CurrentEntry;
                CurrentEntry = LastEntry;
            }
            else
            {
                if (!ExtfsDivideExtent(InodeContext, &CurrentEntry, &LastExtent, BlockOffsetStart))
                {
                    break;
                }
            }

            if (!InodeContext->CacheAllocationEntry)
            {
                if (LastGroup)
                    InodeContext->CacheAllocationGroup = LastGroup;

                if (!InodeContext->CacheAllocationGroup)
                {
                    InodeContext->CacheAllocationEntry =
                        ExtfsAllocationManagerAcquireAllocationEntryByFreeBlock(AllocationManager);
                    ExtfsAddAllocationEntryToList(InodeContext, InodeContext->CacheAllocationEntry);

                    if (InodeContext->CacheAllocationEntry)
                        InodeContext->CacheAllocationGroup = InodeContext->CacheAllocationEntry->Group;
                }
                else
                {
                    InodeContext->CacheAllocationEntry =
                        ExtfsAllocationManagerAcquireAllocationEntryByGroup(AllocationManager,
                                                                            InodeContext->CacheAllocationGroup);
                    ExtfsAddAllocationEntryToList(InodeContext, InodeContext->CacheAllocationEntry);
                }
            }

            while (SliceBlocks)
            {
                if (!InodeContext->CacheAllocationEntry)
                {
                    Status = STATUS_DISK_FULL;
                    DPRINT1("Disk is full?\n");
                    goto result;
                }

                ULONGLONG FreeBlock = ExtfsAllocationManagerGetAllocationEntryFreeBlock(InodeContext->CacheAllocationEntry, TRUE);
                if (!FreeBlock)
                {
                    InodeContext->CacheAllocationEntry =
                        ExtfsAllocationManagerAcquireAllocationEntryByFreeBlock(AllocationManager);
                    ExtfsAddAllocationEntryToList(InodeContext, InodeContext->CacheAllocationEntry);

                    if (InodeContext->CacheAllocationEntry)
                        InodeContext->CacheAllocationGroup = InodeContext->CacheAllocationEntry->Group;

                    continue;
                }

                DPRINT1("FreeBlock = %I64u\n", FreeBlock);

                if (!ExtfsAppendExtentWithBlock(InodeContext, &CurrentEntry, &LastExtent, FreeBlock))
                {
                    DPRINT1("Error!\n");
                    ExtfsAllocationManagerClearAllocationEntryBlock(InodeContext->CacheAllocationEntry, FreeBlock);
                    break;
                }

                BytesAllocated += BlockSize;
                BytesTotal += BlockSize;
                SliceBlocks--;
            }

            ExtfsDumpExtentList(InodeContext->ExtentList);

            if (BytesAllocated >= BytesToAllocate)
                break;

            CurrentOffset.QuadPart = 0;
            continue;
        }

skip_entry:
        if (!CurrentEntry->Next)
        {
            ULONGLONG RemainingBytes = CurrentOffset.QuadPart + (ULONGLONG)BytesToAllocate;
            ULONGLONG RemainingBlocks = ((RemainingBytes) + (BlockSize - 1)) / BlockSize;

            DPRINT1("LastEntry = 0x%p\n", LastEntry);
            DPRINT1("NextEntry = 0x%p\n", NextEntry);
            DPRINT1("CurrentEntry = 0x%p\n", CurrentEntry);

            DPRINT1("CurrentEntry->Length = %I64u\n", CurrentEntry->Length);

            DPRINT1("Reached the end point!\n");

            if (!CurrentEntry->IsSparse)
            {
                ExtfsCreateExtent(InodeContext, CurrentEntry, RemainingBytes, TRUE);
                NextEntry = CurrentEntry->Next;
            }
            else
            {
                CurrentEntry->Length += RemainingBlocks;
                CurrentEntry->LengthInBytes = CurrentEntry->Length * BlockSize;
                continue;
            }
        }

        LastEntry = CurrentEntry;
        CurrentEntry = NextEntry;
    }

result:
    AlignedBytesTotal = (BytesTotal + (BlockSize - 1)) & ~(BlockSize - 1);
    AlignedFileSize = (InodeContext->FileSize + (BlockSize - 1)) & ~(BlockSize - 1);

    if (BytesAllocated - BytesSparse > 0)
    {
        ULONGLONG StartOffset = Offset.QuadPart;
        ULONGLONG EndOffset = StartOffset + (ULONGLONG)Length;

        if (ReadFieldLE(InodeContext->Inode.Flags) & EXT4_INODE_FLAG_EXTENTS)
        {
            DPRINT1("Unimplemented extents generation\n");
            ASSERT(FALSE);
        }
        else
        {
            Status = ExtfsGeneratePointersWithExtentList(InodeContext, StartOffset, EndOffset, FALSE);
        }
        ExtfsWriteInodeContext(InodeContext);
    }

    if (InodeContext->FileSize < BytesTotal)
    {
        InodeContext->OldFileSize = InodeContext->FileSize;
        InodeContext->FileSize = BytesTotal;

        // if (AlignedFileSize < AlignedBytesTotal)
        // {
        //     InodeContext->AdditionalBlocksCount = 0;
        //     if (ReadFieldLE(InodeContext->Inode.Flags) & EXT4_INODE_FLAG_EXTENTS)
        //     {
        //         DPRINT1("Unimplemented extents generation\n");
        //         ASSERT(FALSE);
        //     }
        //     else
        //     {
        //         Status = ExtfsGeneratePointersWithExtentList(InodeContext, InodeContext->OldFileSize, InodeContext->FileSize);
        //     }
        //     ExtfsWriteInodeContext(InodeContext);
        // }

        ExtfsChangeInodeSize(InodeContext, BytesTotal, TRUE);
    }

    InodeContext->OperationStatus = Status;
    ExReleaseResourceLite(&InodeContext->InodeDataLock);
    return BytesAllocated;
}

ULONG
ExtfsWriteInodeData(
    PEXTFS_INODE_CONTEXT InodeContext,
    PCHAR Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTFS_EXTENT CurrentEntry;
    ULONGLONG FileSize = 0;
    ULONGLONG CurrentOffset = Offset.QuadPart;
    ULONG BytesToWrite = 0;
    ULONGLONG BytesTotal = 0;
    ULONG BytesWritten = 0, BytesAllocated = 0;
    ULONG BlockSize = InodeContext->VolumeExtension->BlockSize;
    LARGE_INTEGER DiskOffset;

    DPRINT("ExtfsWriteInodeData(0x%p, 0x%p, %I64u, %u)\n", InodeContext, Buffer, Offset.QuadPart, Length);

    ExAcquireResourceExclusiveLite(&InodeContext->InodeDataLock, TRUE);
    if (InodeContext->VolumeExtension->ReadOnly)
    {
        DPRINT1("Volume is read only\n");
        goto result;
    }

    BytesAllocated = ExtfsAllocateInodeData(InodeContext, Offset, Length);
    FileSize = ExtfsGetFileSizeByExtents(InodeContext->ExtentList);
    BytesToWrite = (ULONG)min(FileSize - min(Offset.QuadPart, FileSize), (ULONGLONG)Length);

    if (!BytesToWrite)
        goto result;

    Status = InodeContext->OperationStatus;
    if (!NT_SUCCESS(Status))
        goto result;

    if (InodeContext->IsReparsePoint && InodeContext->FileSize <= sizeof(InodeContext->Inode.SymLink))
    {
        RtlCopyMemory(&InodeContext->Inode.SymLink[(ULONG)Offset.QuadPart], Buffer, BytesToWrite);
        BytesWritten = BytesToWrite;
        goto result;
    }

    if (!BytesAllocated)
    {
        DPRINT1("Disk is full?\n");
    }

    while (BytesWritten < BytesToWrite)
    {
        ULONGLONG AlignedOffset = CurrentOffset & ~(ULONGLONG)(BlockSize - 1);
        ULONGLONG AlignedOffsetInBlocks = AlignedOffset / (ULONGLONG)BlockSize;
        ULONGLONG DifferenceOffset = CurrentOffset - AlignedOffset;

        ULONG SliceLength = (ULONG)min(BytesToWrite - BytesWritten, (ULONGLONG)BlockSize - DifferenceOffset);
        ULONGLONG Index = 0;

        ULONGLONG PhysicalBlock = 0;

        BOOLEAN State = ExtfsGetInodeDataPhysicalBlock(InodeContext, AlignedOffsetInBlocks, &PhysicalBlock);
        if (!State)
        {
            DPRINT1("ERROR: Cannot get physical block!\n");
            break;
        }

        DiskOffset.QuadPart = (PhysicalBlock * (ULONGLONG)BlockSize) + DifferenceOffset;

        if (DiskOffset.QuadPart)
        {
            Status = ExtfsFastDiskWrite(InodeContext->VolumeExtension, Buffer + BytesWritten, DiskOffset, SliceLength, 0);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Bad block??\n");
                break;
            }
        }
        else
        {
            DPRINT1("ERROR: Cannot write on sparse block!\n");
            break;
        }

skip:
        CurrentOffset += SliceLength;
        BytesWritten += SliceLength;
    }

    if (BytesWritten)
    {
        ExAcquireResourceExclusiveLite(&InodeContext->InodeLock, TRUE);

        InodeContext->LastWriteTime = ExtfsGetSystemTime();
        ExtfsConvertTime(InodeContext);

        ExReleaseResourceLite(&InodeContext->InodeLock);
    }

result:
    InodeContext->OperationStatus = Status;
    ExReleaseResourceLite(&InodeContext->InodeDataLock);
    return BytesWritten;
}
