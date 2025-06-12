#include <extfs.h>

#define BYTES_TO_BITS(x) (x * 8)

UCHAR UuidDigitsList[5] = {
    8, 4, 4, 4, 12
};

VOID _ExtfsInitListEntry(PEXTFS_LIST_ENTRY Entry, LONG StructOffset)
{
    ASSERT(Entry != NULL);

    RtlZeroMemory(Entry, sizeof(*Entry));

    Entry->StructOffset = StructOffset;
    Entry->Prev = Entry;
    Entry->Next = Entry;
}

PVOID ExtfsGetListEntryStructure(PEXTFS_LIST_ENTRY Entry)
{
    ASSERT(Entry != NULL);
    ASSERT(Entry->Prev != NULL);
    ASSERT(Entry->Next != NULL);

    return (PCHAR)Entry + Entry->StructOffset;
}

VOID ExtfsInsertTailList(PEXTFS_LIST_ENTRY Head, PEXTFS_LIST_ENTRY ListToInsert)
{
    PEXTFS_LIST_ENTRY StartHead = Head;
    PEXTFS_LIST_ENTRY EndHead = StartHead ? StartHead->Prev : NULL;

    PEXTFS_LIST_ENTRY StartList = ListToInsert;
    PEXTFS_LIST_ENTRY EndList = StartList ? StartList->Prev : NULL;

    ASSERT(StartHead != NULL);
    ASSERT(EndHead != NULL);

    ASSERT(StartList != NULL);
    ASSERT(EndList != NULL);

    EndHead->Next = StartList;
    StartList->Prev = EndHead;

    StartHead->Prev = EndList;
    EndList->Next = StartHead;
}

VOID ExtfsInsertBodyList(PEXTFS_LIST_ENTRY Body, PEXTFS_LIST_ENTRY ListToInsert)
{
    PEXTFS_LIST_ENTRY Prev;
    PEXTFS_LIST_ENTRY Next;

    PEXTFS_LIST_ENTRY StartList = ListToInsert;
    PEXTFS_LIST_ENTRY EndList = StartList ? StartList->Prev : NULL;

    ASSERT(Body != NULL);
    ASSERT(StartList != NULL);
    ASSERT(EndList != NULL);

    Prev = Body->Prev;
    Next = Body->Next;

    ASSERT(Prev != NULL);
    ASSERT(Next != NULL);

    Prev->Next = StartList;
    StartList->Prev = Prev;

    Next->Prev = EndList;
    EndList->Next = Next;
}

VOID ExtfsRemoveEntryList(PEXTFS_LIST_ENTRY Entry)
{
    PEXTFS_LIST_ENTRY Prev;
    PEXTFS_LIST_ENTRY Next;

    ASSERT(Entry != NULL);

    Prev = Entry->Prev;
    Next = Entry->Next;

    if (Prev && Next)
    {
        Prev->Next = Next;
        Next->Prev = Prev;

        Prev = Entry->Prev = Entry;
        Next = Entry->Next = Entry;
    }

    ASSERT(Prev == Entry);
    ASSERT(Next == Entry);
}

LONGLONG ExtfsGetSystemTime()
{
    LARGE_INTEGER Time;

    KeQuerySystemTime(&Time);
    return Time.QuadPart;
}

ULONG ExtfsBitmapGetSize(ULONG BitmapEntries)
{
    ULONG Bits = 8;

    return (BitmapEntries + (Bits - 1)) / Bits;
}

BOOLEAN ExtfsBitmapSet(PUCHAR Bitmap, ULONG BitmapEntries, ULONG EntryIndex)
{
    ULONG Index = EntryIndex / BYTES_TO_BITS(sizeof(*Bitmap));
    ULONG Offset = EntryIndex % BYTES_TO_BITS(sizeof(*Bitmap));
    UCHAR Value = 1 << Offset;

    if (EntryIndex >= BitmapEntries)
        return FALSE;

    Bitmap[Index] |= Value;
    return TRUE;
}

BOOLEAN ExtfsBitmapGet(PUCHAR Bitmap, ULONG BitmapEntries, ULONG EntryIndex)
{
    ULONG Index = EntryIndex / BYTES_TO_BITS(sizeof(*Bitmap));
    ULONG Offset = EntryIndex % BYTES_TO_BITS(sizeof(*Bitmap));
    UCHAR Value = 1 << Offset;

    if (EntryIndex >= BitmapEntries)
        return FALSE;

    return !!(Bitmap[Index] & Value);
}

