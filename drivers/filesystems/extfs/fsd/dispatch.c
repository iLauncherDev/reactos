#include <extfs.h>

const char* IrpMajorNames[] = {
    "CREATE", "CREATE_NAMED_PIPE", "CLOSE", "READ", "WRITE", "QUERY_INFORMATION", "SET_INFORMATION",
    "QUERY_EA", "SET_EA", "FLUSH_BUFFERS", "QUERY_VOLUME_INFORMATION",
    "SET_VOLUME_INFORMATION", "DIRECTORY_CONTROL", "FILE_SYSTEM_CONTROL",
    "DEVICE_CONTROL", "INTERNAL_DEVICE_CONTROL", "SHUTDOWN", "LOCK_CONTROL",
    "CLEANUP", "CREATE_MAILSLOT", "QUERY_SECURITY", "SET_SECURITY", "POWER",
    "SYSTEM_CONTROL", "DEVICE_CHANGE", "QUERY_QUOTA", "SET_QUOTA", "PNP"
};

PVOID ExtfsFsdGetBuffer(PIRP Irp)
{
    if (Irp->AssociatedIrp.SystemBuffer)
    {
        DPRINT1("Using System Buffer\n");
        return Irp->AssociatedIrp.SystemBuffer;
    }
    else if (Irp->MdlAddress)
    {
        DPRINT1("Using Mdl Buffer\n");
        return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, HighPagePriority);
    }
    else if (Irp->UserBuffer)
    {
        DPRINT1("Using User Buffer\n");
        return Irp->UserBuffer;
    }

    return NULL;
}

PVOID ExtfsFsdGetDeviceExtension()
{
    
}

NTSTATUS NTAPI ExtfsIrpCompletionRoutine(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    PEXTFS_COMPLETION_ROUNTINE_CTX IrpCompletionRoutineContext = Context;

    DPRINT("ExtfsIrpCompletionRoutine(0x%p, 0x%p, 0x%p)\n", DeviceObject, Irp, Context);

    IrpCompletionRoutineContext->IoStatus = Irp->IoStatus;
    KeSetEvent(&IrpCompletionRoutineContext->Event, IO_NO_INCREMENT, FALSE);

    return STATUS_CONTINUE_COMPLETION;
}

NTSTATUS
NTAPI
ExtfsFsdDispatch(PDEVICE_OBJECT DeviceObject,
                 PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    BOOLEAN IsImplemented = TRUE;

    FsRtlEnterFileSystem();
    //ExAcquireResourceExclusiveLite(&ExtfsGlobalData->AnotherResource, TRUE);
    DPRINT1("IRP_MJ_%s requested started\n", IrpMajorNames[IrpSp->MajorFunction]);

    switch (IrpSp->MajorFunction)
    {
    case IRP_MJ_CREATE:
        Status = ExtfsFsdDispatchCreate(DeviceObject, Irp);
        break;

    case IRP_MJ_CLOSE:
        Status = ExtfsFsdDispatchClose(DeviceObject, Irp);
        break;

    case IRP_MJ_CLEANUP:
        Status = ExtfsFsdDispatchCleanup(DeviceObject, Irp);
        break;

    case IRP_MJ_FLUSH_BUFFERS:
        Status = ExtfsFsdDispatchFlushBuffers(DeviceObject, Irp);
        break;

    case IRP_MJ_LOCK_CONTROL:
        Status = ExtfsFsdDispatchLockControl(DeviceObject, Irp);
        break;

    case IRP_MJ_READ:
        Status = ExtfsFsdDispatchRead(DeviceObject, Irp);
        break;

    case IRP_MJ_WRITE:
        Status = ExtfsFsdDispatchWrite(DeviceObject, Irp);
        break;

    case IRP_MJ_QUERY_INFORMATION:
        Status = ExtfsFsdDispatchQueryInformation(DeviceObject, Irp);
        break;

    case IRP_MJ_SET_INFORMATION:
        Status = ExtfsFsdDispatchSetInformation(DeviceObject, Irp);
        break;

    case IRP_MJ_DIRECTORY_CONTROL:
        Status = ExtfsFsdDispatchDirectoryControl(DeviceObject, Irp);
        break;

    case IRP_MJ_FILE_SYSTEM_CONTROL:
        Status = ExtfsFsdDispatchFsControl(DeviceObject, Irp);
        break;

    case IRP_MJ_PNP:
        Status = ExtfsFsdDispatchPnp(DeviceObject, Irp);
        break;

    case IRP_MJ_DEVICE_CONTROL:
        Status = ExtfsFsdDispatchDeviceControl(DeviceObject, Irp);
        break;

    case IRP_MJ_SET_VOLUME_INFORMATION:
        Status = ExtfsFsdDispatchSetVolumeInformation(DeviceObject, Irp);
        break;

    case IRP_MJ_QUERY_VOLUME_INFORMATION:
        Status = ExtfsFsdDispatchQueryVolumeInformation(DeviceObject, Irp);
        break;

    case IRP_MJ_SHUTDOWN:
        Status = ExtfsFsdDispatchShutdown(DeviceObject, Irp);
        break;

    default:
        IsImplemented = FALSE;
        break;
    }

    DPRINT1("IRP_MJ_%s (%s) requested ended\n",
            IrpMajorNames[IrpSp->MajorFunction], IsImplemented ? "Implemented" : "Unimplemented");
    //ExReleaseResourceLite(&ExtfsGlobalData->AnotherResource);
    FsRtlExitFileSystem();

    return Status;
}
