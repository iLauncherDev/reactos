#include "extfs.h"

NTSTATUS ExtfsCheckSuperBlock(PEXT_SUPER_BLOCK SuperBlock)
{
    if (ReadFieldLE(SuperBlock->Magic) != EXT_SUPERBLOCK_MAGIC)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (ReadFieldLE(SuperBlock->LogBlockSize) != ReadFieldLE(SuperBlock->LogFragSize))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    return STATUS_SUCCESS;
}

BOOLEAN ExtfsAcquireSuperBlockReadLock(PEXTFS_VOLUME_EXTENSION VolumeExtension, BOOLEAN Wait)
{
    return ExAcquireResourceSharedLite(&VolumeExtension->SuperBlockLock, Wait);
}

BOOLEAN ExtfsAcquireSuperBlockWriteLock(PEXTFS_VOLUME_EXTENSION VolumeExtension, BOOLEAN Wait)
{
    return ExAcquireResourceExclusiveLite(&VolumeExtension->SuperBlockLock, Wait);
}

VOID ExtfsReleaseSuperBlockLock(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    return ExReleaseResourceLite(&VolumeExtension->SuperBlockLock);
}

VOID ExtfsFlushSuperBlock(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    PEXT_SUPER_BLOCK SuperBlock = &VolumeExtension->SuperBlock;
    ULONG Length = 1024;
    LARGE_INTEGER DiskOffset = {.QuadPart = 1024};

    if (VolumeExtension->ReadOnly)
        return;

    ExtfsDiskWrite(VolumeExtension, SuperBlock, DiskOffset, Length);
}

VOID ExtfsUpdateVolumeExtensionUsage(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    PEXT_SUPER_BLOCK SuperBlock = &VolumeExtension->SuperBlock;

    VolumeExtension->TotalBlocks = ReadFieldLE(SuperBlock->BlocksCountLo);
    VolumeExtension->UsedBlocks = VolumeExtension->TotalBlocks - ReadFieldLE(SuperBlock->FreeBlocksCountLo);
}

