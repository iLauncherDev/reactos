#include "extfs.h"

/*
typedef struct _FAST_IO_DISPATCH {
  ULONG SizeOfFastIoDispatch;
  PFAST_IO_CHECK_IF_POSSIBLE FastIoCheckIfPossible;
  PFAST_IO_READ FastIoRead;
  PFAST_IO_WRITE FastIoWrite;
  PFAST_IO_QUERY_BASIC_INFO FastIoQueryBasicInfo;
  PFAST_IO_QUERY_STANDARD_INFO FastIoQueryStandardInfo;
  PFAST_IO_LOCK FastIoLock;
  PFAST_IO_UNLOCK_SINGLE FastIoUnlockSingle;
  PFAST_IO_UNLOCK_ALL FastIoUnlockAll;
  PFAST_IO_UNLOCK_ALL_BY_KEY FastIoUnlockAllByKey;
  PFAST_IO_DEVICE_CONTROL FastIoDeviceControl;
  PFAST_IO_ACQUIRE_FILE AcquireFileForNtCreateSection;
  PFAST_IO_RELEASE_FILE ReleaseFileForNtCreateSection;
  PFAST_IO_DETACH_DEVICE FastIoDetachDevice;
  PFAST_IO_QUERY_NETWORK_OPEN_INFO FastIoQueryNetworkOpenInfo;
  PFAST_IO_ACQUIRE_FOR_MOD_WRITE AcquireForModWrite;
  PFAST_IO_MDL_READ MdlRead;
  PFAST_IO_MDL_READ_COMPLETE MdlReadComplete;
  PFAST_IO_PREPARE_MDL_WRITE PrepareMdlWrite;
  PFAST_IO_MDL_WRITE_COMPLETE MdlWriteComplete;
  PFAST_IO_READ_COMPRESSED FastIoReadCompressed;
  PFAST_IO_WRITE_COMPRESSED FastIoWriteCompressed;
  PFAST_IO_MDL_READ_COMPLETE_COMPRESSED MdlReadCompleteCompressed;
  PFAST_IO_MDL_WRITE_COMPLETE_COMPRESSED MdlWriteCompleteCompressed;
  PFAST_IO_QUERY_OPEN FastIoQueryOpen;
  PFAST_IO_RELEASE_FOR_MOD_WRITE ReleaseForModWrite;
  PFAST_IO_ACQUIRE_FOR_CCFLUSH AcquireForCcFlush;
  PFAST_IO_RELEASE_FOR_CCFLUSH ReleaseForCcFlush;
} FAST_IO_DISPATCH, *PFAST_IO_DISPATCH;
*/

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
    PDEVICE_OBJECT DeviceObject)
{
    DPRINT1("ExtfsFastIoCheckIfPossible\n");
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(FileOffset);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(LockKey);
    UNREFERENCED_PARAMETER(CheckForReadOperation);
    UNREFERENCED_PARAMETER(IoStatus);
    UNREFERENCED_PARAMETER(DeviceObject);
    return FALSE;
}

BOOLEAN
NTAPI
ExtfsAcquireFileForNtCreateSection(PFILE_OBJECT FileObject)
{
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        return FALSE;
    }
    DPRINT1("ExtfsAcquireFileForNtCreateSection\n");

    return ExAcquireResourceExclusiveLite(&FileContext->StandardFCB.MainResource, TRUE);
}

BOOLEAN
NTAPI
ExtfsReleaseFileForNtCreateSection(PFILE_OBJECT FileObject)
{
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        return FALSE;
    }
    DPRINT1("ExtfsReleaseFileForNtCreateSection\n");

    ExReleaseResourceLite(&FileContext->StandardFCB.MainResource);
    return TRUE;
}

NTSTATUS
NTAPI
ExtfsAcquireFileForModWrite(
    PFILE_OBJECT FileObject,
    PLARGE_INTEGER EndingOffset,
    PERESOURCE *ResourceToRelease,
    PDEVICE_OBJECT DeviceObject)
{
    BOOLEAN Result = TRUE;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        return FALSE;
    }
    DPRINT1("ExtfsAcquireFileForModWrite\n");

    *ResourceToRelease = &FileContext->StandardFCB.MainResource;
    Result = ExAcquireResourceExclusiveLite(*ResourceToRelease, TRUE);
    if (!Result)
    {
        *ResourceToRelease = NULL;
    }

    return Result ? STATUS_SUCCESS : STATUS_CANT_WAIT;
}

