#pragma once
#include <pshpack1.h>

#define EXT_SUPERBLOCK_MAGIC 0xEF53
#define EXT_DYNAMIC_REVISION 1
#define EXT_DEFAULT_INODE_SIZE 128
#define EXT_DEFAULT_GROUP_DESC_SIZE 32

#define EXT_DIR_ENTRY_MAX_NAME_LENGTH 255
#define EXT_HALF_BLOCK_SIZE 512

#define EXT_GROUP_DESC_INODE_BITMAP_SPARSE  0x0001
#define EXT_GROUP_DESC_BLOCK_BITMAP_SPARSE  0x0002
#define EXT_GROUP_DESC_INODE_TABLE_SPARSE   0x0004

typedef struct _ExtSuperBlock
{
    /* SuperBlock Information Ext2 */
    ULONG InodesCount;
    ULONG BlocksCountLo;
    ULONG RBlocksCountLo;
    ULONG FreeBlocksCountLo;
    ULONG FreeInodesCount;
    ULONG FirstDataBlock;
    ULONG LogBlockSize;
    LONG LogFragSize;
    ULONG BlocksPerGroup;
    ULONG FragsPerGroup;
    ULONG InodesPerGroup;
    ULONG MTime;
    ULONG WTime;
    USHORT MntCount;
    USHORT MaxMntCount;
    USHORT Magic;
    USHORT State;
    USHORT Errors;
    USHORT MinorRevisionLevel;
    ULONG LastCheck;
    ULONG CheckInterval;
    ULONG CreatorOS;
    ULONG RevisionLevel;
    USHORT DefResUID;
    USHORT DefResGID;

    /* SuperBlock Information Ext3 */
    ULONG FirstInode;
    USHORT InodeSize;
    USHORT BlockGroupNr;
    ULONG FeatureCompat;
    ULONG FeatureIncompat;
    ULONG FeatureROCompat;
    UCHAR UUID[16];
    CHAR VolumeName[16];
    CHAR LastMounted[64];
    ULONG AlgorithmUsageBitmap;
    UCHAR PreallocBlocks;
    UCHAR PreallocDirBlocks;
    USHORT ReservedGdtBlocks;
    UCHAR JournalUUID[16];
    ULONG JournalInum;
    ULONG JournalDev;
    ULONG LastOrphan;
    ULONG HashSeed[4];
    UCHAR DefHashVersion;
    UCHAR JournalBackupType;
    USHORT GroupDescSize;
    UCHAR Reserved[768];
} EXT_SUPER_BLOCK, *PEXT_SUPER_BLOCK;

typedef struct _ExtGroupDescriptor
{
    ULONG BlockBitmap;
    ULONG InodeBitmap;
    ULONG InodeTable;
    USHORT FreeBlocksCount;
    USHORT FreeInodesCount;
    USHORT UsedDirsCount;
    USHORT Flags;
    ULONG ExcludeBitmap;
    USHORT BlockBitmapChecksum;
    USHORT InodeBitmapChecksum;
    USHORT InodeTableUnused;
    USHORT Checksum;
} EXT_GROUP_DESC, *PEXT_GROUP_DESC;

typedef struct _Ext4ExtentHeader
{
    USHORT Magic;
    USHORT Entries;
    USHORT Max;
    USHORT Depth;
    ULONG Generation;
} EXT4_EXTENT_HEADER, *PEXT4_EXTENT_HEADER;

typedef struct _Ext4ExtentIdx
{
    ULONG Block;
    ULONG Leaf;
    USHORT LeafHigh;
    USHORT Unused;
} EXT4_EXTENT_IDX, *PEXT4_EXTENT_IDX;

typedef struct _Ext4Extent
{
    ULONG Block;
    USHORT Length;
    USHORT StartHigh;
    ULONG Start;
} EXT4_EXTENT, *PEXT4_EXTENT;

typedef struct _ExtInode
{
    USHORT Mode;
    USHORT UID;
    ULONG Size;
    ULONG Atime;
    ULONG Ctime;
    ULONG Mtime;
    ULONG Dtime;
    USHORT GID;
    USHORT LinksCount;
    ULONG BlocksCount;
    ULONG Flags;
    ULONG OSD1;
    union
    {
        CHAR SymLink[60];
        ULONG TotalBlocks[15];
        struct
        {
            ULONG DirectBlocks[12];
            ULONG IndirectBlock;
            ULONG DoubleIndirectBlock;
            ULONG TripleIndirectBlock;
        } Blocks;
        EXT4_EXTENT_HEADER ExtentHeader;
    };
    ULONG Generation;
    ULONG FileACL;
    ULONG DirACL;
    ULONG FragAddress;
    UCHAR OSD2[12];

    USHORT ExtraInodeSize;
    USHORT ChecksumHi;
    ULONG CtimeExtra;
    ULONG MtimeExtra;
    ULONG AtimeExtra;
    ULONG CRtime;
    ULONG CRtimeExtra;
    ULONG VersionHi;
    ULONG ProjectId;
} EXT_INODE, *PEXT_INODE;

