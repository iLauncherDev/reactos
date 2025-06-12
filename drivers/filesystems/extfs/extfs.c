#include "extfs.h"

PEXTFS_GLOBAL_DATA ExtfsGlobalData = NULL;

CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(DEVICE_NAME);
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE DriverKey = NULL;

    UNREFERENCED_PARAMETER(Attributes);
    UNREFERENCED_PARAMETER(DriverKey);

    DPRINT1("DriverEntry(%p, '%wZ')\n", DriverObject, RegistryPath);

    Status = IoCreateDevice(DriverObject,
                            sizeof(EXTFS_GLOBAL_DATA),
                            &DeviceName,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoCreateDevice failed with status: %lx\n", Status);
        return Status;
    }

    ExtfsGlobalData = DeviceObject->DeviceExtension;
    RtlZeroMemory(ExtfsGlobalData, sizeof(*ExtfsGlobalData));

    ExtfsGlobalData->DeviceObject = DeviceObject;

    ExInitializeResourceLite(&ExtfsGlobalData->Resource);

    ExtfsGlobalData->DriverObject = DriverObject;

    DriverObject->MajorFunction[IRP_MJ_CREATE]                   = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]                    = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]                  = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_READ]                     = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_WRITE]                    = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION]        = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION]          = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION]   = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL]        = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL]      = ExtfsFsdDispatch;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]           = ExtfsFsdDispatch;

    IoRegisterFileSystem(DeviceObject);

    DPRINT1("Extfs driver initialized!\n");

    return STATUS_SUCCESS;
}
