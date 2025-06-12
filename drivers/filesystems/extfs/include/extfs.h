#pragma once
#define __CRT_STRSAFE_IMPL
#include <ntifs.h>
#include <ntdddisk.h>
#include <ntddk.h>
#include <mountmgr.h>
#include <mountdev.h>
#include <ntstrsafe.h>
#ifdef __REACTOS__
#include <section_attribs.h>
#endif
#include "extfs-structs.h"
#include "utils.h"

#define NDEBUG
#include <debug.h>

#define DEVICE_VOLUME_NAME L"\\Device\\ExtfsVolume"
#define DEVICE_NAME L"\\Device\\Extfs"
#define DOS_DEVICE_NAME L"\\DosDevices\\Extfs"
#define EXTFS_TAG_INODE_CONTEXT 'CIxE'
#define EXTFS_TAG_FILE_CONTEXT 'CFxE'
#define EXTFS_TAG_FILE_STREAM 'SFxE'
#define EXTFS_TAG_ALLOCATION_MANAGER 'MAxE'
#define EXTFS_TAG_ALLOCATION_ENTRY 'EAxE'
#define EXTFS_TAG_EXTENT_LIST 'LExE'
#define EXTFS_TAG_BUFFER 'BtxE'
#define EXTFS_TAG_SUPER_BLOCK 'StxE'
#define EXTFS_TAG_GROUP_DESC 'GtxE'
#define EXTFS_TAG_WORKER_CONTEXT 'CWxE'
#define EXTFS_TAG_INODE_CACHE 'CCIE'

#define EXTFS_VOLUME_EXTENSION_MAGIC 'EVxE'
#define EXTFS_FILE_CTX_MAGIC 'XTFE'
#define EXTFS_FILE_STR_MAGIC 'RTFE'
#define EXTFS_GLOBAL_DATA_MAGIC 'DGxE'

#define EXTFS_ALLOCATION_MANAGER_WRITE_MODE_FLAG        (1 << 0)
#define EXTFS_ALLOCATION_MANAGER_NO_CACHE_FLAG          (1 << 1)

#define EXTFS_FCB_SIGN 0x1234
#define EXTFS_INIT_FCB_HEADER(fcb, struct, magic)                               \
{                                                                               \
    ((PFSRTL_COMMON_FCB_HEADER)fcb)->NodeTypeCode = EXTFS_FCB_SIGN;             \
    ((PFSRTL_COMMON_FCB_HEADER)fcb)->NodeByteSize = sizeof(struct);             \
    ((PEXTFS_STANDARD_FCB)fcb)->Identifier.Magic = magic;                       \
    ((PEXTFS_STANDARD_FCB)fcb)->Identifier.Size  = sizeof(struct);              \
}

#define EXTFS_CHECK_FCB_HEADER(fcb, struct, magic)                              \
(                                                                               \
    ((PFSRTL_COMMON_FCB_HEADER)fcb)->NodeTypeCode == EXTFS_FCB_SIGN &&          \
    ((PFSRTL_COMMON_FCB_HEADER)fcb)->NodeByteSize == sizeof(struct) &&          \
    ((PEXTFS_STANDARD_FCB)fcb)->Identifier.Magic == magic           &&          \
    ((PEXTFS_STANDARD_FCB)fcb)->Identifier.Size  == sizeof(struct)              \
)

#define EXTFS_DIRECTORY_MAX_REPARSE_RECURSION 8
#define EXTFS_MAX_MOUNTED_VOLUMES (64 * 1024)

// for INTs only
#define ReadFieldLE(field) ExtfsReadPointerLittleEndian(&(field), sizeof((field)))
#define WriteFieldLE(field, value) ExtfsWritePointerLittleEndian(&(field), sizeof((field)), (ULONGLONG)(value))

#define ExtfsInitListEntry(Entry, StructPointer)  \
    _ExtfsInitListEntry(Entry, (LONG)((ULONG_PTR)StructPointer - (ULONG_PTR)Entry));

typedef NTSTATUS (*EXTFS_ALLOCATION_FLUSH_EVENT)(PVOID Context, PVOID AllocationContext);

