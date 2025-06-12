#include <extfs.h>

VOID ExtfsFlushAllInodeContext(PEXTFS_VOLUME_EXTENSION VolumeExtension)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;

    ExAcquireResourceExclusiveLite(&VolumeExtension->InodeContextListLock, TRUE);

    EndListEntry = &VolumeExtension->InodeContextList;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_INODE_CONTEXT CurrentInodeContext = ExtfsGetListEntryStructure(CurrentListEntry);

        ExtfsFlushInodeContext(CurrentInodeContext);

        CurrentListEntry = CurrentListEntry->Next;
    }

    ExReleaseResourceLite(&VolumeExtension->InodeContextListLock);
}

VOID
ExtfsFlushAllVolumeExtension(PEXTFS_GLOBAL_DATA GlobalData)
{
    PEXTFS_LIST_ENTRY EndListEntry;
    PEXTFS_LIST_ENTRY CurrentListEntry;
    PEXTFS_ALLOCATION_MANAGER AllocationManager;

    ExAcquireResourceExclusiveLite(&ExtfsGlobalData->Resource, TRUE);

    EndListEntry = &GlobalData->MountedVolumeList;
    CurrentListEntry = EndListEntry->Next;

    while (CurrentListEntry != EndListEntry)
    {
        PEXTFS_VOLUME_EXTENSION CurrentVolumeExtension = ExtfsGetListEntryStructure(CurrentListEntry);

        ExtfsFlushAllInodeContext(CurrentVolumeExtension);

        AllocationManager = &CurrentVolumeExtension->AllocationManager;

        AllocationManager->AutoFlushThreadStopRequest = TRUE;
        ExtfsAllocationManagerForceFlush(AllocationManager);
        KeWaitForSingleObject(&AllocationManager->AutoFlushThreadExitedEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);

        CurrentListEntry = CurrentListEntry->Next;
    }

    ExReleaseResourceLite(&ExtfsGlobalData->Resource);
}

NTSTATUS
ExtfsFsdDispatchShutdown(PDEVICE_OBJECT DeviceObject,
                         PIRP Irp)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTFS_GLOBAL_DATA GlobalData = DeviceObject ? DeviceObject->DeviceExtension : NULL;

    if (!GlobalData ||
        !EXTFS_CHECK_FCB_HEADER(&GlobalData->StandardFCB, *GlobalData, EXTFS_GLOBAL_DATA_MAGIC))
    {
        DPRINT1("Invalid GlobalData\n");
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto result;
    }

    ExtfsFlushAllVolumeExtension(DeviceObject->DeviceExtension);

result:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
