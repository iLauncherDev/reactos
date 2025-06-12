#include "extfs.h"

NTSTATUS DiskRead(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;

    DPRINT1("DiskRead(0x%p, 0x%p, %llu, %u)\n", VolumeDevice, Buffer, Offset.QuadPart, Length);

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

    DPRINT1("DiskWrite(0x%p, 0x%p, %llu, %u)\n", VolumeDevice, Buffer, Offset.QuadPart, Length);

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