typedef struct _EXTFS_SIMPLE_LOCK
{
    volatile LONG SharedLockStarted;
    volatile LONG ExclusiveLockStarted;
} EXTFS_SIMPLE_LOCK, *PEXTFS_SIMPLE_LOCK;

typedef struct _EXTFS_LIST_ENTRY
{
    LONG StructOffset;
    PVOID Prev;
    PVOID Next;
} EXTFS_LIST_ENTRY, *PEXTFS_LIST_ENTRY;

typedef struct _EXTFS_IDENTIFIER
{
    ULONG Magic;
    ULONG Size;
} EXTFS_IDENTIFIER, *PEXTFS_IDENTIFIER;

typedef struct _EXTFS_STANDARD_FCB
{
    FSRTL_COMMON_FCB_HEADER StandardHeader;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    EXTFS_IDENTIFIER Identifier;
    PFILE_OBJECT StreamFileObject;
    CC_FILE_SIZES FileSizes;
    ERESOURCE MainResource, PagingIoResource;
} EXTFS_STANDARD_FCB, *PEXTFS_STANDARD_FCB;

typedef struct _EXTFS_GLOBAL_DATA
{
    EXTFS_STANDARD_FCB StandardFCB;
    ERESOURCE Resource;

    LONGLONG VolumeNumber;

    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT DeviceObject;

    CACHE_MANAGER_CALLBACKS CacheMgrCallbacks;
    ULONG Flags;
    FAST_IO_DISPATCH FastIoDispatch;

    ERESOURCE MountedVolumeBitmapLock;
    PUCHAR MountedVolumeBitmap;
    ULONG MountedVolumeBitmapSize;
    ULONG MountedVolumeBitmapEntries;

    ERESOURCE AnotherResource;
    EXTFS_LIST_ENTRY MountedVolumeList;
} EXTFS_GLOBAL_DATA, *PEXTFS_GLOBAL_DATA;

typedef struct _EXTFS_EXTENT
{
    BOOLEAN IsSparse;
    ULONGLONG LogicalBlock, LogicalBlockInBytes;
    ULONGLONG Block, BlockInBytes;
    ULONGLONG Length, LengthInBytes;

    struct _EXTFS_EXTENT *Next;
    struct _EXTFS_EXTENT *Prev;
} EXTFS_EXTENT, *PEXTFS_EXTENT;

typedef struct _EXTFS_COMPLETION_ROUNTINE_CTX
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
} EXTFS_COMPLETION_ROUNTINE_CTX, *PEXTFS_COMPLETION_ROUNTINE_CTX;

typedef struct _EXTFS_DATA_POINTER
{
    BOOLEAN IsUsing, IsReady;

    BOOLEAN IsDataLockInitialized;
    ERESOURCE DataLock;

    ULONG UsedDataStart, UsedDataEnd;
    ULONGLONG Block;

    PVOID Data;
    PVOID OriginalData;
    ULONGLONG AccessTime;
} EXTFS_DATA_POINTER, *PEXTFS_DATA_POINTER;

typedef struct _EXTFS_ALLOCATION_ENTRY
{
    PVOID AllocationManager;
    PEXT_GROUP_DESC GroupDesc;
    ULONG Group;

    LONGLONG ReferenceCount;

    PVOID TempBlockBitmap;
    PVOID BlockBitmap, BlockBitmapOriginal, BlockBitmapFastWrite, BlockReservedBitmap;
    ULONG BlockBitmapSize, BlockBitmapEntries;
    ERESOURCE BlockBitmapLock;

    PVOID TempInodeBitmap;
    PVOID InodeBitmap, InodeBitmapOriginal;
    ULONG InodeBitmapSize, InodeBitmapEntries;
    ERESOURCE InodeBitmapLock;

    EXTFS_LIST_ENTRY ContextListEntry;
    EXTFS_LIST_ENTRY ListEntry;
} EXTFS_ALLOCATION_ENTRY, *PEXTFS_ALLOCATION_ENTRY;