BOOLEAN ExtfsBitmapClear(PUCHAR Bitmap, ULONG BitmapEntries, ULONG EntryIndex)
{
    ULONG Index = EntryIndex / BYTES_TO_BITS(sizeof(*Bitmap));
    ULONG Offset = EntryIndex % BYTES_TO_BITS(sizeof(*Bitmap));
    UCHAR Value = 1 << Offset;

    if (EntryIndex >= BitmapEntries)
        return FALSE;

    Bitmap[Index] &= ~Value;
    return TRUE;
}

ULONG ExtfsBitmapGetFilteredEntries(PUCHAR Bitmap, ULONG BitmapEntries, BOOLEAN IsFree)
{
    ULONG Index;
    UCHAR Offset;
    ULONG Entries = 0;
    ULONG FinalIndex = ExtfsBitmapGetSize(BitmapEntries);
    ULONG FinalOffset = BitmapEntries % BYTES_TO_BITS(sizeof(*Bitmap));
    if (!FinalOffset)
        FinalOffset = BYTES_TO_BITS(sizeof(*Bitmap));

    for (Index = 0; Index < FinalIndex; Index++)
    {
        UCHAR MaxOffset = Index == FinalIndex - 1 ? FinalOffset : BYTES_TO_BITS(sizeof(*Bitmap));
        UCHAR Value = IsFree ? (~Bitmap[Index]) : (Bitmap[Index]);
        if (!Value)
            continue;

        for (Offset = 0; Offset < MaxOffset; Offset++)
        {
            UCHAR Bit = 1 << Offset;

            if (Value & Bit)
                Entries++;
        }
    }

    return Entries;
}

ULONG ExtfsBitmapGetUsedEntries(PUCHAR Bitmap, ULONG BitmapEntries)
{
    return ExtfsBitmapGetFilteredEntries(Bitmap, BitmapEntries, FALSE);
}

ULONG ExtfsBitmapGetFreeEntries(PUCHAR Bitmap, ULONG BitmapEntries)
{
    return ExtfsBitmapGetFilteredEntries(Bitmap, BitmapEntries, TRUE);
}

ULONG ExtfsBitmapGetFreeEntry(PUCHAR Bitmap, ULONG BitmapEntries)
{
    ULONG Index;
    UCHAR Offset;
    ULONG FinalIndex = ExtfsBitmapGetSize(BitmapEntries);
    ULONG FinalOffset = BitmapEntries % BYTES_TO_BITS(sizeof(*Bitmap));
    if (!FinalOffset)
        FinalOffset = BYTES_TO_BITS(sizeof(*Bitmap));

    for (Index = 0; Index < FinalIndex; Index++)
    {
        UCHAR MaxOffset = Index == FinalIndex - 1 ? FinalOffset : BYTES_TO_BITS(sizeof(*Bitmap));
        UCHAR Value = ~Bitmap[Index];
        if (!Value)
            continue;

        for (Offset = 0; Offset < MaxOffset; Offset++)
        {
            UCHAR Bit = 1 << Offset;

            if (Value & Bit)
                return (Index * BYTES_TO_BITS(sizeof(*Bitmap))) + Offset;
        }
    }

    return (ULONG)-1;
}

NTSTATUS ExtfsAcquireFreeVolumeNumber(PEXTFS_GLOBAL_DATA GlobalData, PULONG OutputNumber)
{
    PUCHAR Bitmap = GlobalData->MountedVolumeBitmap;
    ULONG BitmapEntries = GlobalData->MountedVolumeBitmapEntries;
    ULONG FreeEntry;
    NTSTATUS Status = STATUS_SUCCESS;

    ExAcquireResourceExclusiveLite(&GlobalData->MountedVolumeBitmapLock, TRUE);

    FreeEntry = ExtfsBitmapGetFreeEntry(Bitmap, BitmapEntries);
    if (FreeEntry == (ULONG)-1)
    {
        Status = STATUS_UNSUCCESSFUL;
        goto result;
    }

    *OutputNumber = FreeEntry;
result:
    ExReleaseResourceLite(&GlobalData->MountedVolumeBitmapLock);
    return Status;
}

