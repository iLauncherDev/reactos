#include "extfs.h"

VOID NTAPI
ExtfsFsdWorkerRead(PDEVICE_OBJECT DeviceObject,
                   PVOID Context)
{
    PEXTFS_IRP_WORKER_CONTEXT WorkerContext = Context;
    PIRP Irp = WorkerContext->Irp;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PEXTFS_FILE_STREAM FileStream = FileObject->FsContext2;
    PVOID UserBuffer = ExtfsFsdGetBuffer(Irp);
    LARGE_INTEGER ByteOffset = IrpSp->Parameters.Read.ByteOffset;
    ULONG Length = IrpSp->Parameters.Read.Length;
    ULONG BytesRead = 0;

    UNREFERENCED_PARAMETER(DeviceObject);
    DPRINT1("ExtfsFsdWorkerRead - Started\n");

    Irp->IoStatus.Information = 0;

    if (!UserBuffer)
    {
        DPRINT1("Cannot get UserBuffer\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    BytesRead = ExtfsReadInodeData(FileContext->InodeContext,
                                   UserBuffer, ByteOffset, Length);

    FileStream->CurrentOffset = ByteOffset.QuadPart + BytesRead;
    Irp->IoStatus.Information = BytesRead;

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        FileObject->CurrentByteOffset.QuadPart = FileStream->CurrentOffset;
    }

    Status = STATUS_SUCCESS;
    goto result;
result:
    DPRINT1("ExtfsFsdWorkerRead - Ended\n");
    Irp->IoStatus.Status = Status;
    if (WorkerContext->Status)
        *WorkerContext->Status = Status;
    IoFreeWorkItem(WorkerContext->WorkItem);
    ExFreePoolWithTag(Context, EXTFS_TAG_WORKER_CONTEXT);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

NTSTATUS
ExtfsFsdDispatchRead(PDEVICE_OBJECT DeviceObject,
                     PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PEXTFS_FILE_STREAM FileStream = FileObject->FsContext2;
    LARGE_INTEGER ByteOffset = IrpSp->Parameters.Read.ByteOffset;

    Irp->IoStatus.Information = 0;

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

    if (FileContext->InodeContext->IsDirectory)
    {
        DPRINT1("Cannot read directory\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    if (ByteOffset.QuadPart == FILE_USE_FILE_POINTER_POSITION)
    {
        ByteOffset.QuadPart = FileStream->CurrentOffset;
    }

    if (ByteOffset.QuadPart >= FileContext->InodeContext->FileSize)
    {
        DPRINT1("Reached end of file\n");
        Status = STATUS_END_OF_FILE;
        goto result;
    }

    PEXTFS_IRP_WORKER_CONTEXT IrpWorkerContext = ExAllocatePoolWithTag(NonPagedPool,
                                                                       sizeof(*IrpWorkerContext),
                                                                       EXTFS_TAG_WORKER_CONTEXT);
    if (!IrpWorkerContext)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }
    RtlZeroMemory(IrpWorkerContext, sizeof(*IrpWorkerContext));
    IrpWorkerContext->Irp = Irp;

    IrpWorkerContext->WorkItem = IoAllocateWorkItem(DeviceObject);
    if (!IrpWorkerContext->WorkItem)
    {
        ExFreePoolWithTag(IrpWorkerContext, EXTFS_TAG_WORKER_CONTEXT);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        IrpWorkerContext->Status = &Status;
        ExtfsFsdWorkerRead(DeviceObject, IrpWorkerContext);
    }
    else
    {
        Status = STATUS_PENDING;
        IoQueueWorkItem(IrpWorkerContext->WorkItem, ExtfsFsdWorkerRead, DelayedWorkQueue, IrpWorkerContext);
    }

    return Status;

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
