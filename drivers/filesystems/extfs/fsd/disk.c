#include <extfs.h>

NTSTATUS DiskGetBytesPerSector(PDEVICE_OBJECT VolumeDevice, PULONG BytesPerSectors)
{
    EXTFS_COMPLETION_ROUNTINE_CTX DiskCompletionRoutineContext = {0};
    PIRP Irp;
    DISK_GEOMETRY DiskGeometry = {0};

    PAGED_CODE();

    KeInitializeEvent(&DiskCompletionRoutineContext.Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(
        IOCTL_DISK_GET_DRIVE_GEOMETRY,
        VolumeDevice,
        NULL,
        0,
        &DiskGeometry,
        sizeof(DiskGeometry),
        FALSE,
        NULL,
        &DiskCompletionRoutineContext.IoStatus
    );

    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoSetCompletionRoutine(
        Irp,
        ExtfsIrpCompletionRoutine,
        &DiskCompletionRoutineContext,
        TRUE, TRUE, TRUE
    );

    NTSTATUS Status = IoCallDriver(VolumeDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&DiskCompletionRoutineContext.Event, Executive, KernelMode, FALSE, NULL);
        Status = DiskCompletionRoutineContext.IoStatus.Status;
    }

    *BytesPerSectors = DiskGeometry.BytesPerSector;

    return Status;
}

NTSTATUS DiskRead(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    EXTFS_COMPLETION_ROUNTINE_CTX DiskCompletionRoutineContext = {0};
    PIRP Irp;

    PAGED_CODE();

    DPRINT("DiskRead(0x%p, 0x%p, %llu, %u)\n", VolumeDevice, Buffer, Offset.QuadPart, Length);

    KeInitializeEvent(&DiskCompletionRoutineContext.Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_READ,
        VolumeDevice,
        Buffer,
        Length,
        &Offset,
        NULL,
        &DiskCompletionRoutineContext.IoStatus
    );

    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoSetCompletionRoutine(
        Irp,
        ExtfsIrpCompletionRoutine,
        &DiskCompletionRoutineContext,
        TRUE, TRUE, TRUE
    );

    NTSTATUS Status = IoCallDriver(VolumeDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&DiskCompletionRoutineContext.Event, Executive, KernelMode, FALSE, NULL);
        Status = DiskCompletionRoutineContext.IoStatus.Status;
    }

    DPRINT("DiskRead (0x%p) - Ended\n", Status);

    return Status;
}

NTSTATUS DiskWrite(PDEVICE_OBJECT VolumeDevice, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    EXTFS_COMPLETION_ROUNTINE_CTX DiskCompletionRoutineContext = {0};
    PIRP Irp;

    PAGED_CODE();

    DPRINT("DiskWrite(0x%p, 0x%p, %llu, %u)\n", VolumeDevice, Buffer, Offset.QuadPart, Length);

    KeInitializeEvent(&DiskCompletionRoutineContext.Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_WRITE,
        VolumeDevice,
        Buffer,
        Length,
        &Offset,
        NULL,
        &DiskCompletionRoutineContext.IoStatus
    );

    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoSetCompletionRoutine(
        Irp,
        ExtfsIrpCompletionRoutine,
        &DiskCompletionRoutineContext,
        TRUE, TRUE, TRUE
    );

    NTSTATUS Status = IoCallDriver(VolumeDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&DiskCompletionRoutineContext.Event, Executive, KernelMode, FALSE, NULL);
        Status = DiskCompletionRoutineContext.IoStatus.Status;
    }

    DPRINT("DiskWrite (0x%p) - Ended\n", Status);

    return Status;
}

