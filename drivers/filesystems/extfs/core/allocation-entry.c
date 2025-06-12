#include <extfs.h>

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerGetAllocationEntry(PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONG Group);

VOID ExtfsAllocationManagerAddAllocationEntry(
    PEXTFS_LIST_ENTRY BitmapList, PERESOURCE BitmapListLock, PEXTFS_ALLOCATION_ENTRY AllocationEntry);
VOID ExtfsAllocationManagerRemoveAllocationEntry(
    PEXTFS_LIST_ENTRY BitmapList, PERESOURCE BitmapListLock, PEXTFS_ALLOCATION_ENTRY AllocationEntry);

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByGroup(
    PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONG Group);

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerFindAllocationEntry(
    PEXTFS_LIST_ENTRY BitmapList, PERESOURCE BitmapListLock, ULONG Group)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;

    ExAcquireResourceExclusiveLite(BitmapListLock, TRUE);

    EndListEntry = BitmapList;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_ALLOCATION_ENTRY CurrentAllocationEntry = ExtfsGetListEntryStructure(CurrentListEntry);

        if (CurrentAllocationEntry->Group == Group)
        {
            ExReleaseResourceLite(BitmapListLock);
            return CurrentAllocationEntry;
        }

        CurrentListEntry = CurrentListEntry->Next;
    }

    ExReleaseResourceLite(BitmapListLock);
    return NULL;
}

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerFindAllocationEntryByFreeEntries(
    PEXTFS_LIST_ENTRY BitmapList, PERESOURCE BitmapListLock, BOOLEAN IsBlock)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;
    ULONG LastFreeEntries = 0;
    PEXTFS_ALLOCATION_ENTRY LastAllocationEntry = NULL;

    ExAcquireResourceExclusiveLite(BitmapListLock, TRUE);

    EndListEntry = BitmapList;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_ALLOCATION_ENTRY CurrentAllocationEntry = ExtfsGetListEntryStructure(CurrentListEntry);

        ULONG FreeEntries = IsBlock ?
            ExtfsAllocationManagerGetAllocationEntryFreeBlocks(CurrentAllocationEntry) :
            ExtfsAllocationManagerGetAllocationEntryFreeInodes(CurrentAllocationEntry);
        if (FreeEntries < 1)
            goto skip_entry;

        if (LastFreeEntries < FreeEntries)
        {
            LastFreeEntries = FreeEntries;
            LastAllocationEntry = CurrentAllocationEntry;
        }

skip_entry:
        CurrentListEntry = CurrentListEntry->Next;
    }

    ExReleaseResourceLite(BitmapListLock);
    return LastAllocationEntry;
}

VOID ExtfsAllocationManagerUpdateAllocationEntryGroupDescriptor(PEXTFS_ALLOCATION_ENTRY AllocationEntry, BOOLEAN IsInode, BOOLEAN AlreadyLocked)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    PEXT_SUPER_BLOCK SuperBlock = &VolumeExtension->SuperBlock;
    PERESOURCE Lock = IsInode ? &AllocationEntry->InodeBitmapLock : &AllocationEntry->BlockBitmapLock;
    ULONGLONG FreeEntries;

    if (!AlreadyLocked)
        ExAcquireResourceExclusiveLite(Lock, TRUE);

    ExtfsAcquireSuperBlockWriteLock(VolumeExtension, TRUE);

    if (IsInode)
    {
        FreeEntries = ReadFieldLE(SuperBlock->FreeInodesCount);
        FreeEntries -= ReadFieldLE(AllocationEntry->GroupDesc->FreeInodesCount);

        WriteFieldLE(AllocationEntry->GroupDesc->FreeInodesCount,
                     ExtfsBitmapGetFreeEntries(AllocationEntry->InodeBitmap, AllocationEntry->InodeBitmapEntries));

        FreeEntries += ReadFieldLE(AllocationEntry->GroupDesc->FreeInodesCount);

        WriteFieldLE(SuperBlock->FreeInodesCount, FreeEntries);
    }
    else
    {
        FreeEntries = ReadFieldLE(SuperBlock->FreeBlocksCountLo);
        FreeEntries -= ReadFieldLE(AllocationEntry->GroupDesc->FreeBlocksCount);

        WriteFieldLE(AllocationEntry->GroupDesc->FreeBlocksCount,
                     ExtfsBitmapGetFreeEntries(AllocationEntry->BlockBitmap, AllocationEntry->BlockBitmapEntries));

        FreeEntries += ReadFieldLE(AllocationEntry->GroupDesc->FreeBlocksCount);

        WriteFieldLE(SuperBlock->FreeBlocksCountLo, FreeEntries);
    }

    ExtfsUpdateVolumeExtensionUsage(VolumeExtension);
    ExtfsReleaseSuperBlockLock(VolumeExtension);

    if (!AlreadyLocked)
        ExReleaseResourceLite(Lock);
}

