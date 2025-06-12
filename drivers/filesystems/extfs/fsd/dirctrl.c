#include <extfs.h>

NTSTATUS
ExtfsFsdDispatchDirectoryControl(PDEVICE_OBJECT DeviceObject,
                                 PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_FILE_CONTEXT FileContext = FileObject->FsContext;
    PEXTFS_FILE_STREAM FileStream = FileObject->FsContext2;
    NTSTATUS Status;
    FILE_INFORMATION_CLASS FileInformationClass;
    PVOID UserBuffer = ExtfsFsdGetBuffer(Irp);
    BOOLEAN RestartScan;
    BOOLEAN ReturnSingleEntry;
    BOOLEAN IndexSpecified;
    PUNICODE_STRING FilterName;
    BOOLEAN IsUsingWildcards;
    //PFILE_NOTIFY_INFORMATION FileNotifyInformation;
    ULONG Length;

    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        FileContext = NULL;
        goto result;
    }

    if (!FileStream || !EXTFS_CHECK_FCB_HEADER(FileStream, *FileStream, EXTFS_FILE_STR_MAGIC))
    {
        DPRINT1("Invalid FileStream\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        FileContext = NULL;
        goto result;
    }

    if (!UserBuffer)
    {
        DPRINT1("Cannot get UserBuffer\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        FileContext = NULL;
        goto result;
    }

    DPRINT1("IrpSp->FileObject->FileName = \"%wZ\"\n", &IrpSp->FileObject->FileName);

    Status = STATUS_SUCCESS;

    Irp->IoStatus.Information = 0;

    switch (IrpSp->MinorFunction)
    {
    case IRP_MN_QUERY_DIRECTORY:
        FileInformationClass = IrpSp->Parameters.QueryDirectory.FileInformationClass;
        DPRINT1("FileInformationClass = %u\n", FileInformationClass);

        RestartScan = IrpSp->Flags & SL_RESTART_SCAN;
        ReturnSingleEntry = IrpSp->Flags & SL_RETURN_SINGLE_ENTRY;
        IndexSpecified = IrpSp->Flags & SL_INDEX_SPECIFIED;

        FilterName = IrpSp->Parameters.QueryDirectory.FileName;
        Length = IrpSp->Parameters.QueryDirectory.Length;

        if (RestartScan)
        {
            DPRINT1("Will restart scan\n");
            FileStream->CurrentDirectoryOffset = 0;
            FileStream->CurrentOutputDirectoryOffset = 0;
            FileStream->OldOutputDirectoryOffset = 0;
        }

        if (!FileStream->IsNotForcingFilterNameUpdate)
        {
            DPRINT1("Will update FilterName\n");
            FileStream->IsNotForcingFilterNameUpdate = TRUE;

            ExtfsFreeDuplicatedUnicodeString(&FileStream->FilterName);
            if (FilterName && FilterName->Length)
            {
                DPRINT1("\"%wZ\"\n", FilterName);

                FileStream->IsUsingWildcards = FsRtlDoesNameContainWildCards(FilterName);
                ExtfsDuplicateUnicodeString(&FileStream->FilterName, FilterName);
            }
        }

        if (ReturnSingleEntry)
        {
            DPRINT1("Will return single entry\n");
        }

        IsUsingWildcards = FileStream->IsUsingWildcards;
        FilterName = &FileStream->FilterName;
        if (!FilterName->Buffer)
            FilterName = NULL;

        if (IndexSpecified)
        {
            DPRINT1("Will find index\n");
            ASSERT(FALSE);
        }

        RtlZeroMemory(UserBuffer, Length);

        while (TRUE)
        {
            ULONG EntrySize = ExtfsQueryDirectoryEntry(FileStream, UserBuffer, Length,
                                                       FilterName, IsUsingWildcards,
                                                       &Status, FileInformationClass);
            Irp->IoStatus.Information += EntrySize;

            if (!EntrySize || ReturnSingleEntry)
            {
                BOOLEAN IsEndEntry = FileStream->CurrentDirectoryOffset >= ExtfsGetInodeContextFileSize(FileContext->InodeContext);
                if (IsEndEntry)
                    FileStream->IsNotForcingFilterNameUpdate = FALSE;

                FileStream->CurrentOutputDirectoryOffset = 0;
                FileStream->OldOutputDirectoryOffset = 0;
                FileStream->MatchedAnyEntry = FALSE;

                DPRINT1("Status = 0x%lx\n", Status);
                break;
            }
        }
        break;
    case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
        Length = IrpSp->Parameters.NotifyDirectory.Length;

        if (!FileContext->InodeContext->IsDirectory)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    default:
        Status = STATUS_NOT_IMPLEMENTED;
        break;
    }

result:
    if (Status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = Status;
        Irp->UserIosb->Status = Status;
        Irp->UserIosb->Information = Irp->IoStatus.Information;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    else
    {
        IoMarkIrpPending(Irp);
    }
    return Status;
}
