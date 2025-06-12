#include <extfs.h>

#define EXT_DIR_ENTRY_ALIGNMENT 4

BOOLEAN ExtfsInitializeDirectory(PEXTFS_VOLUME_EXTENSION VolumeExtension,
                                 PEXTFS_INODE_CONTEXT InodeContext,
                                 PEXTFS_INODE_CONTEXT ParentInodeContext)
{
    BOOLEAN Result = FALSE;
    ULONG BlockSize = VolumeExtension->BlockSize;
    PEXT_DIR_ENTRY DirEntry = ExAllocatePoolWithTag(NonPagedPool, BlockSize, EXTFS_TAG_BUFFER);
    PEXT_DIR_ENTRY CurrentEntry = DirEntry;
    USHORT FirstEntrySize = ALIGN_UP_BY(FIELD_OFFSET(EXT_DIR_ENTRY, Name) + 1, EXT_DIR_ENTRY_ALIGNMENT);
    LARGE_INTEGER Offset = {0};
    if (!DirEntry)
    {
        DPRINT1("Cannot allocate DirEntry\n");
        goto error;
    }
    RtlZeroMemory(DirEntry, BlockSize);

    PCHAR FirstEntryName = ".";
    PCHAR SecondEntryName = "..";

    ExtfsIncrementInodeContextLinkCount(InodeContext);
    ExtfsIncrementInodeContextLinkCount(InodeContext);
    ExtfsSetInodeContextType(InodeContext, EXT_DIR_ENTRY_TYPE_DIRECTORY);

    ExtfsIncrementInodeContextLinkCount(ParentInodeContext);
 
    // Create first directory
    CurrentEntry->EntryLen = FirstEntrySize;
    CurrentEntry->Inode = (ULONG)InodeContext->InodeNum;
    CurrentEntry->FileType = EXT_DIR_ENTRY_TYPE_DIRECTORY;

    CurrentEntry->NameLen = strlen(FirstEntryName);
    RtlCopyMemory(CurrentEntry->Name, FirstEntryName, CurrentEntry->NameLen);

    CurrentEntry = (PVOID)((PCHAR)CurrentEntry + CurrentEntry->EntryLen);

    // Create second directory
    CurrentEntry->EntryLen = BlockSize - FirstEntrySize;
    CurrentEntry->Inode = (ULONG)ParentInodeContext->InodeNum;
    CurrentEntry->FileType = EXT_DIR_ENTRY_TYPE_DIRECTORY;

    CurrentEntry->NameLen = strlen(SecondEntryName);
    RtlCopyMemory(CurrentEntry->Name, SecondEntryName, CurrentEntry->NameLen);

    Result = ExtfsWriteInodeData(InodeContext, (PVOID)DirEntry, Offset, BlockSize) == BlockSize;
    ExtfsChangeInodeSize(InodeContext, BlockSize, FALSE);
error:
    if (DirEntry)
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);

    return Result;
}

BOOLEAN ExtfsUninitializeDirectory(PEXTFS_VOLUME_EXTENSION VolumeExtension,
                                   PEXTFS_INODE_CONTEXT InodeContext,
                                   PEXTFS_INODE_CONTEXT ParentInodeContext)
{
    if (!InodeContext->IsDirectory)
        return FALSE;

    ExtfsRemoveInodeContext(ParentInodeContext);
    ExtfsRemoveInodeContext(InodeContext);
    ExtfsRemoveInodeContext(InodeContext);

    return TRUE;
}

BOOLEAN ExtfsRelinkParentDirectory(PEXTFS_VOLUME_EXTENSION VolumeExtension,
                                   PEXTFS_INODE_CONTEXT InodeContext,
                                   PEXTFS_INODE_CONTEXT ParentInodeContext)
{
    BOOLEAN Result = FALSE;
    ULONG BlockSize = VolumeExtension->BlockSize;
    PEXT_DIR_ENTRY DirEntry = ExAllocatePoolWithTag(NonPagedPool, BlockSize, EXTFS_TAG_BUFFER);
    PEXT_DIR_ENTRY CurrentEntry = DirEntry;
    LARGE_INTEGER Offset = {0};
    if (!DirEntry)
    {
        DPRINT1("Cannot allocate DirEntry\n");
        goto error;
    }
    RtlZeroMemory(DirEntry, BlockSize);
    
    Result = ExtfsReadInodeData(InodeContext, (PVOID)DirEntry, Offset, BlockSize) == BlockSize;
    if (!Result)
    {
        DPRINT1("Cannot read first block\n");
        goto error;
    }

    ExtfsIncrementInodeContextLinkCount(ParentInodeContext);
    ExtfsIncrementInodeContextLinkCount(InodeContext);

    CurrentEntry = (PVOID)((PCHAR)CurrentEntry + CurrentEntry->EntryLen);
    CurrentEntry->Inode = (ULONG)ParentInodeContext->InodeNum;

    Result = ExtfsWriteInodeData(InodeContext, (PVOID)DirEntry, Offset, BlockSize) == BlockSize;
error:
    if (DirEntry)
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);

    return Result;
}

ULONG ExtfsFindFile(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_INODE_CONTEXT InodeContext, PCHAR Name)
{
    PEXT_DIR_ENTRY DirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DirEntry), EXTFS_TAG_BUFFER);
    LARGE_INTEGER Start = {0}, End = {0};
    ULONG InodeNum;
    ULONG BytesRead;
    ULONG_PTR NameLen = strlen(Name);
    if (!DirEntry)
    {
        DPRINT1("Cannot allocate DirEntry\n");
        goto error;
    }

    if (!InodeContext->IsDirectory)
        goto error;

    if (!strcmp(Name, "."))
    {
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);
        return InodeContext->InodeNum;
    }

    End.QuadPart = InodeContext->FileSize;

    while (Start.QuadPart < End.QuadPart)
    {
        BytesRead = ExtfsReadInodeData(InodeContext,
                                       (PVOID)DirEntry, Start, sizeof(*DirEntry));
        if (!BytesRead)
            goto error;
        if (!DirEntry->EntryLen)
        {
            DPRINT1("Infinity loop detected!\n");
            goto error;
        }
        if (!DirEntry->Inode || !DirEntry->NameLen)
            goto skip_entry;

        DPRINT("%s - %.*s\n", Name, DirEntry->NameLen, DirEntry->Name);

        if (DirEntry->NameLen == NameLen &&
            !_strnicmp(DirEntry->Name, Name, NameLen))
        {
            InodeNum = DirEntry->Inode;
            ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);
            return InodeNum;
        }

skip_entry:
        Start.QuadPart += DirEntry->EntryLen;
    }

error:
    if (DirEntry)
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);

    return 0;
}

