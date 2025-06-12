#include "extfs.h"

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
        if (!DirEntry->NameLen)
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

//ULONG ExtfsResolveParentPath(PCHAR FilePath, ULONG Index)
//{
//    ULONG Start = Index;
//
//    if (Index < 1)
//    {
//        return 0;
//    }
//    else
//    {
//        while (TRUE)
//        {
//            CHAR Character = FilePath[Index];
//
//
//        }
//    }
//}

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
                return NULL;
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

PEXTFS_INODE_CONTEXT ExtfsFindFileByPath(
    PEXTFS_VOLUME_EXTENSION VolumeExtension,
    PCHAR FilePath,
    BOOLEAN ResolveReparse, ULONG ReparseRecursion)
{
    ULONG InodeNum = EXT_ROOT_INODE;
    PEXTFS_INODE_CONTEXT CurrentInodeContext = ExtfsReadInodeContext(VolumeExtension, InodeNum), OutputInodeContext;
    ULONG Start = 0, Index = 0, Length = strlen(FilePath), NameBufferSize = 256 + 1;
    LARGE_INTEGER Offset = {0};
    PCHAR NameBuffer = ExAllocatePoolWithTag(NonPagedPool, NameBufferSize, EXTFS_TAG_BUFFER);
    if (!NameBuffer)
    {
        DPRINT1("Cannot allocate NameBuffer\n");
        return NULL;
    }

    if (!CurrentInodeContext)
    {
        DPRINT1("Cannot get root Inode context\n");
        return NULL;
    }

    if (*FilePath == '\\' || *FilePath == '/')
    {
        Index++, Start++;
    }
    else
    {
        DPRINT1("It is necessary to have an initial \"\\\" or \"/\" prefix\n");
        ExtfsReleaseInodeContext(CurrentInodeContext);
        return NULL;
    }

    if (Index >= Length)
    {
        ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);
        OutputInodeContext =
            ExtfsReadInodeContext(VolumeExtension, ExtfsFindFile(VolumeExtension, CurrentInodeContext, "."));
        ExtfsReleaseInodeContext(CurrentInodeContext);
        return OutputInodeContext;
    }

    while (TRUE)
    {
        CHAR Character = FilePath[Index];
        if (Character == '\\' || Character == '/' || Character == '\0')
        {
            ULONG NameLen = Index - Start;

            if (NameLen >= 256)
            {
                DPRINT1("Too big file name\n");
                ExtfsReleaseInodeContext(CurrentInodeContext);
                ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);
                return NULL;
            }

            if (Character == '\0' && !NameLen)
                break;

            strncpy(NameBuffer, &FilePath[Start], NameLen);
            NameBuffer[NameLen] = '\0';

            OutputInodeContext = 
                ExtfsReadInodeContext(VolumeExtension, ExtfsFindFile(VolumeExtension, CurrentInodeContext, NameBuffer));
            if (!OutputInodeContext)
            {
                DPRINT1("File \"%s\" not found in \"%.*s\"\n", NameBuffer, Index + 1, FilePath);
                ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);
                return NULL;
            }
            ExtfsReleaseInodeContext(CurrentInodeContext);
            CurrentInodeContext = OutputInodeContext;

            if (CurrentInodeContext->IsReparsePoint && ReparseRecursion > 0 && ResolveReparse)
            {
                ULONG TempFilePathLen = (ULONG)min(4096, CurrentInodeContext->FileSize);
                PCHAR TempFilePath = ExAllocatePoolWithTag(NonPagedPool, TempFilePathLen + 1, EXTFS_TAG_BUFFER);
                if (!TempFilePath)
                {
                    DPRINT1("Cannot allocate TempFilePath\n");
                    ExtfsReleaseInodeContext(CurrentInodeContext);
                    return NULL;
                }
                PCHAR AbsoluteFilePath;

                ExtfsReadInodeData(CurrentInodeContext, TempFilePath, Offset, TempFilePathLen);
                TempFilePath[TempFilePathLen] = '\0';

                AbsoluteFilePath = ExtfsResolveLinkedPath(FilePath, Start - 1, TempFilePath);
                ExFreePoolWithTag(TempFilePath, EXTFS_TAG_BUFFER);
                if (!AbsoluteFilePath)
                {
                    DPRINT1("Cannot allocate AbsoluteFilePath\n");
                    ExtfsReleaseInodeContext(CurrentInodeContext);
                    return NULL;
                }

                DPRINT1("%s\n", AbsoluteFilePath);

                OutputInodeContext = ExtfsFindFileByPath(VolumeExtension, AbsoluteFilePath, TRUE, ReparseRecursion - 1);
                ExtfsReleaseInodeContext(CurrentInodeContext);
                CurrentInodeContext = OutputInodeContext;

                if (!OutputInodeContext)
                {
                    DPRINT1("File \"%s\" not found in \"%.*s\"\n", NameBuffer, Index + 1, FilePath);
                    ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);
                    ExFreePoolWithTag(AbsoluteFilePath, EXTFS_TAG_BUFFER);
                    return NULL;
                }
            }
            else if (CurrentInodeContext->IsReparsePoint)
            {
                DPRINT1("There's still more to search\n");
                ExtfsReleaseInodeContext(CurrentInodeContext);
                CurrentInodeContext = NULL;
                break;
            }

            InodeNum = CurrentInodeContext->InodeNum;
            Start = Index + 1;

            if (Character == '\0')
                break;
        }

        Index++;
    }

    ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);
    return CurrentInodeContext;
}