typedef struct _EXTFS_ALLOCATION_CONTEXT
{
    ULONGLONG Id;
    volatile BOOLEAN IsReady;
    volatile BOOLEAN IgnorePatch;

    PEXTFS_ALLOCATION_ENTRY AllocationEntry;

    PVOID Context;
    EXTFS_ALLOCATION_FLUSH_EVENT FlushEvent;

    ULONG BlockBitmapSize, InodeBitmapSize;
    ERESOURCE BlockBitmapLock, InodeBitmapLock;

    PVOID PatchBlockBitmapToMark, PatchInodeBitmapToMark;     // Bitmap |= BitmapToMark
    PVOID PatchBlockBitmapToUnmark, PatchInodeBitmapToUnmark; // Bitmap &= ~BitmapToUnmark

    EXTFS_LIST_ENTRY ListEntry;
} EXTFS_ALLOCATION_CONTEXT, *PEXTFS_ALLOCATION_CONTEXT;

typedef struct _EXTFS_ALLOCATION_MANAGER
{
    PVOID VolumeExtension;
    HANDLE AutoFlushThread;
    ULONG_PTR AutoFlushThreadStopRequest;
    KTIMER FlushWaitTimer;
    KEVENT AutoFlushThreadExitedEvent;

    ULONG DataListEntries;
    PEXTFS_DATA_POINTER DataList;
    EXTFS_LIST_ENTRY AllocationList;

    ERESOURCE DataListLock;
    ERESOURCE AllocationListLock;
    ERESOURCE MainLock;
    BOOLEAN InitializedLocks, InitializedEvents;
} EXTFS_ALLOCATION_MANAGER, *PEXTFS_ALLOCATION_MANAGER;

typedef struct _EXTFS_VOLUME_EXTENSION
{
    EXTFS_STANDARD_FCB StandardFCB;
    PEXTFS_GLOBAL_DATA GlobalData;
    PFILE_OBJECT StreamFileObject;

    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT RealDevice;
    PVPB Vpb;

    ULONG VolumeNumber;

    WCHAR DeviceName[256];
    UNICODE_STRING DeviceNameString;

    CHAR TempUUID[128];

    CHAR VolumeLabel[16];
    CHAR FileSystemName[8];

    ULONG BytesPerSector;
    ULONG DiskBlockSize;

    ULONG BlockSize;
    ULONGLONG UsedBlocks;
    ULONGLONG TotalBlocks;

    ULONG FirstDataBlock;
    ULONG BlocksPerGroup, InodesPerGroup;
    ULONG GroupDescCount, GroupDescBlocks;
    PVOID GroupDescBuffer;

    ULONG InodeSizeInBytes, GroupDescSizeInBytes;
    ULONG InodesPerBlock, GroupDescPerBlock;
    ULONG PointersPerBlock;

    BOOLEAN IsExcludeBitmapAvailable;

    EXT_SUPER_BLOCK SuperBlock;
    ERESOURCE SuperBlockLock;

    EXTFS_ALLOCATION_MANAGER AllocationManager;

    BOOLEAN ReadOnly;
    BOOLEAN Dismounted;
    BOOLEAN Locked;

    LONGLONG OpenFileCount;
    EXTFS_LIST_ENTRY OpenFileList;
    ERESOURCE OpenFileListLock;

    LONGLONG InodeContextCount;
    EXTFS_LIST_ENTRY InodeContextList;
    ERESOURCE InodeContextListLock;

    ERESOURCE FileOperationLock;
    ERESOURCE DiskOperationLock;

    EXTFS_LIST_ENTRY ListEntry;
} EXTFS_VOLUME_EXTENSION, *PEXTFS_VOLUME_EXTENSION;

typedef struct _EXTFS_CACHE_ALLOCATION_ENTRY
{
    LONGLONG ReferenceCount;
    PEXTFS_ALLOCATION_ENTRY AllocationEntry;

    EXTFS_LIST_ENTRY ListEntry;
} EXTFS_CACHE_ALLOCATION_ENTRY, *PEXTFS_CACHE_ALLOCATION_ENTRY;

