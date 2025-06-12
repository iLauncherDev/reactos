#include "extfs.h"

PEXT_GROUP_DESC ExtfsGetGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, ULONG Group)
{
    DPRINT("ExtfsGetGroupDesc(0x%p, %u)\n", VolumeExtension, Group);

    if (Group < 1 || Group > VolumeExtension->GroupDescCount)
        return NULL;

    ULONG GroupOffset = (Group - 1) * VolumeExtension->GroupDescSizeInBytes;

    return (PVOID)((PCHAR)VolumeExtension->GroupDescBuffer + GroupOffset);
}

NTSTATUS ExtfsReadGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_GROUP_DESC Buffer, ULONG Group)
{
    DPRINT("ExtfsReadGroupDesc(0x%p, 0x%p, %u)\n", VolumeExtension, Buffer, Group);

    PEXT_GROUP_DESC Source = ExtfsGetGroupDesc(VolumeExtension, Group);
    if (!Source)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Buffer, sizeof(*Buffer));
    RtlCopyMemory(Buffer, Source, min(sizeof(*Buffer), VolumeExtension->GroupDescSizeInBytes));
    return STATUS_SUCCESS;
}

NTSTATUS ExtfsWriteGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_GROUP_DESC Buffer, ULONG Group)
{
    LARGE_INTEGER DiskOffset;
    ULONGLONG FirstDataBlock = VolumeExtension->FirstDataBlock;
    ULONGLONG BlockSize = VolumeExtension->BlockSize;

    DPRINT("ExtfsWriteGroupDesc(0x%p, 0x%p, %u)\n", VolumeExtension, Buffer, Group);
    if (Group < 1 || Group > VolumeExtension->GroupDescCount)
        return STATUS_INVALID_PARAMETER;

    DiskOffset.QuadPart = ((FirstDataBlock + 1) * BlockSize) + (Group - 1) * (ULONGLONG)VolumeExtension->GroupDescSizeInBytes;
    return ExtfsDiskWrite(VolumeExtension, Buffer, DiskOffset, min(sizeof(*Buffer), VolumeExtension->GroupDescSizeInBytes));
}

NTSTATUS ExtfsReadInode(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Buffer, ULONGLONG Index)
{
    DPRINT("ExtfsReadInode(0x%p, 0x%p, %u)\n", VolumeExtension, Buffer, Index);

    if (Index < 1)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS Status;
    LARGE_INTEGER Offset;
    ULONGLONG Group = (Index - 1) / VolumeExtension->InodesPerGroup;
    ULONGLONG InodeIndex = (Index - 1) % VolumeExtension->InodesPerGroup;
    ULONGLONG InodeIndexOffset = InodeIndex * VolumeExtension->InodeSizeInBytes;
    PEXT_GROUP_DESC GroupDesc = ExtfsGetGroupDesc(VolumeExtension, (ULONG)(Group + 1));
    if (!GroupDesc)
    {
        DPRINT1("Cannot get GroupDescriptor\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Buffer, sizeof(*Buffer));

    Offset.QuadPart = (ReadFieldLE(GroupDesc->InodeTable) * (ULONGLONG)VolumeExtension->BlockSize) + InodeIndexOffset;
    Status = ExtfsFastDiskRead(VolumeExtension, Buffer, Offset, min(sizeof(*Buffer), VolumeExtension->InodeSizeInBytes), 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot read InodeBlock to Buffer\n");
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS ExtfsWriteInode(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Buffer, ULONGLONG Index)
{
    DPRINT("ExtfsWriteInode(0x%p, 0x%p, %u)\n", VolumeExtension, Buffer, Index);

    if (Index < 1)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS Status;
    LARGE_INTEGER Offset;
    ULONGLONG Group = (Index - 1) / VolumeExtension->InodesPerGroup;
    ULONGLONG InodeIndex = (Index - 1) % VolumeExtension->InodesPerGroup;
    ULONGLONG InodeIndexOffset = InodeIndex * VolumeExtension->InodeSizeInBytes;
    PEXT_GROUP_DESC GroupDesc = ExtfsGetGroupDesc(VolumeExtension, (ULONG)(Group + 1));
    if (!GroupDesc)
    {
        DPRINT1("Cannot get GroupDescriptor\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Offset.QuadPart = (ReadFieldLE(GroupDesc->InodeTable) * (ULONGLONG)VolumeExtension->BlockSize) + InodeIndexOffset;
    Status = ExtfsFastDiskWrite(VolumeExtension, Buffer, Offset, min(sizeof(*Buffer), VolumeExtension->InodeSizeInBytes), 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Cannot write Inode to disk\n");
        return Status;
    }

    return STATUS_SUCCESS;
}

ULONGLONG ExtfsGetInodeSize(PEXT_INODE Inode)
{
    if ((ReadFieldLE(Inode->Mode) & EXT_S_IFMT) == EXT_S_IFDIR)
    {
        return (ULONGLONG)ReadFieldLE(Inode->Size);
    }
    else
    {
        return ((ULONGLONG)(ReadFieldLE(Inode->DirACL)) << 32) | (ULONGLONG)ReadFieldLE(Inode->Size);
    }
}

VOID ExtfsSetInodeSize(PEXT_INODE Inode, ULONGLONG InodeSize)
{
    ULONG LowPart = InodeSize & 0xFFFFFFFF;
    ULONG HighPart = (InodeSize >> 32) & 0xFFFFFFFF;

    WriteFieldLE(Inode->Size, LowPart);
    if ((Inode->Mode & EXT_S_IFMT) != EXT_S_IFDIR)
    {
        WriteFieldLE(Inode->DirACL, HighPart);
    }
}
