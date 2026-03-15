#pragma once

#define ENDIAN_TEST(OutIsBigEndian)                                                                                       \
    {                                                                                                                     \
        USHORT EndianTest = 0x0001;                                                                                       \
        UCHAR EndianByte = *((PUCHAR)&EndianTest);                                                                        \
        OutIsBigEndian = EndianByte != 0x01;                                                                              \
    }

typedef enum
{
    FIELD_BYTE_TYPE_BIG_ENDIAN,
    FIELD_BYTE_TYPE_LITTLE_ENDIAN,
    FIELD_BYTE_TYPE_BUFFER,
    FIELD_BYTE_TYPE_STRUCT,
} FIELD_BYTE_TYPE;

typedef enum
{
    FIELD_TRANSLATE_TO_HOST,
    FIELD_TRANSLATE_TO_FIELD,
} FIELD_TRANSLATE_TO;

typedef struct _FIELD_TRANSLATOR
{
    PCSTR Name;

    FIELD_BYTE_TYPE ByteType;

    ULONG Offset;
    ULONG Size;

    struct
    {
        PVOID List;
        ULONG Entries;
    } FieldTranslator;

    ULONG EntrySize;
} FIELD_TRANSLATOR, *PFIELD_TRANSLATOR;

#define FIELD_TRANSLATOR_ENTIRES(FieldTranslator) (sizeof(FieldTranslator) / sizeof(*FieldTranslator))

#define FIELD_TRANSLATOR_ENTRY(Type, Field, _ByteType)                                                                 \
    {                                                                                                                  \
        .Name = #Field,                                                                                                \
        .ByteType = _ByteType,                                                                                         \
        .Offset = (ULONG)(&((Type *)0)->Field),                                                                        \
        .Size = sizeof(((Type *)0)->Field),                                                                            \
    }

#define FIELD_TRANSLATOR_STRUCT_ARRAY(Type, Field, OtherFieldTranslator)                                               \
    {                                                                                                                  \
        .Name = #Field,                                                                                                \
        .ByteType = FIELD_BYTE_TYPE_STRUCT,                                                                            \
        .Offset = (ULONG)(&((Type *)0)->Field),                                                                        \
        .Size = sizeof(((Type *)0)->Field),                                                                            \
        .FieldTranslator.List = OtherFieldTranslator,                                                                  \
        .FieldTranslator.Entries = FIELD_TRANSLATOR_ENTIRES(OtherFieldTranslator),                                     \
        .EntrySize = sizeof(*((Type *)0)->Field)                                                                       \
    }

#define FIELD_TRANSLATOR_STRUCT(Type, Field, OtherFieldTranslator)                                                     \
    {                                                                                                                  \
        .Name = #Field,                                                                                                \
        .ByteType = FIELD_BYTE_TYPE_STRUCT,                                                                            \
        .Offset = (ULONG)(&((Type *)0)->Field),                                                                        \
        .Size = sizeof(((Type *)0)->Field),                                                                            \
        .FieldTranslator.List = OtherFieldTranslator,                                                                  \
        .FieldTranslator.Entries = FIELD_TRANSLATOR_ENTIRES(OtherFieldTranslator),                                     \
        .EntrySize = sizeof(((Type *)0)->Field)                                                                        \
    }

#define DumpStructBytes(FieldTranslator, Struct) _DumpStructBytes(FieldTranslator, FIELD_TRANSLATOR_ENTIRES(FieldTranslator), #Struct, 0, Struct)
#define TranslateStructTo(FieldTranslator, Dest, Src, TranslateTo) _TranslateStructTo(FieldTranslator, FIELD_TRANSLATOR_ENTIRES(FieldTranslator), Dest, Src, TranslateTo)

VOID _DumpStructBytes(PFIELD_TRANSLATOR FieldTranslator, ULONG Entries, PCSTR Name, ULONG Index, PVOID Struct);
VOID _TranslateStructTo(PFIELD_TRANSLATOR FieldTranslator, ULONG Entries, PVOID Dest, PVOID Src, FIELD_TRANSLATE_TO TranslateTo);
