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
#define EXTFS_TAG_FILE_STREAM 'SFxE'
#define EXTFS_TAG_EXTENT_LIST 'LExE'
#define EXTFS_TAG_BUFFER 'BtxE'
#define EXTFS_TAG_SUPER_BLOCK 'StxE'
#define EXTFS_TAG_GROUP_DESC 'GtxE'
#define EXTFS_TAG_WORKER_CONTEXT 'CWxE'

#define EXTFS_VOLUME_EXTENSION_MAGIC 'EVxE'
#define EXTFS_FILE_CTX_MAGIC 'XTFE'
#define EXTFS_FILE_STR_MAGIC 'RTFE'
#define EXTFS_GLOBAL_DATA_MAGIC 'DGxE'

#define EXTFS_DIRECTORY_MAX_REPARSE_RECURSION 8

typedef struct _EXTFS_IDENTIFIER
{
    ULONG Magic;
    ULONG Size;
} EXTFS_IDENTIFIER, *PEXTFS_IDENTIFIER;

typedef struct _EXTFS_GLOBAL_DATA
{
    EXTFS_IDENTIFIER Identifier;
    ERESOURCE Resource;
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT DeviceObject;
    CACHE_MANAGER_CALLBACKS CacheMgrCallbacks;
    ULONG Flags;
    FAST_IO_DISPATCH FastIoDispatch;
} EXTFS_GLOBAL_DATA, *PEXTFS_GLOBAL_DATA;

typedef struct _EXTFS_EXTENT
{
    BOOLEAN IsSparse;
    ULONGLONG Block, BlockInBytes;
    ULONGLONG Length, LengthInBytes;

    struct _EXTFS_EXTENT *Next;
    struct _EXTFS_EXTENT *Prev;
} EXTFS_EXTENT, *PEXTFS_EXTENT;

typedef struct _EXTFS_DISK_COMPLETION_ROUNTINE_CTX
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
} EXTFS_DISK_COMPLETION_ROUNTINE_CTX, *PEXTFS_DISK_COMPLETION_ROUNTINE_CTX;

typedef struct _EXTFS_VOLUME_EXTENSION
{
    EXTFS_IDENTIFIER Identifier;
    PDEVICE_OBJECT VolumeDevice;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT RealDevice;
    PVPB Vpb;

    CHAR VolumeLabel[16];
    CHAR FileSystemName[8];

    ULONG BlockSize;
    ULONGLONG UsedBlocks;
    ULONGLONG TotalBlocks;

    ULONG GroupDescCount, GroupDescBlocks;
    PVOID GroupDescBuffer;

    ULONG InodeSizeInBytes, GroupDescSizeInBytes;
    ULONG InodePerBlock, GroupDescPerBlock;
    ULONG PointersPerBlock;

    EXT_SUPER_BLOCK SuperBlock;

    BOOLEAN ReadOnly;

    PVOID OpenFileList;
    ERESOURCE FileOperationLock;
} EXTFS_VOLUME_EXTENSION, *PEXTFS_VOLUME_EXTENSION;

typedef struct _EXTFS_INODE_CONTEXT
{
    PCHAR CacheBuffer;
    ULONG CacheBufferSize;
    ULONGLONG CacheBufferOffset;
    PEXTFS_VOLUME_EXTENSION VolumeExtension;
    PEXTFS_EXTENT ExtentList;
    ULONGLONG FileSize;
    BOOLEAN IsDirectory, IsReparsePoint;
    ULONG InodeNum;
    EXT_INODE Inode;
} EXTFS_INODE_CONTEXT, *PEXTFS_INODE_CONTEXT;

typedef struct _EXTFS_STANDARD_FCB
{
    FSRTL_COMMON_FCB_HEADER StandardHeader;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    CC_FILE_SIZES FileSizes;
    EXTFS_IDENTIFIER Identifier;
    ERESOURCE MainResource, PagingIoResource;
} EXTFS_STANDARD_FCB, *PEXTFS_STANDARD_FCB;

typedef struct _EXTFS_FILE_STREAM
{
    EXTFS_STANDARD_FCB StandardFCB;
    PVOID FileContext;

    ULONGLONG CurrentOffset, CurrentDirectoryOffset;
    ULONG CurrentFBDOffset, OldFBDOffset;

    struct _EXTFS_FILE_STREAM *Next;
    struct _EXTFS_FILE_STREAM *Prev;
} EXTFS_FILE_STREAM, *PEXTFS_FILE_STREAM;

typedef struct _EXTFS_FILE_CONTEXT
{
    EXTFS_STANDARD_FCB StandardFCB;

    PEXTFS_INODE_CONTEXT InodeContext;
    FILE_LOCK FileLock;

    PEXTFS_FILE_STREAM Streams;

    PCHAR FilePath;

    struct _EXTFS_FILE_CONTEXT *Next;
    struct _EXTFS_FILE_CONTEXT *Prev;
} EXTFS_FILE_CONTEXT, *PEXTFS_FILE_CONTEXT;

typedef struct _EXTFS_IRP_WORKER_CONTEXT
{
    PNTSTATUS Status;
    PIO_WORKITEM WorkItem;
    PIRP Irp;
} EXTFS_IRP_WORKER_CONTEXT, *PEXTFS_IRP_WORKER_CONTEXT;

extern PEXTFS_GLOBAL_DATA ExtfsGlobalData;

#define SECONDS_BETWEEN_EPOCHS 11644473600LL
#define HUNDREDS_OF_NANOSECONDS 10000000LL

static inline LONGLONG UnixTimeToWindowsTime(ULONGLONG UnixTime)
{
    return (UnixTime + SECONDS_BETWEEN_EPOCHS) * HUNDREDS_OF_NANOSECONDS;
}

