#include "extfs.h"

NTSTATUS
NTAPI
ExtfsFsdDispatchFsControl(PDEVICE_OBJECT DeviceObject,
                          PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PEXTFS_VOLUME_EXTENSION VolumeExtension;
    PDEVICE_OBJECT VolumeDeviceObject;
    NTSTATUS Status;
    EXT_SUPER_BLOCK SuperBlock;
    PDEVICE_OBJECT VolumeDevice;
    LARGE_INTEGER VolumeOffset;

    switch (IrpSp->MinorFunction)
    {
    case IRP_MN_VERIFY_VOLUME:
        VolumeDevice = IrpSp->Parameters.VerifyVolume.DeviceObject;

        VolumeOffset.QuadPart = 1024;
        Status = DiskRead(VolumeDevice, &SuperBlock, VolumeOffset, sizeof(EXT_SUPER_BLOCK));
        if (!NT_SUCCESS(Status))
        {
            return STATUS_WRONG_VOLUME;
        }

        Status = ExtfsCheckSuperBlock(&SuperBlock);
        if (!NT_SUCCESS(Status))
        {
            return STATUS_WRONG_VOLUME;
        }

        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;

    case IRP_MN_MOUNT_VOLUME:
        VolumeDevice = IrpSp->Parameters.MountVolume.DeviceObject;

        VolumeOffset.QuadPart = 1024;
        Status = DiskRead(VolumeDevice, &SuperBlock, VolumeOffset, sizeof(EXT_SUPER_BLOCK));
        if (!NT_SUCCESS(Status))
        {
            return STATUS_WRONG_VOLUME;
        }

        Status = ExtfsCheckSuperBlock(&SuperBlock);
        if (!NT_SUCCESS(Status))
        {
            return STATUS_WRONG_VOLUME;
        }

        Status = IoCreateDevice(
            ExtfsGlobalData->DriverObject,
            sizeof(EXTFS_VOLUME_EXTENSION),
            NULL,
            FILE_DEVICE_DISK_FILE_SYSTEM,
            0,
            FALSE,
            &VolumeDeviceObject
        );
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        VolumeExtension = VolumeDeviceObject->DeviceExtension;
        RtlZeroMemory(VolumeExtension, sizeof(*VolumeExtension));

        VolumeExtension->DeviceObject = IrpSp->Parameters.MountVolume.Vpb->DeviceObject;
        VolumeExtension->RealDevice = IrpSp->Parameters.MountVolume.Vpb->RealDevice;
        VolumeExtension->Vpb = IrpSp->Parameters.MountVolume.Vpb;
        VolumeExtension->SuperBlock = SuperBlock;
        ExtfsInitializeVolume(VolumeExtension);

        IoAttachDeviceToDeviceStack(VolumeDeviceObject, VolumeExtension->RealDevice);

        VolumeExtension->Vpb->DeviceObject = VolumeDeviceObject;
        VolumeExtension->Vpb->Flags |= VPB_MOUNTED;

        VolumeDeviceObject->Flags |= DO_DIRECT_IO;
        VolumeDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        DbgPrint("Volume mounted!\n");
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
ExtfsFsdDispatch(PDEVICE_OBJECT DeviceObject,
                 PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MajorFunction)
    {
    case IRP_MJ_FILE_SYSTEM_CONTROL:
        return ExtfsFsdDispatchFsControl(DeviceObject, Irp);
    }

    return STATUS_NOT_IMPLEMENTED;
}
