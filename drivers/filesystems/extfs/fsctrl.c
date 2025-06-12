#include "extfs.h"

NTSTATUS
ExtfsFsdDispatchFsControl(PDEVICE_OBJECT DeviceObject,
                          PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PEXTFS_VOLUME_EXTENSION VolumeExtension;
    PDEVICE_OBJECT VolumeDeviceObject;
    NTSTATUS Status;
    PEXT_SUPER_BLOCK SuperBlock;
    PDEVICE_OBJECT VolumeDevice;
    LARGE_INTEGER VolumeOffset;

    DPRINT1("IrpSp->MinorFunction = %u\n", IrpSp->MinorFunction);

    switch (IrpSp->MinorFunction)
    {
    case IRP_MN_VERIFY_VOLUME:
        VolumeDevice = IrpSp->Parameters.VerifyVolume.DeviceObject;
        if (VolumeDevice->Vpb->Flags & VPB_MOUNTED)
        {
            DPRINT1("Already mounted!\n");
            Status = STATUS_ALREADY_REGISTERED;
            break;
        }

        SuperBlock = ExAllocatePool(NonPagedPool, sizeof(*SuperBlock));
        if (!SuperBlock)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        VolumeOffset.QuadPart = 1024;
        Status = DiskRead(VolumeDevice, SuperBlock, VolumeOffset, sizeof(*SuperBlock), TRUE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Invalid SuperBlock\n");
            Status = STATUS_WRONG_VOLUME;
            ExFreePool(SuperBlock);
            break;
        }

        Status = ExtfsCheckSuperBlock(SuperBlock);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_WRONG_VOLUME;
            break;
        }

        ExFreePool(SuperBlock);

        Status = STATUS_SUCCESS;
        break;

    case IRP_MN_MOUNT_VOLUME:
        VolumeDevice = IrpSp->Parameters.MountVolume.DeviceObject;
        if (VolumeDevice->Vpb->Flags & VPB_MOUNTED)
        {
            DPRINT1("Already mounted!\n");
            Status = STATUS_ALREADY_REGISTERED;
            break;
        }

        SuperBlock = ExAllocatePool(NonPagedPool, sizeof(*SuperBlock));
        if (!SuperBlock)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        VolumeOffset.QuadPart = 1024;
        Status = DiskRead(VolumeDevice, SuperBlock, VolumeOffset, sizeof(*SuperBlock), TRUE);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_WRONG_VOLUME;
            break;
        }

        Status = ExtfsCheckSuperBlock(SuperBlock);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Invalid SuperBlock\n");
            Status = STATUS_WRONG_VOLUME;
            ExFreePool(SuperBlock);
            break;
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
            break;
        }

        VolumeExtension = VolumeDeviceObject->DeviceExtension;
        RtlZeroMemory(VolumeExtension, sizeof(*VolumeExtension));

        VolumeExtension->Identifier.Magic = EXTFS_VOLUME_EXTENSION_MAGIC;
        VolumeExtension->Identifier.Size = sizeof(*VolumeExtension);

        VolumeExtension->Vpb = IrpSp->Parameters.MountVolume.Vpb;
        VolumeExtension->VolumeDevice = VolumeDevice;
        VolumeExtension->DeviceObject = VolumeDeviceObject;
        VolumeExtension->RealDevice = VolumeExtension->Vpb->RealDevice;
        VolumeExtension->SuperBlock = *SuperBlock;
        ExtfsInitializeVolume(VolumeExtension);

        IoRegisterFileSystem(VolumeDeviceObject);

        VolumeExtension->Vpb->DeviceObject = VolumeExtension->DeviceObject;
        VolumeExtension->Vpb->Flags |= VPB_MOUNTED;

        VolumeDeviceObject->Flags |= DO_DIRECT_IO;
        VolumeDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
        VolumeDeviceObject->StackSize = VolumeDevice->StackSize + 1;
        VolumeDeviceObject->Vpb = VolumeExtension->Vpb;

        ExFreePool(SuperBlock);

        DPRINT1("Volume mounted!\n");
        Status = STATUS_SUCCESS;
        break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