BOOLEAN ExtfsMergeFreeDirEntries(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_INODE_CONTEXT InodeContext)
{
    BOOLEAN Result = FALSE;
    PEXT_DIR_ENTRY SelectedDirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*SelectedDirEntry), EXTFS_TAG_BUFFER);
    PEXT_DIR_ENTRY DirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DirEntry), EXTFS_TAG_BUFFER);
    LARGE_INTEGER Start = {0}, End = {0}, SelectedOffset = {0};
    ULONG BytesRead;
    USHORT MinEntryLen = ALIGN_UP_BY(FIELD_OFFSET(EXT_DIR_ENTRY, Name), EXT_DIR_ENTRY_ALIGNMENT);

    ExAcquireResourceExclusiveLite(&InodeContext->DirectoryDataLock, TRUE);

    if (!DirEntry || !SelectedDirEntry)
    {
        DPRINT1("Cannot allocate DirEntry\n");
        goto result;
    }

    if (!InodeContext->IsDirectory)
        goto result;

    End.QuadPart = InodeContext->FileSize;

    while (Start.QuadPart < End.QuadPart)
    {
        BytesRead = ExtfsReadInodeData(InodeContext,
                                       (PVOID)DirEntry, Start, sizeof(*DirEntry));
        if (!BytesRead)
            goto result;
        if (!DirEntry->EntryLen)
        {
            DPRINT1("Infinity loop detected!\n");
            goto result;
        }

        if (Result && !DirEntry->Inode)
        {
            SelectedDirEntry->EntryLen += DirEntry->EntryLen;
        }
        else if (Result)
        {
            Result = ExtfsWriteInodeData(InodeContext, (PVOID)SelectedDirEntry, SelectedOffset, MinEntryLen) == MinEntryLen;
            if (!Result)
                goto result;

            Result = FALSE;
        }

        if (!Result)
        {
            SelectedOffset = Start;
            RtlCopyMemory(SelectedDirEntry, DirEntry, sizeof(*SelectedDirEntry));

            Result = TRUE;
        }

        goto skip_entry;
skip_entry:
        Start.QuadPart += DirEntry->EntryLen;
    }

    if (Result)
    {
        Result = ExtfsWriteInodeData(InodeContext, (PVOID)SelectedDirEntry, SelectedOffset, MinEntryLen) == MinEntryLen;
        if (!Result)
            goto result;

        Result = FALSE;
    }

result:
    if (DirEntry)
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);
    if (SelectedDirEntry)
        ExFreePoolWithTag(SelectedDirEntry, EXTFS_TAG_BUFFER);

    ExReleaseResourceLite(&InodeContext->DirectoryDataLock);
    return TRUE;
}

BOOLEAN ExtfsRemoveFileFromDirEntries(PEXTFS_VOLUME_EXTENSION VolumeExtension, PEXTFS_INODE_CONTEXT InodeContext, PCHAR FileName)
{
    BOOLEAN Result = FALSE;
    PEXT_DIR_ENTRY SelectedDirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*SelectedDirEntry), EXTFS_TAG_BUFFER);
    PEXT_DIR_ENTRY DirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DirEntry), EXTFS_TAG_BUFFER);
    LARGE_INTEGER Start = {0}, End = {0}, SelectedOffset = {0};
    ULONG BytesRead;
    ULONG_PTR NameLen = strlen(FileName);
    USHORT MinEntryLen = ALIGN_UP_BY(FIELD_OFFSET(EXT_DIR_ENTRY, Name), EXT_DIR_ENTRY_ALIGNMENT);

    ExAcquireResourceExclusiveLite(&InodeContext->DirectoryDataLock, TRUE);

    if (!DirEntry || !SelectedDirEntry)
    {
        DPRINT1("Cannot allocate DirEntry\n");
        goto result;
    }

    if (!InodeContext->IsDirectory)
        goto result;

    if (!strcmp(FileName, ".") || !strcmp(FileName, ".."))
        goto result;

    End.QuadPart = InodeContext->FileSize;

    while (Start.QuadPart < End.QuadPart)
    {
        BytesRead = ExtfsReadInodeData(InodeContext,
                                       (PVOID)DirEntry, Start, sizeof(*DirEntry));
        if (!BytesRead)
            goto result;
        if (!DirEntry->EntryLen)
        {
            DPRINT1("Infinity loop detected!\n");
            goto result;
        }

        DPRINT("%s - %.*s\n", FileName, DirEntry->NameLen, DirEntry->Name);

        if (Result && !DirEntry->Inode)
        {
            SelectedDirEntry->EntryLen += DirEntry->EntryLen;
        }
        else if (Result)
        {
            goto result;
        }

        if (DirEntry->Inode &&
            DirEntry->NameLen == NameLen &&
            !_strnicmp(DirEntry->Name, FileName, NameLen) &&
            !Result)
        {
            SelectedOffset = Start;
            RtlCopyMemory(SelectedDirEntry, DirEntry, sizeof(*SelectedDirEntry));

            SelectedDirEntry->Inode = 0;
            SelectedDirEntry->NameLen = 0;
            Result = TRUE;
        }

        goto skip_entry;
skip_entry:
        Start.QuadPart += DirEntry->EntryLen;
    }

result:
    if (Result)
    {
        ULONGLONG BlockSize = VolumeExtension->BlockSize;
        ULONGLONG RoundedSelectedOffset = ((SelectedOffset.QuadPart + MinEntryLen) + (BlockSize - 1)) & ~(BlockSize - 1);
        USHORT SelectedOffsetDifference = (USHORT)(RoundedSelectedOffset - SelectedOffset.QuadPart);

        DPRINT1("SelectedOffset.QuadPart = %I64u\n", SelectedOffset.QuadPart);
        DPRINT1("RoundedSelectedOffset = %I64u\n", RoundedSelectedOffset);

        if (SelectedOffset.QuadPart + SelectedDirEntry->EntryLen >= End.QuadPart)
        {
            SelectedDirEntry->EntryLen = SelectedOffsetDifference;
            ExtfsChangeInodeSize(InodeContext, RoundedSelectedOffset, FALSE);
        }

        Result = ExtfsWriteInodeData(InodeContext, (PVOID)SelectedDirEntry, SelectedOffset, MinEntryLen) == MinEntryLen;
        if (Result)
        {
            DPRINT1("Removed Inode from DirEntries\n");
        }
    }

    if (DirEntry)
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);
    if (SelectedDirEntry)
        ExFreePoolWithTag(SelectedDirEntry, EXTFS_TAG_BUFFER);

    ExReleaseResourceLite(&InodeContext->DirectoryDataLock);
    return Result;
}