static inline LONGLONG WindowsTimeToUnixTime(ULONGLONG WindowsTime)
{
    return (WindowsTime / HUNDREDS_OF_NANOSECONDS) - SECONDS_BETWEEN_EPOCHS;
}

// utils.c
BOOLEAN ExtfsMatchExpressionA(PCHAR Pattern, PCHAR Name, BOOLEAN CaseInsensitive);

// superblock_check.c
NTSTATUS ExtfsCheckSuperBlock(PEXT_SUPER_BLOCK SuperBlock);
NTSTATUS ExtfsInitializeVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension);

// disk.c
NTSTATUS DiskRead(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait);
NTSTATUS DiskWrite(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait);
#define ExtfsDiskRead(VolumeExtension, Buffer, Offset, Length, Wait) \
    DiskRead((VolumeExtension)->VolumeDevice, Buffer, Offset, Length, Wait)
#define ExtfsDiskWrite(VolumeExtension, Buffer, Offset, Length, Wait) \
    DiskWrite((VolumeExtension)->VolumeDevice, Buffer, Offset, Length, Wait)

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
PEXTFS_INODE_CONTEXT ExtfsFindFileByPath(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PCHAR FilePath,
    BOOLEAN ResolveReparse, ULONG ReparseRecursion);
ULONG ExtfsQueryDirectoryListFBD(PEXTFS_FILE_STREAM FileStream, PFILE_BOTH_DIR_INFORMATION OutputList, ULONG SizeLimit, PCHAR FilterName);
ULONG ExtfsQueryDirectoryListFD(PEXTFS_FILE_STREAM FileStream, PFILE_DIRECTORY_INFORMATION OutputList, ULONG SizeLimit, PCHAR FilterName);

// dispatch.c
PVOID ExtfsFsdGetBuffer(PIRP Irp);
NTSTATUS
NTAPI
ExtfsFsdDispatch(PDEVICE_OBJECT DeviceObject,
                 PIRP Irp);

// create.c
PEXTFS_FILE_CONTEXT ExtfsFindExistingFileContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, PCHAR FilePath);
VOID ExtfsAddFileContextToList(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_FILE_CONTEXT FileContext);
VOID ExtfsRemoveFileContextFromList(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_FILE_CONTEXT FileContext);
VOID ExtfsAddFileStreamToList(PEXTFS_FILE_CONTEXT FileContext, PEXTFS_FILE_STREAM FileStream);
VOID ExtfsRemoveFileStreamFromList(PEXTFS_FILE_CONTEXT FileContext, PEXTFS_FILE_STREAM FileStream);
NTSTATUS
ExtfsFsdDispatchCreate(PDEVICE_OBJECT DeviceObject,
                       PIRP Irp);

// close.c
NTSTATUS
ExtfsFsdDispatchClose(PDEVICE_OBJECT DeviceObject,
                      PIRP Irp);
NTSTATUS
ExtfsFsdDispatchCleanup(PDEVICE_OBJECT DeviceObject,
                        PIRP Irp);

// lockctrl.c
NTSTATUS
ExtfsFsdDispatchLockControl(PDEVICE_OBJECT DeviceObject,
                            PIRP Irp);

// read.c
NTSTATUS
ExtfsFsdDispatchRead(PDEVICE_OBJECT DeviceObject,
                     PIRP Irp);

// fileinfo.c
NTSTATUS
ExtfsFsdDispatchQueryInformation(PDEVICE_OBJECT DeviceObject,
                                 PIRP Irp);
NTSTATUS
ExtfsFsdDispatchDirectoryControl(PDEVICE_OBJECT DeviceObject,
                                 PIRP Irp);

// fsctrl.c
NTSTATUS
ExtfsFsdDispatchFsControl(PDEVICE_OBJECT DeviceObject,
                          PIRP Irp);
NTSTATUS
ExtfsFsdDispatchQueryVolumeInformation(PDEVICE_OBJECT DeviceObject,
                                       PIRP Irp);

// cache.c
BOOLEAN NTAPI
ExtfsAcquireForLazyWrite(PVOID Context, BOOLEAN Wait);
VOID NTAPI
ExtfsReleaseFromLazyWrite(PVOID Context);
BOOLEAN NTAPI
ExtfsAcquireForReadAhead(PVOID Context, BOOLEAN Wait);
VOID NTAPI
ExtfsReleaseFromReadAhead(PVOID Context);

// fastio.c
BOOLEAN
NTAPI
ExtfsFastIoCheckIfPossible(
    PFILE_OBJECT FileObject,
    PLARGE_INTEGER FileOffset,
    ULONG Length,
    BOOLEAN Wait,
    ULONG LockKey,
    BOOLEAN CheckForReadOperation,
    PIO_STATUS_BLOCK IoStatus,
    PDEVICE_OBJECT DeviceObject);
BOOLEAN
NTAPI
ExtfsFastIoRead(
    PFILE_OBJECT FileObject,
    PLARGE_INTEGER FileOffset,
    ULONG Length,
    BOOLEAN Wait,
    ULONG LockKey,
    PVOID Buffer,
    PIO_STATUS_BLOCK IoStatus,
    PDEVICE_OBJECT DeviceObject);
BOOLEAN
NTAPI
ExtfsFastIoWrite(
    PFILE_OBJECT FileObject,
    PLARGE_INTEGER FileOffset,
    ULONG Length,
    BOOLEAN Wait,
    ULONG LockKey,
    PVOID Buffer,
    PIO_STATUS_BLOCK IoStatus,
    PDEVICE_OBJECT DeviceObject);
