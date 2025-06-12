#include <extfs.h>

BOOLEAN IsExtfsGlobalData(PVOID Pointer)
{
    PEXTFS_GLOBAL_DATA Struct = Pointer;

    return  Struct &&
            EXTFS_CHECK_FCB_HEADER(&Struct->StandardFCB, *Struct, EXTFS_GLOBAL_DATA_MAGIC);
}

BOOLEAN IsExtfsVolumeExtension(PVOID Pointer)
{
    PEXTFS_VOLUME_EXTENSION Struct = Pointer;

    return  Struct &&
            EXTFS_CHECK_FCB_HEADER(&Struct->StandardFCB, *Struct, EXTFS_VOLUME_EXTENSION_MAGIC);
}

BOOLEAN IsExtfsFileContext(PVOID Pointer)
{
    PEXTFS_FILE_CONTEXT Struct = Pointer;

    return  Struct &&
            EXTFS_CHECK_FCB_HEADER(&Struct->StandardFCB, *Struct, EXTFS_FILE_CTX_MAGIC);
}

BOOLEAN IsExtfsFileStream(PVOID Pointer)
{
    PEXTFS_FILE_STREAM Struct = Pointer;

    return  Struct &&
            EXTFS_CHECK_FCB_HEADER(&Struct->StandardFCB, *Struct, EXTFS_FILE_STR_MAGIC);
}
