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

PEXTFS_INODE_CONTEXT ExtfsFindFileByPath(PEXTFS_VOLUME_EXTENSION VolumeExtension, PCHAR FilePath)
{
    ULONG InodeNum = EXT_ROOT_INODE;
    PEXTFS_INODE_CONTEXT CurrentInodeContext = ExtfsReadInodeContext(VolumeExtension, InodeNum), OutputInodeContext;
    ULONG Start = 0, Index = 0, Length = strlen(FilePath), NameBufferSize = 256 + 1;
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

    if (*FilePath == '\\')
        Index++, Start++;
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
        if (Character == '\\' || Character == '\0')
        {
            ULONG NameLen = Index - Start;

            if (NameLen >= 256)
            {
                DPRINT1("Too big file name\n");
                ExtfsReleaseInodeContext(CurrentInodeContext);
                ExFreePoolWithTag(NameBuffer, EXTFS_TAG_BUFFER);
                return NULL;
            }

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

ULONG ExtfsQueryDirectoryList(PEXTFS_FILE_CONTEXT FileContext, PFILE_BOTH_DIR_INFORMATION OutputList, ULONG SizeLimit)
{
    PEXT_DIR_ENTRY DirEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DirEntry), EXTFS_TAG_BUFFER);
    PFILE_BOTH_DIR_INFORMATION 
        CurrentEntry = (PVOID)((PCHAR)OutputList + FileContext->CurrentFBDOffset),
        OldEntry = (PVOID)((PCHAR)OutputList + FileContext->OldFBDOffset);
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

    Start.QuadPart = FileContext->CurrentDirectoryOffset;
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
        ULONG EntrySize = sizeof(*OutputList) + ((DirEntry->NameLen - 1) * sizeof(OutputList->FileName[0]));
        PEXTFS_INODE_CONTEXT EntryInodeContext = ExtfsReadInodeContext(FileContext->InodeContext->VolumeExtension, DirEntry->Inode);
        if (FileContext->CurrentFBDOffset + EntrySize >= SizeLimit)
        {
            DPRINT1("Truncated directory!!!\n");
            goto error;
        }

        if (!EntryInodeContext)
        {
            DPRINT1("Cannot get directory entry inode context\n");
            goto error;
        }

        EntryFileSize.QuadPart = EntryInodeContext->FileSize;

        RtlZeroMemory(CurrentEntry, sizeof(*CurrentEntry));

        OldEntry->NextEntryOffset = (ULONG)((ULONG_PTR)CurrentEntry - (ULONG_PTR)OldEntry);

        CurrentEntry->FileAttributes |=
            EntryInodeContext->IsDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        CurrentEntry->AllocationSize = CurrentEntry->EndOfFile = EntryFileSize;
        CurrentEntry->NextEntryOffset = EntrySize;
        CurrentEntry->FileNameLength = DirEntry->NameLen;
        for (Index = 0; Index < DirEntry->NameLen; Index++)
            CurrentEntry->FileName[Index] = DirEntry->Name[Index];

        FileContext->OldFBDOffset = FileContext->CurrentFBDOffset;
        FileContext->CurrentFBDOffset += EntrySize;

        Start.QuadPart += DirEntry->EntryLen;
        FileContext->CurrentDirectoryOffset = Start.QuadPart;

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