NTSTATUS ExtfsReleaseVolumeNumber(PEXTFS_GLOBAL_DATA GlobalData, ULONG InputNumber)
{
    PUCHAR Bitmap = GlobalData->MountedVolumeBitmap;
    ULONG BitmapEntries = GlobalData->MountedVolumeBitmapEntries;
    ULONG FreeEntry;
    NTSTATUS Status = STATUS_SUCCESS;

    ExAcquireResourceExclusiveLite(&GlobalData->MountedVolumeBitmapLock, TRUE);

    if (InputNumber >= BitmapEntries)
    {
        Status = STATUS_UNSUCCESSFUL;
        goto result;
    }

    ExtfsBitmapClear(Bitmap, BitmapEntries, InputNumber);
result:
    ExReleaseResourceLite(&GlobalData->MountedVolumeBitmapLock);
    return Status;
}

ULONGLONG ExtfsReadPointerLittleEndian(PVOID Pointer, ULONG Size)
{
    PUCHAR PointerByte = Pointer;
    ULONGLONG Value = 0;
    ULONGLONG Bit;
    for (Bit = 0; Bit < Size * 8; Bit += 8)
        Value |= (ULONGLONG)(*PointerByte++) << Bit;

    return Value;
}

VOID ExtfsWritePointerLittleEndian(PVOID Pointer, ULONG Size, ULONGLONG Value)
{
    PUCHAR PointerByte = Pointer;
    ULONGLONG Bit;
    for (Bit = 0; Bit < Size * 8; Bit += 8)
        *PointerByte++ = (Value >> Bit) & 0xFF;
}

VOID ExtfsFreeDuplicatedUnicodeString(PUNICODE_STRING Destination)
{
    if (Destination &&
        Destination->Buffer)
    {
        ExFreePool(Destination->Buffer);
        Destination->Buffer = NULL;
    }
}

NTSTATUS ExtfsDuplicateUnicodeString(PUNICODE_STRING Destination, PUNICODE_STRING Source)
{
    Destination->Length = Source->Length;
    Destination->MaximumLength = Source->MaximumLength;

    Destination->Buffer = ExAllocatePool(NonPagedPool, Destination->MaximumLength);
    if (!Destination->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Destination->Buffer, Source->Buffer, Destination->MaximumLength);
    return STATUS_SUCCESS;
}

ULONG ExtfsGetUtf8Count(PCHAR String, ULONG Length)
{
    ULONG Index = 0, Output = 0;
    while (Index < Length && String[Index])
    {
        UCHAR Character = String[Index];

        if (Character < 0x80)
        {
            Index++;
        }
        else if ((Character & 0xE0) == 0xC0)
        {
            if (Index + 1 >= Length)
                return 0;
            if ((String[Index + 1] & 0xC0) != 0x80)
                return 0;
            Index += 2;
        }
        else if ((Character & 0xF0) == 0xE0)
        {
            if (Index + 2 >= Length)
                return 0;
            if ((String[Index + 1] & 0xC0) != 0x80)
                return 0;
            if ((String[Index + 2] & 0xC0) != 0x80)
                return 0;
            Index += 3;
        }
        else if ((Character & 0xF8) == 0xF0)
        {
            if (Index + 3 >= Length)
                return 0;
            if ((String[Index + 1] & 0xC0) != 0x80)
                return 0;
            if ((String[Index + 2] & 0xC0) != 0x80)
                return 0;
            if ((String[Index + 3] & 0xC0) != 0x80)
                return 0;
            Index += 4;
        }
        else
        {
            return 0;
        }

        Output++;
    }

    return Output;
}

VOID ExtfsFreeUnicodeString(PUNICODE_STRING UnicodeString)
{
    if (!UnicodeString)
        return;

    if (UnicodeString->Buffer)
        ExFreePool(UnicodeString->Buffer);

    ExFreePool(UnicodeString);
}

