#include <extfs.h>

NTSTATUS
NTAPI
ExtfsFsdDispatchPnp(PDEVICE_OBJECT DeviceObject,
                    PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PEXTFS_VOLUME_EXTENSION VolumeExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    Irp->IoStatus.Information = 0;

    if (!IsExtfsVolumeExtension(VolumeExtension))
    {
        VolumeExtension = NULL;
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    switch (IrpSp->MinorFunction)
    {
    case IRP_MN_QUERY_REMOVE_DEVICE:
        if (VolumeExtension->OpenFileCount > 0)
        {
            Status = STATUS_DEVICE_BUSY;
        }
        break;

    case IRP_MN_SURPRISE_REMOVAL:
        DPRINT1("To be implemented\n");
        break;

    case IRP_MN_REMOVE_DEVICE:
        ExtfsUninitializeVolume(VolumeExtension, TRUE);
        break;

    default:
        DPRINT1("Calling RealDevice for IrpSp->MinorFunction (%u)\n", IrpSp->MinorFunction);
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(VolumeExtension->RealDevice, Irp);
    }

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}