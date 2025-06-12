#include <extfs.h>

BOOLEAN
ExtfsFsdCompareFilePath(PCHAR Path1, PCHAR Path2)
{
    ULONG Index = 0;
    ULONG PathLen1 = strlen(Path1),
        PathLen2 = strlen(Path2);
    if (*Path1 == '\\')
        Path1++, PathLen1--;
    if (*Path2 == '\\')
        Path2++, PathLen2--;

    if (PathLen1 != PathLen2)
        return TRUE;

    while (Index < PathLen1)
    {
        if (tolower(Path1[Index]) != tolower(Path2[Index]))
            return TRUE;
        Index++;
    }

    return FALSE;
}

PEXTFS_FILE_CONTEXT ExtfsFindExistingFileContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, PCHAR FilePath)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;
    PEXTFS_FILE_CONTEXT OutputFileContext = NULL;

    ExAcquireResourceExclusiveLite(&VolumeExtension->OpenFileListLock, TRUE);

    EndListEntry = &VolumeExtension->OpenFileList;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_FILE_CONTEXT CurrentFileContext = ExtfsGetListEntryStructure(CurrentListEntry);

        if (!ExtfsFsdCompareFilePath(CurrentFileContext->FilePathUtf8.Buffer, FilePath))
        {
            OutputFileContext = CurrentFileContext;
            break;
        }

        CurrentListEntry = CurrentListEntry->Next;
    }

    ExReleaseResourceLite(&VolumeExtension->OpenFileListLock);
    return OutputFileContext;
}

VOID ExtfsAddFileContextToList(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_FILE_CONTEXT FileContext)
{
    ExAcquireResourceExclusiveLite(&VolumeExtension->OpenFileListLock, TRUE);

    ExtfsInsertTailList(&VolumeExtension->OpenFileList, &FileContext->ListEntry);

    VolumeExtension->OpenFileCount++;
    ExReleaseResourceLite(&VolumeExtension->OpenFileListLock);
}

VOID ExtfsRemoveFileContextFromList(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_FILE_CONTEXT FileContext)
{
    ExAcquireResourceExclusiveLite(&VolumeExtension->OpenFileListLock, TRUE);

    ExtfsRemoveEntryList(&FileContext->ListEntry);

    VolumeExtension->OpenFileCount--;
    ExReleaseResourceLite(&VolumeExtension->OpenFileListLock);
}

VOID ExtfsAddFileStreamToList(PEXTFS_FILE_CONTEXT FileContext, PEXTFS_FILE_STREAM FileStream)
{
    ExAcquireResourceExclusiveLite(&FileContext->StreamListLock, TRUE);

    ExtfsInsertTailList(&FileContext->StreamList, &FileStream->ListEntry);

    ExReleaseResourceLite(&FileContext->StreamListLock);
}

VOID ExtfsRemoveFileStreamFromListNoLock(PEXTFS_FILE_CONTEXT FileContext, PEXTFS_FILE_STREAM FileStream)
{
    ExtfsRemoveEntryList(&FileStream->ListEntry);

    ExtfsFreeDuplicatedUnicodeString(&FileStream->FilterName);
    ExFreePoolWithTag(FileStream, EXTFS_TAG_FILE_STREAM);
}

VOID ExtfsRemoveFileStreamFromList(PEXTFS_FILE_CONTEXT FileContext, PEXTFS_FILE_STREAM FileStream)
{
    ExAcquireResourceExclusiveLite(&FileContext->StreamListLock, TRUE);

    ExtfsRemoveFileStreamFromListNoLock(FileContext, FileStream);

    ExReleaseResourceLite(&FileContext->StreamListLock);
}

VOID ExtfsRemoveAllClosePendingFileStreamFromList(PEXTFS_FILE_CONTEXT FileContext)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;

    ExAcquireResourceExclusiveLite(&FileContext->StreamListLock, TRUE);

    EndListEntry = &FileContext->StreamList;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_FILE_STREAM CurrentFileStream = ExtfsGetListEntryStructure(CurrentListEntry);
        PEXTFS_LIST_ENTRY NextEntry = CurrentListEntry->Next;

        if (CurrentFileStream->ClosePending)
        {
            if (FileContext->ReferenceCount > 0)
                FileContext->ReferenceCount--;
            else
                ASSERT(FALSE);

            ExtfsRemoveFileStreamFromListNoLock(FileContext, CurrentFileStream);
        }

        CurrentListEntry = NextEntry;
    }

    ExReleaseResourceLite(&FileContext->StreamListLock);
}