NTSTATUS ExtfsDiskRead(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PERESOURCE Lock = &VolumeExtension->DiskOperationLock;
    ULONGLONG CurrentOffset = 0, MaxOffset = 0;
    ULONGLONG BytesPerSector = max(VolumeExtension->BytesPerSector, VolumeExtension->BlockSize);
    PVOID TempSectorBuffer = NULL;

    ASSERT(BytesPerSector != 0);

    CurrentOffset = Offset.QuadPart;
    MaxOffset = CurrentOffset + Length;

    if (!Length)
    {
        DPRINT1("Length is invalid\n");
        goto result;
    }

    TempSectorBuffer = ExAllocatePoolWithTag(NonPagedPool, BytesPerSector, EXTFS_TAG_BUFFER);
    if (!TempSectorBuffer)
    {
        DPRINT1("Cannot allocate TempSectorBuffer\n");
        goto result;
    }

    ExAcquireResourceSharedLite(Lock, TRUE);

    while (CurrentOffset < MaxOffset)
    {
        ULONGLONG CurrentBlock = CurrentOffset / BytesPerSector;
        ULONGLONG AlignedCurrentOffset = CurrentBlock * BytesPerSector;
        ULONGLONG RemaindingCurrentOffset = CurrentOffset - AlignedCurrentOffset;

        ULONG SliceLength = min(MaxOffset - CurrentOffset, BytesPerSector - RemaindingCurrentOffset);
        LARGE_INTEGER CurrentDiskOffset = { .QuadPart = AlignedCurrentOffset };

        Status = DiskRead(VolumeExtension->RealDevice,
                          TempSectorBuffer,
                          CurrentDiskOffset,
                          BytesPerSector);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Cannot read sector into buffer\n");
            goto error_loop;
        }

        RtlCopyMemory(Buffer, (PCHAR)TempSectorBuffer + RemaindingCurrentOffset, SliceLength);

        Buffer = (PCHAR)Buffer + SliceLength;
        CurrentOffset += SliceLength;
        continue;

error_loop:
        break;
    }

    ExReleaseResourceLite(Lock);

result:
    if (TempSectorBuffer)
        ExFreePoolWithTag(TempSectorBuffer, EXTFS_TAG_BUFFER);
    return Status;
}

NTSTATUS ExtfsDiskWrite(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PERESOURCE Lock = &VolumeExtension->DiskOperationLock;
    ULONGLONG CurrentOffset = 0, MaxOffset = 0;
    ULONGLONG BytesPerSector = max(VolumeExtension->BytesPerSector, VolumeExtension->BlockSize);
    PVOID TempSectorBuffer = NULL;

    ASSERT(BytesPerSector != 0);

    if (VolumeExtension->ReadOnly)
    {
        DPRINT1("This volume is readonly\n");
        Status = STATUS_ACCESS_DENIED;
        goto result;
    }

    CurrentOffset = Offset.QuadPart;
    MaxOffset = CurrentOffset + Length;

    if (!Length)
    {
        DPRINT1("Length is invalid\n");
        goto result;
    }

    TempSectorBuffer = ExAllocatePoolWithTag(NonPagedPool, BytesPerSector, EXTFS_TAG_BUFFER);
    if (!TempSectorBuffer)
    {
        DPRINT1("Cannot allocate TempSectorBuffer\n");
        goto result;
    }

    ExAcquireResourceExclusiveLite(Lock, TRUE);

    while (CurrentOffset < MaxOffset)
    {
        ULONGLONG CurrentBlock = CurrentOffset / BytesPerSector;
        ULONGLONG AlignedCurrentOffset = CurrentBlock * BytesPerSector;
        ULONGLONG RemaindingCurrentOffset = CurrentOffset - AlignedCurrentOffset;

        ULONG SliceLength = min(MaxOffset - CurrentOffset, BytesPerSector - RemaindingCurrentOffset);
        LARGE_INTEGER CurrentDiskOffset = { .QuadPart = AlignedCurrentOffset };

        if (SliceLength < BytesPerSector)
        {
            Status = DiskRead(VolumeExtension->RealDevice,
                              TempSectorBuffer,
                              CurrentDiskOffset,
                              BytesPerSector);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Cannot read sector into buffer\n");
                goto error_loop;
            }
        }

        RtlCopyMemory((PCHAR)TempSectorBuffer + RemaindingCurrentOffset, Buffer, SliceLength);

        Status = DiskWrite(VolumeExtension->RealDevice,
                           TempSectorBuffer,
                           CurrentDiskOffset,
                           BytesPerSector);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Cannot write sector from buffer\n");
            goto error_loop;
        }

        Buffer = (PCHAR)Buffer + SliceLength;
        CurrentOffset += SliceLength;
        continue;

error_loop:
        break;
    }

    ExReleaseResourceLite(Lock);

result:
    if (TempSectorBuffer)
        ExFreePoolWithTag(TempSectorBuffer, EXTFS_TAG_BUFFER);
    return Status;
}

NTSTATUS ExtfsFastDiskRead(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, ULONG Flags)
{
    return ExtfsAllocationManagerDiskRead(&VolumeExtension->AllocationManager, Buffer, Offset, Length, Flags);
}

NTSTATUS ExtfsFastDiskWrite(PEXTFS_VOLUME_EXTENSION VolumeExtension, PVOID Buffer, LARGE_INTEGER Offset, ULONG Length, ULONG Flags)
{
    return ExtfsAllocationManagerDiskWrite(&VolumeExtension->AllocationManager, Buffer, Offset, Length, Flags);
}
