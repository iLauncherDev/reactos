#pragma once
#include <pshpack1.h>

#define EXT_SUPERBLOCK_MAGIC 0xEF53
#define EXT_DYNAMIC_REVISION 1
#define EXT_DEFAULT_INODE_SIZE 128
#define EXT_DEFAULT_GROUP_DESC_SIZE 32

#define EXT_DIR_ENTRY_MAX_NAME_LENGTH 255

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
    ULONG OSD2[3];
} EXT_INODE, *PEXT_INODE;

typedef struct _ExtDirEntry
{
    ULONG Inode;
    USHORT EntryLen;
    UCHAR NameLen;
    UCHAR FileType;
    CHAR Name[EXT_DIR_ENTRY_MAX_NAME_LENGTH];
} EXT_DIR_ENTRY, *PEXT_DIR_ENTRY;

#include <poppack.h>

/* Special inode numbers.  */
#define EXT_ROOT_INODE 2

/* Incompat SuperBlock features */
#define EXT_SB_FEATURE_INCOMPAT_EXTENTS (0x0040)
#define EXT_SB_FEATURE_INCOMPAT_JOURNALING (0x0004)

/* The revision level.  */
#define EXT_REVISION(sb) (sb->RevisionLevel)

/* The inode size.  */
#define EXT_INODE_SIZE(sb) \
    (EXT_REVISION(sb) < EXT_DYNAMIC_REVISION ? EXT_DEFAULT_INODE_SIZE : sb->InodeSize)

/* The group descriptor size.  */
#define EXT_GROUP_DESC_SIZE(sb) \
    ((EXT_REVISION(sb) >= EXT_DYNAMIC_REVISION && sb->GroupDescSize) ? sb->GroupDescSize : EXT_DEFAULT_GROUP_DESC_SIZE)

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