VOID ExtfsUpdateFileContextSize(PEXTFS_FILE_CONTEXT FileContext, PFILE_OBJECT FileObject)
{
    PEXTFS_STANDARD_FCB StandardFCB = &FileContext->StandardFCB;
    PEXTFS_INODE_CONTEXT InodeContext = NULL;
    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        return;
    }

    InodeContext = FileContext->InodeContext;

    StandardFCB->FileSizes.FileSize.QuadPart = InodeContext->FileSize;
    StandardFCB->FileSizes.AllocationSize.QuadPart =
        (InodeContext->FileSize + (InodeContext->VolumeExtension->BlockSize - 1)) & ~(InodeContext->VolumeExtension->BlockSize - 1);
    StandardFCB->FileSizes.ValidDataLength = StandardFCB->FileSizes.FileSize;

    StandardFCB->StandardHeader.AllocationSize = StandardFCB->FileSizes.AllocationSize;
    StandardFCB->StandardHeader.ValidDataLength = StandardFCB->FileSizes.ValidDataLength;
    StandardFCB->StandardHeader.FileSize = StandardFCB->FileSizes.FileSize;

    if (FileObject)
    {
        CcSetFileSizes(FileObject, &StandardFCB->FileSizes);
    }
}

PEXTFS_FILE_CONTEXT ExtfsCreateOrFindFileContext(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PCHAR FilePathA, PWCHAR FilePathW,
    PIO_STACK_LOCATION IrpSp, PIRP Irp, PNTSTATUS Status)
{
    UCHAR CreateDisposition = (IrpSp->Parameters.Create.Options >> 24) & 0xFF;
    ULONG CreateOptions = IrpSp->Parameters.Create.Options & FILE_VALID_OPTION_FLAGS;
    BOOLEAN IsDirectory = (CreateOptions & FILE_DIRECTORY_FILE) && !(CreateOptions & FILE_NON_DIRECTORY_FILE);
    BOOLEAN IsAnyFile = !(CreateOptions & (FILE_DIRECTORY_FILE | FILE_NON_DIRECTORY_FILE));
    BOOLEAN ResolveReparse = !(CreateOptions & FILE_OPEN_REPARSE_POINT);
    UCHAR RequestedFileType;

    if ((CreateOptions & FILE_DIRECTORY_FILE) && (CreateOptions & FILE_NON_DIRECTORY_FILE))
    {
        *Status = STATUS_INVALID_PARAMETER;
        return NULL;
    }

    RequestedFileType = IsDirectory ? EXT_DIR_ENTRY_TYPE_DIRECTORY : EXT_DIR_ENTRY_TYPE_REGULAR;
    if (IsAnyFile)
        RequestedFileType = EXT_DIR_ENTRY_TYPE_UNKNOWN;

    DPRINT1("RequestedFileType = %u\n", RequestedFileType);
    DPRINT1("CreateDisposition = %u\n", CreateDisposition);
    DPRINT1("CreateOptions = %u\n", CreateOptions);

    ULONG FileLength = strlen(FilePathA);
    PEXTFS_INODE_CONTEXT InodeContext;
    PEXTFS_FILE_CONTEXT ExistingFileContext = ExtfsFindExistingFileContext(VolumeExtension, FilePathA);
    if (ExistingFileContext)
    {
        if (ExistingFileContext->InodeContext->IsDirectory != IsDirectory && !IsAnyFile)
        {
            if (ExistingFileContext->InodeContext->IsDirectory && !IsDirectory)
                *Status = STATUS_FILE_IS_A_DIRECTORY;
            else
                *Status = STATUS_NOT_A_DIRECTORY;

            return NULL;
        }

        return ExistingFileContext;
    }

    PEXTFS_STANDARD_FCB StandardFCB;
    PEXTFS_FILE_CONTEXT FileContext = ExAllocatePoolWithTag(NonPagedPool,
                                                            sizeof(*FileContext),
                                                            EXTFS_TAG_FILE_CONTEXT);
    if (!FileContext)
        return NULL;

    RtlZeroMemory(FileContext, sizeof(*FileContext));

    // FileContext->StandardFCB.StreamFileObject = IoCreateStreamFileObject(NULL, VolumeExtension->DeviceObject);
    // if (!FileContext->StandardFCB.StreamFileObject)
    // {
    //     ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
    //     return NULL;
    // }
    // FileContext->StandardFCB.StreamFileObject->Flags |= FO_STREAM_FILE;

    if (FileLength == 0)
    {
        InodeContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*InodeContext), EXTFS_TAG_INODE_CONTEXT);
        if (!InodeContext)
        {
            ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
            return NULL;
        }
        RtlZeroMemory(InodeContext, sizeof(*InodeContext));

        InodeContext->FileSize = VolumeExtension->TotalBlocks * VolumeExtension->BlockSize;
        InodeContext->VolumeExtension = VolumeExtension;
        DPRINT1("VolumeExtension->BlockSize = %u\n", VolumeExtension->BlockSize);

        InodeContext->ExtentList = ExAllocatePoolWithTag(NonPagedPool,
                                                         sizeof(*InodeContext->ExtentList),
                                                         EXTFS_TAG_EXTENT_LIST);
        if (!InodeContext->ExtentList)
        {
            ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
            ExFreePoolWithTag(InodeContext, EXTFS_TAG_INODE_CONTEXT);
            return NULL;
        }
        RtlZeroMemory(InodeContext->ExtentList, sizeof(*InodeContext->ExtentList));

        InodeContext->ExtentList->Length = VolumeExtension->TotalBlocks;
        InodeContext->ExtentList->LengthInBytes = InodeContext->ExtentList->Length * VolumeExtension->TotalBlocks;
        InodeContext->IsVolume = TRUE;
    }
    else
    {
        BOOLEAN OverwriteExistingContent = 
            CreateDisposition == FILE_SUPERSEDE ||
            CreateDisposition == FILE_OVERWRITE ||
            CreateDisposition == FILE_OVERWRITE_IF;
        BOOLEAN WillCreateNewFile =
            CreateDisposition == FILE_SUPERSEDE ||
            CreateDisposition == FILE_CREATE ||
            CreateDisposition == FILE_OPEN_IF ||
            CreateDisposition == FILE_OVERWRITE_IF;

        switch (CreateDisposition)
        {
            case FILE_OPEN:
                InodeContext = ExtfsFindFileByPath(VolumeExtension,
                                                   FilePathA, ResolveReparse, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION,
                                                   FALSE, RequestedFileType, Status, Irp);
                if (!InodeContext)
                {
                    DPRINT1("Cannot get \"%s\" InodeContext\n", FilePathA);
                    ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
                    return NULL;
                }
                break;

            case FILE_SUPERSEDE:
            case FILE_CREATE:
            case FILE_OPEN_IF:
            case FILE_OVERWRITE:
            case FILE_OVERWRITE_IF:
                InodeContext = ExtfsFindFileByPath(VolumeExtension,
                                                   FilePathA, ResolveReparse, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION,
                                                   WillCreateNewFile, RequestedFileType, Status, Irp);
                if (!InodeContext)
                {
                    DPRINT1("Cannot get \"%s\" InodeContext\n", FilePathA);
                    ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
                    return NULL;
                }

                if (CreateDisposition == FILE_CREATE &&
                    *Status == STATUS_OBJECT_NAME_EXISTS)
                {
                    DPRINT1("File \"%s\" already exists\n", FilePathA);
                    ExtfsReleaseInodeContext(InodeContext);
                    ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
                    return NULL;
                }

                if (OverwriteExistingContent)
                {
                    ExtfsChangeInodeSize(InodeContext, 0, FALSE);
                }

                *Status = STATUS_SUCCESS;
                break;

            default:
                DPRINT1("Invalid CreateDisposition(%u)\n", CreateDisposition);
                ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
                return NULL;
        }
    }

    StandardFCB = &FileContext->StandardFCB;

    EXTFS_INIT_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC);

    ExtfsInitListEntry(&FileContext->StreamList, FileContext);
    ExtfsInitListEntry(&FileContext->ListEntry, FileContext);

    StandardFCB->StandardHeader.IsFastIoPossible = FastIoIsNotPossible;
    StandardFCB->StandardHeader.Resource = &StandardFCB->MainResource;
    StandardFCB->StandardHeader.PagingIoResource = &StandardFCB->PagingIoResource;
    ExInitializeResourceLite(&StandardFCB->MainResource);
    ExInitializeResourceLite(&StandardFCB->PagingIoResource);

    RtlInitAnsiString(&FileContext->FilePathUtf8, FilePathA);
    RtlInitUnicodeString(&FileContext->FilePathUnicode, FilePathW);
    ExInitializeResourceLite(&FileContext->StreamListLock);

    FileContext->InodeContext = InodeContext;

    ExtfsUpdateFileContextSize(FileContext, NULL);

    FsRtlInitializeFileLock(&FileContext->FileLock, NULL, NULL);

    ExtfsAddFileContextToList(VolumeExtension, FileContext);

    return FileContext;
}