BOOLEAN ExtfsAddFileFromDirEntries(PEXTFS_VOLUME_EXTENSION VolumeExtension,
                                   PEXTFS_INODE_CONTEXT InodeContext,
                                   PEXTFS_INODE_CONTEXT SourceInodeContext, PCHAR FileName)
{
    BOOLEAN Result = FALSE;
    ULONG BlockSize = VolumeExtension->BlockSize;
    PEXT_DIR_ENTRY SecondDirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*SecondDirEntry), EXTFS_TAG_BUFFER);
    PEXT_DIR_ENTRY DirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DirEntry), EXTFS_TAG_BUFFER);
    LARGE_INTEGER Start = {0}, End = {0};
    ULONG BytesRead;
    ULONG_PTR NameLen = strlen(FileName);

    ExAcquireResourceExclusiveLite(&InodeContext->DirectoryDataLock, TRUE);

    if (!NameLen || NameLen > 255)
        goto result;

    if (!DirEntry || !SecondDirEntry)
    {
        DPRINT1("Cannot allocate DirEntry\n");
        goto result;
    }

    if (!InodeContext->IsDirectory)
        goto result;
    if (ExtfsFindFile(VolumeExtension, InodeContext, FileName))
    {
        DPRINT1("File already exists\n");
        goto result;
    }

    if (!strcmp(FileName, ".") || !strcmp(FileName, ".."))
        goto result;

    USHORT NecessaryEntryLen = ALIGN_UP_BY(FIELD_OFFSET(EXT_DIR_ENTRY, Name) + NameLen, EXT_DIR_ENTRY_ALIGNMENT);
    USHORT NecessaryLength = min(NecessaryEntryLen, sizeof(*DirEntry));
    USHORT MinEntryLen = ALIGN_UP_BY(FIELD_OFFSET(EXT_DIR_ENTRY, Name), EXT_DIR_ENTRY_ALIGNMENT);
    UCHAR ReservedEntries = 2;

    End.QuadPart = InodeContext->FileSize;

    (VOID)ExtfsMergeFreeDirEntries(VolumeExtension, InodeContext);

    while (Start.QuadPart < End.QuadPart)
    {
        BytesRead = ExtfsReadInodeData(InodeContext,
                                       (PVOID)DirEntry, Start, sizeof(*DirEntry));
        if (!BytesRead)
            goto result;
        if (!DirEntry->EntryLen)
        {
            DPRINT1("Infinity loop detected!\n");
            goto result;
        }
    
        USHORT RealEntryLen = (DirEntry->NameLen && DirEntry->Inode) ?
            ALIGN_UP_BY(FIELD_OFFSET(EXT_DIR_ENTRY, Name) + DirEntry->NameLen, EXT_DIR_ENTRY_ALIGNMENT) : 0;

        BOOLEAN CanNewEntryFit = !ReservedEntries && (DirEntry->EntryLen >= RealEntryLen &&
                                                      DirEntry->EntryLen - RealEntryLen >= NecessaryEntryLen);

        DPRINT("%s - %.*s\n", FileName, DirEntry->NameLen, DirEntry->Name);

        if (!CanNewEntryFit &&
            Start.QuadPart + DirEntry->EntryLen >= End.QuadPart)
        {
            ULONG AlignedEntryLen = (NecessaryEntryLen + (BlockSize - 1)) & ~(BlockSize - 1);

            End.QuadPart = Start.QuadPart + DirEntry->EntryLen;

            LARGE_INTEGER NewOffsetStart = End, NewOffsetEnd = {.QuadPart = End.QuadPart + AlignedEntryLen};
            DirEntry->EntryLen = NewOffsetEnd.QuadPart - NewOffsetStart.QuadPart;
            DirEntry->NameLen = 0;
            DirEntry->Inode = 0;

            Start = NewOffsetStart;
            End = NewOffsetEnd;

            Result = ExtfsWriteInodeData(InodeContext, (PVOID)DirEntry, Start, MinEntryLen) == MinEntryLen;
            if (!Result)
            {
                DPRINT1("Cannot allocate new entry\n");
                goto result;
            }

            ExtfsChangeInodeSize(InodeContext, End.QuadPart, FALSE);

            RealEntryLen = 0;
            CanNewEntryFit = (DirEntry->EntryLen >= RealEntryLen &&
                              DirEntry->EntryLen - RealEntryLen >= NecessaryEntryLen);
        }

        if (CanNewEntryFit)
        {
            LARGE_INTEGER RemaindingEntryOffset = {.QuadPart = Start.QuadPart};
            USHORT RemaindingEntryLen = DirEntry->EntryLen;

            if (RealEntryLen)
            {
                RemaindingEntryLen -= RealEntryLen;
                RemaindingEntryOffset.QuadPart += RealEntryLen;

                DirEntry->EntryLen = RealEntryLen;
                Result = ExtfsWriteInodeData(InodeContext, (PVOID)DirEntry, Start, MinEntryLen) == MinEntryLen;
                if (!Result)
                    goto result;

                SecondDirEntry->EntryLen = RemaindingEntryLen;
                SecondDirEntry->NameLen = NameLen;
                SecondDirEntry->FileType = ExtfsGetFileTypeDirEntry(SourceInodeContext);
                SecondDirEntry->Inode = (ULONG)SourceInodeContext->InodeNum;
                RtlCopyMemory(SecondDirEntry->Name, FileName, NameLen);

                Result = ExtfsWriteInodeData(InodeContext, (PVOID)SecondDirEntry, RemaindingEntryOffset, NecessaryLength) == NecessaryLength;
            }
            else
            {
                RemaindingEntryLen -= NecessaryEntryLen;
                RemaindingEntryOffset.QuadPart += NecessaryEntryLen;

                SecondDirEntry->Inode = 0;
                SecondDirEntry->NameLen = 0;
                SecondDirEntry->EntryLen = RemaindingEntryLen;

                if (RemaindingEntryLen >= MinEntryLen &&
                    RemaindingEntryLen % EXT_DIR_ENTRY_ALIGNMENT == 0)
                {
                    Result = ExtfsWriteInodeData(InodeContext, (PVOID)SecondDirEntry, RemaindingEntryOffset, MinEntryLen) == MinEntryLen;
                    if (!Result)
                        goto result;
                }
                else if (RemaindingEntryLen)
                {
                    DPRINT1("Cannot be unaligned %u\n", RemaindingEntryLen);
                    ASSERT(FALSE);
                }

                DirEntry->EntryLen = NecessaryEntryLen;
                DirEntry->NameLen = NameLen;
                DirEntry->FileType = ExtfsGetFileTypeDirEntry(SourceInodeContext);
                DirEntry->Inode = (ULONG)SourceInodeContext->InodeNum;
                RtlCopyMemory(DirEntry->Name, FileName, NameLen);

                Result = ExtfsWriteInodeData(InodeContext, (PVOID)DirEntry, Start, NecessaryLength) == NecessaryLength;
            }
            goto result;
        }

        if (ReservedEntries > 0)
            ReservedEntries--;

        goto skip_entry;
skip_entry:
        Start.QuadPart += DirEntry->EntryLen;
    }

