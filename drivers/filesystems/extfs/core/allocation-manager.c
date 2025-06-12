#include <extfs.h>

#define MM_WARNING_LIMIT                                    (32 * 1024 * 1024)
#define MAX_ALLOCATION_MANAGER_BLOCK_CAHCE_USAGE            (64 * 1024 * 1024)
#define ALLOCATION_MANAGER_BLOCK_DATA_POINTER_TIME_LIMIT    (ULONG)(1.5 * 1000)

VOID ExtfsAllocationManagerFlush(PEXTFS_ALLOCATION_MANAGER AllocationManager);

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerFindAllocationEntry(
    PEXTFS_LIST_ENTRY BitmapList, PERESOURCE BitmapListLock, ULONG Group);

VOID ExtfsAllocationManagerSafeFlush(PEXTFS_ALLOCATION_MANAGER AllocationManager)
{
    FsRtlEnterFileSystem();

    DPRINT1("Flushing all allocation entries\n");
    ExtfsAutoReleaseInodeContextThread(AllocationManager->VolumeExtension);
    ExtfsAllocationManagerFlush(AllocationManager);

    FsRtlExitFileSystem();
}

VOID NTAPI ExtfsAllocationManagerAutoFlushThread(PVOID Context)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = Context;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    LARGE_INTEGER Interval;
    Interval.QuadPart = -(LONGLONG)((10 * 1000) * 1000 * 10);

    KeSetTimer(&AllocationManager->FlushWaitTimer, Interval, NULL);

    while (TRUE)
    {
        LARGE_INTEGER Timeout = Interval;
        if (VolumeExtension->Locked)
            continue;

        KeWaitForSingleObject(
            &AllocationManager->FlushWaitTimer,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );

        ExtfsAllocationManagerSafeFlush(AllocationManager);

        KeSetTimer(&AllocationManager->FlushWaitTimer, Interval, NULL);

        if (AllocationManager->AutoFlushThreadStopRequest)
            break;
    }

    ExtfsAllocationManagerSafeFlush(AllocationManager);

    KeSetEvent(&AllocationManager->AutoFlushThreadExitedEvent, IO_NO_INCREMENT, FALSE);

    PsTerminateSystemThread(STATUS_SUCCESS);
}

VOID ExtfsAllocationManagerForceFlush(PEXTFS_ALLOCATION_MANAGER AllocationManager)
{
    LARGE_INTEGER Interval = {0};

    KeSetTimer(&AllocationManager->FlushWaitTimer, Interval, NULL);
}

NTSTATUS ExtfsAllocationManagerUninitializeDataList(PEXTFS_ALLOCATION_MANAGER AllocationManager)
{
    ULONG DataListEntries = AllocationManager->DataListEntries;
    PEXTFS_DATA_POINTER DataPointer = AllocationManager->DataList;
    if (!DataPointer || !DataListEntries)
        return STATUS_INVALID_PARAMETER;

    for (ULONG Index = 0; Index < DataListEntries; Index++)
    {
        PEXTFS_DATA_POINTER CurrentDataPointer = &DataPointer[Index];

        ExDeleteResourceLite(&CurrentDataPointer->DataLock);
        CurrentDataPointer->IsDataLockInitialized = FALSE;
    }

    return STATUS_SUCCESS;
}

NTSTATUS ExtfsAllocationManagerInitializeDataList(PEXTFS_ALLOCATION_MANAGER AllocationManager)
{
    ULONG DataListEntries = AllocationManager->DataListEntries;
    PEXTFS_DATA_POINTER DataPointer = AllocationManager->DataList;
    if (!DataPointer || !DataListEntries)
        return STATUS_INVALID_PARAMETER;

    for (ULONG Index = 0; Index < DataListEntries; Index++)
    {
        PEXTFS_DATA_POINTER CurrentDataPointer = &DataPointer[Index];

        ExInitializeResourceLite(&CurrentDataPointer->DataLock);
        CurrentDataPointer->IsDataLockInitialized = TRUE;
    }

    return STATUS_SUCCESS;
}

