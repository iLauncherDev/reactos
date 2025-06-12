#include "extfs.h"

const char* IrpMajorNames[] = {
    "CREATE", "CREATE_NAMED_PIPE", "CLOSE", "READ", "WRITE", "QUERY_INFORMATION", "SET_INFORMATION",
    "QUERY_EA", "SET_EA", "FLUSH_BUFFERS", "QUERY_VOLUME_INFORMATION",
    "SET_VOLUME_INFORMATION", "DIRECTORY_CONTROL", "FILE_SYSTEM_CONTROL",
    "DEVICE_CONTROL", "INTERNAL_DEVICE_CONTROL", "SHUTDOWN", "LOCK_CONTROL",
    "CLEANUP", "CREATE_MAILSLOT", "QUERY_SECURITY", "SET_SECURITY", "POWER",
    "SYSTEM_CONTROL", "DEVICE_CHANGE", "QUERY_QUOTA", "SET_QUOTA", "PNP"
};

NTSTATUS
ExtfsFsdDispatchCreate(PDEVICE_OBJECT DeviceObject,
                       PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PEXTFS_INODE_CONTEXT InodeContext;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = ExAllocatePoolWithTag(NonPagedPool,
                                                            sizeof(*FileContext),
                                                            EXTFS_TAG_FILE_CONTEXT);
    ULONG Index = 0;
    ULONG FileLength = FileObject->FileName.Length / sizeof(*FileObject->FileName.Buffer);
    PCHAR FilePath = ExAllocatePoolWithTag(NonPagedPool,
                                           FileLength + 1,
                                           EXTFS_TAG_BUFFER);
    if (!FilePath)
    {
        DPRINT1("Failed to allocate ASCII string\n");
        Status = STATUS_NO_MEMORY;
        goto result;
    }

    if (!FileContext)
    {
        DPRINT1("Failed to allocate File Context\n");
        ExFreePoolWithTag(FilePath, EXTFS_TAG_BUFFER);
        Status = STATUS_NO_MEMORY;
        goto result;
    }

    while (Index < FileLength)
    {
        CHAR Character = (CHAR)FileObject->FileName.Buffer[Index];
        FilePath[Index++] = Character;
    }
    FilePath[Index] = '\0';

    InodeContext = ExtfsFindFileByPath(VolumeExtension, FilePath);
    if (!InodeContext)
    {
        DPRINT1("Cannot get \"%s\" InodeContext\n", FilePath);
        ExFreePoolWithTag(FilePath, EXTFS_TAG_BUFFER);
        ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
        Status = STATUS_FILE_NOT_AVAILABLE;
        goto result;
    }

    RtlZeroMemory(FileContext, sizeof(*FileContext));
    FileContext->Magic = EXTFS_FILE_CTX_MAGIC;
    FileContext->InodeContext = InodeContext;
    
    FileObject->FsContext = FileContext;

    Status = STATUS_SUCCESS;
    ExFreePoolWithTag(FilePath, EXTFS_TAG_BUFFER);

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
ExtfsFsdDispatchClose(PDEVICE_OBJECT DeviceObject,
                      PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;

    if (!FileContext || FileContext->Magic != EXTFS_FILE_CTX_MAGIC)
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }

    ExtfsReleaseInodeContext(FileContext->InodeContext);
    FileContext->InodeContext = NULL;
    ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);

    FileObject->FsContext = NULL;
    Status = STATUS_SUCCESS;