ExtfsFsdDispatchQueryVolumeInformation(PDEVICE_OBJECT DeviceObject,
                                       PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PEXTFS_VOLUME_EXTENSION VolumeExtension = DeviceObject->DeviceExtension;
    PVOID UserBuffer = ExtfsFsdGetBuffer(Irp);
    ULONG Length = IrpSp->Parameters.QueryVolume.Length;
    FS_INFORMATION_CLASS FsInformationClass = IrpSp->Parameters.QueryVolume.FsInformationClass;
    PFILE_FS_VOLUME_INFORMATION OutputFileFsVolumeInformation = UserBuffer;
    PFILE_FS_LABEL_INFORMATION OutputFileFsLabelInformation = UserBuffer;
    PFILE_FS_SIZE_INFORMATION OutputFileFsSizeInformation = UserBuffer;
    PFILE_FS_ATTRIBUTE_INFORMATION OutputFileFsAttributeInformation = UserBuffer;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG FsNameLen = 0, Index = 0;
    ULONG VolumeLabelLen = 0;

    if (!VolumeExtension || VolumeExtension->Identifier.Magic != EXTFS_VOLUME_EXTENSION_MAGIC)
    {
        DPRINT1("Invalid VolumeExtension\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    FsNameLen = strlen(VolumeExtension->FileSystemName);
    while (VolumeLabelLen < sizeof(VolumeExtension->VolumeLabel) && VolumeExtension->VolumeLabel[VolumeLabelLen])
        VolumeLabelLen++;

    switch (FsInformationClass)
    {
    case FileFsVolumeInformation:
        Irp->IoStatus.Information =
            FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel) + (VolumeLabelLen * sizeof(WCHAR));
        RtlZeroMemory(OutputFileFsVolumeInformation, Irp->IoStatus.Information);

        OutputFileFsVolumeInformation->VolumeLabelLength = VolumeLabelLen * sizeof(WCHAR);
        while (Index < VolumeLabelLen)
            OutputFileFsVolumeInformation->VolumeLabel[Index] = VolumeExtension->VolumeLabel[Index], Index++;
        break;

    case FileFsLabelInformation:
        Irp->IoStatus.Information =
            FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel) + (VolumeLabelLen * sizeof(WCHAR));
        RtlZeroMemory(OutputFileFsLabelInformation, Irp->IoStatus.Information);

        OutputFileFsLabelInformation->VolumeLabelLength = VolumeLabelLen * sizeof(WCHAR);
        while (Index < VolumeLabelLen)
            OutputFileFsLabelInformation->VolumeLabel[Index] = VolumeExtension->VolumeLabel[Index], Index++;
        break;

    case FileFsSizeInformation:
        Irp->IoStatus.Information =
            sizeof(*OutputFileFsSizeInformation);
        RtlZeroMemory(OutputFileFsSizeInformation, Irp->IoStatus.Information);

        OutputFileFsSizeInformation->SectorsPerAllocationUnit = 1;
        OutputFileFsSizeInformation->BytesPerSector = VolumeExtension->BlockSize;
        OutputFileFsSizeInformation->TotalAllocationUnits.QuadPart = VolumeExtension->TotalBlocks;
        OutputFileFsSizeInformation->AvailableAllocationUnits.QuadPart =
            VolumeExtension->TotalBlocks - VolumeExtension->UsedBlocks;
        break;

    case FileFsAttributeInformation:
        Irp->IoStatus.Information =
            FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName) + (FsNameLen * sizeof(WCHAR));
        RtlZeroMemory(OutputFileFsAttributeInformation, Irp->IoStatus.Information);

        OutputFileFsAttributeInformation->FileSystemNameLength = FsNameLen * sizeof(WCHAR);
        while (Index < FsNameLen)
            OutputFileFsAttributeInformation->FileSystemName[Index] = VolumeExtension->FileSystemName[Index], Index++;
        break;

    default:
        DPRINT1("Unimplemented QueryVolume (%u)\n", FsInformationClass);
        Status = STATUS_NOT_IMPLEMENTED;
        break;
    }

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
