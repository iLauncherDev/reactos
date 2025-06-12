#include "extfs.h"

NTSTATUS
ExtfsFsdDispatchQueryInformation(PDEVICE_OBJECT DeviceObject,
                                 PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PVOID UserBuffer = ExtfsFsdGetBuffer(Irp);
    PFILE_BASIC_INFORMATION OutputFileBasicInformation = UserBuffer;
    PFILE_STANDARD_INFORMATION OutputFileStandardInformation = UserBuffer;
    PFILE_NAME_INFORMATION OutputFileNameInformation = UserBuffer;
    PFILE_INTERNAL_INFORMATION OutputFileInternalInformation = UserBuffer;
    PFILE_EA_INFORMATION OutputFileEaInformation = UserBuffer;
    //PFILE_ALL_INFORMATION OutputFileAllInformation = UserBuffer;
    PFILE_POSITION_INFORMATION OutputFilePositionInformation = UserBuffer;
    PFILE_ALL_INFORMATION OutputFileAllInformation = UserBuffer;
    PFILE_ALLOCATION_INFORMATION OutputFileAllocationInformation = UserBuffer;
    PFILE_END_OF_FILE_INFORMATION OutputFileEndOfFileInformation = UserBuffer;
    PFILE_NETWORK_OPEN_INFORMATION OutputFileNetworkOpenInformation = UserBuffer;
    PFILE_ATTRIBUTE_TAG_INFORMATION OutputFileAttributeTagInformation = UserBuffer;
    FILE_INFORMATION_CLASS FileInformationClass = IrpSp->Parameters.QueryFile.FileInformationClass;
    ULONG Length = IrpSp->Parameters.QueryFile.Length;

    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        FileContext = NULL;
        goto result;
    }

    if (!UserBuffer)
    {
        DPRINT1("Cannot get UserBuffer\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        FileContext = NULL;
        goto result;
    }

    ULONG NameLen = FileContext->FilePathUnicode.Length / sizeof(*FileContext->FilePathUnicode.Buffer), Index = 0;
    LONGLONG CreationTime;
    LONGLONG LastAccessTime;
    LONGLONG LastWriteTime;
    LONGLONG ChangeTime;

    ULONGLONG AllocationSize = ExtfsGetInodeContextAllocationSize(FileContext->InodeContext);
    ULONGLONG FileSize = ExtfsGetInodeContextFileSize(FileContext->InodeContext);

    ExtfsGetTime(FileContext->InodeContext,
                 &CreationTime,
                 &LastAccessTime,
                 &LastWriteTime,
                 &ChangeTime);

    DPRINT1("FileInformationClass = %u\n", FileInformationClass);

    Status = STATUS_SUCCESS;

    switch (FileInformationClass)
    {
    case FileBasicInformation:
        Irp->IoStatus.Information = sizeof(*OutputFileBasicInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFileBasicInformation, Irp->IoStatus.Information);

        OutputFileBasicInformation->CreationTime.QuadPart = CreationTime;
        OutputFileBasicInformation->LastAccessTime.QuadPart = LastAccessTime;
        OutputFileBasicInformation->LastWriteTime.QuadPart = LastWriteTime;
        OutputFileBasicInformation->ChangeTime.QuadPart = ChangeTime;
        OutputFileBasicInformation->FileAttributes = ExtfsGetFileTypeAttribute(FileContext->InodeContext);
        break;

    case FileStandardInformation:
        Irp->IoStatus.Information = sizeof(*OutputFileStandardInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFileStandardInformation, Irp->IoStatus.Information);

        OutputFileStandardInformation->AllocationSize.QuadPart = AllocationSize;
        OutputFileStandardInformation->EndOfFile.QuadPart = FileSize;
        OutputFileStandardInformation->Directory = FileContext->InodeContext->IsDirectory;
        break;

    case FileInternalInformation:
        Irp->IoStatus.Information = sizeof(*OutputFileInternalInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFileInternalInformation, Irp->IoStatus.Information);

        OutputFileInternalInformation->IndexNumber.QuadPart = FileContext->InodeContext->InodeNum;
        break;

    case FileEaInformation:
        Irp->IoStatus.Information = sizeof(*OutputFileEaInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFileEaInformation, Irp->IoStatus.Information);
        break;

    case FileNameInformation:
        Irp->IoStatus.Information =
            FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + (sizeof(WCHAR) * NameLen);
        if (sizeof(FILE_NAME_INFORMATION) > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFileNameInformation, sizeof(*OutputFileNameInformation));

        OutputFileNameInformation->FileNameLength = NameLen * sizeof(WCHAR);

        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer overflow\n");
            DPRINT1("Length = %lu\n", Length);
            DPRINT1("Irp->IoStatus.Information = %lu\n", Irp->IoStatus.Information);
            Irp->IoStatus.Information = Length;
            Status = STATUS_BUFFER_OVERFLOW;
        }

        while (FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + (Index * sizeof(WCHAR)) < Irp->IoStatus.Information)
            OutputFileNameInformation->FileName[Index] = FileContext->FilePathUnicode.Buffer[Index], Index++;

        DPRINT1("(\"%s\")\n", FileContext->FilePathUtf8.Buffer);
        break;

    case FilePositionInformation:
        Irp->IoStatus.Information = sizeof(*OutputFilePositionInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFilePositionInformation, Irp->IoStatus.Information);

        OutputFilePositionInformation->CurrentByteOffset = FileObject->CurrentByteOffset;
        break;

    case FileAllInformation:
        Irp->IoStatus.Information =
            FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation.FileName) + (sizeof(WCHAR) * NameLen);
        if (sizeof(FILE_NAME_INFORMATION) > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFileAllInformation, sizeof(*OutputFileAllInformation));

        OutputFileAllInformation->BasicInformation.CreationTime.QuadPart = CreationTime;
        OutputFileAllInformation->BasicInformation.LastAccessTime.QuadPart = LastAccessTime;
        OutputFileAllInformation->BasicInformation.LastWriteTime.QuadPart = LastWriteTime;
        OutputFileAllInformation->BasicInformation.ChangeTime.QuadPart = ChangeTime;
        OutputFileAllInformation->BasicInformation.FileAttributes = ExtfsGetFileTypeAttribute(FileContext->InodeContext);

        OutputFileAllInformation->StandardInformation.AllocationSize.QuadPart = AllocationSize;
        OutputFileAllInformation->StandardInformation.EndOfFile.QuadPart = FileSize;
        OutputFileAllInformation->StandardInformation.Directory = FileContext->InodeContext->IsDirectory;

        OutputFileAllInformation->InternalInformation.IndexNumber.QuadPart = FileContext->InodeContext->InodeNum;

        OutputFileAllInformation->PositionInformation.CurrentByteOffset = FileObject->CurrentByteOffset;

        OutputFileAllInformation->NameInformation.FileNameLength = NameLen * sizeof(WCHAR);

        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer overflow\n");
            DPRINT1("Length = %lu\n", Length);
            DPRINT1("Irp->IoStatus.Information = %lu\n", Irp->IoStatus.Information);
            Irp->IoStatus.Information = Length;
            Status = STATUS_BUFFER_OVERFLOW;
        }

        while (FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation.FileName) + (Index * sizeof(WCHAR)) < Irp->IoStatus.Information)
            OutputFileAllInformation->NameInformation.FileName[Index] = FileContext->FilePathUnicode.Buffer[Index], Index++;

        DPRINT1("(\"%s\")\n", FileContext->FilePathUtf8.Buffer);
        break;

    case FileAllocationInformation:
        Irp->IoStatus.Information = sizeof(*OutputFileAllocationInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFileAllocationInformation, Irp->IoStatus.Information);
        OutputFileAllocationInformation->AllocationSize.QuadPart = AllocationSize;
        break;

    case FileEndOfFileInformation:
        Irp->IoStatus.Information = sizeof(*OutputFileEndOfFileInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFileEndOfFileInformation, Irp->IoStatus.Information);
        OutputFileEndOfFileInformation->EndOfFile.QuadPart = FileSize;
        break;

    case FileStreamInformation:
        DPRINT1("FileStreamInformation is not implemented!\n");
        Status = STATUS_INVALID_INFO_CLASS;
        break;

    case FileNetworkOpenInformation:
        Irp->IoStatus.Information = sizeof(*OutputFileNetworkOpenInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFileNetworkOpenInformation, Irp->IoStatus.Information);

        OutputFileNetworkOpenInformation->CreationTime.QuadPart = CreationTime;
        OutputFileNetworkOpenInformation->LastAccessTime.QuadPart = LastAccessTime;
        OutputFileNetworkOpenInformation->LastWriteTime.QuadPart = LastWriteTime;
        OutputFileNetworkOpenInformation->ChangeTime.QuadPart = ChangeTime;

        OutputFileNetworkOpenInformation->AllocationSize.QuadPart = AllocationSize;
        OutputFileNetworkOpenInformation->EndOfFile.QuadPart = FileSize;

        OutputFileNetworkOpenInformation->FileAttributes = ExtfsGetFileTypeAttribute(FileContext->InodeContext);
        break;

    case FileAttributeTagInformation:
        Irp->IoStatus.Information = sizeof(*OutputFileAttributeTagInformation);
        if (Irp->IoStatus.Information > Length)
        {
            DPRINT1("Buffer too small\n");
            Irp->IoStatus.Information = 0;
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        RtlZeroMemory(OutputFileAttributeTagInformation, Irp->IoStatus.Information);

        OutputFileAttributeTagInformation->FileAttributes = ExtfsGetFileTypeAttribute(FileContext->InodeContext);
        OutputFileAttributeTagInformation->ReparseTag = ExtfsGetFileTypeReparseTag(FileContext->InodeContext);
        break;

    default:
        Status = STATUS_INVALID_DEVICE_REQUEST;
        DPRINT1("Unimplemented operation\n");
        ASSERT(FALSE);
        break;
    }

