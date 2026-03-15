#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

static ULONGLONG
ReadPointerXEndian(PVOID Pointer, ULONG Size, BOOLEAN IsBigEndian)
{
    PUCHAR PointerByte = Pointer;
    ULONGLONG Value = 0;
    ULONGLONG Bit;
    ULONGLONG SizeInBits = Size * 8;

    if (IsBigEndian)
    {
        for (Bit = SizeInBits; Bit >= 8; Bit -= 8)
            Value |= (ULONGLONG)(*PointerByte++) << (Bit - 8);
    }
    else
    {
        for (Bit = 0; Bit < SizeInBits; Bit += 8)
            Value |= (ULONGLONG)(*PointerByte++) << Bit;
    }

    return Value;
}

static VOID
WritePointerXEndian(PVOID Pointer, ULONG Size, BOOLEAN IsBigEndian, ULONGLONG Value)
{
    PUCHAR PointerByte = Pointer;
    ULONGLONG Bit;
    ULONGLONG SizeInBits = Size * 8;

    if (IsBigEndian)
    {
        for (Bit = SizeInBits; Bit >= 8; Bit -= 8)
            *PointerByte++ = (Value >> (Bit - 8)) & 0xFF;
    }
    else
    {
        for (Bit = 0; Bit < SizeInBits; Bit += 8)
            *PointerByte++ = (Value >> Bit) & 0xFF;
    }
}

VOID _DumpStructBytes(PFIELD_TRANSLATOR FieldTranslator, ULONG Entries, PCSTR Name, ULONG Index, PVOID Struct)
{
    TRACE("Dumping struct %s (%u)\n", Name, Index);

    for (ULONG Index = 0; Index < Entries; Index++)
    {
        PFIELD_TRANSLATOR FieldTranslatorEntry = &FieldTranslator[Index];

        PCHAR StructPointer = (PCHAR)Struct + FieldTranslatorEntry->Offset;
        ULONG FieldSize = FieldTranslatorEntry->Size;
        ULONG FieldEntrySize = FieldTranslatorEntry->EntrySize;

        if (FieldTranslatorEntry->ByteType == FIELD_BYTE_TYPE_STRUCT)
        {
            for (ULONG Offset = 0; Offset < FieldSize; Offset += FieldEntrySize)
            {
                _DumpStructBytes(FieldTranslatorEntry->FieldTranslator.List, FieldTranslatorEntry->FieldTranslator.Entries,
                                 FieldTranslatorEntry->Name, Offset / FieldEntrySize,
                                 StructPointer + Offset);
            }
        }
        else
        {
            TRACE("%s: ", FieldTranslatorEntry->Name);

            for (ULONG Byte = 0; Byte < FieldSize; Byte++)
                TRACE("0x%02hhx ", StructPointer[Byte]);

            TRACE("\n");
        }
    }

    TRACE("End of dump struct\n");
}

VOID _TranslateStructTo(PFIELD_TRANSLATOR FieldTranslator, ULONG Entries, PVOID Dest, PVOID Src, FIELD_TRANSLATE_TO TranslateTo)
{
    BOOLEAN TranslateToHost = TranslateTo == FIELD_TRANSLATE_TO_HOST;

    BOOLEAN IsBigEndianHost;

    ENDIAN_TEST(IsBigEndianHost)

    for (ULONG Index = 0; Index < Entries; Index++)
    {
        PFIELD_TRANSLATOR FieldTranslatorEntry = &FieldTranslator[Index];
        
        BOOLEAN IsBigEndianField = FieldTranslatorEntry->ByteType == FIELD_BYTE_TYPE_BIG_ENDIAN;

        PCHAR DestPointer = (PCHAR)Dest + FieldTranslatorEntry->Offset;
        PCHAR SrcPointer = (PCHAR)Dest + FieldTranslatorEntry->Offset;
        ULONG FieldSize = FieldTranslatorEntry->Size;
        ULONG FieldEntrySize = FieldTranslatorEntry->EntrySize;

        BOOLEAN DestEndian = TranslateToHost ?  IsBigEndianHost : IsBigEndianField;
        BOOLEAN SrcEndian = TranslateToHost ? IsBigEndianField : IsBigEndianHost;

        switch (FieldTranslatorEntry->ByteType)
        {
            case FIELD_BYTE_TYPE_BIG_ENDIAN:
            case FIELD_BYTE_TYPE_LITTLE_ENDIAN:
            {
                ULONGLONG FieldValue = ReadPointerXEndian(SrcPointer, FieldSize, SrcEndian);

                WritePointerXEndian(DestPointer, FieldSize, DestEndian, FieldValue);
                break;
            }

            case FIELD_BYTE_TYPE_BUFFER:
                RtlCopyMemory(DestPointer, SrcPointer, FieldSize);
                break;

            case FIELD_BYTE_TYPE_STRUCT:
            {
                for (ULONG Offset = 0; Offset < FieldSize; Offset += FieldEntrySize)
                {
                    _TranslateStructTo(FieldTranslatorEntry->FieldTranslator.List, FieldTranslatorEntry->FieldTranslator.Entries,
                                       DestPointer + Offset,
                                       SrcPointer + Offset,
                                       TranslateTo);
                }

                break;
            }

            default:
                ASSERT(FALSE && "Unknown FieldTranslatorEntry->ByteType");
                break;
        }
    }
}
