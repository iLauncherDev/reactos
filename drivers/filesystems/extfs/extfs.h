#pragma once
#include <section_attribs.h>
#include <ntifs.h>
#include <ntddk.h>
#include "extfs-structs.h"

#define NDEBUG
#include <debug.h>

#define DEVICE_NAME L"\\Extfs"
#define EXTFS_TAG_INODE_CONTEXT 'CIxE'
#define EXTFS_TAG_FILE_CONTEXT 'CFxE'
#define EXTFS_TAG_EXTENT_LIST 'LExE'
#define EXTFS_TAG_BUFFER 'BtxE'
#define EXTFS_TAG_SUPER_BLOCK 'StxE'
#define EXTFS_TAG_GROUP_DESC 'GtxE'
#define EXTFS_FILE_CTX_MAGIC (((ULONGLONG)'XTCs' << 32) | (ULONGLONG)'ftxE')

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
    PDEVICE_OBJECT VolumeDevice;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT RealDevice;
    PVPB Vpb;

    CHAR FileSystemName[8];

    ULONG BlockSize;
    ULONGLONG TotalBlocks;

    ULONG GroupDescCount, GroupDescBlocks;
    PVOID GroupDescBuffer;

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

typedef struct _EXTFS_INODE_CONTEXT
{
    PCHAR CacheBuffer;
    ULONG CacheBufferSize;
    ULONGLONG CacheBufferOffset;
    PEXTFS_VOLUME_EXTENSION VolumeExtension;
    PEXTFS_EXTENT ExtentList;
    ULONGLONG FileSize;
    BOOLEAN IsDirectory;
    ULONG InodeNum;
    EXT_INODE Inode;
} EXTFS_INODE_CONTEXT, *PEXTFS_INODE_CONTEXT;

typedef struct _EXTFS_FILE_CONTEXT
{
    ULONGLONG Magic;
    ULONGLONG CurrentOffset, CurrentDirectoryOffset;
    ULONG CurrentFBDOffset, OldFBDOffset;
    PEXTFS_INODE_CONTEXT InodeContext;
} EXTFS_FILE_CONTEXT, *PEXTFS_FILE_CONTEXT;

extern PEXTFS_GLOBAL_DATA ExtfsGlobalData;

// superblock_check.c
NTSTATUS ExtfsCheckSuperBlock(PEXT_SUPER_BLOCK SuperBlock);
NTSTATUS ExtfsInitializeVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension);

// disk.c
NTSTATUS DiskRead(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);
NTSTATUS DiskWrite(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);
#define ExtfsDiskRead(VolumeExtension, Buffer, Offset, Length) \
    DiskRead((VolumeExtension)->VolumeDevice, Buffer, Offset, Length)
#define ExtfsDiskWrite(VolumeExtension, Buffer, Offset, Length) \
    DiskWrite((VolumeExtension)->VolumeDevice, Buffer, Offset, Length)

// inode.c
NTSTATUS ExtfsReadGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_GROUP_DESC Buffer, ULONG Group);
NTSTATUS ExtfsReadInode(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Buffer, ULONG Index);
ULONGLONG ExtfsGetInodeSize(PEXT_INODE Inode);

// inode-data.c
PEXTFS_INODE_CONTEXT
ExtfsPrepareInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Inode);
VOID
ExtfsReleaseInodeContext(PEXTFS_INODE_CONTEXT InodeContext);
ULONG 
ExtfsReadInodeData(
    PEXTFS_INODE_CONTEXT InodeContext,
    PCHAR Buffer, LARGE_INTEGER Offset, ULONG Length);
PEXTFS_INODE_CONTEXT ExtfsReadInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, ULONG Index);

// directory.c
ULONG ExtfsFindFile(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_INODE_CONTEXT InodeContext, PCHAR Name);
PEXTFS_INODE_CONTEXT ExtfsFindFileByPath(PEXTFS_VOLUME_EXTENSION VolumeExtension, PCHAR FilePath);
ULONG ExtfsQueryDirectoryList(PEXTFS_FILE_CONTEXT FileContext, PFILE_BOTH_DIR_INFORMATION OutputList, ULONG SizeLimit);

// dispatch.c
NTSTATUS
NTAPI
ExtfsFsdDispatch(PDEVICE_OBJECT DeviceObject,
                 PIRP Irp);