typedef struct _ExtDirEntry
{
    ULONG Inode;
    USHORT EntryLen;
    UCHAR NameLen;
    UCHAR FileType;
    CHAR Name[EXT_DIR_ENTRY_MAX_NAME_LENGTH+1];
} EXT_DIR_ENTRY, *PEXT_DIR_ENTRY;

#include <poppack.h>

/* Ext directory entry file types */
#define EXT_DIR_ENTRY_TYPE_UNKNOWN 0x00
#define EXT_DIR_ENTRY_TYPE_REGULAR 0x01
#define EXT_DIR_ENTRY_TYPE_DIRECTORY 0x02
#define EXT_DIR_ENTRY_TYPE_CHARACTER_DEVICE 0x03
#define EXT_DIR_ENTRY_TYPE_BLOCK_DEVICE 0x04
#define EXT_DIR_ENTRY_TYPE_FIFO 0x05
#define EXT_DIR_ENTRY_TYPE_SOCKET 0x06
#define EXT_DIR_ENTRY_TYPE_SYMBOLIC_LINK 0x07

/* Special inode numbers.  */
#define EXT_ROOT_INODE 2

/* Incompat SuperBlock features */
#define EXT_SB_FEATURE_IMCOMPAT_64BIT (0x0080)
#define EXT_SB_FEATURE_INCOMPAT_EXTENTS (0x0040)
#define EXT_SB_FEATURE_INCOMPAT_JOURNAL_DEV (0x0008)
#define EXT_SB_FEATURE_INCOMPAT_RECOVER (0x0004)

#define EXT_SB_FEATURE_INCOMPAT_EXT4 (EXT_SB_FEATURE_IMCOMPAT_64BIT | EXT_SB_FEATURE_INCOMPAT_EXTENTS)
#define EXT_SB_FEATURE_INCOMPAT_EXT3 (EXT_SB_FEATURE_INCOMPAT_JOURNAL_DEV | EXT_SB_FEATURE_INCOMPAT_RECOVER)

/* Compat SuperBlock features */
#define EXT_SB_FEATURE_COMPAT_HAS_JOURNAL (0x0004)
#define EXT_SB_FEATURE_COMPAT_EXCLUDE_BITMAP (0x0100)

#define EXT_SB_FEATURE_COMPAT_EXT3 (EXT_SB_FEATURE_COMPAT_HAS_JOURNAL)

/* The revision level.  */
#define EXT_REVISION(sb) (ReadFieldLE(sb->RevisionLevel))

/* The inode size.  */
#define EXT_INODE_SIZE(sb) \
    (EXT_REVISION(sb) < EXT_DYNAMIC_REVISION ? EXT_DEFAULT_INODE_SIZE : ReadFieldLE(sb->InodeSize))

/* The group descriptor size.  */
#define EXT_GROUP_DESC_SIZE(sb) \
    ((EXT_REVISION(sb) < EXT_DYNAMIC_REVISION || !ReadFieldLE(sb->GroupDescSize)) ? EXT_DEFAULT_GROUP_DESC_SIZE : ReadFieldLE(sb->GroupDescSize))

/* The inode extents flag.  */
#define EXT4_INODE_FLAG_EXTENTS 0x80000

/* The extent header magic value.  */
#define EXT4_EXTENT_HEADER_MAGIC 0xF30A

/* The maximum extent level.  */
#define EXT4_EXTENT_MAX_LEVEL 5

/* The maximum extent length used to check for sparse extents.  */
#define EXT4_EXTENT_MAX_LENGTH 32768

// EXT_INODE::mode values
#define EXT_S_IRWXO 0x0007 // Other mask
#define EXT_S_IXOTH 0x0001 // ---------x execute
#define EXT_S_IWOTH 0x0002 // --------w- write
#define EXT_S_IROTH 0x0004 // -------r-- read

#define EXT_S_IRWXG 0x0038 // Group mask
#define EXT_S_IXGRP 0x0008 // ------x--- execute
#define EXT_S_IWGRP 0x0010 // -----w---- write
#define EXT_S_IRGRP 0x0020 // ----r----- read

#define EXT_S_IRWXU 0x01C0 // User mask
#define EXT_S_IXUSR 0x0040 // ---x------ execute
#define EXT_S_IWUSR 0x0080 // --w------- write
#define EXT_S_IRUSR 0x0100 // -r-------- read

#define EXT_S_ISVTX 0x0200 // Sticky bit
#define EXT_S_ISGID 0x0400 // SGID
#define EXT_S_ISUID 0x0800 // SUID

#define EXT_S_IFMT 0xF000   // Format mask
#define EXT_S_IFIFO 0x1000  // FIFO buffer
#define EXT_S_IFCHR 0x2000  // Character device
#define EXT_S_IFDIR 0x4000  // Directory
#define EXT_S_IFBLK 0x6000  // Block device
#define EXT_S_IFREG 0x8000  // Regular file
#define EXT_S_IFLNK 0xA000  // Symbolic link
#define EXT_S_IFSOCK 0xC000 // Socket

#define FAST_SYMLINK_MAX_NAME_SIZE 60
