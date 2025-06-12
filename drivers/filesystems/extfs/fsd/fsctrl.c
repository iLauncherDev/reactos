#include <extfs.h>

VOID ExtfsAddVolumeExtensionToList(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    PEXTFS_GLOBAL_DATA GlobalData = VolumeExtension->GlobalData;

    ASSERT(GlobalData != NULL);

    ExAcquireResourceExclusiveLite(&GlobalData->Resource, TRUE);

    ExtfsInsertTailList(&GlobalData->MountedVolumeList, &VolumeExtension->ListEntry);

    ExReleaseResourceLite(&GlobalData->Resource);
}

VOID ExtfsRemoveVolumeExtensionFromList(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    PEXTFS_GLOBAL_DATA GlobalData = VolumeExtension->GlobalData;

    ASSERT(GlobalData != NULL);

    ExAcquireResourceExclusiveLite(&ExtfsGlobalData->Resource, TRUE);

    ExtfsRemoveEntryList(&VolumeExtension->ListEntry);

    ExReleaseResourceLite(&ExtfsGlobalData->Resource);
}

NTSTATUS
ExtfsMountVolumeMountMgr(PWCHAR VolumeName, ULONG VolumeNameLength)
{
    WCHAR TempBuffer[512];
    PMOUNTMGR_TARGET_NAME MountPoint = (PVOID)&TempBuffer;
    RtlZeroMemory(TempBuffer, sizeof(TempBuffer));

    MountPoint->DeviceNameLength = VolumeNameLength;
    RtlCopyMemory(&MountPoint->DeviceName, VolumeName, MountPoint->DeviceNameLength);

    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    PIRP Irp = IoBuildDeviceIoControlRequest(
        IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION,
        MountMgrDevice,
        MountPoint,
        sizeof(*MountPoint) + MountPoint->DeviceNameLength,
        NULL,
        0,
        FALSE,
        &Event,
        &IoStatus
    );
    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NTSTATUS Status = IoCallDriver(MountMgrDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

NTSTATUS
ExtfsFsdDispatchFsControl(PDEVICE_OBJECT DeviceObject,
                          PIRP Irp)
{
    KIRQL OldIrql;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PEXTFS_GLOBAL_DATA GlobalData = NULL;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = NULL;
    PDEVICE_OBJECT VolumeExtensionObject = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    PDEVICE_OBJECT RealDevice;
    PVPB RealDeviceVBP;
    ULONG VolumeNumber;
    UNICODE_STRING VolumeName;
    WCHAR VolumeNameBuffer[256];
    ULONG VolumeNameBufferMaxChars = sizeof(VolumeNameBuffer) / sizeof(*VolumeNameBuffer);
    ULONG BytesPerSector;

    Irp->IoStatus.Information = 0;

    switch (IrpSp->MinorFunction)
    {
    case IRP_MN_USER_FS_REQUEST:
        VolumeExtension = DeviceObject->DeviceExtension;
        if (!IsExtfsVolumeExtension(VolumeExtension))
        {
            VolumeExtension = NULL;
            Status = STATUS_INVALID_DEVICE_REQUEST;
            goto result;
        }

        switch (IrpSp->Parameters.FileSystemControl.FsControlCode)
        {
        case FSCTL_GET_REPARSE_POINT:
            DPRINT1("FSCTL_GET_REPARSE_POINT\n");
            break;

        case FSCTL_SET_REPARSE_POINT:
            DPRINT1("FSCTL_SET_REPARSE_POINT\n");
            break;

        case FSCTL_DELETE_REPARSE_POINT:
            DPRINT1("FSCTL_DELETE_REPARSE_POINT\n");
            break;

        case FSCTL_LOCK_VOLUME:
            Status = ExtfsLockVolume(VolumeExtension);
            DPRINT1("FSCTL_LOCK_VOLUME\n");
            break;

        case FSCTL_UNLOCK_VOLUME:
            Status = ExtfsUnlockVolume(VolumeExtension);
            DPRINT1("FSCTL_UNLOCK_VOLUME\n");
            break;

        case FSCTL_DISMOUNT_VOLUME:
            FsRtlNotifyVolumeEvent(VolumeExtension->StreamFileObject, FSRTL_VOLUME_DISMOUNT);

            Status = ExtfsUninitializeVolume(VolumeExtension, FALSE);
            DPRINT1("FSCTL_DISMOUNT_VOLUME\n");
            break;

        case FSCTL_IS_VOLUME_MOUNTED:
            DPRINT1("FSCTL_IS_VOLUME_MOUNTED\n");
            break;

        case FSCTL_INVALIDATE_VOLUMES:
            DPRINT1("FSCTL_INVALIDATE_VOLUMES\n");
            break;

#if (_WIN32_WINNT >= 0x0500)
        case FSCTL_ALLOW_EXTENDED_DASD_IO:
            DPRINT1("FSCTL_ALLOW_EXTENDED_DASD_IO\n");
            break;
#endif

        case FSCTL_REQUEST_OPLOCK_LEVEL_1:
        case FSCTL_REQUEST_OPLOCK_LEVEL_2:
        case FSCTL_REQUEST_BATCH_OPLOCK:
        case FSCTL_OPLOCK_BREAK_ACKNOWLEDGE:
        case FSCTL_OPBATCH_ACK_CLOSE_PENDING:
        case FSCTL_OPLOCK_BREAK_NOTIFY:
        case FSCTL_OPLOCK_BREAK_ACK_NO_2:
            DPRINT1("FSCTL_(*OPLOCK*|*OPBATCH*)\n");
            break;

        case FSCTL_IS_VOLUME_DIRTY:
            DPRINT1("FSCTL_IS_VOLUME_DIRTY\n");
            break;

        case FSCTL_QUERY_RETRIEVAL_POINTERS:
            DPRINT1("FSCTL_QUERY_RETRIEVAL_POINTERS\n");
            break;

        case FSCTL_GET_RETRIEVAL_POINTERS:
            DPRINT1("FSCTL_GET_RETRIEVAL_POINTERS\n");
            break;

        default:
            DPRINT1("IrpSp->Parameters.FileSystemControl.FsControlCode = %u\n", IrpSp->Parameters.FileSystemControl.FsControlCode);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        break;

    case IRP_MN_VERIFY_VOLUME:
        DPRINT1("IRP_MN_VERIFY_VOLUME is not implemented\n");
        break;

    case IRP_MN_MOUNT_VOLUME:
        GlobalData = DeviceObject->DeviceExtension;
        if (!IsExtfsGlobalData(GlobalData))
        {
            DPRINT1("The global data is not from this FSD\n");
            Status = STATUS_UNRECOGNIZED_VOLUME;
            break;
        }

        RealDevice = IrpSp->Parameters.MountVolume.DeviceObject;
        RealDeviceVBP = IrpSp->Parameters.MountVolume.Vpb;

        if (!RealDevice || !RealDeviceVBP)
        {
            DPRINT1("Invalid VolumeDevice\n");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (RealDeviceVBP->DeviceObject)
        {
            DPRINT1("Already mounted!\n");
            Status = STATUS_UNRECOGNIZED_VOLUME;
            break;
        }

        DPRINT1("RealDevice: RealDevice = 0x%p, RealDeviceVBP->RealDevice = 0x%p, RealDeviceVBP = 0x%p\n",
            RealDevice,
            RealDeviceVBP->RealDevice,
            RealDeviceVBP);

        Status = DiskGetBytesPerSector(RealDevice, &BytesPerSector);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Cannot get BytesPerSector\n");
            break;
        }

        Status = ExtfsAcquireFreeVolumeNumber(GlobalData, &VolumeNumber);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Cannot acquire free volume number\n");
            break;
        }

        RtlZeroMemory(VolumeNameBuffer, sizeof(VolumeNameBuffer));
        RtlStringCchPrintfW(VolumeNameBuffer, VolumeNameBufferMaxChars, DEVICE_VOLUME_NAME L"%u", VolumeNumber);

        DPRINT1("%.*ws\n", VolumeNameBufferMaxChars, VolumeNameBuffer);

        RtlInitUnicodeString(&VolumeName, VolumeNameBuffer);

        Status = IoCreateDevice(
            GlobalData->DriverObject,
            sizeof(EXTFS_VOLUME_EXTENSION),
            &VolumeName,
            FILE_DEVICE_DISK_FILE_SYSTEM,
            0,
            FALSE,
            &VolumeExtensionObject
        );
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto mount_error;
        }

        VolumeExtension = VolumeExtensionObject->DeviceExtension;
        RtlZeroMemory(VolumeExtension, sizeof(*VolumeExtension));

        ExtfsInitListEntry(&VolumeExtension->ListEntry, VolumeExtension);
        ExtfsInitListEntry(&VolumeExtension->OpenFileList, VolumeExtension);
        ExtfsInitListEntry(&VolumeExtension->InodeContextList, VolumeExtension);

        EXTFS_INIT_FCB_HEADER(&VolumeExtension->StandardFCB, *VolumeExtension, EXTFS_VOLUME_EXTENSION_MAGIC);

        VolumeExtension->GlobalData = GlobalData;

        RtlCopyMemory(VolumeExtension->DeviceName, VolumeNameBuffer, min(sizeof(VolumeNameBuffer), sizeof(VolumeExtension->DeviceName)));

        VolumeExtension->DeviceNameString.Buffer =
            (PVOID)&VolumeExtension->DeviceName;
        VolumeExtension->DeviceNameString.MaximumLength =
            sizeof(VolumeExtension->DeviceName);
        VolumeExtension->DeviceNameString.Length =
            wcsnlen(VolumeExtension->DeviceName, sizeof(VolumeExtension->DeviceName) / sizeof(WCHAR)) * sizeof(WCHAR);

        VolumeExtension->VolumeNumber = VolumeNumber;

        VolumeExtension->BytesPerSector = BytesPerSector;
        VolumeExtension->DiskBlockSize = VolumeExtension->BytesPerSector;

        VolumeExtension->DeviceObject = VolumeExtensionObject;
        VolumeExtension->RealDevice = RealDevice;
        VolumeExtension->Vpb = RealDeviceVBP;
        VolumeExtensionObject->Vpb = VolumeExtension->Vpb;

        IoAcquireVpbSpinLock(&OldIrql);

        VolumeExtension->Vpb->ReferenceCount++;

        VolumeExtension->Vpb->VolumeLabelLength = sizeof(*VolumeExtension->Vpb->VolumeLabel) * 2;
        RtlZeroMemory(VolumeExtension->Vpb->VolumeLabel, VolumeExtension->Vpb->VolumeLabelLength);

        VolumeExtension->Vpb->VolumeLabel[0] = '?';

        VolumeExtension->Vpb->DeviceObject = VolumeExtension->DeviceObject;
        VolumeExtension->Vpb->Flags |= VPB_MOUNTED;

        IoReleaseVpbSpinLock(OldIrql);

        ExInitializeResourceLite(&VolumeExtension->SuperBlockLock);
        ExInitializeResourceLite(&VolumeExtension->OpenFileListLock);
        ExInitializeResourceLite(&VolumeExtension->InodeContextListLock);

        ExInitializeResourceLite(&VolumeExtension->FileOperationLock);
        ExInitializeResourceLite(&VolumeExtension->DiskOperationLock);

        VolumeExtension->StreamFileObject = IoCreateStreamFileObject(NULL, VolumeExtension->RealDevice);
        if (!VolumeExtension->StreamFileObject)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto mount_error;
        }

        Status = ExtfsInitializeVolume(VolumeExtension, FALSE);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_UNRECOGNIZED_VOLUME;
            goto mount_error;
        }

        VolumeExtensionObject->StackSize = VolumeExtension->RealDevice->StackSize + 1;

        // VolumeExtensionObject->Flags |= RealDevice->Flags & (DO_DIRECT_IO | DO_BUFFERED_IO);
        VolumeExtensionObject->Flags &= ~DO_DEVICE_INITIALIZING;

        ExtfsAddVolumeExtensionToList(VolumeExtension);

        FsRtlNotifyVolumeEvent(VolumeExtension->StreamFileObject, FSRTL_VOLUME_MOUNT);
        // ExtfsMountVolumeMountMgr(VolumeExtension->DeviceNameString.Buffer, VolumeExtension->DeviceNameString.Length);

        ExtfsReadInodeContext(VolumeExtension, EXT_ROOT_INODE);

        if (!VolumeExtension->ReadOnly && FALSE)
        {
            DPRINT1("\nTest List\n\n");
            ExtfsFindFileByPath(VolumeExtension, "/__REACTOS__", FALSE, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION, FALSE, 0, NULL, NULL);

            DPRINT1("\nTest File\n\n");
            ExtfsFindFileByPath(VolumeExtension, "/myRootFile", FALSE, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION, TRUE, 0, NULL, NULL);

            DPRINT1("\nTest Done\n\n");
            ExtfsFindFileByPath(VolumeExtension, "/__REACTOS__", FALSE, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION, FALSE, 0, NULL, NULL);
        }

        DPRINT1("Volume mounted!\n");
        break;

mount_error:
        if (VolumeExtensionObject)
        {
            ExtfsUninitializeVolume(VolumeExtension, TRUE);
        }
        else
        {
            ExtfsReleaseVolumeNumber(GlobalData, VolumeNumber);
        }

        break;

    default:
        DPRINT1("IrpSp->MinorFunction (%u) is not implemented\n", IrpSp->MinorFunction);
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    goto result;
result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