result:

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
ExtfsFsdDispatchSetInformation(PDEVICE_OBJECT DeviceObject,
                               PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_VOLUME_EXTENSION VolumeExtension;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext, RootFileContext = NULL;
    PVOID UserBuffer = ExtfsFsdGetBuffer(Irp);
    PFILE_BASIC_INFORMATION InputFileBasicInformation = UserBuffer;
    PFILE_RENAME_INFORMATION InputFileRenameInformation = UserBuffer;
    PFILE_DISPOSITION_INFORMATION InputFileDispositionInformation = UserBuffer;
    PFILE_POSITION_INFORMATION InputFilePositionInformation = UserBuffer;
    PFILE_ALLOCATION_INFORMATION InputFileAllocationInformation = UserBuffer;
    PFILE_END_OF_FILE_INFORMATION InputFileEndOfFileInformation = UserBuffer;
    FILE_INFORMATION_CLASS FileInformationClass = IrpSp->Parameters.SetFile.FileInformationClass;
    ULONG Length = IrpSp->Parameters.SetFile.Length;
    PFILE_OBJECT RootFileObject = NULL;
    ULONG PathLength = 0, PathCurrentIndex = 0, PathIndex = 0;
    PWCHAR PathString = NULL;
    UNICODE_STRING PathStringUnicode;
    PCHAR PathStringUtf8 = NULL;
    LONGLONG CreationTime;
    LONGLONG LastAccessTime;
    LONGLONG LastWriteTime;
    LONGLONG ChangeTime;

    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        FileContext = NULL;
        goto result;
    }

    VolumeExtension = FileContext->InodeContext->VolumeExtension;

    if (!UserBuffer)
    {
        DPRINT1("Cannot get UserBuffer\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        FileContext = NULL;
        goto result;
    }

    DPRINT1("FileInformationClass = %u\n", FileInformationClass);

    Status = STATUS_SUCCESS;

    switch (FileInformationClass)
    {
    case FileBasicInformation:
        UNREFERENCED_PARAMETER(InputFileBasicInformation);

        CreationTime = InputFileBasicInformation->ChangeTime.QuadPart;
        LastAccessTime = InputFileBasicInformation->LastAccessTime.QuadPart;
        LastWriteTime = InputFileBasicInformation->LastWriteTime.QuadPart;
        ChangeTime = InputFileBasicInformation->ChangeTime.QuadPart;

        if (!(InputFileBasicInformation->FileAttributes & ExtfsGetFileTypeAttribute(FileContext->InodeContext)))
        {
            DPRINT1("Invalid FileAttributes\n");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        ExtfsChangeTime(FileContext->InodeContext,
                        CreationTime ? CreationTime : FileContext->InodeContext->CreationTime,
                        LastAccessTime ? LastAccessTime : FileContext->InodeContext->LastAccessTime,
                        LastWriteTime ? LastWriteTime : FileContext->InodeContext->LastWriteTime,
                        ChangeTime ? ChangeTime : FileContext->InodeContext->ChangeTime);
        break;

    case FileRenameInformation:
        PathLength = 0;
        if (InputFileRenameInformation->RootDirectory)
        {
            Status = ObReferenceObjectByHandle(
                InputFileRenameInformation->RootDirectory,
                FILE_TRAVERSE,
                *IoFileObjectType,
                Irp->RequestorMode,
                (PVOID)&RootFileObject,
                NULL
            );
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Cannot get FileObject of HANDLE\n");
                break;
            }

            if (RootFileObject->Vpb->DeviceObject != VolumeExtension->DeviceObject)
            {
                DPRINT1("It's from a different device!\n");
                Status = STATUS_NOT_SAME_DEVICE;
                break;
            }

            RootFileContext = RootFileObject->FsContext;
            if (!RootFileContext || !EXTFS_CHECK_FCB_HEADER(RootFileContext, *RootFileContext, EXTFS_FILE_CTX_MAGIC))
            {
                DPRINT1("Invalid RootFileContext\n");
                Status = STATUS_INVALID_DEVICE_REQUEST;
                goto result;
            }

            PathLength += (RootFileContext->FilePathUnicode.Length / sizeof(WCHAR)) + 1;
            DPRINT1("The path was relative\n");
        }
        PathLength += (InputFileRenameInformation->FileNameLength / sizeof(WCHAR)) + 1;

        PathString = ExAllocatePoolWithTag(NonPagedPool, PathLength * sizeof(WCHAR), EXTFS_TAG_BUFFER);
        if (!PathString)
        {
            DPRINT1("Cannot allocate PathString");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        PathCurrentIndex = 0;
        if (RootFileContext)
        {
            PathIndex = 0;
            while (PathIndex < RootFileContext->FilePathUnicode.Length / sizeof(WCHAR))
            {
                PathString[PathCurrentIndex] = RootFileContext->FilePathUnicode.Buffer[PathIndex];
                PathIndex++, PathCurrentIndex++;
            }

            if (PathString[PathCurrentIndex - 1] != '\\')
                PathString[PathCurrentIndex++] = '\\';
        }

        PathIndex = 0;
        while (PathIndex < InputFileRenameInformation->FileNameLength / sizeof(WCHAR))
        {
            PathString[PathCurrentIndex] = InputFileRenameInformation->FileName[PathIndex];
            PathIndex++, PathCurrentIndex++;
        }
        PathString[PathCurrentIndex] = '\0';

        if (!RootFileContext)
        {
            PWCHAR PathToCut = L"\\??\\X:";
            ULONG PathToCutLength = wcslen(PathToCut);

            wcscpy(PathString, &PathString[PathToCutLength]);
        }

        RtlInitUnicodeString(&PathStringUnicode, PathString);
        PathStringUtf8 = ExtfsConvertUnicodeToUtf8(&PathStringUnicode);
        if (!PathStringUtf8)
        {
            DPRINT1("Cannot convert Unicode to Utf8 string\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        DPRINT1("Full Path \"%s\"\n", PathStringUtf8);

        ExtfsRenameFileByPath(VolumeExtension,
                              FileContext->FilePathUtf8.Buffer, PathStringUtf8,
                              InputFileRenameInformation->ReplaceIfExists,
                              TRUE, &Status, Irp);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Cannot rename file path\n");
            break;
        }

        ExAcquireResourceExclusiveLite(&VolumeExtension->FileOperationLock, TRUE);

        if (FileContext->FilePathUtf8.Buffer)
            ExFreePool(FileContext->FilePathUtf8.Buffer);
        if (FileContext->FilePathUnicode.Buffer)
            ExFreePool(FileContext->FilePathUnicode.Buffer);

        FileContext->FilePathUtf8.Buffer = PathStringUtf8;
        FileContext->FilePathUtf8.Length = strlen(PathStringUtf8);
        FileContext->FilePathUtf8.MaximumLength = FileContext->FilePathUtf8.Length + 1;

        FileContext->FilePathUnicode.Buffer = PathString;
        FileContext->FilePathUnicode.Length = wcslen(PathString) * sizeof(WCHAR);
        FileContext->FilePathUnicode.MaximumLength = FileContext->FilePathUnicode.Length + sizeof(WCHAR);

        ExReleaseResourceLite(&VolumeExtension->FileOperationLock);
        break;

    case FileDispositionInformation:
        if (Length < sizeof(*InputFileDispositionInformation))
        {
            DPRINT1("Buffer too small\n");
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        FileContext->DeletePending = InputFileDispositionInformation->DeleteFile;
        break;

    case FilePositionInformation:
        if (Length < sizeof(*InputFilePositionInformation))
        {
            DPRINT1("Buffer too small\n");
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        FileObject->CurrentByteOffset.QuadPart = InputFilePositionInformation->CurrentByteOffset.QuadPart;
        break;

    case FileEndOfFileInformation:
        if (Length < sizeof(*InputFileEndOfFileInformation))
        {
            DPRINT1("Buffer too small\n");
            Status = STATUS_BUFFER_TOO_SMALL;
            goto result;
        }

        ExtfsChangeInodeSize(FileContext->InodeContext, InputFileEndOfFileInformation->EndOfFile.QuadPart, FALSE);
        break;
    case FileAllocationInformation:
        UNREFERENCED_PARAMETER(InputFileAllocationInformation);
        break;
    default:
        Status = STATUS_INVALID_DEVICE_REQUEST;
        DPRINT1("Unimplemented operation\n");
        break;
    }

    ExtfsUpdateFileContextSize(FileContext, FileObject);
result:

    if (!NT_SUCCESS(Status))
    {
        if (PathString)
            ExFreePoolWithTag(PathString, EXTFS_TAG_BUFFER);
        if (PathStringUtf8)
            ExtfsFreeUtf8String(PathStringUtf8);
    }
    if (RootFileObject)
        ObDereferenceObject(RootFileObject);

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
