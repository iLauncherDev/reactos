
#include <arch/amd64/paging_extend.h>

extern void *get_cr3();

void ExtendAmd64Paging()
{
    PHARDWARE_PTE TableL4 = get_cr3();
    PHARDWARE_PTE TableL3 = (PHARDWARE_PTE)(TableL4[0].PageFrameNumber << 12);

    ULONGLONG TotalPages1GB = (TotalPagesInLookupTable >> 30) & 0x1ff;

    for (ULONG IndexL3 = 1; IndexL3 < TotalPages1GB; IndexL3++)
    {
        ULONGLONG OffsetL3 = IndexL3 << 30;
        PHARDWARE_PTE EntryL3 = &TableL3[IndexL3];
        PHARDWARE_PTE TableL2 = MmAllocateMemoryWithType(0x1000, LoaderFirmwareTemporary);
        RtlZeroMemory(TableL2, 0x1000);

        for (ULONG IndexL2 = 0; IndexL2 < 512; IndexL2++)
        {
            ULONGLONG OffsetL2 = OffsetL3 + (IndexL2 << 21);
            PHARDWARE_PTE EntryL2 = &TableL2[IndexL2];
        
            EntryL2->LargePage = TRUE;
            EntryL2->Valid = TRUE;
            EntryL2->Write = TRUE;
            EntryL2->PageFrameNumber = OffsetL2 >> 12;
        }

        EntryL3->Valid = TRUE;
        EntryL3->Write = TRUE;
        EntryL3->PageFrameNumber = (ULONGLONG)TableL2 >> 12;
    }
}
