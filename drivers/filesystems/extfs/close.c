#include "extfs.h"

NTSTATUS
ExtfsFsdDispatchClose(PDEVICE_OBJECT DeviceObject,
                      PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PEXTFS_FILE_STREAM FileStream = FileObject->FsContext2;

    if (!FileContext || FileContext->StandardFCB.Identifier.Magic != EXTFS_FILE_CTX_MAGIC)
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }

    if (!FileStream || FileStream->StandardFCB.Identifier.Magic != EXTFS_FILE_STR_MAGIC)
    {
        DPRINT1("Invalid FileStream\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }

    ExtfsRemoveFileStreamFromList(FileContext, FileStream);
    if (!FileContext->Streams)
    {
        ExtfsRemoveFileContextFromList(FileContext->InodeContext->VolumeExtension, FileContext);
    }
    FileObject->FsContext = FileObject->FsContext2 = NULL;

    Status = STATUS_SUCCESS;
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
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;

    if (!FileContext || FileContext->StandardFCB.Identifier.Magic != EXTFS_FILE_CTX_MAGIC)
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }

    if (FileObject->PrivateCacheMap)
    {
        CcUninitializeCacheMap(FileObject, NULL, NULL);
    }

    FsRtlFastUnlockAll(
        &FileContext->FileLock,
        FileObject,
        IoGetRequestorProcess(Irp),
        NULL
    );

    Status = STATUS_SUCCESS;
result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
