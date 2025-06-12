#include <extfs.h>

PEXTFS_GLOBAL_DATA ExtfsGlobalData = NULL;
PFILE_OBJECT MountMgrFile = NULL;
PDEVICE_OBJECT MountMgrDevice = NULL;
NPAGED_LOOKASIDE_LIST ExtfsExtentLookasideList;

NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    UNICODE_STRING MountPointManagerName = RTL_CONSTANT_STRING(MOUNTMGR_DEVICE_NAME);
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(DEVICE_NAME);
    UNICODE_STRING DosDeviceName = RTL_CONSTANT_STRING(DOS_DEVICE_NAME);
    NTSTATUS Status = STATUS_SUCCESS;
    PDEVICE_OBJECT DeviceObject = NULL;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE DriverKey = NULL;

    UNREFERENCED_PARAMETER(Attributes);
    UNREFERENCED_PARAMETER(DriverKey);

    DPRINT1("DriverEntry(%p, '%wZ')\n", DriverObject, RegistryPath);

    Status = IoGetDeviceObjectPointer(
        &MountPointManagerName,
        FILE_READ_ATTRIBUTES,
        &MountMgrFile,
        &MountMgrDevice
    );
    if (!NT_SUCCESS(Status))
    {
        DPRINT("Cannot get mount point manager status: 0x%lx\n", Status);
        goto result;
    }

    Status = IoCreateDevice(DriverObject,
                            sizeof(EXTFS_GLOBAL_DATA),
                            &DeviceName,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoCreateDevice failed with status: 0x%lx\n", Status);
        goto result;
    }

    ExInitializeNPagedLookasideList(
        &ExtfsExtentLookasideList,
        NULL,
        NULL,
        0,
        sizeof(EXTFS_EXTENT),
        EXTFS_TAG_EXTENT_LIST,
        0
    );

    ExtfsGlobalData = DeviceObject->DeviceExtension;
    RtlZeroMemory(ExtfsGlobalData, sizeof(*ExtfsGlobalData));

    ExtfsInitListEntry(&ExtfsGlobalData->MountedVolumeList, ExtfsGlobalData);

    EXTFS_INIT_FCB_HEADER(&ExtfsGlobalData->StandardFCB, *ExtfsGlobalData, EXTFS_GLOBAL_DATA_MAGIC);

    ExtfsGlobalData->DeviceObject = DeviceObject;
    ExtfsGlobalData->DriverObject = DriverObject;

    ExtfsGlobalData->MountedVolumeBitmapEntries = EXTFS_MAX_MOUNTED_VOLUMES;
    ExtfsGlobalData->MountedVolumeBitmapSize = ExtfsBitmapGetSize(ExtfsGlobalData->MountedVolumeBitmapEntries);
    ExtfsGlobalData->MountedVolumeBitmap = ExAllocatePoolWithTag(NonPagedPool, ExtfsGlobalData->MountedVolumeBitmapSize, EXTFS_TAG_BUFFER);
    if (!ExtfsGlobalData->MountedVolumeBitmap)
    {
        DPRINT1("Cannot allocate MountedVolumeBitmap\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }
    RtlZeroMemory(ExtfsGlobalData->MountedVolumeBitmap, ExtfsGlobalData->MountedVolumeBitmapSize);

    ExInitializeResourceLite(&ExtfsGlobalData->MountedVolumeBitmapLock);

    ExInitializeResourceLite(&ExtfsGlobalData->Resource);
    ExInitializeResourceLite(&ExtfsGlobalData->AnotherResource);

    ExtfsGlobalData->CacheMgrCallbacks.AcquireForLazyWrite = ExtfsAcquireForLazyWrite;
    ExtfsGlobalData->CacheMgrCallbacks.ReleaseFromLazyWrite = ExtfsReleaseFromLazyWrite;
    ExtfsGlobalData->CacheMgrCallbacks.AcquireForReadAhead = ExtfsAcquireForReadAhead;
    ExtfsGlobalData->CacheMgrCallbacks.ReleaseFromReadAhead = ExtfsReleaseFromReadAhead;

    ExtfsGlobalData->FastIoDispatch.FastIoCheckIfPossible = ExtfsFastIoCheckIfPossible;
    ExtfsGlobalData->FastIoDispatch.AcquireFileForNtCreateSection = (PFAST_IO_ACQUIRE_FILE)ExtfsAcquireFileForNtCreateSection;
    ExtfsGlobalData->FastIoDispatch.ReleaseFileForNtCreateSection = (PFAST_IO_RELEASE_FILE)ExtfsReleaseFileForNtCreateSection;
    ExtfsGlobalData->FastIoDispatch.FastIoRead = ExtfsFastIoRead;
    ExtfsGlobalData->FastIoDispatch.FastIoWrite = ExtfsFastIoWrite;
    ExtfsGlobalData->FastIoDispatch.SizeOfFastIoDispatch = sizeof(ExtfsGlobalData->FastIoDispatch);

    DriverObject->FastIoDispatch = &ExtfsGlobalData->FastIoDispatch;

    ULONG MajorFunctions[] = {
        IRP_MJ_CREATE,
        IRP_MJ_CLOSE,
        IRP_MJ_READ,
        IRP_MJ_WRITE,
        IRP_MJ_QUERY_INFORMATION,
        IRP_MJ_SET_INFORMATION,
        IRP_MJ_QUERY_EA,
        IRP_MJ_SET_EA,
        IRP_MJ_FLUSH_BUFFERS,
        IRP_MJ_QUERY_VOLUME_INFORMATION,
        IRP_MJ_SET_VOLUME_INFORMATION,
        IRP_MJ_DIRECTORY_CONTROL,
        IRP_MJ_FILE_SYSTEM_CONTROL,
        IRP_MJ_DEVICE_CONTROL,
        IRP_MJ_SHUTDOWN,
        IRP_MJ_LOCK_CONTROL,
        IRP_MJ_CLEANUP,
        IRP_MJ_QUERY_SECURITY,
        IRP_MJ_SET_SECURITY,
        IRP_MJ_POWER,
        IRP_MJ_SYSTEM_CONTROL,
        IRP_MJ_PNP
    };

    for (ULONG i = 0; i < sizeof(MajorFunctions) / sizeof(*MajorFunctions); i++)
        DriverObject->MajorFunction[MajorFunctions[i]] = ExtfsFsdDispatch;

    DeviceObject->Flags |= DO_DIRECT_IO;

    IoCreateSymbolicLink(&DosDeviceName, &DeviceName);

    IoRegisterFileSystem(DeviceObject);

    DPRINT1("Extfs driver initialized!\n");

result:
    if (!NT_SUCCESS(Status))
    {
        if (DeviceObject)
            IoDeleteDevice(DeviceObject);
    }

    return Status;
}
