#include "extfs.h"

NTSTATUS
ExtfsFsdDispatchLockControl(PDEVICE_OBJECT DeviceObject,
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

    Status = FsRtlProcessFileLock(
        &FileContext->FileLock,
        Irp,
        NULL
    );

result:
    if (Status != STATUS_PENDING)
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