NTSTATUS ExtfsAllocationManagerInitialize(
    PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_ALLOCATION_MANAGER AllocationManager)
{
    ULONG DataListEntrySize = (VolumeExtension->DiskBlockSize * 2) + sizeof(*AllocationManager->DataList);
    ULONG DataListEntries = MAX_ALLOCATION_MANAGER_BLOCK_CAHCE_USAGE / DataListEntrySize;
    ULONG DataListSize = DataListEntries * sizeof(*AllocationManager->DataList);

    NTSTATUS Status = STATUS_SUCCESS;
    RtlZeroMemory(AllocationManager, sizeof(*AllocationManager));

    ExtfsInitListEntry(&AllocationManager->AllocationList, AllocationManager);

    AllocationManager->VolumeExtension = VolumeExtension;

    ExInitializeResourceLite(&AllocationManager->AllocationListLock);
    ExInitializeResourceLite(&AllocationManager->DataListLock);
    ExInitializeResourceLite(&AllocationManager->MainLock);
    AllocationManager->InitializedLocks = TRUE;

    KeInitializeTimer(&AllocationManager->FlushWaitTimer);
    KeInitializeEvent(&AllocationManager->AutoFlushThreadExitedEvent, NotificationEvent, FALSE);
    AllocationManager->InitializedEvents = TRUE;

    AllocationManager->DataListEntries = DataListEntries;
    AllocationManager->DataList = ExAllocatePoolWithTag(NonPagedPool, DataListSize, EXTFS_TAG_BUFFER);
    if (!AllocationManager->DataList)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    RtlZeroMemory(AllocationManager->DataList, DataListSize);

    Status = ExtfsAllocationManagerInitializeDataList(AllocationManager);
    if (!NT_SUCCESS(Status))
    {
        goto result;
    }

    Status = PsCreateSystemThread(&AllocationManager->AutoFlushThread,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  ExtfsAllocationManagerAutoFlushThread,
                                  AllocationManager);

result:
    return Status;
}

