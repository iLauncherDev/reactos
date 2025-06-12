#include "extfs.h"

NTSTATUS DiskRead(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_READ,
        VolumeDevice,
        Buffer,
        Length,
        &Offset,
        &Event,
        &IoStatus
    );

    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    NTSTATUS Status = IoCallDriver(VolumeDevice, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    return IoStatus.Status;
}

NTSTATUS DiskWrite(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_WRITE,
        VolumeDevice,
        Buffer,
        Length,
        &Offset,
        &Event,
        &IoStatus
    );

    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    NTSTATUS Status = IoCallDriver(VolumeDevice, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    return IoStatus.Status;
}

NTSTATUS ExtfsDiskRead(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    return DiskRead(VolumeExtension->DeviceObject, Buffer, Offset, Length);
}

NTSTATUS ExtfsDiskWrite(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    if (VolumeExtension->ReadOnly)
        return STATUS_ACCESS_DENIED;
    return DiskWrite(VolumeExtension->DeviceObject, Buffer, Offset, Length);
}