NTSTATUS ExtfsInitializeVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension, BOOLEAN ReadOnly)
{
    PEXT_SUPER_BLOCK SuperBlock = &VolumeExtension->SuperBlock;
    PVOID GroupDescBuffer;
    LARGE_INTEGER DiskOffset;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN IsReadOnly = ReadOnly;
    PCHAR TempUUID = (PVOID)&VolumeExtension->TempUUID;
    ULONG TempUUIDMaxChars = sizeof(VolumeExtension->TempUUID) / sizeof(*VolumeExtension->TempUUID);
    UCHAR UuidPosition = 0;
    ULONG UuidCursor = 0;

    DiskOffset.QuadPart = 1024;
    Status = ExtfsDiskRead(VolumeExtension, SuperBlock, DiskOffset, sizeof(*SuperBlock));
    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_UNRECOGNIZED_VOLUME;
        goto result;
    }

    Status = ExtfsCheckSuperBlock(SuperBlock);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Invalid superblock!\n");
        goto result;
    }

    VolumeExtension->BlocksPerGroup = ReadFieldLE(SuperBlock->BlocksPerGroup);
    VolumeExtension->InodesPerGroup = ReadFieldLE(SuperBlock->InodesPerGroup);

    VolumeExtension->BlockSize = 1024 << ReadFieldLE(SuperBlock->LogBlockSize);
    VolumeExtension->DiskBlockSize = max(VolumeExtension->DiskBlockSize, VolumeExtension->BlockSize);

    VolumeExtension->TotalBlocks = ReadFieldLE(SuperBlock->BlocksCountLo);
    VolumeExtension->UsedBlocks = VolumeExtension->TotalBlocks - ReadFieldLE(SuperBlock->FreeBlocksCountLo);
    VolumeExtension->FirstDataBlock = ReadFieldLE(SuperBlock->FirstDataBlock);

    VolumeExtension->GroupDescCount =
        ((VolumeExtension->TotalBlocks - VolumeExtension->FirstDataBlock) + (VolumeExtension->BlocksPerGroup - 1)) / VolumeExtension->BlocksPerGroup;

    VolumeExtension->InodeSizeInBytes = EXT_INODE_SIZE(SuperBlock);
    VolumeExtension->GroupDescSizeInBytes = EXT_GROUP_DESC_SIZE(SuperBlock);

    VolumeExtension->InodesPerBlock = VolumeExtension->BlockSize / VolumeExtension->InodeSizeInBytes;
    VolumeExtension->GroupDescPerBlock = VolumeExtension->BlockSize / VolumeExtension->GroupDescSizeInBytes;
    VolumeExtension->PointersPerBlock = VolumeExtension->BlockSize / sizeof(ULONG);

    VolumeExtension->IsExcludeBitmapAvailable = !!(ReadFieldLE(SuperBlock->FeatureCompat) & EXT_SB_FEATURE_COMPAT_EXCLUDE_BITMAP);

    VolumeExtension->GroupDescBlocks = 
        (VolumeExtension->GroupDescCount + (VolumeExtension->GroupDescPerBlock - 1)) / VolumeExtension->GroupDescPerBlock;
    GroupDescBuffer = ExAllocatePoolWithTag(NonPagedPool, 
                                            VolumeExtension->GroupDescBlocks * VolumeExtension->BlockSize,
                                            EXTFS_TAG_GROUP_DESC);
    if (!GroupDescBuffer)
    {
        DPRINT1("Cannot allocate group descriptor buffer\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    DiskOffset.QuadPart = (VolumeExtension->FirstDataBlock + 1) * VolumeExtension->BlockSize;
    Status = ExtfsDiskRead(VolumeExtension,
                           GroupDescBuffer,
                           (LARGE_INTEGER)DiskOffset,
                           VolumeExtension->GroupDescBlocks * VolumeExtension->BlockSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot read all group descriptor\n");
        ExFreePoolWithTag(GroupDescBuffer, EXTFS_TAG_GROUP_DESC);
        goto result;
    }

    VolumeExtension->GroupDescBuffer = GroupDescBuffer;

    for (ULONG y = 0; y < sizeof(UuidDigitsList) / sizeof(*UuidDigitsList); y++)
    {
        ULONG UuidBytesCount = UuidDigitsList[y] / 2;

        for (ULONG x = UuidPosition; x < UuidPosition + UuidBytesCount; x++)
        {
            if (TempUUIDMaxChars - UuidCursor < 2)
                break;

            UCHAR Value = VolumeExtension->SuperBlock.UUID[x];

            RtlStringCchPrintfA(&TempUUID[UuidCursor], TempUUIDMaxChars - UuidCursor, "%02x", Value);
            UuidCursor += min(TempUUIDMaxChars - UuidCursor, 2);
        }

        UuidPosition += UuidBytesCount;

        if (UuidPosition < sizeof(VolumeExtension->SuperBlock.UUID) &&
            TempUUIDMaxChars - UuidCursor > 0)
            RtlStringCchPrintfA(&TempUUID[UuidCursor], TempUUIDMaxChars - UuidCursor, "-");
        UuidCursor += min(TempUUIDMaxChars - UuidCursor, 1);
    }

    strcpy(VolumeExtension->FileSystemName, "EXT");

    if (SuperBlock->FeatureIncompat & EXT_SB_FEATURE_INCOMPAT_EXT4)
        strcat(VolumeExtension->FileSystemName, "4"), IsReadOnly = TRUE;
    else if (SuperBlock->FeatureIncompat & EXT_SB_FEATURE_INCOMPAT_EXT3 ||
             SuperBlock->FeatureCompat & EXT_SB_FEATURE_COMPAT_EXT3)
        strcat(VolumeExtension->FileSystemName, "3");
    else
        strcat(VolumeExtension->FileSystemName, "2");

    RtlCopyMemory(VolumeExtension->VolumeLabel, SuperBlock->VolumeName, sizeof(SuperBlock->VolumeName));

    DPRINT1("VolumeExtension = 0x%p\n", VolumeExtension);
    DPRINT1("VolumeExtension->DeviceObject = 0x%p\n", VolumeExtension->DeviceObject);
    DPRINT1("VolumeExtension->RealDevice = 0x%p\n", VolumeExtension->RealDevice);
    DPRINT1("VolumeExtension->BlocksPerGroup = %u\n", VolumeExtension->BlocksPerGroup);
    DPRINT1("VolumeExtension->InodesPerGroup = %u\n", VolumeExtension->InodesPerGroup);
    DPRINT1("VolumeExtension->BytesPerSector = %u\n", VolumeExtension->BytesPerSector);
    DPRINT1("VolumeExtension->BlockSize = %u\n", VolumeExtension->BlockSize);
    DPRINT1("VolumeExtension->TotalBlocks = %I64u\n", VolumeExtension->TotalBlocks);
    DPRINT1("VolumeExtension->FileSystemName = %s\n", &VolumeExtension->FileSystemName);
    DPRINT1("VolumeExtension->IsExcludeBitmapAvailable = %s\n",
        VolumeExtension->IsExcludeBitmapAvailable ? "TRUE" : "FALSE");

    VolumeExtension->ReadOnly = IsReadOnly;

    Status = ExtfsAllocationManagerInitialize(VolumeExtension, &VolumeExtension->AllocationManager);
result:
    return Status;
}

NTSTATUS ExtfsLockVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    KIRQL OldIrql;
    PVPB RealDeviceVpb = VolumeExtension->Vpb;

    if (RealDeviceVpb)
    {
        IoAcquireVpbSpinLock(&OldIrql);

        RealDeviceVpb->Flags |= VPB_LOCKED;

        IoReleaseVpbSpinLock(OldIrql);
    }

    ExtfsAllocationManagerForceFlush(&VolumeExtension->AllocationManager);
    VolumeExtension->Locked = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS ExtfsUnlockVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    KIRQL OldIrql;
    PVPB RealDeviceVpb = VolumeExtension->Vpb;

    if (RealDeviceVpb)
    {
        IoAcquireVpbSpinLock(&OldIrql);

        RealDeviceVpb->Flags &= ~VPB_LOCKED;

        IoReleaseVpbSpinLock(OldIrql);
    }

    VolumeExtension->Locked = FALSE;
    return STATUS_SUCCESS;
}

