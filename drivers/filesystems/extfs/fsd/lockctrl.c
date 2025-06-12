#include "extfs.h"

NTSTATUS
ExtfsFsdDispatchLockControl(PDEVICE_OBJECT DeviceObject,
                            PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;

    Irp->IoStatus.Information = 0;

    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    Status = FsRtlProcessFileLock(
        &FileContext->FileLock,
        Irp,
        NULL
    );

    goto skip_completion;
result:
    if (Status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

skip_completion:
    return Status;
}