NTSTATUS ExtfsAllocationManagerUninitialize(
    PEXTFS_ALLOCATION_MANAGER AllocationManager)
{
    if (AllocationManager->InitializedLocks && AllocationManager->InitializedEvents)
    {
        AllocationManager->AutoFlushThreadStopRequest = TRUE;
        ExtfsAllocationManagerForceFlush(AllocationManager);
        KeWaitForSingleObject(&AllocationManager->AutoFlushThreadExitedEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    if (AllocationManager->InitializedLocks)
    {
        ExDeleteResourceLite(&AllocationManager->AllocationListLock);
        ExDeleteResourceLite(&AllocationManager->DataListLock);
        ExDeleteResourceLite(&AllocationManager->MainLock);
        AllocationManager->InitializedLocks = FALSE;
    }

    if (AllocationManager->InitializedEvents)
    {
        KeCancelTimer(&AllocationManager->FlushWaitTimer);
        KeClearEvent(&AllocationManager->AutoFlushThreadExitedEvent);
        AllocationManager->InitializedEvents = FALSE;
    }

    if (AllocationManager->DataList)
    {
        ExtfsAllocationManagerUninitializeDataList(AllocationManager);
        ExFreePoolWithTag(AllocationManager->DataList, EXTFS_TAG_BUFFER);
        AllocationManager->DataList = NULL;
    }

    return STATUS_SUCCESS;
}

VOID ExtfsAllocationManagerDeleteAllocationEntry(PEXTFS_ALLOCATION_ENTRY AllocationEntry)
{
    if (!AllocationEntry)
        return;

    ExDeleteResourceLite(&AllocationEntry->InodeBitmapLock);
    ExDeleteResourceLite(&AllocationEntry->BlockBitmapLock);

    if (AllocationEntry->InodeBitmap)
        ExFreePoolWithTag(AllocationEntry->InodeBitmap, EXTFS_TAG_BUFFER);
    if (AllocationEntry->InodeBitmapOriginal)
        ExFreePoolWithTag(AllocationEntry->InodeBitmapOriginal, EXTFS_TAG_BUFFER);

    if (AllocationEntry->BlockBitmap)
        ExFreePoolWithTag(AllocationEntry->BlockBitmap, EXTFS_TAG_BUFFER);
    if (AllocationEntry->BlockBitmapOriginal)
        ExFreePoolWithTag(AllocationEntry->BlockBitmapOriginal, EXTFS_TAG_BUFFER);
    if (AllocationEntry->BlockBitmapFastWrite)
        ExFreePoolWithTag(AllocationEntry->BlockBitmapFastWrite, EXTFS_TAG_BUFFER);
    if (AllocationEntry->BlockReservedBitmap)
        ExFreePoolWithTag(AllocationEntry->BlockReservedBitmap, EXTFS_TAG_BUFFER);

    ExFreePoolWithTag(AllocationEntry, EXTFS_TAG_ALLOCATION_ENTRY);
}

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerGetAllocationEntry(PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONG Group)
{
    ULONG Index;
    NTSTATUS Status;
    LARGE_INTEGER DiskOffset;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    PVOID TempInodeBitmap = NULL;
    PVOID InodeBitmap = NULL;
    PVOID InodeBitmapOriginal = NULL;
    PVOID TempBlockBitmap = NULL;
    PVOID BlockBitmap = NULL;
    PVOID BlockBitmapOriginal = NULL;
    PVOID BlockBitmapFastWrite = NULL;
    PVOID BlockReservedBitmap = NULL;
    PEXTFS_ALLOCATION_ENTRY AllocationEntry = NULL;

    PEXT_GROUP_DESC GroupDesc = ExtfsGetGroupDesc(VolumeExtension, Group);
    if (!GroupDesc)
    {
        DPRINT1("Cannot get GroupDesc\n");
        goto error;
    }

    ULONG InodeBitmapEntries = VolumeExtension->InodesPerGroup;
    ULONG InodeBitmapSize = ExtfsBitmapGetSize(InodeBitmapEntries);

    TempInodeBitmap = ExAllocatePoolWithTag(NonPagedPool, InodeBitmapSize, EXTFS_TAG_BUFFER);
    InodeBitmap = ExAllocatePoolWithTag(NonPagedPool, InodeBitmapSize, EXTFS_TAG_BUFFER);
    InodeBitmapOriginal = ExAllocatePoolWithTag(NonPagedPool, InodeBitmapSize, EXTFS_TAG_BUFFER);

    if (!TempInodeBitmap)
    {
        DPRINT1("Cannot allocate TempInodeBitmap\n");
        goto error;
    }

    if (!InodeBitmap)
    {
        DPRINT1("Cannot allocate InodeBitmap\n");
        goto error;
    }

    if (!InodeBitmapOriginal)
    {
        DPRINT1("Cannot allocate InodeBitmapOriginal\n");
        goto error;
    }

    DiskOffset.QuadPart = ReadFieldLE(GroupDesc->InodeBitmap) * VolumeExtension->BlockSize;
    Status = ExtfsDiskRead(VolumeExtension, InodeBitmap, DiskOffset, InodeBitmapSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot read InodeBitmap\n");
        goto error;
    }

    ULONG BlockBitmapEntries = VolumeExtension->BlocksPerGroup;
    ULONG BlockBitmapSize = ExtfsBitmapGetSize(BlockBitmapEntries);

    TempBlockBitmap = ExAllocatePoolWithTag(NonPagedPool, BlockBitmapSize, EXTFS_TAG_BUFFER);
    BlockBitmap = ExAllocatePoolWithTag(NonPagedPool, BlockBitmapSize, EXTFS_TAG_BUFFER);
    BlockBitmapOriginal = ExAllocatePoolWithTag(NonPagedPool, BlockBitmapSize, EXTFS_TAG_BUFFER);
    BlockBitmapFastWrite = ExAllocatePoolWithTag(NonPagedPool, BlockBitmapSize, EXTFS_TAG_BUFFER);

    if (!TempBlockBitmap)
    {
        DPRINT1("Cannot allocate TempBlockBitmap\n");
        goto error;
    }

    if (!BlockBitmap)
    {
        DPRINT1("Cannot allocate BlockBitmap\n");
        goto error;
    }

    if (!BlockBitmapOriginal)
    {
        DPRINT1("Cannot allocate BlockBitmapOriginal\n");
        goto error;
    }

    if (!BlockBitmapFastWrite)
    {
        DPRINT1("Cannot allocate BlockBitmapFastWrite\n");
        goto error;
    }

    RtlZeroMemory(BlockBitmapFastWrite, BlockBitmapSize);

    DiskOffset.QuadPart = ReadFieldLE(GroupDesc->BlockBitmap) * VolumeExtension->BlockSize;
    Status = ExtfsDiskRead(VolumeExtension, BlockBitmap, DiskOffset, BlockBitmapSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot read BlockBitmap\n");
        goto error;
    }

    BlockReservedBitmap = ExAllocatePoolWithTag(NonPagedPool, BlockBitmapSize, EXTFS_TAG_BUFFER);
    if (!BlockReservedBitmap)
    {
        DPRINT1("Cannot allocate BlockReservedBitmap\n");
        goto error;
    }

    DiskOffset.QuadPart = ReadFieldLE(GroupDesc->ExcludeBitmap) * VolumeExtension->BlockSize;
    if (!VolumeExtension->IsExcludeBitmapAvailable || !DiskOffset.QuadPart)
    {
        RtlZeroMemory(BlockReservedBitmap, BlockBitmapSize);
    }
    else
    {
        Status = ExtfsDiskRead(VolumeExtension, BlockReservedBitmap, DiskOffset, BlockBitmapSize);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Cannot read BlockBitmap\n");
            goto error;
        }
    }

    AllocationEntry = ExAllocatePoolWithTag(NonPagedPool,
                                            sizeof(*AllocationEntry),
                                            EXTFS_TAG_ALLOCATION_ENTRY);
    if (!AllocationEntry)
    {
        DPRINT1("Cannot allocate AllocationEntry\n");
        goto error;
    }
    RtlZeroMemory(AllocationEntry, sizeof(*AllocationEntry));

    ExtfsInitListEntry(&AllocationEntry->ContextListEntry, AllocationEntry);
    ExtfsInitListEntry(&AllocationEntry->ListEntry, AllocationEntry);

    AllocationEntry->AllocationManager = AllocationManager;
    AllocationEntry->GroupDesc = GroupDesc;
    AllocationEntry->Group = Group;

    AllocationEntry->BlockBitmap = BlockBitmap;
    AllocationEntry->BlockBitmapOriginal = BlockBitmapOriginal;
    AllocationEntry->BlockBitmapFastWrite = BlockBitmapFastWrite;
    AllocationEntry->BlockReservedBitmap = BlockReservedBitmap;
    AllocationEntry->BlockBitmapSize = BlockBitmapSize;
    AllocationEntry->BlockBitmapEntries = BlockBitmapEntries;

    AllocationEntry->InodeBitmap = InodeBitmap;
    AllocationEntry->InodeBitmapOriginal = InodeBitmapOriginal;
    AllocationEntry->InodeBitmapSize = InodeBitmapSize;
    AllocationEntry->InodeBitmapEntries = InodeBitmapEntries;

    Index = 0;
    while (Index < BlockBitmapSize)
        ((PCHAR)(BlockBitmap))[Index] |= ((PCHAR)(BlockReservedBitmap))[Index], Index++;

    RtlCopyMemory(BlockBitmapOriginal, BlockBitmap, BlockBitmapSize);
    RtlCopyMemory(InodeBitmapOriginal, InodeBitmap, InodeBitmapSize);

    ExInitializeResourceLite(&AllocationEntry->BlockBitmapLock);
    ExInitializeResourceLite(&AllocationEntry->InodeBitmapLock);
    return AllocationEntry;

error:
    if (TempInodeBitmap)
        ExFreePoolWithTag(TempInodeBitmap, EXTFS_TAG_BUFFER);

    if (InodeBitmap)
        ExFreePoolWithTag(InodeBitmap, EXTFS_TAG_BUFFER);

    if (InodeBitmapOriginal)
        ExFreePoolWithTag(InodeBitmapOriginal, EXTFS_TAG_BUFFER);

    if (TempBlockBitmap)
        ExFreePoolWithTag(TempBlockBitmap, EXTFS_TAG_BUFFER);

    if (BlockBitmap)
        ExFreePoolWithTag(BlockBitmap, EXTFS_TAG_BUFFER);

    if (BlockBitmapOriginal)
        ExFreePoolWithTag(BlockBitmapOriginal, EXTFS_TAG_BUFFER);

    if (BlockBitmapFastWrite)
        ExFreePoolWithTag(BlockBitmapFastWrite, EXTFS_TAG_BUFFER);

    if (BlockReservedBitmap)
        ExFreePoolWithTag(BlockReservedBitmap, EXTFS_TAG_BUFFER);

    if (AllocationEntry)
        ExFreePoolWithTag(AllocationEntry, EXTFS_TAG_ALLOCATION_ENTRY);

    return NULL;
}

VOID ExtfsAllocationManagerAddAllocationEntry(
    PEXTFS_LIST_ENTRY BitmapList, PERESOURCE BitmapListLock, PEXTFS_ALLOCATION_ENTRY AllocationEntry)
{
    ExAcquireResourceExclusiveLite(BitmapListLock, TRUE);

    ExtfsInsertTailList(BitmapList, &AllocationEntry->ListEntry);

    ExReleaseResourceLite(BitmapListLock);
}

VOID ExtfsAllocationManagerRemoveAllocationEntry(
    PEXTFS_LIST_ENTRY BitmapList, PERESOURCE BitmapListLock, PEXTFS_ALLOCATION_ENTRY AllocationEntry)
{
    ExAcquireResourceExclusiveLite(BitmapListLock, TRUE);

    ExtfsRemoveEntryList(&AllocationEntry->ListEntry);
    ExtfsAllocationManagerDeleteAllocationEntry(AllocationEntry);

    ExReleaseResourceLite(BitmapListLock);
}

PEXTFS_DATA_POINTER ExtfsAllocationManagerAcquireBlockDataPointer(PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONGLONG Block)
{
    ULONG Index;
    PEXTFS_DATA_POINTER BlockDataPointer = NULL;
    ExAcquireResourceExclusiveLite(&AllocationManager->DataListLock, TRUE);

    for (Index = 0; Index < AllocationManager->DataListEntries; Index++)
    {
        PEXTFS_DATA_POINTER CurrentBlockDataPointer = &AllocationManager->DataList[Index];

        if (CurrentBlockDataPointer->Block == Block &&
            CurrentBlockDataPointer->IsUsing &&
            CurrentBlockDataPointer->IsReady)
        {
            BlockDataPointer = CurrentBlockDataPointer;
            goto result;
        }
    }

    for (Index = 0; Index < AllocationManager->DataListEntries; Index++)
    {
        PEXTFS_DATA_POINTER CurrentBlockDataPointer = &AllocationManager->DataList[Index];

        if (!CurrentBlockDataPointer->IsUsing)
        {
            CurrentBlockDataPointer->IsReady = FALSE;
            BlockDataPointer = CurrentBlockDataPointer;
            goto result;
        }
    }

result:
    if (BlockDataPointer)
    {
        ExAcquireResourceExclusiveLite(&BlockDataPointer->DataLock, TRUE);
        BlockDataPointer->IsUsing = TRUE;
    }

    ExReleaseResourceLite(&AllocationManager->DataListLock);
    return BlockDataPointer;
}

NTSTATUS ExtfsAllocationManagerRequestBlock(
    PEXTFS_ALLOCATION_MANAGER AllocationManager,
    ULONGLONG Block, ULONG *OutputOffset, PEXTFS_DATA_POINTER *OutBlockDataPointer, BOOLEAN WillOverwrite)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    PEXTFS_DATA_POINTER BlockDataPointer = NULL;
    
    PVOID MemTest = NULL;
    PVOID DataPointer = NULL, OriginalDataPointer = NULL;

    ULONG Group = (Block / VolumeExtension->BlocksPerGroup) + 1;
    ULONGLONG RelativeBlock = Block % VolumeExtension->BlocksPerGroup;

    PEXTFS_ALLOCATION_ENTRY AllocationEntry;
    ULONGLONG AccessTime = KeQueryInterruptTime() / 10000;
    ULONGLONG BlockSize = VolumeExtension->BlockSize;
    ULONGLONG DiskBlockSize = VolumeExtension->DiskBlockSize;
    ULONGLONG FirstDataBlock = VolumeExtension->FirstDataBlock;
    ULONGLONG DiskBlock;
    LARGE_INTEGER DiskOffset;
    ULONGLONG AlignedDiskOffset;
    ULONGLONG RemainderDiskOffset;
    BOOLEAN WillRead = !WillOverwrite;

    DiskOffset.QuadPart = (FirstDataBlock + Block) * BlockSize;

    AlignedDiskOffset = (ULONGLONG)DiskOffset.QuadPart & ~(ULONGLONG)(DiskBlockSize - 1);
    DiskBlock = AlignedDiskOffset / DiskBlockSize;

    RemainderDiskOffset = (ULONGLONG)DiskOffset.QuadPart - AlignedDiskOffset;

    if (OutputOffset)
        *OutputOffset = (ULONG)RemainderDiskOffset;

    ExAcquireResourceExclusiveLite(&AllocationManager->MainLock, TRUE);

    AllocationEntry = ExtfsAllocationManagerFindAllocationEntry(&AllocationManager->AllocationList,
                                                                &AllocationManager->AllocationListLock,
                                                                Group);

    if (AllocationEntry)
    {
        PERESOURCE BitmapLock = &AllocationEntry->BlockBitmapLock;

        ExAcquireResourceExclusiveLite(BitmapLock, TRUE);

        if (ExtfsBitmapGet(AllocationEntry->BlockBitmapFastWrite, AllocationEntry->BlockBitmapEntries, RelativeBlock))
        {
            DPRINT1("Lazy cache\n");
            WillRead = FALSE;
            ExtfsBitmapClear(AllocationEntry->BlockBitmapFastWrite, AllocationEntry->BlockBitmapEntries, RelativeBlock);
        }

        ExReleaseResourceLite(BitmapLock);
    }

    ExReleaseResourceLite(&AllocationManager->MainLock);

    BlockDataPointer = ExtfsAllocationManagerAcquireBlockDataPointer(AllocationManager, DiskBlock);
    if (!BlockDataPointer)
    {
        DPRINT1("Cache is full\n");
        goto result;
    }

    BlockDataPointer->AccessTime = AccessTime;

    if (BlockDataPointer->IsReady)
    {
        DPRINT("Cache already exists\n");
        goto result;
    }

    MemTest = ExAllocatePoolWithTag(NonPagedPool, MM_WARNING_LIMIT, EXTFS_TAG_BUFFER);
    if (!MemTest)
    {
        DPRINT1("Skipping cache because of low memory\n");
        
        BlockDataPointer->IsUsing = FALSE;
        ExReleaseResourceLite(&BlockDataPointer->DataLock);
        BlockDataPointer = NULL;

        ExtfsAllocationManagerForceFlush(AllocationManager);
        goto result; 
    }

    ExFreePoolWithTag(MemTest, EXTFS_TAG_BUFFER); 
    MemTest = NULL;

    DataPointer = ExAllocatePoolWithTag(NonPagedPool, DiskBlockSize, EXTFS_TAG_BUFFER);
    OriginalDataPointer = ExAllocatePoolWithTag(NonPagedPool, DiskBlockSize, EXTFS_TAG_BUFFER);
    if (!DataPointer || !OriginalDataPointer)
    {
        DPRINT1("Cannot allocate the blocks\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    if (WillRead)
    {
        DiskOffset.QuadPart = AlignedDiskOffset;
        Status = ExtfsDiskRead(VolumeExtension, DataPointer, DiskOffset, DiskBlockSize);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Cannot read block to data pointer\n");
            goto result;
        }
    }

    RtlCopyMemory(OriginalDataPointer, DataPointer, DiskBlockSize);

    BlockDataPointer->AccessTime = KeQueryInterruptTime() / 10000;
    BlockDataPointer->Block = DiskBlock;
    BlockDataPointer->Data = DataPointer;
    BlockDataPointer->OriginalData = OriginalDataPointer;
    BlockDataPointer->IsReady = TRUE;

result:
    if (MemTest)
        ExFreePoolWithTag(MemTest, EXTFS_TAG_BUFFER);

    if (!NT_SUCCESS(Status))
    {
        ExReleaseResourceLite(&BlockDataPointer->DataLock);
        if (BlockDataPointer)
            BlockDataPointer->IsUsing = FALSE;
        if (DataPointer)
            ExFreePoolWithTag(DataPointer, EXTFS_TAG_BUFFER);
        if (OriginalDataPointer)
            ExFreePoolWithTag(OriginalDataPointer, EXTFS_TAG_BUFFER);
    }
    else
    {
        *OutBlockDataPointer = BlockDataPointer;
    }

    return Status;
}

NTSTATUS ExtfsAllocationManagerDiskRW(PEXTFS_ALLOCATION_MANAGER AllocationManager, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, ULONG Flags)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    ULONGLONG BlockSize = VolumeExtension->BlockSize;
    ULONGLONG FirstDataBlock = VolumeExtension->FirstDataBlock;
    LARGE_INTEGER CurrentDiskOffset;

    BOOLEAN WriteModeFlag = Flags & EXTFS_ALLOCATION_MANAGER_WRITE_MODE_FLAG;
    BOOLEAN NoCacheFlag = Flags & EXTFS_ALLOCATION_MANAGER_NO_CACHE_FLAG;

    DPRINT("ExtfsAllocationManagerDiskRW(0x%p, 0x%p, %llu, %u, %u)\n", AllocationManager, Buffer, Offset.QuadPart, Length, Flags);

    if (Offset.QuadPart < VolumeExtension->FirstDataBlock * VolumeExtension->BlockSize)
    {
        DPRINT1("Invalid offset\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }
    Offset.QuadPart -= VolumeExtension->FirstDataBlock * VolumeExtension->BlockSize;

    ULONGLONG CurrentOffset = Offset.QuadPart;
    ULONGLONG MaxOffset = CurrentOffset + Length;

    while (CurrentOffset < MaxOffset)
    {
        ULONGLONG CurrentDiskBlock = CurrentOffset / BlockSize;
        ULONGLONG DifferenceOffset = CurrentOffset - (CurrentDiskBlock * BlockSize);
        ULONG BufferOffset = 0;

        ULONGLONG SliceLength = min(MaxOffset - CurrentOffset, BlockSize - DifferenceOffset);
        BOOLEAN WillOverwrite = WriteModeFlag && SliceLength == BlockSize;
        PEXTFS_DATA_POINTER BlockDataPointer = NULL;

        Status = ExtfsAllocationManagerRequestBlock(AllocationManager, CurrentDiskBlock, &BufferOffset, &BlockDataPointer, WillOverwrite);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Cannot request block inside allocation manager\n");
            goto result;
        }

        if (BlockDataPointer)
        {
            PVOID Dest = Buffer;
            PVOID Src = (PCHAR)BlockDataPointer->Data + BufferOffset + DifferenceOffset;

            if (!WriteModeFlag)
                RtlCopyMemory(Dest, Src, SliceLength);
            else
                RtlCopyMemory(Src, Dest, SliceLength);
        }
        else
        {
            CurrentDiskOffset.QuadPart = (CurrentDiskBlock + FirstDataBlock) * BlockSize;
            CurrentDiskOffset.QuadPart += DifferenceOffset;

            ExAcquireResourceExclusiveLite(&AllocationManager->MainLock, TRUE);

            if (!WriteModeFlag)
            {
                Status = ExtfsDiskRead(VolumeExtension, Buffer, CurrentDiskOffset, SliceLength);
            }
            else
            {
                Status = ExtfsDiskWrite(VolumeExtension, Buffer, CurrentDiskOffset, SliceLength);
            }

            ExReleaseResourceLite(&AllocationManager->MainLock);

            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Cannot read slice from the buffer\n");
                goto result;
            }
        }

        if (BlockDataPointer)
        {
            ExReleaseResourceLite(&BlockDataPointer->DataLock);
        }

        CurrentOffset += SliceLength;
        Buffer = (PCHAR)Buffer + SliceLength;
    }

result:
    return Status;
}