/* WIP */
NTSTATUS ExtfsUninitializeVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension, BOOLEAN WillDestroy)
{
    KIRQL OldIrql;
    PVPB RealDeviceVpb = VolumeExtension->Vpb;

    ExtfsAllocationManagerUninitialize(&VolumeExtension->AllocationManager);

    if (WillDestroy)
    {
        ExDeleteResourceLite(&VolumeExtension->SuperBlockLock);
        ExDeleteResourceLite(&VolumeExtension->OpenFileListLock);
        ExDeleteResourceLite(&VolumeExtension->InodeContextListLock);

        ExDeleteResourceLite(&VolumeExtension->FileOperationLock);
        ExDeleteResourceLite(&VolumeExtension->DiskOperationLock);
    }

    if (VolumeExtension->GroupDescBuffer)
    {
        ExFreePoolWithTag(VolumeExtension->GroupDescBuffer, EXTFS_TAG_GROUP_DESC);
        VolumeExtension->GroupDescBuffer = NULL;
    }

    IoAcquireVpbSpinLock(&OldIrql);

    if (RealDeviceVpb)
    {
        RealDeviceVpb->Flags &= ~VPB_MOUNTED;

        if (WillDestroy)
        {
            RealDeviceVpb->DeviceObject = NULL;
            RealDeviceVpb->ReferenceCount--;
            VolumeExtension->DeviceObject->Vpb = VolumeExtension->Vpb = NULL;
        }
    }

    IoReleaseVpbSpinLock(OldIrql);

    if (VolumeExtension->StreamFileObject && WillDestroy)
    {
        ObDereferenceObject(VolumeExtension->StreamFileObject);
        VolumeExtension->StreamFileObject = NULL;
    }

    if (WillDestroy)
    {
        ExtfsReleaseVolumeNumber(VolumeExtension->GlobalData, VolumeExtension->VolumeNumber);
        IoDeleteDevice(VolumeExtension->DeviceObject);
    }

    return STATUS_SUCCESS;
}