VOID ExtfsFreeFileContext(PEXTFS_FILE_CONTEXT FileContext)
{
    ExDeleteResourceLite(&FileContext->StandardFCB.MainResource);
    ExDeleteResourceLite(&FileContext->StandardFCB.PagingIoResource);

    if (FileContext->FilePathUtf8.Buffer)
        ExFreePool(FileContext->FilePathUtf8.Buffer);
    if (FileContext->FilePathUnicode.Buffer)
        ExFreePool(FileContext->FilePathUnicode.Buffer);

    FsRtlUninitializeFileLock(&FileContext->FileLock);
    ExDeleteResourceLite(&FileContext->StreamListLock);

    ExtfsReleaseInodeContext(FileContext->InodeContext);
    ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
}

PEXTFS_FILE_STREAM ExtfsCreateFileStream(PEXTFS_FILE_CONTEXT FileContext, PIO_STACK_LOCATION IrpSp, PFILE_OBJECT FileObject)
{
    ULONG DesiredAccess = IrpSp->Parameters.Create.SecurityContext->DesiredAccess;
    PEXTFS_FILE_STREAM FileStream = ExAllocatePoolWithTag(NonPagedPool, sizeof(*FileStream), EXTFS_TAG_FILE_STREAM);
    if (!FileStream)
    {
        return NULL;
    }
    RtlZeroMemory(FileStream, sizeof(*FileStream));

    EXTFS_INIT_FCB_HEADER(FileStream, *FileStream, EXTFS_FILE_STR_MAGIC);

    ExtfsInitListEntry(&FileStream->ListEntry, FileStream);

    FileStream->FileContext = FileContext;
    FileStream->FileObject = FileObject;

    FileStream->CanListDirectory = DesiredAccess & FILE_LIST_DIRECTORY;
    FileStream->CanExecute = DesiredAccess & FILE_EXECUTE;
    FileStream->CanRead = DesiredAccess & FILE_READ_DATA;
    FileStream->CanWrite = DesiredAccess & FILE_WRITE_DATA;

    FileObject->CurrentByteOffset.QuadPart = 0;

    ExtfsAddFileStreamToList(FileContext, FileStream);
    return FileStream;
}