result:
    if (DirEntry)
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);
    if (SecondDirEntry)
        ExFreePoolWithTag(SecondDirEntry, EXTFS_TAG_BUFFER);

    if (Result)
    {
        DPRINT1("Added Inode from DirEntries\n");
    }

    ExReleaseResourceLite(&InodeContext->DirectoryDataLock);
    return Result;
}

PCHAR ExtfsResolveLinkedPath(
    PCHAR CurrentFilePath, ULONG CurrentFilePathIndex,
    PCHAR FilePath)
{
    ULONG FilePathStart = 0, FilePathIndex = 0, FilePathLen = strlen(FilePath);

    if (*FilePath == '\\' || *FilePath == '/')
        return FilePath;

    ULONG NameBufferSize = NameBufferSize = 256 + 1;
    PCHAR NameBuffer = ExAllocatePoolWithTag(NonPagedPool, NameBufferSize, EXTFS_TAG_BUFFER);
    if (!NameBuffer)
    {
        DPRINT1("Cannot allocate NameBuffer\n");
        return NULL;
    }

    while (TRUE)
    {
        CHAR Character = FilePath[FilePathIndex];

        if (Character == '\\' || Character == '/' || Character == '\0')
        {
            ULONG NameLen = FilePathIndex - FilePathStart;
            if (NameLen >= 256)
            {
                DPRINT1("Too big file name\n");
                ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);
                return NULL;
            }

            strncpy(NameBuffer, &FilePath[FilePathStart], NameLen);
            NameBuffer[NameLen] = '\0';

            if (Character == '\0' && !NameLen)
                break;

            if (!strcmp(NameBuffer, ".."))
            {
                if (CurrentFilePathIndex < 1)
                    break;

                if (CurrentFilePath[CurrentFilePathIndex] == '\\' || CurrentFilePath[CurrentFilePathIndex] == '/')
                    CurrentFilePathIndex--;

                while (CurrentFilePathIndex > 0)
                {
                    CHAR CurrentFilePathCharacter = CurrentFilePath[CurrentFilePathIndex];

                    if (CurrentFilePathCharacter == '\\' ||
                        CurrentFilePathCharacter == '/')
                    {
                        break;
                    }

                    CurrentFilePathIndex--;
                }
            }
            else if (strcmp(NameBuffer, "."))
            {
                CurrentFilePathIndex++;

                ULONG OutputNameLen = CurrentFilePathIndex + (FilePathLen - (FilePathIndex + 1));
                PCHAR OutputName = ExAllocatePoolWithTag(NonPagedPool, OutputNameLen + 1, EXTFS_TAG_BUFFER);
                if (!OutputName)
                {
                    DPRINT1("Cannot allocate OutputName\n");
                    break;
                }
                OutputName[CurrentFilePathIndex] = '\0';
                strncpy(OutputName, CurrentFilePath, CurrentFilePathIndex);
                strcat(OutputName, &FilePath[FilePathStart]);
                for (OutputNameLen = 0; OutputName[OutputNameLen]; OutputNameLen++)
                {
                    if (OutputName[OutputNameLen] == '/')
                        OutputName[OutputNameLen] = '\\';
                }

                ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);
                return OutputName;
            }

            if (Character == '\0')
                break;

            FilePathStart = FilePathIndex + 1;
        }

        FilePathIndex++;
    }

    ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);
    return NULL;
}

BOOLEAN ExtfsSeparateFileNameFromFilePath(PCHAR FilePath, PCHAR *OutputFilePath, PCHAR *OutputFileName)
{
    BOOLEAN Result = FALSE;
    ULONG NameBufferSize = 256 + 1;
    ULONG Length = strlen(FilePath), ClonedFilePathSize = Length + 1;
    ULONG Index = Length - 1, Start = Index;
    PCHAR ClonedFilePath = ExAllocatePoolWithTag(NonPagedPool, ClonedFilePathSize, EXTFS_TAG_BUFFER);
    PCHAR NameBuffer = ExAllocatePoolWithTag(NonPagedPool, NameBufferSize, EXTFS_TAG_BUFFER);
    if (!ClonedFilePath || !NameBuffer)
    {
        DPRINT1("Cannot allocate temporary buffers");
        goto result;
    }
    RtlCopyMemory(ClonedFilePath, FilePath, ClonedFilePathSize);

    while (TRUE)
    {
        CHAR Character = ClonedFilePath[Index];

        if (Character == '\\' || Character == '/')
        {
            ULONG NameLen = Start - Index;
            if (NameLen > 256)
            {
                DPRINT1("Too big file name\n");
                goto result;
            }

            if (NameLen)
            {
                RtlCopyMemory(NameBuffer, &ClonedFilePath[Index + 1], NameLen);
                NameBuffer[NameLen] = '\0';
                break;
            }

            Start = Index - 1;
        }

        if (Index < 1)
            break;

        Index--;
    }

    if (Index < 1)
        Index = 1;

    ClonedFilePath[Index] = '\0';
    Result = TRUE;
result:
    if (!Result)
    {
        if (ClonedFilePath)
            ExFreePoolWithTag(ClonedFilePath, EXTFS_TAG_BUFFER), ClonedFilePath = NULL;
        if (NameBuffer)
            ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER), NameBuffer = NULL;
    }

    *OutputFilePath = ClonedFilePath;
    *OutputFileName = NameBuffer;

    return Result;
}