typedef struct _EXTFS_INODE_CONTEXT
{
    NTSTATUS OperationStatus;

    LONGLONG CreationTime;
    LONGLONG LastAccessTime;
    LONGLONG LastWriteTime;
    LONGLONG ChangeTime;

    ULONG CacheAllocationGroup, CacheSecondAllocationGroup;
    PEXTFS_ALLOCATION_ENTRY CacheAllocationEntry, CacheSecondAllocationEntry;
    EXTFS_LIST_ENTRY CacheAllocationEntries;
    ERESOURCE CacheAllocationEntriesLock;

    PEXTFS_VOLUME_EXTENSION VolumeExtension;

    ULONGLONG CurrentExtentOffset;
    PEXTFS_EXTENT CurrentExtent;

    PEXTFS_EXTENT ExtentList;

    ULONGLONG AdditionalBlocksCount;
    ULONGLONG FileSize, OldFileSize;
    BOOLEAN IsDirectory, IsReparsePoint, IsUsingExtents, IsVolume;
    BOOLEAN AvoidInodeFlush;

    ULONGLONG InodeNum;
    ERESOURCE InodeDataLock, DirectoryDataLock;
    ERESOURCE InodeLock;
    EXT_INODE Inode;

    LONGLONG ReferenceCount;
    ERESOURCE ReferenceCountLock;

    EXTFS_LIST_ENTRY ListEntry;
} EXTFS_INODE_CONTEXT, *PEXTFS_INODE_CONTEXT;

typedef struct _EXTFS_FILE_STREAM
{
    EXTFS_STANDARD_FCB StandardFCB;
    PVOID FileContext;
    PFILE_OBJECT FileObject;

    UNICODE_STRING FilterName;
    BOOLEAN IsUsingWildcards;
    BOOLEAN IsNotForcingFilterNameUpdate;
    BOOLEAN MatchedAnyEntry;

    ULONGLONG CurrentOffset, CurrentDirectoryOffset;
    ULONG CurrentOutputDirectoryOffset, OldOutputDirectoryOffset;

    BOOLEAN ClosePending;
    BOOLEAN CanListDirectory;
    BOOLEAN CanExecute;
    BOOLEAN CanRead, CanWrite;

    EXTFS_LIST_ENTRY ListEntry;
} EXTFS_FILE_STREAM, *PEXTFS_FILE_STREAM;

typedef struct _EXTFS_FILE_CONTEXT
{
    EXTFS_STANDARD_FCB StandardFCB;

    LONGLONG ReferenceCount;

    FILE_LOCK FileLock;
    ERESOURCE StreamListLock;
    PEXTFS_INODE_CONTEXT InodeContext;

    EXTFS_LIST_ENTRY StreamList;

    ANSI_STRING FilePathUtf8;
    UNICODE_STRING FilePathUnicode;

    BOOLEAN DeletePending;
    BOOLEAN CanExecute;

    EXTFS_LIST_ENTRY ListEntry;
} EXTFS_FILE_CONTEXT, *PEXTFS_FILE_CONTEXT;

typedef struct _EXTFS_IRP_WORKER_CONTEXT
{
    PNTSTATUS Status;
    PIO_WORKITEM WorkItem;
    PIRP Irp;
} EXTFS_IRP_WORKER_CONTEXT, *PEXTFS_IRP_WORKER_CONTEXT;

extern PEXTFS_GLOBAL_DATA ExtfsGlobalData;
extern PFILE_OBJECT MountMgrFile;
extern PDEVICE_OBJECT MountMgrDevice;
extern NPAGED_LOOKASIDE_LIST ExtfsExtentLookasideList;

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

extern UCHAR UuidDigitsList[5];

// utils.c
VOID _ExtfsInitListEntry(PEXTFS_LIST_ENTRY Entry, LONG StructOffset);
PVOID ExtfsGetListEntryStructure(PEXTFS_LIST_ENTRY Entry);
VOID ExtfsInsertTailList(PEXTFS_LIST_ENTRY Head, PEXTFS_LIST_ENTRY ListToInsert);
VOID ExtfsInsertBodyList(PEXTFS_LIST_ENTRY Body, PEXTFS_LIST_ENTRY ListToInsert);
VOID ExtfsRemoveEntryList(PEXTFS_LIST_ENTRY Entry);

