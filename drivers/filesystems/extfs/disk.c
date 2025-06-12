#include "extfs.h"

NTSTATUS NTAPI DiskCompletionRoutine(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    PEXTFS_DISK_COMPLETION_ROUNTINE_CTX DiskCompletionRoutineContext = Context;

    DPRINT1("DiskCompletionRoutine(0x%p, 0x%p, 0x%p)\n", DeviceObject, Irp, Context);

    if (!NT_SUCCESS(Irp->IoStatus.Status))
    {
        Irp->IoStatus.Information = 0;
    }

    DiskCompletionRoutineContext->IoStatus = Irp->IoStatus;
    KeSetEvent(&DiskCompletionRoutineContext->Event, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS DiskRead(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait)
{
    EXTFS_DISK_COMPLETION_ROUNTINE_CTX DiskCompletionRoutineContext = {0};
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    DPRINT1("DiskRead(0x%p, 0x%p, %llu, %u)\n", VolumeDevice, Buffer, Offset.QuadPart, Length);

    if (Wait)
        KeInitializeEvent(&DiskCompletionRoutineContext.Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_READ,
        VolumeDevice,
        Buffer,
        Length,
        &Offset,
        NULL,
        &IoStatus
    );

    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoSetCompletionRoutine(
        Irp,
        DiskCompletionRoutine,
        &DiskCompletionRoutineContext,
        TRUE, TRUE, TRUE
    );

    NTSTATUS Status = IoCallDriver(VolumeDevice, Irp);
    if (Wait && Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&DiskCompletionRoutineContext.Event, Executive, KernelMode, FALSE, NULL);
        Status = DiskCompletionRoutineContext.IoStatus.Status;
    }

    DPRINT1("DiskRead (0x%p) - Ended\n", Status);

    return Status;
}

NTSTATUS DiskWrite(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, BOOLEAN Wait)
{
    EXTFS_DISK_COMPLETION_ROUNTINE_CTX DiskCompletionRoutineContext = {0};
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    DPRINT1("DiskWrite(0x%p, 0x%p, %llu, %u)\n", VolumeDevice, Buffer, Offset.QuadPart, Length);

    if (Wait)
        KeInitializeEvent(&DiskCompletionRoutineContext.Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_WRITE,
        VolumeDevice,
        Buffer,
        Length,
        &Offset,
        NULL,
        &IoStatus
    );

    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoSetCompletionRoutine(
        Irp,
        DiskCompletionRoutine,
        &DiskCompletionRoutineContext,
        TRUE, TRUE, TRUE
    );

    NTSTATUS Status = IoCallDriver(VolumeDevice, Irp);
    if (Wait && Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&DiskCompletionRoutineContext.Event, Executive, KernelMode, FALSE, NULL);
        Status = DiskCompletionRoutineContext.IoStatus.Status;
    }

    DPRINT1("DiskRead (0x%p) - Ended\n", Status);

    return Status;
}
