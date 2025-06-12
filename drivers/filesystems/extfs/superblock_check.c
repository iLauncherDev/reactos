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

    VolumeExtension->BlockSize = 1024 << SuperBlock->LogBlockSize;
    VolumeExtension->TotalBlocks = SuperBlock->BlocksCountLo;

    VolumeExtension->InodeSizeInBytes = EXT_INODE_SIZE(SuperBlock);
    VolumeExtension->GroupDescSizeInBytes = EXT_GROUP_DESC_SIZE(SuperBlock);

    VolumeExtension->InodePerBlock = VolumeExtension->BlockSize / VolumeExtension->InodeSizeInBytes;
    VolumeExtension->GroupDescPerBlock = VolumeExtension->BlockSize / VolumeExtension->GroupDescSizeInBytes;

    VolumeExtension->ReadOnly = TRUE;

    return STATUS_SUCCESS;
}
