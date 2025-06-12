#include <extfs.h>

NTSTATUS
ExtfsFsdDispatchSetVolumeInformation(PDEVICE_OBJECT DeviceObject,
                                     PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PEXTFS_VOLUME_EXTENSION VolumeExtension = DeviceObject->DeviceExtension;
    PVOID UserBuffer = ExtfsFsdGetBuffer(Irp);
    ULONG Length = IrpSp->Parameters.SetVolume.Length;
    FS_INFORMATION_CLASS FsInformationClass = IrpSp->Parameters.SetVolume.FsInformationClass;
    PFILE_FS_LABEL_INFORMATION OutputFileFsLabelInformation = UserBuffer;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG FsConvertedNameLen = 0, Index = 0;
    ULONG VolumeLabelLen = sizeof(VolumeExtension->VolumeLabel);
    PCHAR Utf8String;
    UNICODE_STRING UnicodeString;

    if (!IsExtfsVolumeExtension(VolumeExtension))
    {
        DPRINT1("Invalid VolumeExtension\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        VolumeExtension = NULL;
        goto result;
    }

    DPRINT1("FsInformationClass = %u\n", FsInformationClass);

    ExtfsAcquireSuperBlockWriteLock(VolumeExtension, TRUE);

    switch (FsInformationClass)
    {
    case FileFsLabelInformation:
        if (Length < sizeof(*OutputFileFsLabelInformation))
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }

        UnicodeString.Buffer = OutputFileFsLabelInformation->VolumeLabel;
        UnicodeString.MaximumLength = UnicodeString.Length = OutputFileFsLabelInformation->VolumeLabelLength;
        Utf8String = ExtfsConvertUnicodeToUtf8(&UnicodeString);
        if (!Utf8String)
        {
            DPRINT1("Cannot convert unicode to utf-8 string\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        FsConvertedNameLen = strlen(Utf8String);

        if (FsConvertedNameLen < VolumeLabelLen)
        {
            VolumeLabelLen = FsConvertedNameLen;
            VolumeExtension->VolumeLabel[VolumeLabelLen] = '\0';
        }
        else if (FsConvertedNameLen > VolumeLabelLen)
        {
            ExtfsFreeUtf8String(Utf8String);
            Status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        while (Index < VolumeLabelLen)
        {
            VolumeExtension->VolumeLabel[Index] = Utf8String[Index];
            Index++;
        }

        ExtfsFreeUtf8String(Utf8String);

        RtlCopyMemory(VolumeExtension->SuperBlock.VolumeName, VolumeExtension->VolumeLabel, sizeof(VolumeExtension->VolumeLabel));
        break;
    default:
        DPRINT1("Unimplemented SetVolume (%u)\n", FsInformationClass);
        Status = STATUS_NOT_IMPLEMENTED;
        break;
    }

    goto result;
result:
    if (VolumeExtension)
        ExtfsReleaseSuperBlockLock(VolumeExtension);
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
    PFILE_FS_DEVICE_INFORMATION OutputFileFsDeviceInformation = UserBuffer;
    PFILE_FS_ATTRIBUTE_INFORMATION OutputFileFsAttributeInformation = UserBuffer;
    PFILE_FS_FULL_SIZE_INFORMATION OutputFileFsFullSizeInformation = UserBuffer;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG FsNameLen = 0, Index = 0;
    ULONG VolumeLabelLen = 0;
    PUNICODE_STRING VolumeLabelUnicode;

    if (!IsExtfsVolumeExtension(VolumeExtension))
    {
        DPRINT1("Invalid VolumeExtension\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        VolumeExtension = NULL;
        goto result;
    }

    DPRINT1("FsInformationClass = %u\n", FsInformationClass);

    ExtfsAcquireSuperBlockReadLock(VolumeExtension, TRUE);

    FsNameLen = strnlen(VolumeExtension->FileSystemName, sizeof(VolumeExtension->FileSystemName));
    while (VolumeLabelLen < sizeof(VolumeExtension->VolumeLabel) && VolumeExtension->VolumeLabel[VolumeLabelLen])
        VolumeLabelLen++;

    switch (FsInformationClass)
    {
    case FileFsVolumeInformation:
        VolumeLabelUnicode = ExtfsConvertUtf8ToUnicode(VolumeExtension->VolumeLabel, VolumeLabelLen);
        if (!VolumeLabelUnicode)
        {
            Irp->IoStatus.Information = sizeof(*OutputFileFsVolumeInformation);
            if (Irp->IoStatus.Information > Length)
            {
                DPRINT1("Buffer too small\n");
                Irp->IoStatus.Information = 0;
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            RtlZeroMemory(OutputFileFsVolumeInformation, Irp->IoStatus.Information);
            break;
        }

        Irp->IoStatus.Information =
            FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        Irp->IoStatus.Information += VolumeLabelUnicode->Length;

        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer overflow\n");
            Irp->IoStatus.Information = Length;
            Status = STATUS_BUFFER_OVERFLOW;
        }
        RtlZeroMemory(OutputFileFsVolumeInformation, Irp->IoStatus.Information);

        OutputFileFsVolumeInformation->VolumeLabelLength = VolumeLabelUnicode->Length;
        while (FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel) + (Index * sizeof(WCHAR)) < Irp->IoStatus.Information)
            OutputFileFsVolumeInformation->VolumeLabel[Index] = VolumeLabelUnicode->Buffer[Index], Index++;

        ExtfsFreeUnicodeString(VolumeLabelUnicode);
        break;

    case FileFsLabelInformation:
        VolumeLabelUnicode = ExtfsConvertUtf8ToUnicode(VolumeExtension->VolumeLabel, VolumeLabelLen);
        if (!VolumeLabelUnicode)
        {
            Irp->IoStatus.Information = sizeof(*OutputFileFsLabelInformation);
            if (Irp->IoStatus.Information > Length)
            {
                DPRINT1("Buffer too small\n");
                Irp->IoStatus.Information = 0;
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlZeroMemory(OutputFileFsLabelInformation, Irp->IoStatus.Information);
            break;
        }

        Irp->IoStatus.Information =
            FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        Irp->IoStatus.Information += VolumeLabelUnicode->Length;

        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer overflow\n");
            Irp->IoStatus.Information = Length;
            Status = STATUS_BUFFER_OVERFLOW;
        }
        RtlZeroMemory(OutputFileFsLabelInformation, Irp->IoStatus.Information);

        OutputFileFsLabelInformation->VolumeLabelLength = VolumeLabelUnicode->Length;
        while (FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel) + (Index * sizeof(WCHAR)) < Irp->IoStatus.Information)
            OutputFileFsLabelInformation->VolumeLabel[Index] = VolumeLabelUnicode->Buffer[Index], Index++;

        ExtfsFreeUnicodeString(VolumeLabelUnicode);
        break;

    case FileFsSizeInformation:
        Irp->IoStatus.Information =
            sizeof(*OutputFileFsSizeInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlZeroMemory(OutputFileFsSizeInformation, Irp->IoStatus.Information);

        OutputFileFsSizeInformation->SectorsPerAllocationUnit = 1;
        OutputFileFsSizeInformation->BytesPerSector = VolumeExtension->BlockSize;
        OutputFileFsSizeInformation->TotalAllocationUnits.QuadPart = VolumeExtension->TotalBlocks;
        OutputFileFsSizeInformation->AvailableAllocationUnits.QuadPart =
            VolumeExtension->TotalBlocks - VolumeExtension->UsedBlocks;
        break;

    case FileFsDeviceInformation:
        Irp->IoStatus.Information =
            sizeof(*OutputFileFsDeviceInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlZeroMemory(OutputFileFsDeviceInformation, Irp->IoStatus.Information);

        OutputFileFsDeviceInformation->DeviceType = FILE_DEVICE_DISK;
        break;

    case FileFsAttributeInformation:
        Irp->IoStatus.Information =
            FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        Irp->IoStatus.Information += FsNameLen * sizeof(WCHAR);

        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer overflow\n");
            Irp->IoStatus.Information = Length;
            Status = STATUS_BUFFER_OVERFLOW;
        }

        RtlZeroMemory(OutputFileFsAttributeInformation, Irp->IoStatus.Information);

        OutputFileFsAttributeInformation->FileSystemAttributes = FILE_CASE_PRESERVED_NAMES | FILE_UNICODE_ON_DISK;
        OutputFileFsAttributeInformation->MaximumComponentNameLength = EXT_DIR_ENTRY_MAX_NAME_LENGTH;
        OutputFileFsAttributeInformation->FileSystemNameLength = FsNameLen * sizeof(WCHAR);

        while (FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName) + (Index * sizeof(WCHAR)) < Irp->IoStatus.Information)
            OutputFileFsAttributeInformation->FileSystemName[Index] = VolumeExtension->FileSystemName[Index], Index++;
        break;

    case FileFsFullSizeInformation:
        Irp->IoStatus.Information =
            sizeof(*OutputFileFsFullSizeInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlZeroMemory(OutputFileFsFullSizeInformation, Irp->IoStatus.Information);

        OutputFileFsFullSizeInformation->SectorsPerAllocationUnit = 1;
        OutputFileFsFullSizeInformation->BytesPerSector = VolumeExtension->BlockSize;
        OutputFileFsFullSizeInformation->TotalAllocationUnits.QuadPart = VolumeExtension->TotalBlocks;
        OutputFileFsFullSizeInformation->ActualAvailableAllocationUnits.QuadPart =
            VolumeExtension->TotalBlocks - VolumeExtension->UsedBlocks;
        OutputFileFsFullSizeInformation->CallerAvailableAllocationUnits = OutputFileFsFullSizeInformation->ActualAvailableAllocationUnits;
        break;

    default:
        DPRINT1("Unimplemented QueryVolume (%u)\n", FsInformationClass);
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

result:
    if (VolumeExtension)
        ExtfsReleaseSuperBlockLock(VolumeExtension);
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