BOOLEAN ExtfsRenameFileByPath(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PCHAR SourceFilePath, PCHAR DestinationFilePath,
    BOOLEAN ReplaceIfExists, BOOLEAN ResolveReparse, PNTSTATUS Status, PIRP Irp)
{
    BOOLEAN Result = FALSE;
    PEXTFS_INODE_CONTEXT DirectoryInodeContext = NULL, InodeContext = NULL, InodeContext2 = NULL;
    PCHAR OutputFilePath = NULL;
    PCHAR OutputFileName = NULL;
    NTSTATUS DummyStatus;
    if (!Status)
        Status = &DummyStatus;

    if (!ExtfsSeparateFileNameFromFilePath(DestinationFilePath, &OutputFilePath, &OutputFileName))
    {
        DPRINT1("Cannot split FileName from FilePath\n");
        *Status = STATUS_INSUFFICIENT_RESOURCES;
        goto result;
    }

    DirectoryInodeContext = ExtfsFindFileByPath(
        VolumeExtension, OutputFilePath, ResolveReparse, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION, FALSE,
        EXT_DIR_ENTRY_TYPE_UNKNOWN, Status, Irp);
    if (!DirectoryInodeContext || !DirectoryInodeContext->IsDirectory)
    {
        DPRINT1("Destination directory doesn't exists\n");
        *Status = STATUS_OBJECT_PATH_NOT_FOUND;
        goto result;
    }

    InodeContext = ExtfsFindFileByPath(
        VolumeExtension, SourceFilePath, ResolveReparse, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION, FALSE,
        EXT_DIR_ENTRY_TYPE_UNKNOWN, Status, Irp);
    InodeContext2 = ExtfsFindFileByPath(
        VolumeExtension, DestinationFilePath, ResolveReparse, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION, FALSE,
        EXT_DIR_ENTRY_TYPE_UNKNOWN, Status, Irp);
    if (!InodeContext)
    {
        DPRINT1("Cannot get InodeContext of \"%s\"\n", SourceFilePath);
        goto result;
    }
    if (InodeContext2)
    {
        if (!ReplaceIfExists)
        {
            DPRINT1("Avoid replacing existing file because ReplaceIfExists is FALSE\n");
            *Status = STATUS_OBJECT_NAME_EXISTS;
            goto result;
        }

        if (InodeContext->IsDirectory && InodeContext2->IsDirectory)
        {
            DPRINT1("Cannot replace existing directory\n");
            *Status = STATUS_FILE_IS_A_DIRECTORY;
            goto result;
        }

        if (InodeContext->IsDirectory != InodeContext2->IsDirectory)
        {
            DPRINT1("Cannot replace existing different file types\n");
            *Status = STATUS_OBJECT_TYPE_MISMATCH;
            goto result;
        }
    }

    ExtfsIncrementInodeContextLinkCount(InodeContext);
    if (InodeContext->IsDirectory)
        ExtfsRelinkParentDirectory(VolumeExtension, InodeContext, DirectoryInodeContext);

    (VOID)ExtfsDeleteFileByPath(VolumeExtension, SourceFilePath, ResolveReparse, Irp);
    (VOID)ExtfsDeleteFileByPath(VolumeExtension, DestinationFilePath, ResolveReparse, Irp);
    (VOID)ExtfsAddFileFromDirEntries(VolumeExtension, DirectoryInodeContext, InodeContext, OutputFileName);

    *Status = STATUS_SUCCESS;
    Result = TRUE;
result:
    ExtfsReleaseInodeContext(DirectoryInodeContext);
    ExtfsReleaseInodeContext(InodeContext2);
    ExtfsReleaseInodeContext(InodeContext);

    if (OutputFilePath)
        ExFreePoolWithTag(OutputFilePath, EXTFS_TAG_BUFFER);
    if (OutputFileName)
        ExFreePoolWithTag(OutputFileName, EXTFS_TAG_BUFFER);
    return Result;
}

BOOLEAN
ExtfsDeleteFileByPath(PEXTFS_VOLUME_EXTENSION VolumeExtension, PCHAR FilePath, BOOLEAN ResolveReparse, PIRP Irp)
{
    BOOLEAN Result = FALSE;
    PEXTFS_INODE_CONTEXT DirectoryInodeContext = NULL, InodeContext = NULL;
    PCHAR OutputFilePath = NULL;
    PCHAR OutputFileName = NULL;
    if (!ExtfsSeparateFileNameFromFilePath(FilePath, &OutputFilePath, &OutputFileName))
    {
        DPRINT1("Cannot split FileName from FilePath\n");
        goto result;
    }

    InodeContext = ExtfsFindFileByPath(
        VolumeExtension, FilePath, ResolveReparse, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION, FALSE,
        EXT_DIR_ENTRY_TYPE_UNKNOWN, NULL, Irp);
    if (!InodeContext)
    {
        DPRINT1("Cannot delete inexistent file\n");
        goto result;
    }

    DirectoryInodeContext = ExtfsFindFileByPath(
        VolumeExtension, OutputFilePath, ResolveReparse, EXTFS_DIRECTORY_MAX_REPARSE_RECURSION, FALSE,
        EXT_DIR_ENTRY_TYPE_UNKNOWN, NULL, Irp);
    if (DirectoryInodeContext)
    {
        ExtfsRemoveFileFromDirEntries(VolumeExtension, DirectoryInodeContext, OutputFileName);

        if (InodeContext->IsDirectory)
            ExtfsUninitializeDirectory(VolumeExtension, InodeContext, DirectoryInodeContext);
        else
            ExtfsRemoveInodeContext(InodeContext);
    }

    DPRINT1("\"%s\" will be deleted on \"%s\"\n", OutputFileName, OutputFilePath);

    Result = TRUE;
result:
    ExtfsReleaseInodeContext(DirectoryInodeContext);
    ExtfsReleaseInodeContext(InodeContext);

    if (OutputFilePath)
        ExFreePoolWithTag(OutputFilePath, EXTFS_TAG_BUFFER);
    if (OutputFileName)
        ExFreePoolWithTag(OutputFileName, EXTFS_TAG_BUFFER);
    return Result;
}

