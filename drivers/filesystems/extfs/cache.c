#include "extfs.h"

BOOLEAN NTAPI
ExtfsAcquireForLazyWrite(PVOID Context, BOOLEAN Wait)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Wait);
    return TRUE;
}

VOID NTAPI
ExtfsReleaseFromLazyWrite(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

BOOLEAN NTAPI
ExtfsAcquireForReadAhead(PVOID Context, BOOLEAN Wait)
{
    DPRINT1("ExtfsAcquireForReadAhead\n");
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Wait);
    return TRUE;
}

VOID NTAPI
ExtfsReleaseFromReadAhead(PVOID Context)
{
    DPRINT1("ExtfsReleaseFromReadAhead\n");
    UNREFERENCED_PARAMETER(Context);
}