ULONG ExtfsAllocationManagerGetAllocationEntryUsedInodes(PEXTFS_ALLOCATION_ENTRY AllocationEntry)
{
    ULONG UsedInodes = 0;
    PERESOURCE Lock = &AllocationEntry->InodeBitmapLock;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    UsedInodes = ExtfsBitmapGetUsedEntries(AllocationEntry->InodeBitmap, AllocationEntry->InodeBitmapEntries);
    ExReleaseResourceLite(Lock);

    return UsedInodes;
}

ULONG ExtfsAllocationManagerGetAllocationEntryFreeInodes(PEXTFS_ALLOCATION_ENTRY AllocationEntry)
{
    ULONG FreeInodes = 0;
    PERESOURCE Lock = &AllocationEntry->InodeBitmapLock;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    FreeInodes = ExtfsBitmapGetFreeEntries(AllocationEntry->InodeBitmap, AllocationEntry->InodeBitmapEntries);
    ExReleaseResourceLite(Lock);

    return FreeInodes;
}

ULONGLONG ExtfsAllocationManagerGetAllocationEntryFreeInode(PEXTFS_ALLOCATION_ENTRY AllocationEntry, BOOLEAN AllocationRequest)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    ULONG FreeInode = 0;
    PERESOURCE Lock = &AllocationEntry->InodeBitmapLock;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    FreeInode = ExtfsBitmapGetFreeEntry(AllocationEntry->InodeBitmap, AllocationEntry->InodeBitmapEntries);
    if (AllocationRequest)
    {
        ExtfsBitmapSet(AllocationEntry->InodeBitmap, AllocationEntry->InodeBitmapEntries, FreeInode);
    }
    ExtfsAllocationManagerUpdateAllocationEntryGroupDescriptor(AllocationEntry, TRUE, TRUE);
    ExReleaseResourceLite(Lock);

    if (FreeInode != (ULONG)-1)
        return ((ULONGLONG)FreeInode + 1) + ((ULONGLONG)VolumeExtension->InodesPerGroup * (AllocationEntry->Group - 1));
    else
        return 0;
}

BOOLEAN ExtfsAllocationManagerSetAllocationEntryInode(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Inode)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    BOOLEAN Result = FALSE;
    PERESOURCE Lock = &AllocationEntry->InodeBitmapLock;
    ULONG Group = (Inode - 1) / VolumeExtension->InodesPerGroup;
    ULONG EntryIndex = (Inode - 1) % VolumeExtension->InodesPerGroup;

    if (AllocationEntry->Group != Group + 1)
        return Result;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    Result = ExtfsBitmapSet(AllocationEntry->InodeBitmap, AllocationEntry->InodeBitmapEntries, EntryIndex);
    ExtfsAllocationManagerUpdateAllocationEntryGroupDescriptor(AllocationEntry, TRUE, TRUE);
    ExReleaseResourceLite(Lock);

    return Result;
}

BOOLEAN ExtfsAllocationManagerGetAllocationEntryInode(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Inode)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    BOOLEAN Result = FALSE;
    PERESOURCE Lock = &AllocationEntry->InodeBitmapLock;
    ULONG Group = (Inode - 1) / VolumeExtension->InodesPerGroup;
    ULONG EntryIndex = (Inode - 1) % VolumeExtension->InodesPerGroup;

    if (AllocationEntry->Group != Group + 1)
        return Result;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    Result = ExtfsBitmapGet(AllocationEntry->InodeBitmap, AllocationEntry->InodeBitmapEntries, EntryIndex);
    ExReleaseResourceLite(Lock);

    return Result;
}