PUNICODE_STRING ExtfsConvertUtf8ToUnicode(PCHAR String, ULONG Length)
{
    PUNICODE_STRING UnicodeString = ExAllocatePool(NonPagedPool, sizeof(*UnicodeString));
    if (!UnicodeString)
    {
        return NULL;
    }

    ULONG WCount = ExtfsGetUtf8Count(String, Length), WSize = (WCount + 1) * sizeof(WCHAR);
    PWCHAR WString = WCount ? ExAllocatePool(NonPagedPool, WSize) : NULL;
    if (!WString)
    {
        ExFreePool(UnicodeString);
        return NULL;
    }
    RtlZeroMemory(WString, WSize);

    ULONG Index = 0, Output = 0;

    while (Index < Length && String[Index])
    {
        UCHAR Character = String[Index];

        if (Character < 0x80)
        {
            WString[Output] = Character;
            Index++;
        }
        else if ((Character & 0xE0) == 0xC0)
        {
            WCHAR WCharacter = ((Character & 0x1F) << 6) |
                               (String[Index + 1] & 0x3F);
            WString[Output] = WCharacter;
            Index += 2;
        }
        else if ((Character & 0xF0) == 0xE0)
        {
            WCHAR WCharacter = ((Character & 0x0F) << 12) |
                               ((String[Index + 1] & 0x3F) << 6) |
                               (String[Index + 2] & 0x3F);
            WString[Output] = WCharacter;
            Index += 3;
        }
        else if ((Character & 0xF8) == 0xF0)
        {
            ULONG LCharacter = ((Character & 0x07) << 18) |
                               ((String[Index + 1] & 0x3F) << 12) |
                               ((String[Index + 2] & 0x3F) << 6) |
                               (String[Index + 3] & 0x3F);

            LCharacter -= 0x10000;
            WString[Output++] = 0xD800 | (LCharacter >> 10);
            WString[Output] = 0xDC00 | (LCharacter & 0x3FF);

            Index += 4;
        }

        Output++;
    }

    RtlInitUnicodeString(UnicodeString, WString);
    return UnicodeString;
}

VOID ExtfsFreeUtf8String(PCHAR Utf8String)
{
    if (!Utf8String)
        return;
    ExFreePool(Utf8String);
}

PCHAR ExtfsConvertUnicodeToUtf8(PUNICODE_STRING UnicodeString)
{
    if (!UnicodeString || !UnicodeString->Buffer || !UnicodeString->Length)
        return NULL;

    ULONG WLength = UnicodeString->Length / sizeof(WCHAR);
    ULONG MaxUtf8Size = WLength * 4;
    PCHAR Utf8Buffer = ExAllocatePool(NonPagedPool, MaxUtf8Size + 1);
    if (!Utf8Buffer)
        return NULL;

    RtlZeroMemory(Utf8Buffer, MaxUtf8Size + 1);

    ULONG Index = 0, Output = 0;

    while (Index < WLength)
    {
        WCHAR WCharacter = UnicodeString->Buffer[Index];

        if (WCharacter < 0x80)
        {
            Utf8Buffer[Output++] = (CHAR)WCharacter;
        }
        else if (WCharacter < 0x800)
        {
            Utf8Buffer[Output++] = 0xC0 | (WCharacter >> 6);
            Utf8Buffer[Output++] = 0x80 | (WCharacter & 0x3F);
        }
        else if (WCharacter >= 0xD800 && WCharacter <= 0xDBFF)
        {
            if (Index + 1 >= WLength)
            {
                ExFreePool(Utf8Buffer);
                return NULL;
            }

            WCHAR WCharacter2 = UnicodeString->Buffer[Index + 1];
            if (WCharacter2 < 0xDC00 || WCharacter2 > 0xDFFF)
            {
                ExFreePool(Utf8Buffer);
                return NULL;
            }

            ULONG LCharacter = (((WCharacter - 0xD800) << 10) | (WCharacter2 - 0xDC00)) + 0x10000;

            Utf8Buffer[Output++] = 0xF0 | (LCharacter >> 18);
            Utf8Buffer[Output++] = 0x80 | ((LCharacter >> 12) & 0x3F);
            Utf8Buffer[Output++] = 0x80 | ((LCharacter >> 6) & 0x3F);
            Utf8Buffer[Output++] = 0x80 | (LCharacter & 0x3F);

            Index++;
        }
        else if (WCharacter >= 0xDC00 && WCharacter <= 0xDFFF)
        {
            ExFreePool(Utf8Buffer);
            return NULL;
        }
        else
        {
            Utf8Buffer[Output++] = 0xE0 | (WCharacter >> 12);
            Utf8Buffer[Output++] = 0x80 | ((WCharacter >> 6) & 0x3F);
            Utf8Buffer[Output++] = 0x80 | (WCharacter & 0x3F);
        }

        Index++;
    }

    return Utf8Buffer;
}
