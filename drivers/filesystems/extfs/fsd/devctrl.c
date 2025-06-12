#include <extfs.h>

NTSTATUS
NTAPI
ExtfsFsdDispatchDeviceControl(PDEVICE_OBJECT DeviceObject,
                              PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PEXTFS_VOLUME_EXTENSION VolumeExtension = DeviceObject->DeviceExtension;
    ULONG IoControlCode = IrpSp->Parameters.DeviceIoControl.IoControlCode;
    PVOID UserBuffer = ExtfsFsdGetBuffer(Irp);
    ULONG InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS Status = STATUS_SUCCESS;

    Irp->IoStatus.Information = 0;

    if (!IsExtfsVolumeExtension(VolumeExtension))
    {
        DPRINT1("Invalid VolumeExtension\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    switch (IoControlCode)
    {
        case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
        {
            ULONG RequiredSize = sizeof(MOUNTDEV_NAME);
            PMOUNTDEV_NAME DeviceName = UserBuffer;

            RtlZeroMemory(DeviceName, RequiredSize);

            DeviceName->NameLength = VolumeExtension->DeviceNameString.Length;
            RequiredSize += DeviceName->NameLength;

            if (RequiredSize > OutputBufferLength)
            {
                DPRINT1("Buffer overflow\n");

                Irp->IoStatus.Information = OutputBufferLength;
                Status = STATUS_BUFFER_OVERFLOW;
                break;
            }

            RtlZeroMemory((PCHAR)DeviceName + sizeof(MOUNTDEV_NAME), RequiredSize - sizeof(MOUNTDEV_NAME));

            RtlCopyMemory(DeviceName->Name, VolumeExtension->DeviceNameString.Buffer, DeviceName->NameLength);
            Irp->IoStatus.Information = RequiredSize;
            break;
        }

        case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
        {
            ULONG CharsCount = strnlen(VolumeExtension->TempUUID, sizeof(VolumeExtension->TempUUID));
            ULONG RequiredSize = sizeof(MOUNTDEV_UNIQUE_ID);
            PMOUNTDEV_UNIQUE_ID UniqueId = UserBuffer;

            RtlZeroMemory(UniqueId, RequiredSize);

            UniqueId->UniqueIdLength = CharsCount;
            RequiredSize += UniqueId->UniqueIdLength;

            if (RequiredSize > OutputBufferLength)
            {
                DPRINT1("Buffer overflow\n");

                Irp->IoStatus.Information = OutputBufferLength;
                Status = STATUS_BUFFER_OVERFLOW;
                break;
            }

            RtlZeroMemory((PCHAR)UniqueId + sizeof(MOUNTDEV_UNIQUE_ID), RequiredSize - sizeof(MOUNTDEV_UNIQUE_ID));

            RtlCopyMemory(UniqueId->UniqueId, VolumeExtension->TempUUID, UniqueId->UniqueIdLength);
            Irp->IoStatus.Information = RequiredSize;
            break;
        }

        default:
            DPRINT1("Calling RealDevice for IoControlCode (%u)\n", IoControlCode);
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(VolumeExtension->RealDevice, Irp);
    }

    goto result;
result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
