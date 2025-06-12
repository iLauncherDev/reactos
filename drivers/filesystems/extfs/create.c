#include "extfs.h"

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
    PEXTFS_FILE_CONTEXT CurrentFileContext = VolumeExtension->OpenFileList;
    PEXTFS_FILE_CONTEXT EndFileContext = CurrentFileContext;
    if (!CurrentFileContext)
        return NULL;

    do
    {
        if (!ExtfsFsdCompareFilePath(CurrentFileContext->FilePath, FilePath))
            return CurrentFileContext;

        CurrentFileContext = CurrentFileContext->Next;
    } while (CurrentFileContext != EndFileContext);

    return NULL;
}

VOID ExtfsAddFileContextToList(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_FILE_CONTEXT FileContext)
{
    if (!VolumeExtension->OpenFileList)
    {
        FileContext->Prev = FileContext;
        FileContext->Next = FileContext;
        VolumeExtension->OpenFileList = FileContext;
    }
    else
    {
        PEXTFS_FILE_CONTEXT StartFileContext = (PEXTFS_FILE_CONTEXT)VolumeExtension->OpenFileList;
        PEXTFS_FILE_CONTEXT PrevFileContext = StartFileContext->Prev;

        FileContext->Next = PrevFileContext->Next;
        FileContext->Prev = PrevFileContext;
        PrevFileContext->Next = FileContext;
        StartFileContext->Prev = FileContext;
    }
}

VOID ExtfsRemoveFileContextFromList(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_FILE_CONTEXT FileContext)
{
    PEXTFS_FILE_CONTEXT CurrentFileContext = VolumeExtension->OpenFileList;
    PEXTFS_FILE_CONTEXT EndFileContext = CurrentFileContext;
    if (!CurrentFileContext)
        return;

    if (CurrentFileContext == FileContext)
    {
        if (CurrentFileContext->Next != FileContext)
        {
            CurrentFileContext->Prev->Next = CurrentFileContext->Next; 
            VolumeExtension->OpenFileList = CurrentFileContext->Next;
        }
        else
        {
            VolumeExtension->OpenFileList = NULL;
        }
    }
    else
    {
        do
        {
            if (CurrentFileContext == FileContext)
            {
                PEXTFS_FILE_CONTEXT PrevFileContext = CurrentFileContext->Prev;
                PEXTFS_FILE_CONTEXT NextFileContext = CurrentFileContext->Next;

                PrevFileContext->Next = NextFileContext;
                NextFileContext->Prev = PrevFileContext;
                break;
            }

            CurrentFileContext = CurrentFileContext->Next;
        } while (CurrentFileContext != EndFileContext);
    }

    ExDeleteResourceLite(&FileContext->StandardFCB.MainResource);
    ExDeleteResourceLite(&FileContext->StandardFCB.PagingIoResource);
    ExtfsReleaseInodeContext(FileContext->InodeContext);
    ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
}

VOID ExtfsAddFileStreamToList(PEXTFS_FILE_CONTEXT FileContext, PEXTFS_FILE_STREAM FileStream)
{
    if (!FileContext->Streams)
    {
        FileStream->Prev = FileStream;
        FileStream->Next = FileStream;
        FileContext->Streams = FileStream;
    }
    else
    {
        PEXTFS_FILE_STREAM StartFileStream = (PEXTFS_FILE_STREAM)FileContext->Streams;
        PEXTFS_FILE_STREAM PrevFileStream = StartFileStream->Prev;

        FileStream->Next = PrevFileStream->Next;
        FileStream->Prev = PrevFileStream;
        PrevFileStream->Next = FileStream;
        StartFileStream->Prev = FileStream;
    }
}