BOOLEAN ExtfsAllocationManagerClearAllocationEntryInode(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Inode)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    BOOLEAN Result = FALSE;
    PERESOURCE Lock = &AllocationEntry->InodeBitmapLock;
    ULONG Group = (Inode - 1) / VolumeExtension->InodesPerGroup;
    ULONG EntryIndex = (Inode - 1) % VolumeExtension->InodesPerGroup;

    if (AllocationEntry->Group != Group + 1)
        return Result;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    Result = ExtfsBitmapClear(AllocationEntry->InodeBitmap, AllocationEntry->InodeBitmapEntries, EntryIndex);
    ExtfsAllocationManagerUpdateAllocationEntryGroupDescriptor(AllocationEntry, TRUE, TRUE);
    ExReleaseResourceLite(Lock);

    return Result;
}

ULONG ExtfsAllocationManagerGetAllocationEntryUsedBlocks(PEXTFS_ALLOCATION_ENTRY AllocationEntry)
{
    ULONG UsedBlocks = 0;
    PERESOURCE Lock = &AllocationEntry->BlockBitmapLock;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    UsedBlocks = ExtfsBitmapGetUsedEntries(AllocationEntry->BlockBitmap, AllocationEntry->BlockBitmapEntries);
    ExReleaseResourceLite(Lock);

    return UsedBlocks;
}

ULONG ExtfsAllocationManagerGetAllocationEntryFreeBlocks(PEXTFS_ALLOCATION_ENTRY AllocationEntry)
{
    ULONG FreeBlocks = 0;
    PERESOURCE Lock = &AllocationEntry->BlockBitmapLock;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    FreeBlocks = ExtfsBitmapGetFreeEntries(AllocationEntry->BlockBitmap, AllocationEntry->BlockBitmapEntries);
    ExReleaseResourceLite(Lock);

    return FreeBlocks;
}

ULONGLONG ExtfsAllocationManagerGetAllocationEntryFreeBlock(PEXTFS_ALLOCATION_ENTRY AllocationEntry, BOOLEAN AllocateRequest)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    ULONG FreeBlock = 0;
    ULONGLONG FirstDataBlock = VolumeExtension->FirstDataBlock;
    PERESOURCE Lock = &AllocationEntry->BlockBitmapLock;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    FreeBlock = ExtfsBitmapGetFreeEntry(AllocationEntry->BlockBitmap, AllocationEntry->BlockBitmapEntries);
    if (AllocateRequest)
    {
        ExtfsBitmapSet(AllocationEntry->BlockBitmap, AllocationEntry->BlockBitmapEntries, FreeBlock);
        ExtfsBitmapSet(AllocationEntry->BlockBitmapFastWrite, AllocationEntry->BlockBitmapEntries, FreeBlock);
    }
    ExtfsAllocationManagerUpdateAllocationEntryGroupDescriptor(AllocationEntry, FALSE, TRUE);
    ExReleaseResourceLite(Lock);

    if (FreeBlock != (ULONG)-1)
        return (ULONGLONG)FreeBlock + FirstDataBlock + (ULONGLONG)(VolumeExtension->BlocksPerGroup * (AllocationEntry->Group - 1));
    else
        return 0;
}

BOOLEAN ExtfsAllocationManagerSetAllocationEntryBlock(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Block)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    BOOLEAN Result = FALSE;
    PERESOURCE Lock = &AllocationEntry->BlockBitmapLock;
    ULONG Group = (Block - VolumeExtension->FirstDataBlock) / VolumeExtension->BlocksPerGroup;
    ULONG EntryIndex = (Block - VolumeExtension->FirstDataBlock) % VolumeExtension->BlocksPerGroup;

    if (AllocationEntry->Group != Group + 1)
        return Result;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    Result = ExtfsBitmapSet(AllocationEntry->BlockBitmap, AllocationEntry->BlockBitmapEntries, EntryIndex);
    ExtfsAllocationManagerUpdateAllocationEntryGroupDescriptor(AllocationEntry, FALSE, TRUE);
    ExReleaseResourceLite(Lock);

    return Result;
}

