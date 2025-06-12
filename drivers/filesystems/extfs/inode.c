#include "extfs.h"

PEXT_GROUP_DESC ExtfsGetGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, ULONG Group)
{
    DPRINT1("ExtfsGetGroupDesc(0x%p, %u)\n", VolumeExtension, Group);

    if (Group >= VolumeExtension->GroupDescCount)
        return NULL;

    ULONG GroupOffset = Group * VolumeExtension->GroupDescSizeInBytes;

    return (PVOID)((PCHAR)VolumeExtension->GroupDescBuffer + GroupOffset);
}

NTSTATUS ExtfsReadGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_GROUP_DESC Buffer, ULONG Group)
{
    DPRINT1("ExtfsReadGroupDesc(0x%p, 0x%p, %u)\n", VolumeExtension, Buffer, Group);

    if (Group >= VolumeExtension->GroupDescCount)
        return STATUS_INVALID_PARAMETER;

    ULONG GroupOffset = Group * VolumeExtension->GroupDescSizeInBytes;

    RtlCopyMemory(Buffer, (PCHAR)VolumeExtension->GroupDescBuffer + GroupOffset, sizeof(*Buffer));
    return STATUS_SUCCESS;
}

NTSTATUS ExtfsReadInode(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Buffer, ULONG Index)
{
    if (Index < 1)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("ExtfsReadInode(0x%p, 0x%p, %u)\n", VolumeExtension, Buffer, Index);

    NTSTATUS Status;
    LARGE_INTEGER Offset;
    ULONGLONG Group = (Index - 1) / VolumeExtension->SuperBlock.InodesPerGroup;
    ULONGLONG InodeIndex = (Index - 1) % VolumeExtension->SuperBlock.InodesPerGroup;
    ULONGLONG InodeIndexOffset = InodeIndex * VolumeExtension->InodeSizeInBytes;
    ULONGLONG InodeIndexOffsetInBlock = (InodeIndex * VolumeExtension->InodeSizeInBytes) / VolumeExtension->BlockSize;
    PEXT_GROUP_DESC GroupDesc = ExtfsGetGroupDesc(VolumeExtension, (ULONG)Group);
    PCHAR BlockBuffer = ExAllocatePoolWithTag(NonPagedPool, VolumeExtension->BlockSize, EXTFS_TAG_BUFFER);
    InodeIndexOffset -= InodeIndexOffsetInBlock * VolumeExtension->BlockSize;

    if (!BlockBuffer)
    {
        DPRINT1("Cannot allocate BlockBuffer\n");
        return STATUS_NO_MEMORY;
    }

    Offset.QuadPart = ((ULONGLONG)GroupDesc->InodeTable + InodeIndexOffsetInBlock) * (ULONGLONG)VolumeExtension->BlockSize;
    Status = ExtfsDiskRead(VolumeExtension, BlockBuffer, Offset, VolumeExtension->BlockSize, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot read InodeBlock to BlockBuffer\n");
        ExFreePoolWithTag(BlockBuffer, EXTFS_TAG_BUFFER);
        return Status;
    }

    RtlCopyMemory(Buffer, BlockBuffer + InodeIndexOffset, sizeof(*Buffer));
    ExFreePoolWithTag(BlockBuffer, EXTFS_TAG_BUFFER);
    return STATUS_SUCCESS;
}

ULONGLONG ExtfsGetInodeSize(PEXT_INODE Inode)
{
    if ((Inode->Mode & EXT_S_IFMT) == EXT_S_IFDIR)
    {
        return (ULONGLONG)Inode->Size;
    }
    else
    {
        return ((ULONGLONG)(Inode->DirACL) << 32) | (ULONGLONG)Inode->Size;
    }
}