ULONG ExtfsQueryDirectoryListFBD(PEXTFS_FILE_STREAM FileStream, PFILE_BOTH_DIR_INFORMATION OutputList, ULONG SizeLimit, PCHAR FilterName)
{
    PEXTFS_FILE_CONTEXT FileContext = FileStream->FileContext;
    PEXT_DIR_ENTRY DirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DirEntry), EXTFS_TAG_BUFFER);
    CHAR DirName[257];
    PFILE_BOTH_DIR_INFORMATION 
        CurrentEntry = (PVOID)((PCHAR)OutputList + FileStream->CurrentFBDOffset),
        OldEntry = (PVOID)((PCHAR)OutputList + FileStream->OldFBDOffset);
    LARGE_INTEGER Start = {0}, End = {0};
    ULONG Index;
    ULONG BytesRead;
    if (!DirEntry)
    {
        DPRINT1("Cannot allocate DirEntry\n");
        goto error;
    }

    if (!FileContext->InodeContext->IsDirectory)
        goto error;

    Start.QuadPart = FileStream->CurrentDirectoryOffset;
    End.QuadPart = FileContext->InodeContext->FileSize;

    while (Start.QuadPart < End.QuadPart)
    {
        BytesRead = ExtfsReadInodeData(FileContext->InodeContext,
                                       (PVOID)DirEntry, Start, sizeof(*DirEntry));
        if (!BytesRead)
            goto error;
        if (!DirEntry->EntryLen)
        {
            DPRINT1("Infinity loop detected!\n");
            goto error;
        }
        if (!DirEntry->NameLen)
            goto L1_skip_entry;

        LARGE_INTEGER EntryFileSize;
        ULONG EntrySize = FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName) + (DirEntry->NameLen * sizeof(WCHAR));
        PEXTFS_INODE_CONTEXT EntryInodeContext = ExtfsReadInodeContext(FileContext->InodeContext->VolumeExtension, DirEntry->Inode);
        if (FileStream->CurrentFBDOffset + EntrySize >= SizeLimit)
        {
            DPRINT1("Truncated directory!!!\n");
            goto error;
        }

        if (!EntryInodeContext)
        {
            DPRINT1("Cannot get directory entry inode context\n");
            goto error;
        }

        RtlCopyMemory(DirName, DirEntry->Name, DirEntry->NameLen);
        DirName[DirEntry->NameLen] = '\0';

        if (FilterName && !ExtfsMatchExpressionA(FilterName, DirName, TRUE))
            goto L1_skip_entry;

        LONGLONG CreationTime = -1;
        LONGLONG LastAccessTime = UnixTimeToWindowsTime(EntryInodeContext->Inode.Atime);
        LONGLONG LastWriteTime = UnixTimeToWindowsTime(EntryInodeContext->Inode.Mtime);
        LONGLONG ChangeTime = UnixTimeToWindowsTime(EntryInodeContext->Inode.Ctime);

        EntryFileSize.QuadPart = EntryInodeContext->FileSize;

        RtlZeroMemory(CurrentEntry, sizeof(*CurrentEntry));

        OldEntry->NextEntryOffset = (ULONG)((ULONG_PTR)CurrentEntry - (ULONG_PTR)OldEntry);

        CurrentEntry->ChangeTime.QuadPart = CreationTime;
        CurrentEntry->LastAccessTime.QuadPart = LastAccessTime;
        CurrentEntry->LastWriteTime.QuadPart = LastWriteTime;
        CurrentEntry->ChangeTime.QuadPart = ChangeTime;

        CurrentEntry->FileIndex = OldEntry->FileIndex + 1;
        CurrentEntry->FileAttributes |=
            EntryInodeContext->IsDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        CurrentEntry->AllocationSize = CurrentEntry->EndOfFile = EntryFileSize;
        CurrentEntry->AllocationSize.QuadPart =
            ALIGN_UP_BY(CurrentEntry->AllocationSize.QuadPart, FileContext->InodeContext->VolumeExtension->BlockSize);
        CurrentEntry->FileNameLength = DirEntry->NameLen * sizeof(WCHAR);
        for (Index = 0; Index < DirEntry->NameLen; Index++)
            CurrentEntry->FileName[Index] = DirEntry->Name[Index];

        FileStream->OldFBDOffset = FileStream->CurrentFBDOffset;
        FileStream->CurrentFBDOffset += EntrySize;

        Start.QuadPart += DirEntry->EntryLen;
        FileStream->CurrentDirectoryOffset = Start.QuadPart;

        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);
        return EntrySize;

