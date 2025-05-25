/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Windows-compatible NT OS Loader.
 * COPYRIGHT:   Copyright 2006-2019 Aleksey Bragin <aleksey@reactos.org>
 */

#pragma once

#include <arc/setupblk.h>

/* Entry-point to kernel */
typedef VOID (NTAPI *KERNEL_ENTRY_POINT) (PLOADER_PARAMETER_BLOCK LoaderBlock);

/* Descriptors */
#define NUM_GDT 128     // Must be 128
#define NUM_IDT 0x100   // Only 16 are used though. Must be 0x100

#if 0

#include <pshpack1.h>
typedef struct  /* Root System Descriptor Pointer */
{
    CHAR             signature [8];          /* contains "RSD PTR " */
    UCHAR            checksum;               /* to make sum of struct == 0 */
    CHAR             oem_id [6];             /* OEM identification */
    UCHAR            revision;               /* Must be 0 for 1.0, 2 for 2.0 */
    ULONG            rsdt_physical_address;  /* 32-bit physical address of RSDT */
    ULONG            length;                 /* XSDT Length in bytes including hdr */
    ULONGLONG        xsdt_physical_address;  /* 64-bit physical address of XSDT */
    UCHAR            extended_checksum;      /* Checksum of entire table */
    CHAR             reserved [3];           /* reserved field must be 0 */
} RSDP_DESCRIPTOR, *PRSDP_DESCRIPTOR;
#include <poppack.h>

typedef struct _ARC_DISK_SIGNATURE_EX
{
    ARC_DISK_SIGNATURE DiskSignature;
    CHAR ArcName[MAX_PATH];
} ARC_DISK_SIGNATURE_EX, *PARC_DISK_SIGNATURE_EX;

#endif

#define MAX_OPTIONS_LENGTH 255

typedef struct _LOADER_PARAMETER_BLOCK1
{
    LIST_ENTRY LoadOrderListHead;
    LIST_ENTRY MemoryDescriptorListHead;
    LIST_ENTRY BootDriverListHead;
    ULONG_PTR KernelStack;
    ULONG_PTR Prcb;
    ULONG_PTR Process;
    ULONG_PTR Thread;
    ULONG RegistryLength;
    PVOID RegistryBase;
    PCONFIGURATION_COMPONENT_DATA ConfigurationRoot;
    PSTR ArcBootDeviceName;
    PSTR ArcHalDeviceName;
    PSTR NtBootPathName;
    PSTR NtHalPathName;
    PSTR LoadOptions;
    PNLS_DATA_BLOCK NlsData;
    PARC_DISK_INFORMATION ArcDiskInformation;
    PVOID OemFontFile;
} LOADER_PARAMETER_BLOCK1, *PLOADER_PARAMETER_BLOCK1;

typedef struct _LOADER_PARAMETER_BLOCK2
{
    PVOID Extension;
    union
    {
        I386_LOADER_BLOCK I386;
        ALPHA_LOADER_BLOCK Alpha;
        IA64_LOADER_BLOCK IA64;
        PPC_LOADER_BLOCK PowerPC;
        ARM_LOADER_BLOCK Arm;
    } u;
    FIRMWARE_INFORMATION_LOADER_BLOCK FirmwareInformation;
} LOADER_PARAMETER_BLOCK2, *PLOADER_PARAMETER_BLOCK2;

typedef struct _LOADER_PARAMETER_BLOCK_VISTA
{
    LOADER_PARAMETER_BLOCK1 Block1;
    PSETUP_LOADER_BLOCK SetupLdrBlock;
    LOADER_PARAMETER_BLOCK2 Block2;
} LOADER_PARAMETER_BLOCK_VISTA, *PLOADER_PARAMETER_BLOCK_VISTA;

typedef struct _LOADER_PARAMETER_EXTENSION1
{
    ULONG Size;
    PROFILE_PARAMETER_BLOCK Profile;
} LOADER_PARAMETER_EXTENSION1, *PLOADER_PARAMETER_EXTENSION1;

#pragma pack(push)
#pragma pack(1)