PEXTFS_INODE_CONTEXT ExtfsFindFileByPath(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PCHAR FilePath,
    BOOLEAN ResolveReparse, ULONG ReparseRecursion,
    BOOLEAN CreateFiles, UCHAR LastNameType, PNTSTATUS Status, PIRP Irp)
{
    ULONGLONG InodeNum = EXT_ROOT_INODE;
    PEXTFS_INODE_CONTEXT CurrentInodeContext = NULL, OutputInodeContext = NULL;
    ULONG Start = 0, Index = 0, Length = strlen(FilePath), NameBufferSize = 256 + 1;
    LARGE_INTEGER Offset = {0};
    PCHAR NameBuffer = NULL;
    BOOLEAN IsLastNameDirectory = LastNameType == EXT_DIR_ENTRY_TYPE_DIRECTORY;
    BOOLEAN IsAnyFileType = !LastNameType;
    NTSTATUS DummyStatus;
    if (!Status)
        Status = &DummyStatus;

    *Status = STATUS_SUCCESS;

    CurrentInodeContext = ExtfsReadInodeContext(VolumeExtension, InodeNum);
    if (!CurrentInodeContext)
    {
        DPRINT1("Cannot get root Inode context\n");
        *Status = STATUS_INSUFFICIENT_RESOURCES;
        goto error;
    }

    NameBuffer = ExAllocatePoolWithTag(NonPagedPool, NameBufferSize, EXTFS_TAG_BUFFER);
    if (!NameBuffer)
    {
        DPRINT1("Cannot allocate NameBuffer\n");
        *Status = STATUS_INSUFFICIENT_RESOURCES;
        goto error;
    }

    if (*FilePath == '\\' || *FilePath == '/')
    {
        Index++, Start++;
    }
    else
    {
        DPRINT1("It is necessary to have an initial \"\\\" or \"/\" prefix\n");
        *Status = STATUS_UNSUCCESSFUL;
        goto error;
    }

    while (TRUE)
    {
        CHAR Character = FilePath[Index];
        if (Character == '\\' || Character == '/' || Character == '\0')
        {
            ULONG NameLen = Index - Start;
            ULONG NextIndex = Index + 1;
            BOOLEAN IsLastName = NextIndex >= Length;

            if (NameLen >= 256)
            {
                DPRINT1("Too big file name\n");
                *Status = STATUS_NAME_TOO_LONG;
                goto error;
            }

            if (Character == '\0' && !NameLen)
                break;

            strncpy(NameBuffer, &FilePath[Start], NameLen);
            NameBuffer[NameLen] = '\0';

            if (!CurrentInodeContext->IsDirectory)
            {
                DPRINT1("File \"%s\" is not an directory in \"%.*s\"\n", NameBuffer, Start, FilePath);
                *Status = STATUS_NOT_A_DIRECTORY;
                goto error;
            }

            InodeNum = ExtfsFindFile(VolumeExtension, CurrentInodeContext, NameBuffer);
            if (CreateFiles && !InodeNum)
            {
                BOOLEAN WillNewInodeBeDirectory = !IsLastName || (IsLastName && IsLastNameDirectory);
                BOOLEAN IsDirectoryCreated = FALSE;
                OutputInodeContext = ExtfsCreateInodeContext(VolumeExtension);
                if (!OutputInodeContext)
                {
                    DPRINT1("Disk is full?\n");
                    *Status = STATUS_DISK_FULL;
                    goto error;
                }
                InodeNum = OutputInodeContext->InodeNum;

                if (WillNewInodeBeDirectory)
                {
                    IsDirectoryCreated = ExtfsInitializeDirectory(VolumeExtension, OutputInodeContext, CurrentInodeContext);
                }
                else
                {
                    ExtfsSetInodeContextType(OutputInodeContext, EXT_DIR_ENTRY_TYPE_REGULAR);
                    ExtfsIncrementInodeContextLinkCount(OutputInodeContext);
                }

                if (IsLastName && Irp)
                {
                    Irp->IoStatus.Information = FILE_CREATED;
                }

                if ((WillNewInodeBeDirectory && !IsDirectoryCreated) ||
                    !ExtfsAddFileFromDirEntries(VolumeExtension, CurrentInodeContext, OutputInodeContext, NameBuffer))
                {
                    DPRINT1("IsDirectoryCreated = %u\n", IsDirectoryCreated);
                    DPRINT1("Cannot add directory to the list\n");

                    if (OutputInodeContext->IsDirectory)
                    {
                        ExtfsUninitializeDirectory(VolumeExtension, OutputInodeContext, CurrentInodeContext);
                    }
                    else
                    {
                        ExtfsRemoveInodeContext(OutputInodeContext);
                        ExtfsRemoveInodeContext(OutputInodeContext);
                    }

                    *Status = STATUS_DISK_FULL;
                    goto error;
                }

                ExtfsReleaseInodeContext(OutputInodeContext);
            }
            else if (IsLastName)
            {
                if (CreateFiles)
                    *Status = STATUS_OBJECT_NAME_EXISTS;

                if (Irp)
                    Irp->IoStatus.Information = FILE_OPENED;
            }

            OutputInodeContext = ExtfsReadInodeContext(VolumeExtension, InodeNum);
            if (!OutputInodeContext)
            {
                DPRINT1("File \"%s\" not found in \"%.*s\"\n", NameBuffer, Start, FilePath);
                *Status = STATUS_OBJECT_PATH_NOT_FOUND;
                goto error;
            }
            ExtfsReleaseInodeContext(CurrentInodeContext);
            CurrentInodeContext = OutputInodeContext;

            if (CurrentInodeContext->IsReparsePoint && ReparseRecursion > 0 && ResolveReparse)
            {
                ULONG TempFilePathLen = (ULONG)CurrentInodeContext->FileSize;
                PCHAR TempFilePath = ExAllocatePoolWithTag(NonPagedPool, TempFilePathLen + 1, EXTFS_TAG_BUFFER);
                if (!TempFilePath)
                {
                    DPRINT1("Cannot allocate TempFilePath\n");
                    *Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto error;
                }
                PCHAR AbsoluteFilePath;

                ExtfsReadInodeData(CurrentInodeContext, TempFilePath, Offset, TempFilePathLen);
                TempFilePath[TempFilePathLen] = '\0';

                AbsoluteFilePath = ExtfsResolveLinkedPath(FilePath, Start - 1, TempFilePath);
                ExFreePoolWithTag(TempFilePath, EXTFS_TAG_BUFFER);
                if (!AbsoluteFilePath)
                {
                    DPRINT1("Cannot allocate AbsoluteFilePath\n");
                    *Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto error;
                }

                OutputInodeContext = ExtfsFindFileByPath(VolumeExtension,
                                                         AbsoluteFilePath, TRUE, ReparseRecursion - 1,
                                                         FALSE, EXT_DIR_ENTRY_TYPE_UNKNOWN, Status, Irp);
                ExtfsReleaseInodeContext(CurrentInodeContext);
                CurrentInodeContext = OutputInodeContext;

                ExFreePoolWithTag(AbsoluteFilePath, EXTFS_TAG_BUFFER);
                if (!OutputInodeContext)
                {
                    DPRINT1("File \"%s\" not found in \"%.*s\"\n", NameBuffer, Index + 1, FilePath);
                    goto error;
                }
            }
            else if (CurrentInodeContext->IsReparsePoint && ResolveReparse)
            {
                DPRINT1("There's still more to search\n");
                *Status = STATUS_TOO_MANY_LINKS;
                goto error;
            }

            Start = Index + 1;

            if (Character == '\0')
                break;
        }

        Index++;
    }

    if (CurrentInodeContext->IsDirectory != IsLastNameDirectory && !IsAnyFileType)
    {
        if (CurrentInodeContext->IsDirectory && !IsLastNameDirectory)
            *Status = STATUS_FILE_IS_A_DIRECTORY;
        else
            *Status = STATUS_NOT_A_DIRECTORY;

        DPRINT1("Unexpected Inode type\n");

        if (Irp)
            Irp->IoStatus.Information = 0;
        goto error;
    }

    ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);
    return CurrentInodeContext;