VOID ExtfsRemoveFileStreamFromList(PEXTFS_FILE_CONTEXT FileContext, PEXTFS_FILE_STREAM FileStream)
{
    PEXTFS_FILE_STREAM CurrentFileStream = FileContext->Streams;
    PEXTFS_FILE_STREAM EndFileStream = CurrentFileStream;
    if (!CurrentFileStream)
        return;

    if (CurrentFileStream == FileStream)
    {
        if (CurrentFileStream->Next != FileStream)
        {
            CurrentFileStream->Prev->Next = CurrentFileStream->Next; 
            FileContext->Streams = CurrentFileStream->Next;
        }
        else
        {
            FileContext->Streams = NULL;
        }
    }
    else
    {
        do
        {
            if (CurrentFileStream == FileStream)
            {
                PEXTFS_FILE_STREAM PrevFileStream = CurrentFileStream->Prev;
                PEXTFS_FILE_STREAM NextFileStream = CurrentFileStream->Next;

                PrevFileStream->Next = NextFileStream;
                NextFileStream->Prev = PrevFileStream;
                break;
            }

            CurrentFileStream = CurrentFileStream->Next;
        } while (CurrentFileStream != EndFileStream);
    }

    ExFreePoolWithTag(FileStream, EXTFS_TAG_FILE_STREAM);
}

PEXTFS_FILE_CONTEXT ExtfsCreateOrFindFileContext(PEXTFS_VOLUME_EXTENSION VolumeExtension, PCHAR FilePath)
{
    ULONG FileLength = strlen(FilePath);
    PEXTFS_INODE_CONTEXT InodeContext;
    PEXTFS_FILE_CONTEXT ExistingFileContext = ExtfsFindExistingFileContext(VolumeExtension, FilePath);
    if (ExistingFileContext)
        return ExistingFileContext;

    PEXTFS_STANDARD_FCB StandardFCB;
    PEXTFS_FILE_CONTEXT FileContext = ExAllocatePoolWithTag(NonPagedPool,
                                                            sizeof(*FileContext),
                                                            EXTFS_TAG_FILE_CONTEXT);
    if (!FileContext)
        return NULL;

    RtlZeroMemory(FileContext, sizeof(*FileContext));

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
        InodeContext->IsDirectory = TRUE;
    }
    else
    {
        InodeContext = ExtfsFindFileByPath(VolumeExtension, FilePath, TRUE, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION);
        if (!InodeContext)
        {
            DPRINT1("Cannot get \"%s\" InodeContext\n", FilePath);
            ExFreePoolWithTag(FileContext, EXTFS_TAG_FILE_CONTEXT);
            return NULL;
        }
    }

    StandardFCB = &FileContext->StandardFCB;

    StandardFCB->Identifier.Magic = EXTFS_FILE_CTX_MAGIC;
    StandardFCB->Identifier.Size = sizeof(*FileContext);

    StandardFCB->StandardHeader.IsFastIoPossible = FastIoIsNotPossible;
    StandardFCB->StandardHeader.NodeByteSize = sizeof(*FileContext);
    StandardFCB->StandardHeader.Resource = &StandardFCB->MainResource;
    StandardFCB->StandardHeader.PagingIoResource = &StandardFCB->PagingIoResource;
    ExInitializeResourceLite(&StandardFCB->MainResource);
    ExInitializeResourceLite(&StandardFCB->PagingIoResource);

    FileContext->InodeContext = InodeContext;
    FileContext->FilePath = FilePath;

    StandardFCB->FileSizes.FileSize.QuadPart = InodeContext->FileSize;
    StandardFCB->FileSizes.AllocationSize.QuadPart =
        ((InodeContext->FileSize + (InodeContext->VolumeExtension->BlockSize - 1)) /
          InodeContext->VolumeExtension->BlockSize) * InodeContext->VolumeExtension->BlockSize;
    StandardFCB->FileSizes.ValidDataLength = StandardFCB->FileSizes.FileSize;

    StandardFCB->StandardHeader.AllocationSize = StandardFCB->FileSizes.AllocationSize;
    StandardFCB->StandardHeader.ValidDataLength = StandardFCB->FileSizes.ValidDataLength;

    FsRtlInitializeFileLock(&FileContext->FileLock, NULL, NULL);

    ExtfsAddFileContextToList(VolumeExtension, FileContext);

    return FileContext;
}