NTSTATUS ExtfsAllocationManagerDiskRead(PEXTFS_ALLOCATION_MANAGER AllocationManager, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, ULONG Flags)
{
    return ExtfsAllocationManagerDiskRW(AllocationManager, Buffer, Offset, Length, Flags);
}

NTSTATUS ExtfsAllocationManagerDiskWrite(PEXTFS_ALLOCATION_MANAGER AllocationManager, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, ULONG Flags)
{
    Flags |= EXTFS_ALLOCATION_MANAGER_WRITE_MODE_FLAG;

    return ExtfsAllocationManagerDiskRW(AllocationManager, Buffer, Offset, Length, Flags);
}

VOID ExtfsAllocationManagerFlush(PEXTFS_ALLOCATION_MANAGER AllocationManager)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    ULONG Group;
    ULONGLONG DiskBlockSize = VolumeExtension->DiskBlockSize;
    ULONGLONG BlockSize = VolumeExtension->BlockSize;
    ULONGLONG FirstDataBlock = VolumeExtension->FirstDataBlock;
    ULONGLONG FreeBlocksCount = 0, FreeInodesCount = 0, Index;
    ULONGLONG AccessTime = KeQueryInterruptTime() / 10000;
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;

    ExAcquireResourceExclusiveLite(&AllocationManager->MainLock, TRUE);
    ExAcquireResourceExclusiveLite(&AllocationManager->AllocationListLock, TRUE);

    EndListEntry = &AllocationManager->AllocationList;
    CurrentListEntry = EndListEntry->Next;

    if (ExAcquireResourceExclusiveLite(&AllocationManager->DataListLock, TRUE))
    {
        for (Index = 0; Index < AllocationManager->DataListEntries; Index++)
        {
            LARGE_INTEGER DiskOffset;
            PEXTFS_DATA_POINTER BlockDataPointer = &AllocationManager->DataList[Index];
            PVOID DataPointer = BlockDataPointer->Data;
            PVOID OriginalDataPointer = BlockDataPointer->OriginalData;
            BOOLEAN IsDirtyDataPointer;
            ULONGLONG TimeDifference = AccessTime - BlockDataPointer->AccessTime;
            if (!BlockDataPointer->IsUsing || !BlockDataPointer->IsReady)
                continue;

            if (TimeDifference < ALLOCATION_MANAGER_BLOCK_DATA_POINTER_TIME_LIMIT &&
                !AllocationManager->AutoFlushThreadStopRequest)
            {
                continue;
            }

            if (!ExAcquireResourceExclusiveLite(&BlockDataPointer->DataLock, AllocationManager->AutoFlushThreadStopRequest))
                continue;

            DiskOffset.QuadPart = BlockDataPointer->Block * DiskBlockSize;

            if (DataPointer && OriginalDataPointer && !VolumeExtension->ReadOnly)
            {
                IsDirtyDataPointer = RtlCompareMemory(OriginalDataPointer,
                                                      DataPointer,
                                                      DiskBlockSize) != DiskBlockSize;
                if (IsDirtyDataPointer)
                {
                    Status = ExtfsDiskWrite(VolumeExtension, DataPointer, DiskOffset, DiskBlockSize);
                    if (!NT_SUCCESS(Status))
                    {
                        DPRINT1("Warning cannot flush dirty block data\n");
                    }
                    else
                    {
                        DPRINT1("Flushed dirty block data\n");
                    }
                }
            }

            if (NT_SUCCESS(Status))
            {
                if (DataPointer)
                    ExFreePoolWithTag(DataPointer, EXTFS_TAG_BUFFER);
                if (OriginalDataPointer)
                    ExFreePoolWithTag(OriginalDataPointer, EXTFS_TAG_BUFFER);

                BlockDataPointer->Data = BlockDataPointer->OriginalData = NULL;
                BlockDataPointer->IsUsing = BlockDataPointer->IsReady = FALSE;
            }

            ExReleaseResourceLite(&BlockDataPointer->DataLock);
        }

        ExReleaseResourceLite(&AllocationManager->DataListLock);
    }

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_LIST_ENTRY NextEntry = CurrentListEntry->Next;
        PEXTFS_ALLOCATION_ENTRY CurrentAllocationEntry = ExtfsGetListEntryStructure(CurrentListEntry);

        BOOLEAN WillBeDestroyed;
        BOOLEAN IsDirtyBlock, IsDirtyInode;
        LARGE_INTEGER DiskOffset;
        PEXT_GROUP_DESC GroupDesc;

        WillBeDestroyed = CurrentAllocationEntry->ReferenceCount < 1;
        GroupDesc = ExtfsGetGroupDesc(VolumeExtension, CurrentAllocationEntry->Group);
        RtlCopyMemory(GroupDesc, CurrentAllocationEntry->GroupDesc, sizeof(*GroupDesc));

        ExAcquireResourceExclusiveLite(&CurrentAllocationEntry->BlockBitmapLock, TRUE);
        ExAcquireResourceExclusiveLite(&CurrentAllocationEntry->InodeBitmapLock, TRUE);

        IsDirtyInode = RtlCompareMemory(CurrentAllocationEntry->InodeBitmap,
                                        CurrentAllocationEntry->InodeBitmapOriginal,
                                        CurrentAllocationEntry->InodeBitmapSize) != CurrentAllocationEntry->InodeBitmapSize;

        IsDirtyBlock = RtlCompareMemory(CurrentAllocationEntry->BlockBitmap,
                                        CurrentAllocationEntry->BlockBitmapOriginal,
                                        CurrentAllocationEntry->BlockBitmapSize) != CurrentAllocationEntry->BlockBitmapSize;

        if (IsDirtyInode && !VolumeExtension->ReadOnly)
        {
            DiskOffset.QuadPart = ReadFieldLE(GroupDesc->InodeBitmap) * VolumeExtension->BlockSize;
            Status = ExtfsDiskWrite(VolumeExtension, CurrentAllocationEntry->InodeBitmap, DiskOffset, CurrentAllocationEntry->InodeBitmapSize);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Warning cannot flush dirty inode bitmap\n");
            }
            else
            {
                RtlCopyMemory(CurrentAllocationEntry->InodeBitmapOriginal,
                              CurrentAllocationEntry->InodeBitmap,
                              CurrentAllocationEntry->InodeBitmapSize);
                DPRINT1("Flushed dirty inode bitmap\n");
            }                
        }

        if (IsDirtyBlock && !VolumeExtension->ReadOnly)
        {
            DiskOffset.QuadPart = ReadFieldLE(GroupDesc->BlockBitmap) * VolumeExtension->BlockSize;
            Status = ExtfsDiskWrite(VolumeExtension, CurrentAllocationEntry->BlockBitmap, DiskOffset, CurrentAllocationEntry->BlockBitmapSize);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Warning cannot flush dirty block bitmap\n");
            }
            else
            {
                RtlCopyMemory(CurrentAllocationEntry->BlockBitmapOriginal,
                              CurrentAllocationEntry->BlockBitmap,
                              CurrentAllocationEntry->BlockBitmapSize);
                DPRINT1("Flushed dirty block bitmap\n");
            }
        }

        if ((IsDirtyBlock ||
             IsDirtyInode) && !VolumeExtension->ReadOnly)
            ExtfsWriteGroupDesc(VolumeExtension, GroupDesc, CurrentAllocationEntry->Group);

        ExReleaseResourceLite(&CurrentAllocationEntry->InodeBitmapLock);
        ExReleaseResourceLite(&CurrentAllocationEntry->BlockBitmapLock);

        if (WillBeDestroyed)
        {
            DPRINT1("Destroying unreferenced AllocationEntry\n");
            ExtfsAllocationManagerRemoveAllocationEntry(&AllocationManager->AllocationList,
                                                        &AllocationManager->AllocationListLock,
                                                        CurrentAllocationEntry);
        }

        CurrentListEntry = NextEntry;
    }

    for (Group = 0; Group < VolumeExtension->GroupDescCount; Group++)
    {
        PEXT_GROUP_DESC GroupDesc = ExtfsGetGroupDesc(VolumeExtension, Group + 1);
        ASSERT(GroupDesc != NULL);

        FreeBlocksCount += ReadFieldLE(GroupDesc->FreeBlocksCount);
        FreeInodesCount += ReadFieldLE(GroupDesc->FreeInodesCount);
    }

    ExtfsAcquireSuperBlockWriteLock(VolumeExtension, TRUE);

    WriteFieldLE(VolumeExtension->SuperBlock.FreeBlocksCountLo, FreeBlocksCount);
    WriteFieldLE(VolumeExtension->SuperBlock.FreeInodesCount, FreeInodesCount);

    ExtfsUpdateVolumeExtensionUsage(VolumeExtension);
    ExtfsFlushSuperBlock(VolumeExtension);
    ExtfsReleaseSuperBlockLock(VolumeExtension);

    ExReleaseResourceLite(&AllocationManager->AllocationListLock);
    ExReleaseResourceLite(&AllocationManager->MainLock);
}

VOID ExtfsAllocationManagerReleaseAllocationEntryWithReferenceCount(PEXTFS_ALLOCATION_ENTRY AllocationEntry, LONGLONG RefereceCount)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;

    ExAcquireResourceExclusiveLite(&AllocationManager->MainLock, TRUE);
    DPRINT("AllocationEntry->ReferenceCount = %I64d, RefereceCount = %I64d\n", AllocationEntry->ReferenceCount, RefereceCount);

    ASSERT(AllocationEntry->ReferenceCount > 0);

    if (AllocationEntry->ReferenceCount >= RefereceCount)
        AllocationEntry->ReferenceCount -= RefereceCount;

    ExReleaseResourceLite(&AllocationManager->MainLock);
}

VOID ExtfsAllocationManagerReleaseAllocationEntry(PEXTFS_ALLOCATION_ENTRY AllocationEntry)
{
    return ExtfsAllocationManagerReleaseAllocationEntryWithReferenceCount(AllocationEntry, 1);
}
