#include "extfs.h"

NTSTATUS ExtfsCheckSuperBlock(PEXT_SUPER_BLOCK SuperBlock)
{
    if (SuperBlock->Magic != EXT_SUPERBLOCK_MAGIC)
    {
        return STATUS_WRONG_VOLUME;
    }

    if (SuperBlock->LogBlockSize != SuperBlock->LogFragSize)
    {
        return STATUS_WRONG_VOLUME;
    }

    return STATUS_SUCCESS;
}

NTSTATUS ExtfsInitializeVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    PEXT_SUPER_BLOCK SuperBlock = &VolumeExtension->SuperBlock;
    PVOID GroupDescBuffer;
    LARGE_INTEGER DiskOffset;
    NTSTATUS Status;

    VolumeExtension->BlockSize = 1024 << SuperBlock->LogBlockSize;
    VolumeExtension->TotalBlocks = SuperBlock->BlocksCountLo;
    VolumeExtension->UsedBlocks = VolumeExtension->TotalBlocks - (ULONGLONG)SuperBlock->FreeBlocksCountLo;

    VolumeExtension->GroupDescCount = (SuperBlock->BlocksCountLo - SuperBlock->FirstDataBlock + SuperBlock->BlocksPerGroup - 1) / SuperBlock->BlocksPerGroup;

    VolumeExtension->InodeSizeInBytes = EXT_INODE_SIZE(SuperBlock);
    VolumeExtension->GroupDescSizeInBytes = EXT_GROUP_DESC_SIZE(SuperBlock);

    VolumeExtension->InodePerBlock = VolumeExtension->BlockSize / VolumeExtension->InodeSizeInBytes;
    VolumeExtension->GroupDescPerBlock = VolumeExtension->BlockSize / VolumeExtension->GroupDescSizeInBytes;
    VolumeExtension->PointersPerBlock = VolumeExtension->BlockSize / sizeof(ULONG);

    VolumeExtension->GroupDescBlocks = 
        (VolumeExtension->GroupDescCount + (VolumeExtension->GroupDescPerBlock - 1)) / VolumeExtension->GroupDescPerBlock;
    GroupDescBuffer = ExAllocatePoolWithTag(NonPagedPool, 
                                            VolumeExtension->GroupDescBlocks * VolumeExtension->BlockSize,
                                            EXTFS_TAG_GROUP_DESC);
    if (!GroupDescBuffer)
    {
        DPRINT1("Cannot allocate group descriptor buffer\n");
        return STATUS_NO_MEMORY;
    }

    DiskOffset.QuadPart = (VolumeExtension->SuperBlock.FirstDataBlock + 1) * VolumeExtension->BlockSize;
    Status = ExtfsDiskRead(VolumeExtension,
                           GroupDescBuffer,
                           (LARGE_INTEGER)DiskOffset,
                           VolumeExtension->GroupDescBlocks * VolumeExtension->BlockSize,
                           TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot read all group descriptor\n");
        ExFreePoolWithTag(GroupDescBuffer, EXTFS_TAG_GROUP_DESC);
        return Status;
    }

    VolumeExtension->GroupDescBuffer = GroupDescBuffer;

    strcpy(VolumeExtension->FileSystemName, "EXT");

    if (SuperBlock->FeatureIncompat & EXT_SB_FEATURE_INCOMPAT_EXTENTS)
        strcat(VolumeExtension->FileSystemName, "4");
    else if (SuperBlock->FeatureIncompat & EXT_SB_FEATURE_INCOMPAT_JOURNALING)
        strcat(VolumeExtension->FileSystemName, "3");
    else
        strcat(VolumeExtension->FileSystemName, "2");

    RtlCopyMemory(VolumeExtension->VolumeLabel, SuperBlock->VolumeName, sizeof(SuperBlock->VolumeName));

    DPRINT1("VolumeExtension = 0x%p\n", VolumeExtension);
    DPRINT1("VolumeExtension->VolumeDevice = 0x%p\n", VolumeExtension->VolumeDevice);
    DPRINT1("VolumeExtension->BlockSize = %u\n", VolumeExtension->BlockSize);
    DPRINT1("VolumeExtension->TotalBlocks = %I64u\n", VolumeExtension->TotalBlocks);
    DPRINT1("VolumeExtension->FileSystemName = %s\n", &VolumeExtension->FileSystemName);

    VolumeExtension->ReadOnly = TRUE;

    return STATUS_SUCCESS;
}