BOOLEAN ExtfsAllocationManagerGetAllocationEntryBlock(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Block, BOOLEAN DisableProtection)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    BOOLEAN Result = FALSE;
    PERESOURCE Lock = &AllocationEntry->BlockBitmapLock;
    ULONG Group = (Block - VolumeExtension->FirstDataBlock) / VolumeExtension->BlocksPerGroup;
    ULONG EntryIndex = (Block - VolumeExtension->FirstDataBlock) % VolumeExtension->BlocksPerGroup;

    if (AllocationEntry->Group != Group + 1)
        return Result;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    Result = !ExtfsBitmapGet(AllocationEntry->BlockReservedBitmap, AllocationEntry->BlockBitmapEntries, EntryIndex);
    if (Result || DisableProtection)
        Result = ExtfsBitmapGet(AllocationEntry->BlockBitmap, AllocationEntry->BlockBitmapEntries, EntryIndex);
    ExReleaseResourceLite(Lock);

    return Result;
}

BOOLEAN ExtfsAllocationManagerClearAllocationEntryBlock(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Block)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    BOOLEAN Result = FALSE;
    PERESOURCE Lock = &AllocationEntry->BlockBitmapLock;
    ULONG Group = (Block - VolumeExtension->FirstDataBlock) / VolumeExtension->BlocksPerGroup;
    ULONG EntryIndex = (Block - VolumeExtension->FirstDataBlock) % VolumeExtension->BlocksPerGroup;

    if (AllocationEntry->Group != Group + 1)
        return Result;

    ExAcquireResourceExclusiveLite(Lock, TRUE);
    Result = !ExtfsBitmapGet(AllocationEntry->BlockReservedBitmap, AllocationEntry->BlockBitmapEntries, EntryIndex);
    if (Result)
    {
        Result = ExtfsBitmapClear(AllocationEntry->BlockBitmap, AllocationEntry->BlockBitmapEntries, EntryIndex) &&
                 ExtfsBitmapClear(AllocationEntry->BlockBitmapFastWrite, AllocationEntry->BlockBitmapEntries, EntryIndex);
    }
    ExtfsAllocationManagerUpdateAllocationEntryGroupDescriptor(AllocationEntry, FALSE, TRUE);
    ExReleaseResourceLite(Lock);

    return Result;
}

BOOLEAN ExtfsAllocationManagerCheckBlock(PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONGLONG Block)
{
    BOOLEAN Result = FALSE;
    PEXTFS_ALLOCATION_ENTRY AllocationEntry = ExtfsAllocationManagerAcquireAllocationEntryByBlock(AllocationManager, Block);
    if (!AllocationEntry)
        return Result;

    Result = ExtfsAllocationManagerGetAllocationEntryBlock(AllocationEntry, Block, FALSE);

    ExtfsAllocationManagerReleaseAllocationEntry(AllocationEntry);
    return Result;
}

