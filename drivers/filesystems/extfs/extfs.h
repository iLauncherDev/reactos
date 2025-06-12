#pragma once
#include <section_attribs.h>
#include <ntifs.h>
#include <ntddk.h>
#include "extfs-structs.h"

#define NDEBUG
#include <debug.h>

#define DEVICE_NAME L"\\Extfs"
#define EXTFS_TAG_EXTENT_LIST 'LtxE'
#define EXTFS_TAG_BUFFER 'BtxE'

typedef struct _EXTFS_GLOBAL_DATA
{
    ERESOURCE Resource;
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT DeviceObject;
    CACHE_MANAGER_CALLBACKS CacheMgrCallbacks;
    ULONG Flags;
    FAST_IO_DISPATCH FastIoDispatch;
} EXTFS_GLOBAL_DATA, *PEXTFS_GLOBAL_DATA;

typedef struct _EXTFS_VOLUME_EXTENSION
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT RealDevice;
    PVPB Vpb;

    CHAR FileSystemName[8];

    ULONG BlockSize;
    ULONGLONG TotalBlocks;

    ULONG InodeSizeInBytes, GroupDescSizeInBytes;
    ULONG InodePerBlock, GroupDescPerBlock;

    EXT_SUPER_BLOCK SuperBlock;

    SECTION_OBJECT_POINTERS SectionObjectPointers;

    BOOLEAN ReadOnly;
    FAST_MUTEX VolumeMutex;

    LIST_ENTRY OpenFileList;
    FAST_MUTEX OpenFileListLock;
} EXTFS_VOLUME_EXTENSION, *PEXTFS_VOLUME_EXTENSION;

typedef struct _EXTFS_EXTENT
{
    ULONGLONG Block, BlockInBytes;
    ULONGLONG Length, LengthInBytes;
    struct _EXTFS_EXTENT *Next;
    struct _EXTFS_EXTENT *Prev;
} EXTFS_EXTENT, *PEXTFS_EXTENT;

extern PEXTFS_GLOBAL_DATA ExtfsGlobalData;

// superblock_check.c
NTSTATUS ExtfsCheckSuperBlock(PEXT_SUPER_BLOCK SuperBlock);
NTSTATUS ExtfsInitializeVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension);

// disk.c
NTSTATUS DiskRead(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);
NTSTATUS DiskWrite(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);
NTSTATUS ExtfsDiskRead(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);
NTSTATUS ExtfsDiskWrite(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);

// inode.c
NTSTATUS ExtfsReadGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_GROUP_DESC Buffer, ULONG Group);
NTSTATUS ExtfsReadInode(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Buffer, ULONG Index);
ULONGLONG ExtfsGetInodeSize(PEXT_INODE Inode);

// inode-data.c
NTSTATUS 
ExtfsReadInodeData(
    PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Inode,
    PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);

// dispatch.c
NTSTATUS
NTAPI
ExtfsFsdDispatch(PDEVICE_OBJECT DeviceObject,
                 PIRP Irp);
