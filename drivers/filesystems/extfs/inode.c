#include "extfs.h"

NTSTATUS ExtfsReadGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_GROUP_DESC Buffer, ULONG Group)
{
    NTSTATUS Status;
    ULONGLONG GroupOffset = Group * VolumeExtension->GroupDescSizeInBytes;
    LARGE_INTEGER Offset;
    Offset.QuadPart = (VolumeExtension->SuperBlock.FirstDataBlock * VolumeExtension->BlockSize) + GroupOffset;

    Status = ExtfsDiskRead(VolumeExtension, Buffer, Offset, sizeof(*Buffer));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS ExtfsReadInode(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Buffer, ULONG Index)
{
    if (Index < 1)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS Status;
    LARGE_INTEGER Offset;
    EXT_GROUP_DESC GroupDesc;
    ULONGLONG Group = (Index - 1) / VolumeExtension->SuperBlock.InodesPerGroup;
    ULONGLONG InodeIndex = (Index - 1) % VolumeExtension->SuperBlock.InodesPerGroup;
    ULONGLONG InodeIndexOffset = InodeIndex * VolumeExtension->InodeSizeInBytes;

    Status = ExtfsReadGroupDesc(VolumeExtension, &GroupDesc, Group);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Offset.QuadPart = (GroupDesc.InodeTable * VolumeExtension->BlockSize) + InodeIndexOffset;

    return ExtfsDiskRead(VolumeExtension, Buffer, Offset, sizeof(*Buffer));
}

ULONGLONG ExtfsGetInodeSize(PEXT_INODE Inode)
{
    if ((Inode->Mode & EXT_S_IFMT) == EXT_S_IFDIR)
    {
        return (ULONGLONG)Inode->Size;
    }
    else
    {
        return (ULONGLONG)Inode->Size | (ULONGLONG)(Inode->DirACL) << 32;
    }
}