LONGLONG ExtfsGetSystemTime();

ULONG ExtfsBitmapGetSize(ULONG BitmapEntries);

BOOLEAN ExtfsBitmapSet(PUCHAR Bitmap, ULONG BitmapEntries, ULONG EntryIndex);
BOOLEAN ExtfsBitmapGet(PUCHAR Bitmap, ULONG BitmapEntries, ULONG EntryIndex);
BOOLEAN ExtfsBitmapClear(PUCHAR Bitmap, ULONG BitmapEntries, ULONG EntryIndex);

ULONG ExtfsBitmapGetUsedEntries(PUCHAR Bitmap, ULONG BitmapEntries);
ULONG ExtfsBitmapGetFreeEntries(PUCHAR Bitmap, ULONG BitmapEntries);
ULONG ExtfsBitmapGetFreeEntry(PUCHAR Bitmap, ULONG BitmapEntries);

NTSTATUS ExtfsAcquireFreeVolumeNumber(PEXTFS_GLOBAL_DATA GlobalData, PULONG OutputNumber);
NTSTATUS ExtfsReleaseVolumeNumber(PEXTFS_GLOBAL_DATA GlobalData, ULONG InputNumber);

ULONGLONG ExtfsReadPointerLittleEndian(PVOID Pointer, ULONG Size);
VOID ExtfsWritePointerLittleEndian(PVOID Pointer, ULONG Size, ULONGLONG Value);

VOID ExtfsFreeDuplicatedUnicodeString(PUNICODE_STRING Destination);
NTSTATUS ExtfsDuplicateUnicodeString(PUNICODE_STRING Destination, PUNICODE_STRING Source);

VOID ExtfsFreeUnicodeString(PUNICODE_STRING UnicodeString);
PUNICODE_STRING ExtfsConvertUtf8ToUnicode(PCHAR String, ULONG Length);

VOID ExtfsFreeUtf8String(PCHAR Utf8String);
PCHAR ExtfsConvertUnicodeToUtf8(PUNICODE_STRING UnicodeString);

// struct_check.c
BOOLEAN IsExtfsGlobalData(PVOID Pointer);
BOOLEAN IsExtfsVolumeExtension(PVOID Pointer);
BOOLEAN IsExtfsFileContext(PVOID Pointer);
BOOLEAN IsExtfsFileStream(PVOID Pointer);

// superblock_check.c
NTSTATUS ExtfsCheckSuperBlock(PEXT_SUPER_BLOCK SuperBlock);
BOOLEAN ExtfsAcquireSuperBlockReadLock(PEXTFS_VOLUME_EXTENSION VolumeExtension, BOOLEAN Wait);
BOOLEAN ExtfsAcquireSuperBlockWriteLock(PEXTFS_VOLUME_EXTENSION VolumeExtension, BOOLEAN Wait);
VOID ExtfsReleaseSuperBlockLock(PEXTFS_VOLUME_EXTENSION VolumeExtension);
VOID ExtfsFlushSuperBlock(PEXTFS_VOLUME_EXTENSION VolumeExtension);
VOID ExtfsUpdateVolumeExtensionUsage(PEXTFS_VOLUME_EXTENSION VolumeExtension);
NTSTATUS ExtfsInitializeVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension, BOOLEAN ReadOnly);
NTSTATUS ExtfsLockVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension);
NTSTATUS ExtfsUnlockVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension);
NTSTATUS ExtfsUninitializeVolume(PEXTFS_VOLUME_EXTENSION VolumeExtension, BOOLEAN WillDestroy);

// disk.c
NTSTATUS DiskGetBytesPerSector(PDEVICE_OBJECT VolumeDevice, PULONG BytesPerSectors);
NTSTATUS DiskRead(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);
NTSTATUS DiskWrite(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);
NTSTATUS ExtfsDiskRead(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);
NTSTATUS ExtfsDiskWrite(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length);
NTSTATUS ExtfsFastDiskRead(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, ULONG Flags);
NTSTATUS ExtfsFastDiskWrite(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, ULONG Flags);