typedef struct _LOADER_PARAMETER_EXTENSION2
{
    PVOID EmInfFileImage;
    ULONG EmInfFileSize;
    PVOID TriageDumpBlock;
    //
    // NT 5.1
    //
    ULONG_PTR LoaderPagesSpanned;   /* Not anymore present starting NT 6.2 */
    PHEADLESS_LOADER_BLOCK HeadlessLoaderBlock;
    PSMBIOS_TABLE_HEADER SMBiosEPSHeader;
    PVOID DrvDBImage;
    ULONG DrvDBSize;
    PNETWORK_LOADER_BLOCK NetworkLoaderBlock;
    //
    // NT 5.2+
    //
#ifdef _X86_
    PUCHAR HalpIRQLToTPR;
    PUCHAR HalpVectorToIRQL;
#endif
    LIST_ENTRY FirmwareDescriptorListHead;
    PVOID AcpiTable;
    ULONG AcpiTableSize;
    //
    // NT 5.2 SP1+
    //
/** NT-version-dependent flags **/
    ULONG BootViaWinload:1;
    ULONG BootViaEFI:1;
    ULONG Reserved:30;
/********************************/
    PLOADER_PERFORMANCE_DATA LoaderPerformanceData;
    LIST_ENTRY BootApplicationPersistentData;
    PVOID WmdTestResult;
    GUID BootIdentifier;
    //
    // NT 6
    //
    ULONG ResumePages;
    PVOID DumpHeader;
} LOADER_PARAMETER_EXTENSION2, *PLOADER_PARAMETER_EXTENSION2;

typedef struct _LOADER_PARAMETER_EXTENSION_VISTA
{
    LOADER_PARAMETER_EXTENSION1 Extension1;
    ULONG MajorVersion;
    ULONG MinorVersion;
    LOADER_PARAMETER_EXTENSION2 Extension2;
} LOADER_PARAMETER_EXTENSION_VISTA, *PLOADER_PARAMETER_EXTENSION_VISTA;

#pragma pack(pop)

typedef union _LOADER_SYSTEM_U1
{
    LOADER_PARAMETER_BLOCK_VISTA LoaderBlockVista;
} LOADER_SYSTEM_U1, *PLOADER_SYSTEM_U1;

typedef union _LOADER_SYSTEM_U2
{
    LOADER_PARAMETER_EXTENSION_VISTA ExtensionVista;
} LOADER_SYSTEM_U2, *PLOADER_SYSTEM_U2;

typedef struct _LOADER_SYSTEM_BLOCK
{
    LOADER_SYSTEM_U1 u1;
    LOADER_SYSTEM_U2 u2;
    SETUP_LOADER_BLOCK SetupBlock;
#ifdef _M_IX86
    HEADLESS_LOADER_BLOCK HeadlessLoaderBlock;
#endif
    NLS_DATA_BLOCK NlsDataBlock;
    CHAR LoadOptions[MAX_OPTIONS_LENGTH+1];
    CHAR ArcBootDeviceName[MAX_PATH+1];
    // CHAR ArcHalDeviceName[MAX_PATH];
    CHAR NtBootPathName[MAX_PATH+1];
    CHAR NtHalPathName[MAX_PATH+1];
    ARC_DISK_INFORMATION ArcDiskInformation;
    LOADER_PERFORMANCE_DATA LoaderPerformanceData;
    PVOID LoaderBlock;
    PLOADER_PARAMETER_BLOCK1 LoaderBlock1;
    PLOADER_PARAMETER_BLOCK2 LoaderBlock2;
    PLOADER_PARAMETER_EXTENSION1 Extension1;
    PLOADER_PARAMETER_EXTENSION2 Extension2;
    PSETUP_LOADER_BLOCK* SetupBlockPtr;
} LOADER_SYSTEM_BLOCK, *PLOADER_SYSTEM_BLOCK;

extern PLOADER_SYSTEM_BLOCK WinLdrSystemBlock;
/**/extern PCWSTR BootFileSystem;/**/


// conversion.c
#if 0
PVOID VaToPa(PVOID Va);
PVOID PaToVa(PVOID Pa);
VOID List_PaToVa(_In_ LIST_ENTRY *ListEntry);
#endif
VOID ConvertConfigToVA(PCONFIGURATION_COMPONENT_DATA Start);

// winldr.c
extern BOOLEAN SosEnabled;
#ifdef _M_IX86
extern BOOLEAN PaeModeOn;
#endif

