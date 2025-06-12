#include <extfs.h>

NTSTATUS
ExtfsFsdDispatchClose(PDEVICE_OBJECT DeviceObject,
                      PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PEXTFS_FILE_STREAM FileStream = FileObject->FsContext2;
    PEXTFS_VOLUME_EXTENSION VolumeExtension;
    LONGLONG ReferenceCount;

    if (IsExtfsGlobalData(DeviceObject ? DeviceObject->DeviceExtension : NULL))
    {
        DPRINT1("Closing ExtfsGlobalData\n");
        goto result;
    }

    if (!IsExtfsFileContext(FileContext))
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    if (!IsExtfsFileStream(FileStream))
    {
        DPRINT1("Invalid FileStream\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    DPRINT1("Path \"%wZ\"\n", &FileObject->FileName);

    VolumeExtension = FileContext->InodeContext->VolumeExtension;

    ExAcquireResourceExclusiveLite(&VolumeExtension->FileOperationLock, TRUE);

    if (FileObject->PrivateCacheMap)
    {
        CcUninitializeCacheMap(FileObject, NULL, NULL);
    }

    FileStream->ClosePending = TRUE;
    ExtfsRemoveAllClosePendingFileStreamFromList(FileContext);

    ReferenceCount = FileContext->ReferenceCount;
    if (ReferenceCount < 1)
    {
        PCHAR FilePath = FileContext->FilePathUtf8.Buffer;
        PEXTFS_INODE_CONTEXT InodeContext = FileContext->InodeContext;

        if (FileContext->DeletePending)
        {
            ExtfsDeleteFileByPath(InodeContext->VolumeExtension, FilePath, TRUE, Irp);
        }

        ExtfsRemoveFileContextFromList(FileContext->InodeContext->VolumeExtension, FileContext);
        ExtfsFreeFileContext(FileContext);
        DPRINT1("Memory should be free\n");
    }

    DPRINT1("ReferenceCount = %I64d\n", ReferenceCount);

    FileObject->FsContext = NULL;
    FileObject->FsContext2 = NULL;
    FileObject->PrivateCacheMap = NULL;
    FileObject->SectionObjectPointer = NULL;

    ExReleaseResourceLite(&VolumeExtension->FileOperationLock);

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
ExtfsFsdDispatchCleanup(PDEVICE_OBJECT DeviceObject,
                        PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PEXTFS_FILE_STREAM FileStream = FileObject->FsContext2;
    PEXTFS_VOLUME_EXTENSION VolumeExtension;

    if (DeviceObject == ExtfsGlobalData->DeviceObject)
    {
        DPRINT1("Cleaning ExtfsGlobalData\n");
        FileObject->Flags |= FO_CLEANUP_COMPLETE;
        goto result;
    }

    if (!IsExtfsFileContext(FileContext))
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    if (!IsExtfsFileStream(FileStream))
    {
        DPRINT1("Invalid FileStream\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    DPRINT1("Path \"%wZ\"\n", &FileObject->FileName);

    VolumeExtension = FileContext->InodeContext->VolumeExtension;

    UNREFERENCED_PARAMETER(VolumeExtension);

    FsRtlFastUnlockAll(
        &FileContext->FileLock,
        FileObject,
        IoGetRequestorProcess(Irp),
        NULL
    );

    //ExtfsFlushInodeContext(FileContext->InodeContext);

    FileStream->IsNotForcingFilterNameUpdate = FALSE;

    FileObject->Flags |= FO_CLEANUP_COMPLETE;

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
ExtfsFsdDispatchFlushBuffers(PDEVICE_OBJECT DeviceObject,
                             PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject ? FileObject->FsContext : NULL;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = DeviceObject->DeviceExtension;

    if (DeviceObject == ExtfsGlobalData->DeviceObject)
    {
        DPRINT1("Flushing ExtfsGlobalData\n");
        goto result;
    }

    if (!IsExtfsVolumeExtension(VolumeExtension))
    {
        DPRINT1("Invalid VolumeExtension\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    if (!IsExtfsFileContext(FileContext))
    {
        DPRINT1("Flushing VolumeExtension\n");
        ExtfsAllocationManagerForceFlush(&VolumeExtension->AllocationManager);
        goto result;
    }

    DPRINT1("Path \"%wZ\"\n", &FileObject->FileName);

    CcFlushCache(FileObject->SectionObjectPointer, NULL, 0, NULL);
    ExtfsFlushInodeContext(FileContext->InodeContext);

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
