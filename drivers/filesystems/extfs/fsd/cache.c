#include <extfs.h>

BOOLEAN NTAPI
ExtfsAcquireForLazyWrite(PVOID Context, BOOLEAN Wait)
{
    PEXTFS_FILE_CONTEXT FileContext = Context;
    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        return FALSE;
    }

    DPRINT1("ExtfsAcquireForLazyWrite\n");

    return ExAcquireResourceExclusiveLite(&FileContext->StandardFCB.MainResource, Wait);
}

VOID NTAPI
ExtfsReleaseFromLazyWrite(PVOID Context)
{
    PEXTFS_FILE_CONTEXT FileContext = Context;
    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        return;
    }

    DPRINT1("ExtfsReleaseFromLazyWrite\n");

    return ExReleaseResourceLite(&FileContext->StandardFCB.MainResource);
}

BOOLEAN NTAPI
ExtfsAcquireForReadAhead(PVOID Context, BOOLEAN Wait)
{
    PEXTFS_FILE_CONTEXT FileContext = Context;
    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        return FALSE;
    }

    DPRINT1("ExtfsAcquireForReadAhead\n");

    return ExAcquireResourceSharedLite(&FileContext->StandardFCB.MainResource, Wait);
}

VOID NTAPI
ExtfsReleaseFromReadAhead(PVOID Context)
{
    PEXTFS_FILE_CONTEXT FileContext = Context;
    if (!FileContext || !EXTFS_CHECK_FCB_HEADER(FileContext, *FileContext, EXTFS_FILE_CTX_MAGIC))
    {
        DPRINT1("Invalid FileContext\n");
        return;
    }

    DPRINT1("ExtfsReleaseFromReadAhead\n");

    return ExReleaseResourceLite(&FileContext->StandardFCB.MainResource);
}