FORCEINLINE
VOID
UiResetForSOS(VOID)
{
#ifdef _M_ARM
    /* Re-initialize the UI */
    UiInitialize(TRUE);
#else
    /* Reset the UI and switch to MiniTui */
    UiVtbl.UnInitialize();
    UiVtbl = MiniTuiVtbl;
    UiVtbl.Initialize();
#endif
    /* Disable the progress bar */
    UiProgressBar.Show = FALSE;
}

VOID
NtLdrOutputLoadMsg(
    _In_ PCSTR FileName,
    _In_opt_ PCSTR Description);

PVOID WinLdrLoadModule(PCSTR ModuleName, PULONG Size,
                       TYPE_OF_MEMORY MemoryType);

// wlmemory.c
BOOLEAN
WinLdrSetupMemoryLayout(IN OUT PLOADER_PARAMETER_BLOCK1 LoaderBlock1);

// wlregistry.c
BOOLEAN
WinLdrInitSystemHive(
    IN OUT PLOADER_PARAMETER_BLOCK1 LoaderBlock1,
    IN PCSTR SystemRoot,
    IN BOOLEAN Setup);

BOOLEAN WinLdrScanSystemHive(IN OUT PLOADER_PARAMETER_BLOCK1 LoaderBlock1,
                             IN PCSTR SystemRoot);

BOOLEAN
WinLdrLoadNLSData(
    _Inout_ PLOADER_PARAMETER_BLOCK1 LoaderBlock1,
    _In_ PCSTR DirectoryPath,
    _In_ PCUNICODE_STRING AnsiFileName,
    _In_ PCUNICODE_STRING OemFileName,
    _In_ PCUNICODE_STRING LangFileName, // CaseTable
    _In_ PCUNICODE_STRING OemHalFileName);

BOOLEAN
WinLdrAddDriverToList(
    _Inout_ PLIST_ENTRY DriverListHead,
    _In_ BOOLEAN InsertAtHead,
    _In_ PCWSTR DriverName,
    _In_opt_ PCWSTR ImagePath,
    _In_opt_ PCWSTR GroupName,
    _In_ ULONG ErrorControl,
    _In_ ULONG Tag);

// winldr.c
VOID
WinLdrInitializePhase1(PLOADER_PARAMETER_BLOCK1 LoaderBlock1,
                       PLOADER_PARAMETER_BLOCK2 LoaderBlock2,
                       PSETUP_LOADER_BLOCK* SetupBlockPtr,
                       PLOADER_PARAMETER_EXTENSION1 Extension1,
                       PLOADER_PARAMETER_EXTENSION2 Extension2,
                       PCSTR Options,
                       PCSTR SystemRoot,
                       PCSTR BootPath,
                       USHORT VersionToBoot);

VOID
WinLdrpDumpMemoryDescriptors(PLOADER_PARAMETER_BLOCK1 LoaderBlock1);

VOID
WinLdrpDumpBootDriver(PLOADER_PARAMETER_BLOCK1 LoaderBlock1);

VOID
WinLdrpDumpArcDisks(PLOADER_PARAMETER_BLOCK1 LoaderBlock1);

ARC_STATUS
LoadAndBootWindowsCommon(
    IN USHORT OperatingSystemVersion,
    IN PVOID LoaderBlock,
    IN PLOADER_PARAMETER_BLOCK1 LoaderBlock1,
    IN PLOADER_PARAMETER_BLOCK2 LoaderBlock2,
    IN PSETUP_LOADER_BLOCK* SetupBlockPtr,
    IN PLOADER_PARAMETER_EXTENSION1 Extension1,
    IN PLOADER_PARAMETER_EXTENSION2 Extension2,
    IN PCSTR BootOptions,
    IN PCSTR BootPath);

VOID
WinLdrSetupMachineDependent(PLOADER_PARAMETER_BLOCK2 LoaderBlock2);

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion);

// arch/xxx/winldr.c
BOOLEAN
MempSetupPaging(IN PFN_NUMBER StartPage,
                IN PFN_NUMBER NumberOfPages,
                IN BOOLEAN KernelMapping);

VOID
MempUnmapPage(PFN_NUMBER Page);

VOID
MempDump(VOID);