NTSTATUS
NTAPI
ExtfsReleaseFileForModWrite(
    PFILE_OBJECT FileObject,
    PERESOURCE ResourceToRelease,
    PDEVICE_OBJECT DeviceObject)
{
    BOOLEAN Result = TRUE;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        return FALSE;
    }
    DPRINT1("ExtfsReleaseFileForModWrite\n");

    if (ResourceToRelease)
    {
        ASSERT(ResourceToRelease == &FileContext->StandardFCB.MainResource);
        ExReleaseResourceLite(ResourceToRelease);
    }
    else
    {
        __debugbreak();
    }

    return STATUS_SUCCESS;
}

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
    PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS Status;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PEXTFS_FILE_STREAM FileStream = FileObject->FsContext2;
    EXTFS_FILE_STREAM DummyFileStream = {0};
    PVOID UserBuffer = Buffer;
    LARGE_INTEGER ByteOffset = {.QuadPart = FileOffset ? FileOffset->QuadPart : 0};
    ULONG BytesRead = 0;

    DPRINT1("ExtfsFastIoRead\n");

    DBG_UNREFERENCED_PARAMETER(LockKey);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);

    return FALSE;

    IoStatus->Information = 0;

    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    if (!FileStream || !EXTFS_CHECK_FCB_HEADER(FileStream, *FileStream, EXTFS_FILE_STR_MAGIC))
    {
        DPRINT1("Invalid FileStream\n");
        FileStream = &DummyFileStream;
    }

    DPRINT1("Path \"%wZ\"\n", &FileObject->FileName);

    if (!UserBuffer)
    {
        DPRINT1("Cannot get UserBuffer\n");
        Status = STATUS_SUCCESS;
        goto result;
    }

    if (FileContext->InodeContext->IsDirectory)
    {
        DPRINT1("Cannot read directory\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    if (ByteOffset.QuadPart >= FileContext->InodeContext->FileSize)
    {
        DPRINT1("Reached end of file\n");
        Status = STATUS_END_OF_FILE;
        goto result;
    }

    if (ByteOffset.HighPart == -1)
    {
        switch (ByteOffset.LowPart)
        {
        case FILE_USE_FILE_POINTER_POSITION:
            DPRINT1("FILE_USE_FILE_POINTER_POSITION\n");
            ByteOffset.QuadPart = FileObject->CurrentByteOffset.QuadPart;
            break;
        }
    }

    FsRtlEnterFileSystem();

    DPRINT1("ExtfsFastIoRead(0x%p, 0x%p, %I64u, %u)\n", FileContext->InodeContext, UserBuffer, ByteOffset.QuadPart, Length);
    BytesRead = ExtfsReadInodeData(FileContext->InodeContext,
                                   UserBuffer, ByteOffset, Length);

    IoStatus->Information = BytesRead;

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + (ULONGLONG)BytesRead;
    }

    FsRtlExitFileSystem();

    Status = FileContext->InodeContext->OperationStatus;

result:
    IoStatus->Status = Status;
    return TRUE;
}

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
    PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS Status;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PEXTFS_FILE_STREAM FileStream = FileObject->FsContext2;
    EXTFS_FILE_STREAM DummyFileStream = {0};
    PVOID UserBuffer = Buffer;
    LARGE_INTEGER ByteOffset = {.QuadPart = FileOffset ? FileOffset->QuadPart : 0};
    ULONG BytesWritten = 0;

    DPRINT1("ExtfsFastIoWrite\n");

    DBG_UNREFERENCED_PARAMETER(LockKey);
    DBG_UNREFERENCED_PARAMETER(DeviceObject);

    return FALSE;

    IoStatus->Information = 0;

    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    if (!FileStream || !EXTFS_CHECK_FCB_HEADER(FileStream, *FileStream, EXTFS_FILE_STR_MAGIC))
    {
        DPRINT1("Invalid FileStream\n");
        FileStream = &DummyFileStream;
    }

    DPRINT1("Path \"%wZ\"\n", &FileObject->FileName);

    if (!UserBuffer)
    {
        DPRINT1("Cannot get UserBuffer\n");
        Status = STATUS_SUCCESS;
        goto result;
    }

    if (FileContext->InodeContext->IsDirectory)
    {
        DPRINT1("Cannot write directory\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    if (ByteOffset.HighPart == -1)
    {
        switch (ByteOffset.LowPart)
        {
        case FILE_USE_FILE_POINTER_POSITION:
            ByteOffset.QuadPart = FileObject->CurrentByteOffset.QuadPart;
            break;

        case FILE_WRITE_TO_END_OF_FILE:
            ByteOffset.QuadPart = FileContext->InodeContext->FileSize;
            break;
        }
    }

    FsRtlEnterFileSystem();

    DPRINT1("ExtfsFastIoWrite(0x%p, 0x%p, %I64u, %u)\n", FileContext->InodeContext, UserBuffer, ByteOffset.QuadPart, Length);
    BytesWritten = ExtfsWriteInodeData(FileContext->InodeContext,
                                       UserBuffer, ByteOffset, Length);

    IoStatus->Information = BytesWritten;

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + (ULONGLONG)BytesWritten;
    }

    ExtfsUpdateFileContextSize(FileContext, FileObject);

    FsRtlExitFileSystem();

    Status = FileContext->InodeContext->OperationStatus;

result:
    IoStatus->Status = Status;
    return TRUE;
}