// allocation-manager.c
VOID ExtfsAllocationManagerForceFlush(PEXTFS_ALLOCATION_MANAGER AllocationManager);

NTSTATUS ExtfsAllocationManagerInitialize(
    PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_ALLOCATION_MANAGER AllocationManager);
NTSTATUS ExtfsAllocationManagerUninitialize(
    PEXTFS_ALLOCATION_MANAGER AllocationManager);

VOID ExtfsAllocationManagerReleaseAllocationEntryWithReferenceCount(PEXTFS_ALLOCATION_ENTRY AllocationEntry, LONGLONG RefereceCount);
VOID ExtfsAllocationManagerReleaseAllocationEntry(PEXTFS_ALLOCATION_ENTRY AllocationEntry);

NTSTATUS ExtfsAllocationManagerDiskRead(PEXTFS_ALLOCATION_MANAGER AllocationManager, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, ULONG Flags);
NTSTATUS ExtfsAllocationManagerDiskWrite(PEXTFS_ALLOCATION_MANAGER AllocationManager, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, ULONG Flags);

// allocation-entry.c
ULONG ExtfsAllocationManagerGetAllocationEntryUsedInodes(PEXTFS_ALLOCATION_ENTRY AllocationEntry);
ULONG ExtfsAllocationManagerGetAllocationEntryFreeInodes(PEXTFS_ALLOCATION_ENTRY AllocationEntry);
ULONGLONG ExtfsAllocationManagerGetAllocationEntryFreeInode(PEXTFS_ALLOCATION_ENTRY AllocationEntry, BOOLEAN AllocateRequest);
BOOLEAN ExtfsAllocationManagerSetAllocationEntryInode(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Inode);
BOOLEAN ExtfsAllocationManagerGetAllocationEntryInode(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Inode);
BOOLEAN ExtfsAllocationManagerClearAllocationEntryInode(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Inode);

ULONG ExtfsAllocationManagerGetAllocationEntryUsedBlocks(PEXTFS_ALLOCATION_ENTRY AllocationEntry);
ULONG ExtfsAllocationManagerGetAllocationEntryFreeBlocks(PEXTFS_ALLOCATION_ENTRY AllocationEntry);
ULONGLONG ExtfsAllocationManagerGetAllocationEntryFreeBlock(PEXTFS_ALLOCATION_ENTRY AllocationEntry, BOOLEAN AllocateRequest);
BOOLEAN ExtfsAllocationManagerSetAllocationEntryBlock(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Block);
BOOLEAN ExtfsAllocationManagerGetAllocationEntryBlock(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Block, BOOLEAN DisableProtection);
BOOLEAN ExtfsAllocationManagerClearAllocationEntryBlock(PEXTFS_ALLOCATION_ENTRY AllocationEntry, ULONGLONG Block);

BOOLEAN ExtfsAllocationManagerCheckBlock(PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONGLONG Block);
BOOLEAN ExtfsAllocationManagerCheckInode(PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONGLONG Inode);
PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByFreeBlock(
    PEXTFS_ALLOCATION_MANAGER AllocationManager);
PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByFreeInode(
    PEXTFS_ALLOCATION_MANAGER AllocationManager);
PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByGroup(
    PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONG Group);
PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByBlock(
    PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONGLONG Block);
PEXTFS_ALLOCATION_ENTRY ExtfsAllocationManagerAcquireAllocationEntryByInode(
    PEXTFS_ALLOCATION_MANAGER AllocationManager, ULONGLONG Inode);

// inode.c
PEXT_GROUP_DESC ExtfsGetGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, ULONG Group);
NTSTATUS ExtfsReadGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_GROUP_DESC Buffer, ULONG Group);
NTSTATUS ExtfsWriteGroupDesc(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_GROUP_DESC Buffer, ULONG Group);
NTSTATUS ExtfsReadInode(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Buffer, ULONGLONG Index);
NTSTATUS ExtfsWriteInode(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Buffer, ULONGLONG Index);
ULONGLONG ExtfsGetInodeSize(PEXT_INODE Inode);
VOID ExtfsSetInodeSize(PEXT_INODE Inode, ULONGLONG InodeSize);