BOOLEAN ExtfsAllocationManagerCheckInode(PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONGLONG Inode)
{
    BOOLEAN Result = FALSE;
    PEXTFS_ALLOCATION_ENTRY AllocationEntry = ExtfsAllocationManagerAcquireAllocationEntryByInode(AllocationManager, Inode);
    if (!AllocationEntry)
        return Result;

    Result = ExtfsAllocationManagerGetAllocationEntryInode(AllocationEntry, Inode);

    ExtfsAllocationManagerReleaseAllocationEntry(AllocationEntry);
    return Result;
}

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByFreeBlockOrInode(
    PEXTFS_ALLOCATION_MANAGER AllocationManager, BOOLEAN IsBlock)
{
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    PEXTFS_ALLOCATION_ENTRY AllocationEntry;
    PERESOURCE Lock;
    ULONG Group;
    PEXT_GROUP_DESC GroupDesc;
    ULONG LastFreeBlocksOrInodes = 0;
    ULONG LastGroup = 0;

    ExAcquireResourceExclusiveLite(&AllocationManager->MainLock, TRUE);
    ExAcquireResourceExclusiveLite(&AllocationManager->AllocationListLock, TRUE);

    AllocationEntry = ExtfsAllocationManagerFindAllocationEntryByFreeEntries(&AllocationManager->AllocationList,
                                                                             &AllocationManager->AllocationListLock,
                                                                             IsBlock);
    if (AllocationEntry)
    {
        LastGroup = AllocationEntry->Group;
        goto result;
    }

    for (Group = 0; Group < VolumeExtension->GroupDescCount; Group++)
    {
        GroupDesc = ExtfsGetGroupDesc(VolumeExtension, Group + 1);
        if (!GroupDesc)
        {
            DPRINT1("It should not fail\n");
            ASSERT(FALSE);
            continue;
        }

        ULONGLONG FreeBlocksOrInodesCount = IsBlock ?
            ReadFieldLE(GroupDesc->FreeBlocksCount) :
            ReadFieldLE(GroupDesc->FreeInodesCount);

        AllocationEntry = ExtfsAllocationManagerFindAllocationEntry(&AllocationManager->AllocationList,
                                                                    &AllocationManager->AllocationListLock,
                                                                    Group + 1);
        Lock = IsBlock ? &AllocationEntry->BlockBitmapLock : &AllocationEntry->InodeBitmapLock;

        if (AllocationEntry)
        {
            ExAcquireResourceExclusiveLite(Lock, TRUE);
            ExtfsAllocationManagerUpdateAllocationEntryGroupDescriptor(AllocationEntry, !IsBlock, TRUE);
        }

        if (FreeBlocksOrInodesCount > LastFreeBlocksOrInodes)
        {
            LastGroup = Group + 1;
            LastFreeBlocksOrInodes = FreeBlocksOrInodesCount;
        }

        if (AllocationEntry)
            ExReleaseResourceLite(Lock);
    }

result:
    AllocationEntry = ExtfsAllocationManagerAcquireAllocationEntryByGroup(AllocationManager, LastGroup);

    ExReleaseResourceLite(&AllocationManager->AllocationListLock);
    ExReleaseResourceLite(&AllocationManager->MainLock);
    return AllocationEntry;
}

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByFreeBlock(
    PEXTFS_ALLOCATION_MANAGER AllocationManager)
{
    return ExtfsAllocationManagerAcquireAllocationEntryByFreeBlockOrInode(AllocationManager, TRUE);
}

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByFreeInode(
    PEXTFS_ALLOCATION_MANAGER AllocationManager)
{
    return ExtfsAllocationManagerAcquireAllocationEntryByFreeBlockOrInode(AllocationManager, FALSE);
}

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByGroup(
    PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONG Group)
{
    ExAcquireResourceExclusiveLite(&AllocationManager->MainLock, TRUE);

    PEXTFS_ALLOCATION_ENTRY AllocationEntry = ExtfsAllocationManagerFindAllocationEntry(&AllocationManager->AllocationList,
                                                                                        &AllocationManager->AllocationListLock,
                                                                                        Group);
    if (!AllocationEntry)
    {
        AllocationEntry = ExtfsAllocationManagerGetAllocationEntry(AllocationManager, Group);
        if (!AllocationEntry)
        {
            DPRINT1("Cannot get AllocationEntry\n");
            ExReleaseResourceLite(&AllocationManager->MainLock);
            return NULL;
        }

        ExtfsAllocationManagerAddAllocationEntry(&AllocationManager->AllocationList,
                                                 &AllocationManager->AllocationListLock,
                                                 AllocationEntry);
    }

    AllocationEntry->ReferenceCount++;
    ExReleaseResourceLite(&AllocationManager->MainLock);
    return AllocationEntry;
}

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByBlock(
    PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONGLONG Block)
{
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    ULONG Group = (Block - VolumeExtension->FirstDataBlock) / VolumeExtension->BlocksPerGroup;

    return ExtfsAllocationManagerAcquireAllocationEntryByGroup(AllocationManager, Group + 1);
}

PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByInode(
    PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONGLONG Inode)
{
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    ULONG Group = (Inode - 1) / VolumeExtension->InodesPerGroup;

    return ExtfsAllocationManagerAcquireAllocationEntryByGroup(AllocationManager, Group + 1);
}

LARGE_INTEGER ExtfsAllocationManagerGetBlockInsideAllocationEntry(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Block)
{
    PEXTFS_ALLOCATION_MANAGER AllocationManager = AllocationEntry->AllocationManager;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = AllocationManager->VolumeExtension;
    ULONGLONG BlockSize = VolumeExtension->BlockSize;
    ULONGLONG BlocksPerGroup = VolumeExtension->BlocksPerGroup;
    ULONGLONG FirstDataBlock = VolumeExtension->FirstDataBlock;
    LARGE_INTEGER DiskOffset = {0};

    if (Block >= BlocksPerGroup)
    {
        DPRINT1("It should not overflow\n");
        ASSERT(FALSE);
        goto result;
    }

    DiskOffset.QuadPart = ((BlocksPerGroup * (ULONGLONG)(AllocationEntry->Group - 1)) + FirstDataBlock + Block) * BlockSize;

result:
    return DiskOffset;
}