L1_skip_entry:
        Start.QuadPart += DirEntry->EntryLen;
    }

error:
    if (DirEntry)
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);

    return 0;
}

ULONG ExtfsQueryDirectoryListFD(PEXTFS_FILE_STREAM FileStream, PFILE_DIRECTORY_INFORMATION OutputList, ULONG SizeLimit, PCHAR FilterName)
{
    PEXTFS_FILE_CONTEXT FileContext = FileStream->FileContext;
    PEXT_DIR_ENTRY DirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DirEntry), EXTFS_TAG_BUFFER);
    CHAR DirName[257];
    PFILE_DIRECTORY_INFORMATION 
        CurrentEntry = (PVOID)((PCHAR)OutputList + FileStream->CurrentFBDOffset),
        OldEntry = (PVOID)((PCHAR)OutputList + FileStream->OldFBDOffset);
    LARGE_INTEGER Start = {0}, End = {0};
    ULONG Index;
    ULONG BytesRead;
    if (!DirEntry)
    {
        DPRINT1("Cannot allocate DirEntry\n");
        goto error;
    }

    if (!FileContext->InodeContext->IsDirectory)
        goto error;

    Start.QuadPart = FileStream->CurrentDirectoryOffset;
    End.QuadPart = FileContext->InodeContext->FileSize;

    while (Start.QuadPart < End.QuadPart)
    {
        BytesRead = ExtfsReadInodeData(FileContext->InodeContext,
                                       (PVOID)DirEntry, Start, sizeof(*DirEntry));
        if (!BytesRead)
            goto error;
        if (!DirEntry->EntryLen)
        {
            DPRINT1("Infinity loop detected!\n");
            goto error;
        }
        if (!DirEntry->NameLen)
            goto L1_skip_entry;

        LARGE_INTEGER EntryFileSize;
        ULONG EntrySize = FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName) + (DirEntry->NameLen * sizeof(WCHAR));
        PEXTFS_INODE_CONTEXT EntryInodeContext = ExtfsReadInodeContext(FileContext->InodeContext->VolumeExtension, DirEntry->Inode);
        if (FileStream->CurrentFBDOffset + EntrySize >= SizeLimit)
        {
            DPRINT1("Truncated directory!!!\n");
            goto error;
        }

        if (!EntryInodeContext)
        {
            DPRINT1("Cannot get directory entry inode context\n");
            goto error;
        }

        RtlCopyMemory(DirName, DirEntry->Name, DirEntry->NameLen);
        DirName[DirEntry->NameLen] = '\0';

        if (FilterName && !ExtfsMatchExpressionA(FilterName, DirName, TRUE))
            goto L1_skip_entry;

        LONGLONG CreationTime = -1;
        LONGLONG LastAccessTime = UnixTimeToWindowsTime(EntryInodeContext->Inode.Atime);
        LONGLONG LastWriteTime = UnixTimeToWindowsTime(EntryInodeContext->Inode.Mtime);
        LONGLONG ChangeTime = UnixTimeToWindowsTime(EntryInodeContext->Inode.Ctime);

        EntryFileSize.QuadPart = EntryInodeContext->FileSize;

        RtlZeroMemory(CurrentEntry, sizeof(*CurrentEntry));

        OldEntry->NextEntryOffset = (ULONG)((ULONG_PTR)CurrentEntry - (ULONG_PTR)OldEntry);

        CurrentEntry->ChangeTime.QuadPart = CreationTime;
        CurrentEntry->LastAccessTime.QuadPart = LastAccessTime;
        CurrentEntry->LastWriteTime.QuadPart = LastWriteTime;
        CurrentEntry->ChangeTime.QuadPart = ChangeTime;

        CurrentEntry->FileIndex = OldEntry->FileIndex + 1;
        CurrentEntry->FileAttributes |=
            EntryInodeContext->IsDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        CurrentEntry->AllocationSize = CurrentEntry->EndOfFile = EntryFileSize;
        CurrentEntry->AllocationSize.QuadPart =
            ALIGN_UP_BY(CurrentEntry->AllocationSize.QuadPart, FileContext->InodeContext->VolumeExtension->BlockSize);
        CurrentEntry->FileNameLength = DirEntry->NameLen * sizeof(WCHAR);
        for (Index = 0; Index < DirEntry->NameLen; Index++)
            CurrentEntry->FileName[Index] = DirEntry->Name[Index];

        FileStream->OldFBDOffset = FileStream->CurrentFBDOffset;
        FileStream->CurrentFBDOffset += EntrySize;

        Start.QuadPart += DirEntry->EntryLen;
        FileStream->CurrentDirectoryOffset = Start.QuadPart;

        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);
        return EntrySize;

L1_skip_entry:
        Start.QuadPart += DirEntry->EntryLen;
    }

error:
    if (DirEntry)
        ExFreePoolWithTag(DirEntry, EXTFS_TAG_BUFFER);

    return 0;
}
