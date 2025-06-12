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
    FILE_INFORMATION_CLASS FileInformationClass = IrpSp->Parameters.QueryFile.FileInformationClass;

    if (!FileContext || FileContext->StandardFCB.Identifier.Magic != EXTFS_FILE_CTX_MAGIC)
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }

    if (!UserBuffer)
    {
        DPRINT1("Cannot get UserBuffer\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    ULONG NameLen = strlen(FileContext->FilePath), Index = 0;
    LONGLONG CreationTime = -1;
    LONGLONG LastAccessTime = UnixTimeToWindowsTime(FileContext->InodeContext->Inode.Atime);
    LONGLONG LastWriteTime = UnixTimeToWindowsTime(FileContext->InodeContext->Inode.Mtime);
    LONGLONG ChangeTime = UnixTimeToWindowsTime(FileContext->InodeContext->Inode.Ctime);

    DPRINT1("FileInformationClass = %u\n", FileInformationClass);

    Status = STATUS_SUCCESS;

    switch (FileInformationClass)
    {
    case FileBasicInformation:
        Irp->IoStatus.Information = sizeof(*OutputFileBasicInformation);
        RtlZeroMemory(OutputFileBasicInformation, Irp->IoStatus.Information);

        OutputFileBasicInformation->CreationTime.QuadPart = CreationTime;
        OutputFileBasicInformation->LastAccessTime.QuadPart = LastAccessTime;
        OutputFileBasicInformation->LastWriteTime.QuadPart = LastWriteTime;
        OutputFileBasicInformation->ChangeTime.QuadPart = ChangeTime;

        OutputFileBasicInformation->FileAttributes =
            FileContext->InodeContext->IsDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        break;
    case FileStandardInformation:
        Irp->IoStatus.Information = sizeof(*OutputFileStandardInformation);
        RtlZeroMemory(OutputFileStandardInformation, Irp->IoStatus.Information);

        OutputFileStandardInformation->Directory = FileContext->InodeContext->IsDirectory;
        OutputFileStandardInformation->AllocationSize.QuadPart = OutputFileStandardInformation->EndOfFile.QuadPart = 
            FileContext->InodeContext->FileSize;
        break;
    case FileNameInformation:
        Irp->IoStatus.Information =
            FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + (sizeof(WCHAR) * NameLen);
        RtlZeroMemory(OutputFileNameInformation, Irp->IoStatus.Information);

        OutputFileNameInformation->FileNameLength = NameLen * sizeof(WCHAR);
        while (Index < NameLen)
            OutputFileNameInformation->FileName[Index] = FileContext->FilePath[Index], Index++;

        DPRINT1("(\"%s\")\n", FileContext->FilePath);
        break;
    default:
        Status = STATUS_INVALID_PARAMETER;
        DPRINT1("Unimplemented operation\n");
        while (TRUE);
        break;
    }

result:
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
    PEXTFS_FILE_STREAM FileStream = FileObject->FsContext2;
    NTSTATUS Status;
    FILE_INFORMATION_CLASS FileInformationClass = IrpSp->Parameters.QueryDirectory.FileInformationClass;
    PVOID UserBuffer = ExtfsFsdGetBuffer(Irp);
    BOOLEAN RestartScan = IrpSp->Flags & SL_RESTART_SCAN;
    BOOLEAN ReturnSingleEntry = IrpSp->Flags & SL_RETURN_SINGLE_ENTRY;
    PUNICODE_STRING FilterName = IrpSp->Parameters.QueryDirectory.FileName;
    PCHAR FilterNameA = NULL;
    ULONG FilterNameLen = 0, Index = 0;

    if (!FileContext || FileContext->StandardFCB.Identifier.Magic != EXTFS_FILE_CTX_MAGIC)
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }

    if (!FileStream || FileStream->StandardFCB.Identifier.Magic != EXTFS_FILE_STR_MAGIC)
    {
        DPRINT1("Invalid FileStream\n");
        Status = STATUS_INVALID_PARAMETER;
        goto result;
    }

    if (!UserBuffer)
    {
        DPRINT1("Cannot get UserBuffer\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    if (FilterName)
    {
        FilterNameLen = FilterName->Length / sizeof(FilterName->Buffer[0]);
        FilterNameA = ExAllocatePoolWithTag(NonPagedPool, FilterNameLen + 1, EXTFS_TAG_BUFFER);
        if (!FilterNameA)
        {
            DPRINT1("Cannot allocate FilterNameA\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto result;
        }

        while (Index < FilterNameLen)
            FilterNameA[Index] = FilterName->Buffer[Index], Index++;
        FilterNameA[Index] = '\0';

        if (!strcmp(FilterNameA, FileContext->FilePath))
        {
            ExFreePoolWithTag(FilterNameA, EXTFS_TAG_BUFFER);
            FilterNameA = NULL;
        }
    }

    if (ReturnSingleEntry)
    {
        DPRINT1("Will return single entry\n");
    }

    if (RestartScan)
    {
        DPRINT1("Will restart scan\n");
        FileStream->CurrentDirectoryOffset = 0;
        FileStream->CurrentFBDOffset = 0;
    }

    DPRINT1("%wZ\n", FilterName);

    DPRINT1("FileInformationClass = %u\n", FileInformationClass);
    DPRINT1("IrpSp->FileObject->FileName = \"%wZ\"\n", &IrpSp->FileObject->FileName);

    Status = STATUS_SUCCESS;

    Irp->IoStatus.Information = 0;

    switch (IrpSp->MinorFunction)
    {
    case IRP_MN_QUERY_DIRECTORY:
        while (TRUE)
        {
            ULONG EntrySize = 0;
            switch (FileInformationClass)
            {
            case FileDirectoryInformation:
                EntrySize = ExtfsQueryDirectoryListFD(FileStream, UserBuffer, IrpSp->Parameters.QueryDirectory.Length, FilterNameA);
                break;
            case FileBothDirectoryInformation:
                EntrySize = ExtfsQueryDirectoryListFBD(FileStream, UserBuffer, IrpSp->Parameters.QueryDirectory.Length, FilterNameA);
                break;
            default:
                DPRINT1("Something is wrong\n");
                Status = STATUS_INVALID_INFO_CLASS;
                while (TRUE);
                break;
            }
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
    if (FilterNameA)
        ExFreePoolWithTag(FilterNameA, EXTFS_TAG_BUFFER);

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