result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
ExtfsFsdDispatchRead(PDEVICE_OBJECT DeviceObject,
                     PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PVOID UserBuffer = Irp->AssociatedIrp.SystemBuffer;
    LARGE_INTEGER ByteOffset = IrpSp->Parameters.Read.ByteOffset;
    ULONG Length = IrpSp->Parameters.Read.Length;
    ULONG BytesRead = 0;

    if (!FileContext || FileContext->Magic != EXTFS_FILE_CTX_MAGIC)
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }

    if (ByteOffset.QuadPart == FILE_USE_FILE_POINTER_POSITION)
    {
        ByteOffset.QuadPart = FileContext->CurrentOffset;
    }

    if (ByteOffset.QuadPart >= FileContext->InodeContext->FileSize)
    {
        DPRINT1("Reached end of file\n");
        Status = STATUS_END_OF_FILE;
        goto result;
    }

    DPRINT1("ExtfsFsdDispatchRead\n");

    BytesRead = ExtfsReadInodeData(FileContext->InodeContext,
                                   UserBuffer, ByteOffset, Length);

    FileContext->CurrentOffset = ByteOffset.QuadPart + BytesRead;

    Status = STATUS_SUCCESS;

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
ExtfsFsdDispatchQueryInformation(PDEVICE_OBJECT DeviceObject,
                                 PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PVOID UserBuffer = Irp->AssociatedIrp.SystemBuffer;
    PFILE_BASIC_INFORMATION OutputFileBasicInformation = UserBuffer;
    PFILE_STANDARD_INFORMATION OutputFileStandardInformation = UserBuffer;
    FILE_INFORMATION_CLASS FileInformationClass = IrpSp->Parameters.QueryDirectory.FileInformationClass;

    if (!FileContext || FileContext->Magic != EXTFS_FILE_CTX_MAGIC)
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }

    DPRINT1("FileInformationClass = %u\n", FileInformationClass);

    Status = STATUS_SUCCESS;

    switch (FileInformationClass)
    {
    case FileBasicInformation:
        RtlZeroMemory(OutputFileBasicInformation, sizeof(*OutputFileBasicInformation));

        OutputFileBasicInformation->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        break;
    case FileStandardInformation:
        RtlZeroMemory(OutputFileStandardInformation, sizeof(*OutputFileStandardInformation));
        OutputFileStandardInformation->Directory = FileContext->InodeContext->IsDirectory;
        OutputFileStandardInformation->AllocationSize.QuadPart = OutputFileStandardInformation->EndOfFile.QuadPart = 
            FileContext->InodeContext->FileSize;
        break;
    default:
        Status = STATUS_INVALID_PARAMETER;
        break;
    }

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

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

    switch (IrpSp->MinorFunction)
    {
    case IRP_MN_VERIFY_VOLUME:
        VolumeDevice = IrpSp->Parameters.VerifyVolume.DeviceObject;

        SuperBlock = ExAllocatePool(NonPagedPool, sizeof(*SuperBlock));
        if (!SuperBlock)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        VolumeOffset.QuadPart = 1024;
        Status = DiskRead(VolumeDevice, SuperBlock, VolumeOffset, sizeof(EXT_SUPER_BLOCK));
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_WRONG_VOLUME;
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

        SuperBlock = ExAllocatePool(NonPagedPool, sizeof(*SuperBlock));
        if (!SuperBlock)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        VolumeOffset.QuadPart = 1024;
        Status = DiskRead(VolumeDevice, SuperBlock, VolumeOffset, sizeof(EXT_SUPER_BLOCK));
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_WRONG_VOLUME;
            break;
        }

        Status = ExtfsCheckSuperBlock(SuperBlock);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_WRONG_VOLUME;
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

        VolumeExtension->VolumeDevice = VolumeDevice;
        VolumeExtension->DeviceObject = VolumeDeviceObject;
        VolumeExtension->RealDevice = VolumeDevice->Vpb->RealDevice;
        VolumeExtension->Vpb = VolumeDevice->Vpb;
        VolumeExtension->SuperBlock = *SuperBlock;
        ExtfsInitializeVolume(VolumeExtension);

        IoAttachDeviceToDeviceStack(VolumeDeviceObject, VolumeExtension->RealDevice);

        VolumeExtension->Vpb->DeviceObject = VolumeDeviceObject;
        VolumeExtension->Vpb->Flags |= VPB_MOUNTED;

        VolumeDeviceObject->Flags |= DO_DIRECT_IO;
        VolumeDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

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
ExtfsFsdDispatchDirectoryControl(PDEVICE_OBJECT DeviceObject,
                                 PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    NTSTATUS Status;
    FILE_INFORMATION_CLASS FileInformationClass = IrpSp->Parameters.QueryDirectory.FileInformationClass;
    PVOID UserBuffer = Irp->AssociatedIrp.SystemBuffer;
    BOOLEAN RestartScan = IrpSp->Flags & SL_RESTART_SCAN;
    BOOLEAN ReturnSingleEntry = IrpSp->Flags & SL_RETURN_SINGLE_ENTRY;

    if (!FileContext || FileContext->Magic != EXTFS_FILE_CTX_MAGIC)
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }

    if (RestartScan)
    {
        FileContext->CurrentDirectoryOffset = 0;
        FileContext->CurrentFBDOffset = 0;
    }

    Status = STATUS_SUCCESS;

    Irp->IoStatus.Information = 0;

    switch (IrpSp->MinorFunction)
    {
    case IRP_MN_QUERY_DIRECTORY:
        if (FileInformationClass != FileBothDirectoryInformation)
        {
            DPRINT1("Something is wrong\n");
            Status = STATUS_INVALID_INFO_CLASS;
            goto result;
        }
        while (TRUE)
        {
            ULONG EntrySize = ExtfsQueryDirectoryList(FileContext, UserBuffer, IrpSp->Parameters.QueryDirectory.Length);
            Irp->IoStatus.Information += EntrySize;
            if (!EntrySize)
            {
                Status = (Irp->IoStatus.Information == 0) ? STATUS_NO_MORE_FILES : STATUS_SUCCESS;
                break;
            }
            if (ReturnSingleEntry)
                break;
        }
        break;
    default:
        Status = STATUS_NOT_IMPLEMENTED;
        break;
    }

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
ExtfsFsdDispatch(PDEVICE_OBJECT DeviceObject,
                 PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);

    DPRINT1("IRP_MJ_%s requested started\n", IrpMajorNames[IrpSp->MajorFunction]);

    switch (IrpSp->MajorFunction)
    {
    case IRP_MJ_CREATE:
        return ExtfsFsdDispatchCreate(DeviceObject, Irp);
    case IRP_MJ_CLOSE:
        return ExtfsFsdDispatchClose(DeviceObject, Irp);
    case IRP_MJ_READ:
        return ExtfsFsdDispatchRead(DeviceObject, Irp);
    case IRP_MJ_QUERY_INFORMATION:
        return ExtfsFsdDispatchQueryInformation(DeviceObject, Irp);
    case IRP_MJ_FILE_SYSTEM_CONTROL:
        return ExtfsFsdDispatchFsControl(DeviceObject, Irp);
    //case IRP_MJ_DIRECTORY_CONTROL:
    //    return ExtfsFsdDispatchDirectoryControl(DeviceObject, Irp);
    }

    DPRINT1("IRP_MJ_%s requested ended\n", IrpMajorNames[IrpSp->MajorFunction]);
    return STATUS_NOT_IMPLEMENTED;
}