PEXTFS_FILE_STREAM ExtfsCreateFileStream(PEXTFS_FILE_CONTEXT FileContext)
{
    PEXTFS_STANDARD_FCB StandardFCB;
    PEXTFS_FILE_STREAM FileStream = ExAllocatePoolWithTag(NonPagedPool, sizeof(*FileStream), EXTFS_TAG_FILE_STREAM);
    if (!FileStream)
    {
        return NULL;
    }
    RtlZeroMemory(FileStream, sizeof(*FileStream));

    StandardFCB = &FileStream->StandardFCB;
    StandardFCB->Identifier.Magic = EXTFS_FILE_STR_MAGIC;
    StandardFCB->Identifier.Size = sizeof(*FileStream);
    StandardFCB->StandardHeader.NodeByteSize = StandardFCB->Identifier.Size;
    FileStream->FileContext = FileContext;

    ExtfsAddFileStreamToList(FileContext, FileStream);
    return FileStream;
}

NTSTATUS
ExtfsFsdDispatchCreate(PDEVICE_OBJECT DeviceObject,
                       PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PEXTFS_INODE_CONTEXT InodeContext;
    PEXTFS_VOLUME_EXTENSION VolumeExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PEXTFS_STANDARD_FCB StandardFCB;
    PEXTFS_FILE_CONTEXT FileContext;
    PEXTFS_FILE_STREAM FileStream;

    ULONG RelatedNameLength = 0;
    ULONG Index = 0, IndexName = 0;
    ULONG FileLength = FileObject->FileName.Length / sizeof(*FileObject->FileName.Buffer);

    PFILE_OBJECT RelatedFileObject = FileObject->RelatedFileObject;
    if (RelatedFileObject)
    {
        RelatedNameLength = RelatedFileObject->FileName.Length / sizeof(*RelatedFileObject->FileName.Buffer);
        FileLength += RelatedNameLength + 1;
    }

    PCHAR FilePath = ExAllocatePoolWithTag(NonPagedPool,
                                           FileLength + 1,
                                           EXTFS_TAG_BUFFER);
    if (!FilePath)
    {
        DPRINT1("Failed to allocate ASCII string\n");
        Status = STATUS_NO_MEMORY;
        goto result;
    }

    if (RelatedFileObject)
    {
        while (Index < RelatedNameLength)
        {
            CHAR Character = (CHAR)RelatedFileObject->FileName.Buffer[IndexName++];
            FilePath[Index++] = Character;
        }
        if (FilePath[Index - 1] != '\\')
            FilePath[Index++] = '\\';
        IndexName = 0;
    }

    while (Index < FileLength)
    {
        CHAR Character = (CHAR)FileObject->FileName.Buffer[IndexName++];
        FilePath[Index++] = Character;
    }
    FilePath[Index] = '\0';

    if (FileObject->Flags & FO_STREAM_FILE)
    {
        DPRINT1("Warning FO_STREAM_FILE\n");
    }

    DPRINT1("Path \"%s\"\n", FilePath);

    if (VolumeExtension && VolumeExtension->Identifier.Magic != EXTFS_VOLUME_EXTENSION_MAGIC)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    FileContext = ExtfsCreateOrFindFileContext(VolumeExtension, FilePath);
    if (!FileContext)
    {
        ExFreePoolWithTag(FilePath, EXTFS_TAG_BUFFER);
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto result;
    }
    InodeContext = FileContext->InodeContext;

    FileStream = ExtfsCreateFileStream(FileContext);
    if (!FileStream)
    {
        ExtfsRemoveFileContextFromList(VolumeExtension, FileContext);
        ExFreePoolWithTag(FilePath, EXTFS_TAG_BUFFER);
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto result;
    }

    StandardFCB = &FileContext->StandardFCB;

    FileObject->FsContext = FileContext;
    FileObject->FsContext2 = FileStream;
    FileObject->SectionObjectPointer = &StandardFCB->SectionObjectPointers;

    if (!InodeContext->IsDirectory && !FileObject->PrivateCacheMap)
    {
        CcInitializeCacheMap(
            FileObject,
            &StandardFCB->FileSizes,
            FALSE,
            &ExtfsGlobalData->CacheMgrCallbacks,
            FileStream
        );
    }

    Irp->IoStatus.Information = FILE_OPENED;
    Status = STATUS_SUCCESS;

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