error:
    if (NameBuffer)
        ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);

    if (CurrentInodeContext == OutputInodeContext && CurrentInodeContext)
        ExtfsReleaseInodeContext(CurrentInodeContext), OutputInodeContext = NULL;
    else if (CurrentInodeContext)
        ExtfsReleaseInodeContext(CurrentInodeContext);

    if (OutputInodeContext)
        ExtfsReleaseInodeContext(OutputInodeContext);

    return NULL;
}

ULONG ExtfsGetEntrySizeByFileInformationClass(FILE_INFORMATION_CLASS FileInformationClass, ULONG WideNameLen)
{
    switch (FileInformationClass)
    {
    case FileDirectoryInformation:
        return sizeof(FILE_DIRECTORY_INFORMATION) + WideNameLen;
    case FileFullDirectoryInformation:
        return sizeof(FILE_FULL_DIR_INFORMATION) + WideNameLen;
    case FileBothDirectoryInformation:
        return sizeof(FILE_BOTH_DIR_INFORMATION) + WideNameLen;
    case FileIdBothDirectoryInformation:
        return sizeof(FILE_ID_BOTH_DIR_INFORMATION) + WideNameLen;
    default:
        break;
    }

    return 0;
}

ULONG ExtfsQueryDirectoryEntry(
    PEXTFS_FILE_STREAM FileStream, PVOID OutputList, ULONG SizeLimit,
    PUNICODE_STRING FilterName, BOOLEAN IsUsingWildcards,
    PNTSTATUS Status, FILE_INFORMATION_CLASS FileInformationClass)
{
    PEXTFS_FILE_CONTEXT FileContext = FileStream->FileContext;
    PEXTFS_INODE_CONTEXT InodeContext = FileContext->InodeContext;
    PEXT_DIR_ENTRY DirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DirEntry), EXTFS_TAG_BUFFER);
    CHAR DirName[257];
    PVOID CurrentEntry = (PVOID)((PCHAR)OutputList + FileStream->CurrentOutputDirectoryOffset);
    PVOID OldEntry = (PVOID)((PCHAR)OutputList + FileStream->OldOutputDirectoryOffset);
    ULONG DifferenceEntryOffset = (ULONG)((ULONG_PTR)CurrentEntry - (ULONG_PTR)OldEntry);
    ULONG BaseEntrySize = ExtfsGetEntrySizeByFileInformationClass(FileInformationClass, 0);
    LARGE_INTEGER Start = {0}, End = {0};
    ULONG Index;
    ULONG BytesRead;
    UNICODE_STRING FilterNameUpper = {0};

    ExAcquireResourceExclusiveLite(&InodeContext->DirectoryDataLock, TRUE);

    if (!DirEntry)
    {
        DPRINT1("Cannot allocate DirEntry\n");
        *Status = STATUS_INSUFFICIENT_RESOURCES;
        goto error;
    }

    if (!BaseEntrySize)
    {
        DPRINT1("Unimplemented FileInformationClass(%u)\n", FileInformationClass);
        *Status = STATUS_INVALID_INFO_CLASS;
        goto error;
    }

    if (!FileContext->InodeContext->IsDirectory)
    {
        DPRINT1("Cannot list non directory inode\n");
        *Status = STATUS_OBJECT_TYPE_MISMATCH;
        goto error;
    }

    Start.QuadPart = FileStream->CurrentDirectoryOffset;
    End.QuadPart = FileContext->InodeContext->FileSize;

    if (FilterName && FilterName->Buffer)
    {
        if (!NT_SUCCESS(RtlUpcaseUnicodeString(&FilterNameUpper, FilterName, TRUE)))
        {
            DPRINT1("Cannot convert filter name to upcase\n");
            *Status = STATUS_INSUFFICIENT_RESOURCES;
            goto error;
        }
    }

    *Status = STATUS_SUCCESS;

    while (Start.QuadPart < End.QuadPart)
    {
        BytesRead = ExtfsReadInodeData(FileContext->InodeContext,
                                       (PVOID)DirEntry, Start, sizeof(*DirEntry));
        if (!BytesRead)
        {
            DPRINT1("Insufficient bytes read\n");
            *Status = STATUS_DATA_ERROR;
            goto error;
        }
        if (!DirEntry->EntryLen)
        {
            DPRINT1("Infinity loop detected!\n");
            *Status = STATUS_UNSUCCESSFUL;
            goto error;
        }
        if (!DirEntry->Inode || !DirEntry->NameLen)
            goto L1_skip_entry;

        LARGE_INTEGER EntryFileSize, EntryAllocationSize;
        ULONG EntrySize = BaseEntrySize;
        PEXTFS_INODE_CONTEXT EntryInodeContext = ExtfsReadInodeContext(FileContext->InodeContext->VolumeExtension, DirEntry->Inode);
        if (!EntryInodeContext)
        {
            DPRINT1("Cannot get directory entry inode context\n");
            goto L1_skip_entry;
        }

        RtlCopyMemory(DirName, DirEntry->Name, DirEntry->NameLen);
        DirName[DirEntry->NameLen] = '\0';

        PUNICODE_STRING UnicodeString = ExtfsConvertUtf8ToUnicode(DirEntry->Name, DirEntry->NameLen);
        if (!UnicodeString)
        {
            ExtfsReleaseInodeContext(EntryInodeContext);
            *Status = STATUS_INSUFFICIENT_RESOURCES;
            goto error;
        }

        if (FilterName)
        {
            if (IsUsingWildcards ?
                    !FsRtlIsNameInExpression(&FilterNameUpper, UnicodeString, TRUE, NULL) :
                    RtlCompareUnicodeString(&FilterNameUpper, UnicodeString, TRUE))
            {
                DPRINT("\"%wZ\" skipped\n", UnicodeString);

                ExtfsFreeUnicodeString(UnicodeString);
                ExtfsReleaseInodeContext(EntryInodeContext);
                goto L1_skip_entry;
            }
        }

        EntrySize += UnicodeString->Length;
        EntrySize = ALIGN_UP_BY(EntrySize, 8);

        if (FileStream->CurrentOutputDirectoryOffset + EntrySize >= SizeLimit)
        {
            DPRINT1("Cannot fit the output directory entry\n");
            ExtfsFreeUnicodeString(UnicodeString);
            ExtfsReleaseInodeContext(EntryInodeContext);
            goto error;
        }

        DPRINT("\"%wZ\"\n", UnicodeString);

        LONGLONG CreationTime;
        LONGLONG LastAccessTime;
        LONGLONG LastWriteTime;
        LONGLONG ChangeTime;

        ExtfsGetTime(FileContext->InodeContext,
                     &CreationTime,
                     &LastAccessTime,
                     &LastWriteTime,
                     &ChangeTime);

        EntryFileSize.QuadPart = ExtfsGetInodeContextFileSize(EntryInodeContext);
        EntryAllocationSize.QuadPart = ExtfsGetInodeContextAllocationSize(EntryInodeContext);

        RtlZeroMemory(CurrentEntry, EntrySize);

        switch (FileInformationClass)
        {
        case FileDirectoryInformation:
        {
            PFILE_DIRECTORY_INFORMATION OutputCurrentEntry = CurrentEntry;
            PFILE_DIRECTORY_INFORMATION OutputOldEntry = OldEntry;

            OutputOldEntry->NextEntryOffset = DifferenceEntryOffset;

            OutputCurrentEntry->FileIndex = EntryInodeContext->InodeNum;

            OutputCurrentEntry->ChangeTime.QuadPart = CreationTime;
            OutputCurrentEntry->LastAccessTime.QuadPart = LastAccessTime;
            OutputCurrentEntry->LastWriteTime.QuadPart = LastWriteTime;
            OutputCurrentEntry->ChangeTime.QuadPart = ChangeTime;

            OutputCurrentEntry->FileAttributes = ExtfsGetFileTypeAttribute(EntryInodeContext);
            OutputCurrentEntry->EndOfFile = EntryFileSize;
            OutputCurrentEntry->AllocationSize = EntryAllocationSize;
            OutputCurrentEntry->FileNameLength = UnicodeString->Length;

            Index = 0;
            while (Index < UnicodeString->Length / sizeof(WCHAR))
                OutputCurrentEntry->FileName[Index] = UnicodeString->Buffer[Index], Index++;

            break;
        }

        case FileFullDirectoryInformation:
        {
            PFILE_FULL_DIR_INFORMATION OutputCurrentEntry = CurrentEntry;
            PFILE_FULL_DIR_INFORMATION OutputOldEntry = OldEntry;

            OutputOldEntry->NextEntryOffset = DifferenceEntryOffset;

            OutputCurrentEntry->FileIndex = EntryInodeContext->InodeNum;

            OutputCurrentEntry->ChangeTime.QuadPart = CreationTime;
            OutputCurrentEntry->LastAccessTime.QuadPart = LastAccessTime;
            OutputCurrentEntry->LastWriteTime.QuadPart = LastWriteTime;
            OutputCurrentEntry->ChangeTime.QuadPart = ChangeTime;

            OutputCurrentEntry->FileAttributes = ExtfsGetFileTypeAttribute(EntryInodeContext);
            OutputCurrentEntry->EndOfFile = EntryFileSize;
            OutputCurrentEntry->AllocationSize = EntryAllocationSize;
            OutputCurrentEntry->FileNameLength = UnicodeString->Length;

            Index = 0;
            while (Index < UnicodeString->Length / sizeof(WCHAR))
                OutputCurrentEntry->FileName[Index] = UnicodeString->Buffer[Index], Index++;

            break;
        }

        case FileBothDirectoryInformation:
        {
            PFILE_BOTH_DIR_INFORMATION OutputCurrentEntry = CurrentEntry;
            PFILE_BOTH_DIR_INFORMATION OutputOldEntry = OldEntry;

            OutputOldEntry->NextEntryOffset = DifferenceEntryOffset;

            OutputCurrentEntry->FileIndex = EntryInodeContext->InodeNum;

            OutputCurrentEntry->ChangeTime.QuadPart = CreationTime;
            OutputCurrentEntry->LastAccessTime.QuadPart = LastAccessTime;
            OutputCurrentEntry->LastWriteTime.QuadPart = LastWriteTime;
            OutputCurrentEntry->ChangeTime.QuadPart = ChangeTime;

            OutputCurrentEntry->FileAttributes = ExtfsGetFileTypeAttribute(EntryInodeContext);
            OutputCurrentEntry->EndOfFile = EntryFileSize;
            OutputCurrentEntry->AllocationSize = EntryAllocationSize;
            OutputCurrentEntry->FileNameLength = UnicodeString->Length;

            Index = 0;
            while (Index < UnicodeString->Length / sizeof(WCHAR))
                OutputCurrentEntry->FileName[Index] = UnicodeString->Buffer[Index], Index++;

            break;
        }

        case FileIdBothDirectoryInformation:
        {
            PFILE_ID_BOTH_DIR_INFORMATION OutputCurrentEntry = CurrentEntry;
            PFILE_ID_BOTH_DIR_INFORMATION OutputOldEntry = OldEntry;

            OutputOldEntry->NextEntryOffset = DifferenceEntryOffset;

            OutputCurrentEntry->FileIndex = EntryInodeContext->InodeNum;
            OutputCurrentEntry->FileId.QuadPart = EntryInodeContext->InodeNum;

            OutputCurrentEntry->ChangeTime.QuadPart = CreationTime;
            OutputCurrentEntry->LastAccessTime.QuadPart = LastAccessTime;
            OutputCurrentEntry->LastWriteTime.QuadPart = LastWriteTime;
            OutputCurrentEntry->ChangeTime.QuadPart = ChangeTime;

            OutputCurrentEntry->FileAttributes = ExtfsGetFileTypeAttribute(EntryInodeContext);
            OutputCurrentEntry->EndOfFile = EntryFileSize;
            OutputCurrentEntry->AllocationSize = EntryAllocationSize;
            OutputCurrentEntry->FileNameLength = UnicodeString->Length;

            Index = 0;
            while (Index < UnicodeString->Length / sizeof(WCHAR))
                OutputCurrentEntry->FileName[Index] = UnicodeString->Buffer[Index], Index++;

            break;
        }

        default:
            DPRINT1("Unimplemented FileInformationClass(%u)\n", FileInformationClass);
            ASSERT(FALSE);
            break;
        }

        FileStream->MatchedAnyEntry = TRUE;
        FileStream->OldOutputDirectoryOffset = FileStream->CurrentOutputDirectoryOffset;
        FileStream->CurrentOutputDirectoryOffset += EntrySize;

        Start.QuadPart += DirEntry->EntryLen;
        FileStream->CurrentDirectoryOffset = Start.QuadPart;

        RtlFreeUnicodeString(&FilterNameUpper);
        ExtfsFreeUnicodeString(UnicodeString);
        ExtfsReleaseInodeContext(EntryInodeContext);
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);
        ExReleaseResourceLite(&InodeContext->DirectoryDataLock);
        return EntrySize;

L1_skip_entry:
        Start.QuadPart += DirEntry->EntryLen;
        FileStream->CurrentDirectoryOffset = Start.QuadPart;
    }

    if (!FileStream->CurrentOutputDirectoryOffset)
        *Status = STATUS_NO_MORE_FILES;

error:
    RtlFreeUnicodeString(&FilterNameUpper);
    if (DirEntry)
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);

    ExReleaseResourceLite(&InodeContext->DirectoryDataLock);
    return 0;
}