// inode-data.c
PEXTFS_INODE_CONTEXT
ExtfsPrepareInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXT_INODE Inode);
VOID
ExtfsFlushInodeContext(PEXTFS_INODE_CONTEXT InodeContext);

VOID
ExtfsReleaseInodeContext(PEXTFS_INODE_CONTEXT InodeContext);
VOID
ExtfsAutoReleaseInodeContextThread(PEXTFS_VOLUME_EXTENSION VolumeExtension);
PEXTFS_INODE_CONTEXT ExtfsReadInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, ULONGLONG Index);

VOID ExtfsSetInodeContextType(PEXTFS_INODE_CONTEXT InodeContext, UCHAR FileType);
VOID ExtfsResetInodeContextLinkCount(PEXTFS_INODE_CONTEXT InodeContext);
VOID ExtfsDecrementInodeContextLinkCount(PEXTFS_INODE_CONTEXT InodeContext);
VOID ExtfsIncrementInodeContextLinkCount(PEXTFS_INODE_CONTEXT InodeContext);
VOID ExtfsRemoveInodeContext(PEXTFS_INODE_CONTEXT InodeContext);
PEXTFS_INODE_CONTEXT ExtfsCreateInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension);

ULONGLONG ExtfsGetInodeContextAllocationSize(PEXTFS_INODE_CONTEXT InodeContext);
ULONGLONG ExtfsGetInodeContextFileSize(PEXTFS_INODE_CONTEXT InodeContext);

UCHAR ExtfsGetFileTypeDirEntry(PEXTFS_INODE_CONTEXT InodeContext);
ULONG ExtfsGetFileTypeAttribute(PEXTFS_INODE_CONTEXT InodeContext);
ULONG ExtfsGetFileTypeReparseTag(PEXTFS_INODE_CONTEXT InodeContext);

VOID
ExtfsChangeInodeSize(
    PEXTFS_INODE_CONTEXT InodeContext,
    ULONGLONG InodeSize,
    BOOLEAN IgnoreBlockChange);
VOID ExtfsConvertTime(
    PEXTFS_INODE_CONTEXT InodeContext);
VOID
ExtfsGetTime(
    PEXTFS_INODE_CONTEXT InodeContext,
    PLONGLONG CreationTime,
    PLONGLONG LastAccessTime,
    PLONGLONG LastWriteTime,
    PLONGLONG ChangeTime);
VOID
ExtfsChangeTime(
    PEXTFS_INODE_CONTEXT InodeContext,
    LONGLONG CreationTime,
    LONGLONG LastAccessTime,
    LONGLONG LastWriteTime,
    LONGLONG ChangeTime);

ULONG 
ExtfsReadInodeData(
    PEXTFS_INODE_CONTEXT InodeContext,
    PCHAR Buffer, LARGE_INTEGER Offset, ULONG Length);
ULONG
ExtfsWriteInodeData(
    PEXTFS_INODE_CONTEXT InodeContext,
    PCHAR Buffer, LARGE_INTEGER Offset, ULONG Length);

// directory.c
ULONG ExtfsFindFile(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_INODE_CONTEXT InodeContext, PCHAR Name);
BOOLEAN ExtfsRemoveFileFromDirEntries(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_INODE_CONTEXT InodeContext, PCHAR Name);
BOOLEAN ExtfsRenameFileByPath(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PCHAR SourceFilePath, PCHAR DestinationFilePath,
    BOOLEAN ReplaceIfExists, BOOLEAN ResolveReparse, PNTSTATUS Status, PIRP Irp);
BOOLEAN ExtfsDeleteFileByPath(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PCHAR FilePath,
    BOOLEAN ResolveReparse, PIRP Irp);
