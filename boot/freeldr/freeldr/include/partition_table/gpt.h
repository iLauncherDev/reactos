#pragma once

#define GPT_HEADER_SIGNATURE                "EFI PART"
#define GPT_PARTITION_NAME_LENGTH           36

#include <pshpack1.h>

typedef struct
{
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} GPT_GUID, *PGPT_GUID;

/* GPT Table Header */
typedef struct _GPT_TABLE_HEADER
{
    CHAR    Signature[8];               /* "EFI PART" */
    UINT32  Revision;                   /* 0x00010000 */
    UINT32  HeaderSize;                 /* Size of header (usually 92) */
    UINT32  HeaderCrc32;                /* CRC32 of header */
    UINT32  Reserved;                   /* Must be 0 */
    UINT64  MyLba;                      /* LBA of this header */
    UINT64  AlternateLba;               /* LBA of alternate header */
    UINT64  FirstUsableLba;             /* First usable LBA for partitions */
    UINT64  LastUsableLba;              /* Last usable LBA for partitions */
    GPT_GUID DiskGuid;                  /* Disk GUID */
    UINT64  PartitionEntryLba;          /* LBA of partition entries array */
    UINT32  NumberOfPartitionEntries;   /* Number of partition entries */
    UINT32  SizeOfPartitionEntry;       /* Size of each entry (usually 128) */
    UINT32  PartitionEntryArrayCrc32;   /* CRC32 of partition entries array */
} GPT_TABLE_HEADER, *PGPT_TABLE_HEADER;

/* GPT Partition Entry */
typedef struct _GPT_PARTITION_ENTRY
{
    GPT_GUID PartitionTypeGuid;            /* Partition type GUID */
    GPT_GUID UniquePartitionGuid;          /* Unique partition GUID */
    UINT64  StartingLba;                   /* Starting LBA */
    UINT64  EndingLba;                     /* Ending LBA */
    UINT64  Attributes;                    /* Partition attributes */
    SHORT  PartitionName[GPT_PARTITION_NAME_LENGTH]; /* Partition name (UTF-16) */
} GPT_PARTITION_ENTRY, *PGPT_PARTITION_ENTRY;

#include <poppack.h>
