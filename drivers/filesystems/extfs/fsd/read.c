#include <extfs.h>

NTSTATUS
ExtfsFsdDispatchRead(PDEVICE_OBJECT DeviceObject,
                     PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PEXTFS_FILE_STREAM FileStream = FileObject->FsContext2;
    EXTFS_FILE_STREAM DummyFileStream = {0};
    PVOID UserBuffer = ExtfsFsdGetBuffer(Irp);
    LARGE_INTEGER ByteOffset = IrpSp->Parameters.Read.ByteOffset;
    ULONG Length = IrpSp->Parameters.Read.Length;
    ULONG BytesRead = 0;
    //IO_STATUS_BLOCK IoStatus = {0};
//
    //CcCopyRead(FileObject,
    //           &ByteOffset,
    //           Length,
    //           TRUE,
    //           UserBuffer,
    //           &IoStatus);
//
    //Status = IoStatus.Status;
    //Irp->IoStatus = IoStatus;
    //goto result;

    Irp->IoStatus.Information = 0;

    if (IrpSp->MinorFunction == IRP_MN_COMPLETE)
    {
        DPRINT1("Hello CC\n");
    }

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

    if (!FileContext->CanExecute &&
        (FileObject->Flags & FO_CACHE_SUPPORTED) &&
        !FileObject->PrivateCacheMap && FALSE)
    {
        CcInitializeCacheMap(
            FileObject,
            &FileContext->StandardFCB.FileSizes,
            FALSE,
            &ExtfsGlobalData->CacheMgrCallbacks,
            FileContext
        );
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

    DPRINT("ExtfsFsdDispatchRead(0x%p, 0x%p, %I64u, %u)\n", FileContext->InodeContext, UserBuffer, ByteOffset.QuadPart, Length);
    BytesRead = ExtfsReadInodeData(FileContext->InodeContext,
                                   UserBuffer, ByteOffset, Length);

    DPRINT("BytesRead = %u\n", BytesRead);

    Irp->IoStatus.Information = BytesRead;

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        DPRINT1("FO_SYNCHRONOUS_IO\n");
        FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + (ULONGLONG)BytesRead;
    }

    Status = FileContext->InodeContext->OperationStatus;
result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