PEXTFS_INODE_CONTEXT ExtfsFindFileByPath(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PCHAR FilePath,
    BOOLEAN ResolveReparse, ULONG ReparseRecursion,
    BOOLEAN CreateFiles, UCHAR LastNameType, PNTSTATUS Status, PIRP Irp);
ULONG ExtfsQueryDirectoryEntry(
    PEXTFS_FILE_STREAM FileStream, PVOID OutputList, ULONG SizeLimit,
    PUNICODE_STRING FilterName, BOOLEAN IsUsingWildcards,
    PNTSTATUS Status, FILE_INFORMATION_CLASS FileInformationClass);

// dispatch.c
PVOID ExtfsFsdGetBuffer(PIRP Irp);
NTSTATUS NTAPI ExtfsIrpCompletionRoutine(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context);
NTSTATUS
NTAPI
ExtfsFsdDispatch(PDEVICE_OBJECT DeviceObject,
                 PIRP Irp);

// create.c
PEXTFS_FILE_CONTEXT ExtfsFindExistingFileContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, PCHAR FilePath);
VOID ExtfsUpdateFileContextSize(PEXTFS_FILE_CONTEXT FileContext, PFILE_OBJECT FileObject);
VOID ExtfsAddFileContextToList(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_FILE_CONTEXT FileContext);
VOID ExtfsRemoveFileContextFromList(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_FILE_CONTEXT FileContext);
VOID ExtfsAddFileStreamToList(PEXTFS_FILE_CONTEXT FileContext, PEXTFS_FILE_STREAM FileStream);
VOID ExtfsRemoveFileStreamFromList(PEXTFS_FILE_CONTEXT FileContext, PEXTFS_FILE_STREAM FileStream);
VOID ExtfsRemoveAllClosePendingFileStreamFromList(PEXTFS_FILE_CONTEXT FileContext);
VOID ExtfsFreeFileContext(PEXTFS_FILE_CONTEXT FileContext);
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
NTSTATUS
ExtfsFsdDispatchFlushBuffers(PDEVICE_OBJECT DeviceObject,
                             PIRP Irp);

// shutdown.c
VOID ExtfsFlushAllInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension);
NTSTATUS
ExtfsFsdDispatchShutdown(PDEVICE_OBJECT DeviceObject,
                         PIRP Irp);

// lockctrl.c
NTSTATUS
ExtfsFsdDispatchLockControl(PDEVICE_OBJECT DeviceObject,
                            PIRP Irp);

// read.c
NTSTATUS
ExtfsFsdDispatchRead(PDEVICE_OBJECT DeviceObject,
                     PIRP Irp);

// write.c
NTSTATUS
ExtfsFsdDispatchWrite(PDEVICE_OBJECT DeviceObject,
                      PIRP Irp);

// fileinfo.c
NTSTATUS
ExtfsFsdDispatchQueryInformation(PDEVICE_OBJECT DeviceObject,
                                 PIRP Irp);
NTSTATUS
ExtfsFsdDispatchSetInformation(PDEVICE_OBJECT DeviceObject,
                               PIRP Irp);

// dirctrl.c
NTSTATUS
ExtfsFsdDispatchDirectoryControl(PDEVICE_OBJECT DeviceObject,
                                 PIRP Irp);

// fsctrl.c
NTSTATUS
ExtfsFsdDispatchFsControl(PDEVICE_OBJECT DeviceObject,
                          PIRP Irp);

// pnp.c
NTSTATUS
NTAPI
ExtfsFsdDispatchPnp(PDEVICE_OBJECT DeviceObject,
                    PIRP Irp);

// devctrl.c
NTSTATUS
NTAPI
ExtfsFsdDispatchDeviceControl(PDEVICE_OBJECT DeviceObject,
                              PIRP Irp);

// volinfo.c
NTSTATUS
ExtfsFsdDispatchSetVolumeInformation(PDEVICE_OBJECT DeviceObject,
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
ExtfsAcquireFileForNtCreateSection(PFILE_OBJECT FileObject);
BOOLEAN
NTAPI
ExtfsReleaseFileForNtCreateSection(PFILE_OBJECT FileObject);
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