VOID ExtfsDumpOpenFileList(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;

    ULONG FO_N = 1;

    ExAcquireResourceExclusiveLite(&VolumeExtension->OpenFileListLock, TRUE);

    EndListEntry = &VolumeExtension->OpenFileList;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        DPRINT1("FO_N = %u\n", FO_N++);

        CurrentListEntry = CurrentListEntry->Next;
    }

    ExReleaseResourceLite(&VolumeExtension->OpenFileListLock);
}

NTSTATUS
ExtfsFsdDispatchCreate(PDEVICE_OBJECT DeviceObject,
                       PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PEXTFS_INODE_CONTEXT InodeContext;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_STANDARD_FCB StandardFCB, StandardFCBStream;
    PEXTFS_FILE_CONTEXT FileContext;
    PEXTFS_FILE_STREAM FileStream;
    PWCHAR FilePathW = NULL;
    PCHAR FilePathA = NULL;

    UNREFERENCED_PARAMETER(InodeContext);
    UNREFERENCED_PARAMETER(StandardFCBStream);

    if (DeviceObject == ExtfsGlobalData->DeviceObject)
    {
        DPRINT1("Opening ExtfsGlobalData\n");

        VolumeExtension = NULL;
        Irp->IoStatus.Information = FILE_OPENED;
        goto result;
    }

    ULONG RelatedNameLength = 0;
    ULONG NameLength = FileObject->FileName.Length / sizeof(*FileObject->FileName.Buffer);
    ULONG Index = 0, IndexName = 0;
    ULONG FileLength = NameLength + 2;

    PFILE_OBJECT RelatedFileObject = FileObject->RelatedFileObject;
    BOOLEAN ValidRelatedFileObject = FALSE;

    if (!IsExtfsVolumeExtension(VolumeExtension))
    {
        VolumeExtension = NULL;
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    if (RelatedFileObject)
    {
        DPRINT1("Does have related FO\n");

        if (RelatedFileObject->Vpb != VolumeExtension->Vpb)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto result;
        }

        RelatedNameLength = RelatedFileObject->FileName.Length / sizeof(*RelatedFileObject->FileName.Buffer);
        FileLength += RelatedNameLength + 1;
        ValidRelatedFileObject = TRUE;
    }

    DPRINT1("RelatedNameLength = %u\n", RelatedNameLength);
    DPRINT1("NameLength = %u\n", NameLength);
    DPRINT1("FileLength = %u\n", FileLength);

    FilePathW = ExAllocatePool(NonPagedPool,
                               FileLength * sizeof(WCHAR));
    if (!FilePathW)
    {
        DPRINT1("Failed to allocate Unicode string\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    UNICODE_STRING FilePathUnicode;
    ANSI_STRING FilePathUtf8;

    if (ValidRelatedFileObject)
    {
        IndexName = 0;
        while (Index < RelatedNameLength)
        {
            FilePathW[Index++] = RelatedFileObject->FileName.Buffer[IndexName++];
        }
        if (FilePathW[Index - 1] != '\\')
            FilePathW[Index++] = '\\';
    }

    IndexName = 0;
    while (IndexName < NameLength)
    {
        FilePathW[Index++] = FileObject->FileName.Buffer[IndexName++];
    }

    FilePathW[Index--] = '\0';

    if (IrpSp->Flags & SL_OPEN_TARGET_DIRECTORY)
    {
        while (Index > 0)
        {
            WCHAR WCharacter = FilePathW[Index];

            if (WCharacter == '\\')
                break;

            Index--;
        }

        if (Index < 1)
            Index = 1;
        FilePathW[Index] = '\0';

        DPRINT1("SL_OPEN_TARGET_DIRECTORY\n");
    }

    ExAcquireResourceExclusiveLite(&VolumeExtension->FileOperationLock, TRUE);

    RtlInitUnicodeString(&FilePathUnicode, FilePathW);
    FilePathA = ExtfsConvertUnicodeToUtf8(&FilePathUnicode);
    if (!FilePathA)
    {
        DPRINT1("Cannot convert Unicode path to Utf8\n");

        if (VolumeExtension->Dismounted)
        {
            KIRQL OldIrql;
            PVPB RealDeviceVpb = VolumeExtension->Vpb;

            Status = ExtfsInitializeVolume(VolumeExtension, VolumeExtension->ReadOnly);
            if (NT_SUCCESS(Status))
            {
                if (RealDeviceVpb)
                {
                    IoAcquireVpbSpinLock(&OldIrql);

                    RealDeviceVpb->Flags |= VPB_MOUNTED;

                    IoReleaseVpbSpinLock(OldIrql);
                }

                VolumeExtension->Dismounted = FALSE;
            }
        }

        FileObject->FsContext = VolumeExtension;
        ExReleaseResourceLite(&VolumeExtension->FileOperationLock);
        goto result;
    }

    RtlInitAnsiString(&FilePathUtf8, FilePathA);

    DPRINT1("Path \"%s\"\n", FilePathA);

    FileContext = ExtfsCreateOrFindFileContext(VolumeExtension, FilePathA, FilePathW, IrpSp, Irp, &Status);
    if (!FileContext)
    {
        DPRINT1("Cannot get file context of a path\n");
        ExReleaseResourceLite(&VolumeExtension->FileOperationLock);
        goto result;
    }
    InodeContext = FileContext->InodeContext;

    FileStream = ExtfsCreateFileStream(FileContext, IrpSp, FileObject);
    if (!FileStream)
    {
        DPRINT1("Cannot create file stream for file context\n");
        if (FileContext->ReferenceCount < 1)
            ExtfsRemoveFileContextFromList(VolumeExtension, FileContext);
        ExReleaseResourceLite(&VolumeExtension->FileOperationLock);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    FileContext->ReferenceCount++;

    if (FileStream->CanExecute)
        FileContext->CanExecute = FileStream->CanExecute;

    StandardFCB = &FileContext->StandardFCB;
    StandardFCBStream = &FileStream->StandardFCB;

    FileObject->FsContext = FileContext;
    FileObject->FsContext2 = FileStream;
    FileObject->PrivateCacheMap = NULL;
    FileObject->SectionObjectPointer = &StandardFCB->SectionObjectPointers;

    // CcInitializeCacheMap(
    //     FileObject,
    //     &FileContext->StandardFCB.FileSizes,
    //     FALSE,
    //     &ExtfsGlobalData->CacheMgrCallbacks,
    //     FileContext
    // );

    ExReleaseResourceLite(&VolumeExtension->FileOperationLock);

result:
    if (!NT_SUCCESS(Status))
    {
        if (FilePathA)
            ExFreePool(FilePathA);
        if (FilePathW)
            ExFreePool(FilePathW);
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
