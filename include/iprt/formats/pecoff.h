/* $Id$ */
/** @file
 * IPRT - Windows NT PE & COFF Structures and Constants.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___iprt_formats_pecoff_h
#define ___iprt_formats_pecoff_h

#include <iprt/types.h>
#include <iprt/assert.h>


/** @defgroup grp_rt_formats_pecoff     PE & Microsoft COFF structurs and definitions
 * @ingroup grp_rt_formats
 * @{
 */


/**
 * PE & COFF file header.
 *
 * This starts COFF files, while in PE files it's preceeded by the PE signature
 * (see IMAGE_NT_HEADERS32, IMAGE_NT_HEADERS64).
 */
typedef struct _IMAGE_FILE_HEADER
{
    uint16_t  Machine;                      /**< 0x00 */
    uint16_t  NumberOfSections;             /**< 0x02 */
    uint32_t  TimeDateStamp;                /**< 0x04 */
    uint32_t  PointerToSymbolTable;         /**< 0x08 */
    uint32_t  NumberOfSymbols;              /**< 0x0c */
    uint16_t  SizeOfOptionalHeader;         /**< 0x10 */
    uint16_t  Characteristics;              /**< 0x12 */
} IMAGE_FILE_HEADER;                        /* size: 0x14 */
AssertCompileSize(IMAGE_FILE_HEADER, 0x14);
typedef IMAGE_FILE_HEADER *PIMAGE_FILE_HEADER;
typedef IMAGE_FILE_HEADER const *PCIMAGE_FILE_HEADER;


/** @name PE & COFF machine types.
 * Used by IMAGE_FILE_HEADER::Machine and IMAGE_SEPARATE_DEBUG_HEADER::Machine.
 * @{ */
/** X86 compatible CPU, 32-bit instructions.   */
#define IMAGE_FILE_MACHINE_I386             UINT16_C(0x014c)
/** AMD64 compatible CPU, 64-bit instructions.   */
#define IMAGE_FILE_MACHINE_AMD64            UINT16_C(0x8664)

/** Unknown target CPU. */
#define IMAGE_FILE_MACHINE_UNKNOWN          UINT16_C(0x0000)
/** Matshushita AM33 CPU. */
#define IMAGE_FILE_MACHINE_AM33             UINT16_C(0x01d3)
/** Little endian ARM CPU. */
#define IMAGE_FILE_MACHINE_ARM              UINT16_C(0x01c0)
/** ARM or Thumb stuff. */
#define IMAGE_FILE_MACHINE_THUMB            UINT16_C(0x01c2)
/** ARMv7 or higher CPU, Thumb mode. */
#define IMAGE_FILE_MACHINE_ARMNT            UINT16_C(0x01c4)
/** ARMv8 CPU, 64-bit mode. */
#define IMAGE_FILE_MACHINE_ARM64            UINT16_C(0xaa64)
/** EFI byte code. */
#define IMAGE_FILE_MACHINE_EBC              UINT16_C(0x0ebc)
/** "Itanic" CPU. */
#define IMAGE_FILE_MACHINE_IA64             UINT16_C(0x0200)
/** Mitsubishi M32R CPU, little endian. */
#define IMAGE_FILE_MACHINE_M32R             UINT16_C(0x9041)
/** MIPS CPU, compact 16-bit instructions only? */
#define IMAGE_FILE_MACHINE_MIPS16           UINT16_C(0x0266)
/** MIPS CPU with FPU, full 32-bit instructions only? */
#define IMAGE_FILE_MACHINE_MIPSFPU          UINT16_C(0x0366)
/** MIPS CPU with FPU, compact 16-bit instructions? */
#define IMAGE_FILE_MACHINE_MIPSFPU16        UINT16_C(0x0466)
/** MIPS CPU, little endian, Windows CE (?) v2 designation. */
#define IMAGE_FILE_MACHINE_WCEMIPSV2        UINT16_C(0x0169)
/** Power PC CPU, little endian. */
#define IMAGE_FILE_MACHINE_POWERPC          UINT16_C(0x01f0)
/** Power PC CPU with FPU, also little endian? */
#define IMAGE_FILE_MACHINE_POWERPCFP        UINT16_C(0x01f1)
/** MIPS R4000 CPU, little endian. */
#define IMAGE_FILE_MACHINE_R4000            UINT16_C(0x0166)
/** Hitachi SH3 CPU. */
#define IMAGE_FILE_MACHINE_SH3              UINT16_C(0x01a2)
/** Hitachi SH3 DSP. */
#define IMAGE_FILE_MACHINE_SH3DSP           UINT16_C(0x01a3)
/** Hitachi SH4 CPU. */
#define IMAGE_FILE_MACHINE_SH4              UINT16_C(0x01a6)
/** Hitachi SH5 CPU. */
#define IMAGE_FILE_MACHINE_SH5              UINT16_C(0x01a8)
/** @} */

/** @name File header characteristics (IMAGE_FILE_HEADER::Characteristics)
 * @{ */
#define IMAGE_FILE_RELOCS_STRIPPED          UINT16_C(0x0001)
#define IMAGE_FILE_EXECUTABLE_IMAGE         UINT16_C(0x0002)
#define IMAGE_FILE_LINE_NUMS_STRIPPED       UINT16_C(0x0004)
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED      UINT16_C(0x0008)
#define IMAGE_FILE_AGGRESIVE_WS_TRIM        UINT16_C(0x0010)
#define IMAGE_FILE_LARGE_ADDRESS_AWARE      UINT16_C(0x0020)
#define IMAGE_FILE_16BIT_MACHINE            UINT16_C(0x0040)
#define IMAGE_FILE_BYTES_REVERSED_LO        UINT16_C(0x0080)
#define IMAGE_FILE_32BIT_MACHINE            UINT16_C(0x0100)
#define IMAGE_FILE_DEBUG_STRIPPED           UINT16_C(0x0200)
#define IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP  UINT16_C(0x0400)
#define IMAGE_FILE_NET_RUN_FROM_SWAP        UINT16_C(0x0800)
#define IMAGE_FILE_SYSTEM                   UINT16_C(0x1000)
#define IMAGE_FILE_DLL                      UINT16_C(0x2000)
#define IMAGE_FILE_UP_SYSTEM_ONLY           UINT16_C(0x4000)
#define IMAGE_FILE_BYTES_REVERSED_HI        UINT16_C(0x8000)
/** @} */


/**
 * PE data directory.
 *
 * This is used to locate data in the loaded image so the dynamic linker or
 * others can make use of it.  However, in the case of
 * IMAGE_DIRECTORY_ENTRY_SECURITY it is referring to raw file offsets.
 */
typedef struct _IMAGE_DATA_DIRECTORY
{
    uint32_t  VirtualAddress;
    uint32_t  Size;
} IMAGE_DATA_DIRECTORY;
AssertCompileSize(IMAGE_DATA_DIRECTORY, 0x8);
typedef IMAGE_DATA_DIRECTORY *PIMAGE_DATA_DIRECTORY;
typedef IMAGE_DATA_DIRECTORY const *PCIMAGE_DATA_DIRECTORY;

/** The standard number of data directories in the optional header.
 * I.e. the dimensions of IMAGE_OPTIONAL_HEADER32::DataDirectory and
 * IMAGE_OPTIONAL_HEADER64::DataDirectory.
 */
#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES        0x10


/**
 * PE optional header, 32-bit version.
 */
typedef struct _IMAGE_OPTIONAL_HEADER32
{
    uint16_t  Magic;                        /**< 0x00 */
    uint8_t   MajorLinkerVersion;           /**< 0x02 */
    uint8_t   MinorLinkerVersion;           /**< 0x03 */
    uint32_t  SizeOfCode;                   /**< 0x04 */
    uint32_t  SizeOfInitializedData;        /**< 0x08 */
    uint32_t  SizeOfUninitializedData;      /**< 0x0c */
    uint32_t  AddressOfEntryPoint;          /**< 0x10 */
    uint32_t  BaseOfCode;                   /**< 0x14 */
    uint32_t  BaseOfData;                   /**< 0x18 */
    uint32_t  ImageBase;                    /**< 0x1c */
    uint32_t  SectionAlignment;             /**< 0x20 */
    uint32_t  FileAlignment;                /**< 0x24 */
    uint16_t  MajorOperatingSystemVersion;  /**< 0x28 */
    uint16_t  MinorOperatingSystemVersion;  /**< 0x2a */
    uint16_t  MajorImageVersion;            /**< 0x2c */
    uint16_t  MinorImageVersion;            /**< 0x2e */
    uint16_t  MajorSubsystemVersion;        /**< 0x30 */
    uint16_t  MinorSubsystemVersion;        /**< 0x32 */
    uint32_t  Win32VersionValue;            /**< 0x34 */
    uint32_t  SizeOfImage;                  /**< 0x38 */
    uint32_t  SizeOfHeaders;                /**< 0x3c */
    uint32_t  CheckSum;                     /**< 0x40 */
    uint16_t  Subsystem;                    /**< 0x44 */
    uint16_t  DllCharacteristics;           /**< 0x46 */
    uint32_t  SizeOfStackReserve;           /**< 0x48 */
    uint32_t  SizeOfStackCommit;            /**< 0x4c */
    uint32_t  SizeOfHeapReserve;            /**< 0x50 */
    uint32_t  SizeOfHeapCommit;             /**< 0x54 */
    uint32_t  LoaderFlags;                  /**< 0x58 */
    uint32_t  NumberOfRvaAndSizes;          /**< 0x5c */
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES]; /**< 0x60; 0x10*8 = 0x80 */
} IMAGE_OPTIONAL_HEADER32;                  /* size: 0xe0 */
AssertCompileSize(IMAGE_OPTIONAL_HEADER32, 0xe0);
typedef IMAGE_OPTIONAL_HEADER32 *PIMAGE_OPTIONAL_HEADER32;
typedef IMAGE_OPTIONAL_HEADER32 const *PCIMAGE_OPTIONAL_HEADER32;

/**
 * PE optional header, 64-bit version.
 */
typedef struct _IMAGE_OPTIONAL_HEADER64
{
    uint16_t  Magic;                        /**< 0x00 */
    uint8_t   MajorLinkerVersion;           /**< 0x02 */
    uint8_t   MinorLinkerVersion;           /**< 0x03 */
    uint32_t  SizeOfCode;                   /**< 0x04 */
    uint32_t  SizeOfInitializedData;        /**< 0x08 */
    uint32_t  SizeOfUninitializedData;      /**< 0x0c */
    uint32_t  AddressOfEntryPoint;          /**< 0x10 */
    uint32_t  BaseOfCode;                   /**< 0x14 */
    uint64_t  ImageBase;                    /**< 0x18 */
    uint32_t  SectionAlignment;             /**< 0x20 */
    uint32_t  FileAlignment;                /**< 0x24 */
    uint16_t  MajorOperatingSystemVersion;  /**< 0x28 */
    uint16_t  MinorOperatingSystemVersion;  /**< 0x2a */
    uint16_t  MajorImageVersion;            /**< 0x2c */
    uint16_t  MinorImageVersion;            /**< 0x2e */
    uint16_t  MajorSubsystemVersion;        /**< 0x30 */
    uint16_t  MinorSubsystemVersion;        /**< 0x32 */
    uint32_t  Win32VersionValue;            /**< 0x34 */
    uint32_t  SizeOfImage;                  /**< 0x38 */
    uint32_t  SizeOfHeaders;                /**< 0x3c */
    uint32_t  CheckSum;                     /**< 0x40 */
    uint16_t  Subsystem;                    /**< 0x44 */
    uint16_t  DllCharacteristics;           /**< 0x46 */
    uint64_t  SizeOfStackReserve;           /**< 0x48 */
    uint64_t  SizeOfStackCommit;            /**< 0x50 */
    uint64_t  SizeOfHeapReserve;            /**< 0x58 */
    uint64_t  SizeOfHeapCommit;             /**< 0x60 */
    uint32_t  LoaderFlags;                  /**< 0x68 */
    uint32_t  NumberOfRvaAndSizes;          /**< 0x6c */
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES]; /**< 0x70; 0x10*8 = 0x80 */
} IMAGE_OPTIONAL_HEADER64;                  /* size: 0xf0 */
AssertCompileSize(IMAGE_OPTIONAL_HEADER64, 0xf0);
typedef IMAGE_OPTIONAL_HEADER64 *PIMAGE_OPTIONAL_HEADER64;
typedef IMAGE_OPTIONAL_HEADER64 const *PCIMAGE_OPTIONAL_HEADER64;

/** @name Optional header magic values.
 * @{ */
#define IMAGE_NT_OPTIONAL_HDR32_MAGIC       UINT16_C(0x010b)
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC       UINT16_C(0x020b)
/** @}  */

/** @name IMAGE_SUBSYSTEM_XXX - Optional header subsystems.
 * IMAGE_OPTIONAL_HEADER32::Subsystem, IMAGE_OPTIONAL_HEADER64::Subsystem
 * @{ */
#define IMAGE_SUBSYSTEM_UNKNOWN             UINT16_C(0x0000)
#define IMAGE_SUBSYSTEM_NATIVE              UINT16_C(0x0001)
#define IMAGE_SUBSYSTEM_WINDOWS_GUI         UINT16_C(0x0002)
#define IMAGE_SUBSYSTEM_WINDOWS_CUI         UINT16_C(0x0003)
#define IMAGE_SUBSYSTEM_OS2_GUI             UINT16_C(0x0004)
#define IMAGE_SUBSYSTEM_OS2_CUI             UINT16_C(0x0005)
#define IMAGE_SUBSYSTEM_POSIX_CUI           UINT16_C(0x0007)
/** @} */

/** @name Optional header characteristics.
 * @{ */
#define IMAGE_LIBRARY_PROCESS_INIT                      UINT16_C(0x0001)
#define IMAGE_LIBRARY_PROCESS_TERM                      UINT16_C(0x0002)
#define IMAGE_LIBRARY_THREAD_INIT                       UINT16_C(0x0004)
#define IMAGE_LIBRARY_THREAD_TERM                       UINT16_C(0x0008)
#define IMAGE_DLLCHARACTERISTICS_RESERVED               UINT16_C(0x0010)
#define IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA        UINT16_C(0x0020)
#define IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE           UINT16_C(0x0040)
#define IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY        UINT16_C(0x0080)
#define IMAGE_DLLCHARACTERISTICS_NX_COMPAT              UINT16_C(0x0100)
#define IMAGE_DLLCHARACTERISTICS_NO_ISOLATION           UINT16_C(0x0200)
#define IMAGE_DLLCHARACTERISTICS_NO_SEH                 UINT16_C(0x0400)
#define IMAGE_DLLCHARACTERISTICS_NO_BIND                UINT16_C(0x0800)
#define IMAGE_DLLCHARACTERISTICS_APPCONTAINER           UINT16_C(0x1000)
#define IMAGE_DLLCHARACTERISTICS_WDM_DRIVER             UINT16_C(0x2000)
#define IMAGE_DLLCHARACTERISTICS_GUARD_CF               UINT16_C(0x4000)
#define IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE  UINT16_C(0x8000)
/** @} */


/** @name IMAGE_DIRECTORY_ENTRY_XXX - Data directory indexes.
 * Used to index IMAGE_OPTIONAL_HEADER32::DataDirectory and
 * IMAGE_OPTIONAL_HEADER64::DataDirectory
 * @{ */
#define IMAGE_DIRECTORY_ENTRY_EXPORT            0x0
#define IMAGE_DIRECTORY_ENTRY_IMPORT            0x1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE          0x2
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION         0x3
#define IMAGE_DIRECTORY_ENTRY_SECURITY          0x4
#define IMAGE_DIRECTORY_ENTRY_BASERELOC         0x5
#define IMAGE_DIRECTORY_ENTRY_DEBUG             0x6
#define IMAGE_DIRECTORY_ENTRY_ARCHITECTURE      0x7
#define IMAGE_DIRECTORY_ENTRY_COPYRIGHT         IMAGE_DIRECTORY_ENTRY_ARCHITECTURE
#define IMAGE_DIRECTORY_ENTRY_GLOBALPTR         0x8
#define IMAGE_DIRECTORY_ENTRY_TLS               0x9
#define IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG       0xa
#define IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT      0xb
#define IMAGE_DIRECTORY_ENTRY_IAT               0xc
#define IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT      0xd
#define IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR    0xe
/** @} */


/**
 * PE (NT) headers, 32-bit version.
 */
typedef struct _IMAGE_NT_HEADERS32
{
    uint32_t  Signature;                    /**< 0x00 */
    IMAGE_FILE_HEADER FileHeader;           /**< 0x04 */
    IMAGE_OPTIONAL_HEADER32 OptionalHeader; /**< 0x18 */
} IMAGE_NT_HEADERS32;                       /* size:  0xf8 */
AssertCompileSize(IMAGE_NT_HEADERS32, 0xf8);
AssertCompileMemberOffset(IMAGE_NT_HEADERS32, FileHeader, 4);
AssertCompileMemberOffset(IMAGE_NT_HEADERS32, OptionalHeader, 24);
typedef IMAGE_NT_HEADERS32 *PIMAGE_NT_HEADERS32;
typedef IMAGE_NT_HEADERS32 const *PCIMAGE_NT_HEADERS32;

/**
 * PE (NT) headers, 64-bit version.
 */
typedef struct _IMAGE_NT_HEADERS64
{
    uint32_t  Signature;                    /**< 0x00 */
    IMAGE_FILE_HEADER FileHeader;           /**< 0x04 */
    IMAGE_OPTIONAL_HEADER64 OptionalHeader; /**< 0x18 */
} IMAGE_NT_HEADERS64;                       /**< 0x108 */
AssertCompileSize(IMAGE_NT_HEADERS64, 0x108);
AssertCompileMemberOffset(IMAGE_NT_HEADERS64, FileHeader, 4);
AssertCompileMemberOffset(IMAGE_NT_HEADERS64, OptionalHeader, 24);
typedef IMAGE_NT_HEADERS64 *PIMAGE_NT_HEADERS64;
typedef IMAGE_NT_HEADERS64 const *PCIMAGE_NT_HEADERS64;

/** The PE signature.
 * Used by IMAGE_NT_HEADERS32::Signature, IMAGE_NT_HEADERS64::Signature. */
#define IMAGE_NT_SIGNATURE                  UINT32_C(0x00004550)


/** Section header short name length (IMAGE_SECTION_HEADER::Name). */
#define IMAGE_SIZEOF_SHORT_NAME             0x8

/**
 * PE & COFF section header.
 */
typedef struct _IMAGE_SECTION_HEADER
{
    uint8_t  Name[IMAGE_SIZEOF_SHORT_NAME];
    union
    {
        uint32_t  PhysicalAddress;
        uint32_t  VirtualSize;
    } Misc;
    uint32_t  VirtualAddress;
    uint32_t  SizeOfRawData;
    uint32_t  PointerToRawData;
    uint32_t  PointerToRelocations;
    uint32_t  PointerToLinenumbers;
    uint16_t  NumberOfRelocations;
    uint16_t  NumberOfLinenumbers;
    uint32_t  Characteristics;
} IMAGE_SECTION_HEADER;
AssertCompileSize(IMAGE_SECTION_HEADER, 40);
typedef IMAGE_SECTION_HEADER *PIMAGE_SECTION_HEADER;
typedef IMAGE_SECTION_HEADER const *PCIMAGE_SECTION_HEADER;

/** @name IMAGE_SCN_XXX - Section header characteristics.
 * Used by IMAGE_SECTION_HEADER::Characteristics.
 * @{ */
#define IMAGE_SCN_TYPE_REG                  UINT32_C(0x00000000)
#define IMAGE_SCN_TYPE_DSECT                UINT32_C(0x00000001)
#define IMAGE_SCN_TYPE_NOLOAD               UINT32_C(0x00000002)
#define IMAGE_SCN_TYPE_GROUP                UINT32_C(0x00000004)
#define IMAGE_SCN_TYPE_NO_PAD               UINT32_C(0x00000008)
#define IMAGE_SCN_TYPE_COPY                 UINT32_C(0x00000010)

#define IMAGE_SCN_CNT_CODE                  UINT32_C(0x00000020)
#define IMAGE_SCN_CNT_INITIALIZED_DATA      UINT32_C(0x00000040)
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA    UINT32_C(0x00000080)

#define IMAGE_SCN_LNK_OTHER                 UINT32_C(0x00000100)
#define IMAGE_SCN_LNK_INFO                  UINT32_C(0x00000200)
#define IMAGE_SCN_TYPE_OVER                 UINT32_C(0x00000400)
#define IMAGE_SCN_LNK_REMOVE                UINT32_C(0x00000800)
#define IMAGE_SCN_LNK_COMDAT                UINT32_C(0x00001000)
#define IMAGE_SCN_MEM_PROTECTED             UINT32_C(0x00004000)
#define IMAGE_SCN_NO_DEFER_SPEC_EXC         UINT32_C(0x00004000)
#define IMAGE_SCN_GPREL                     UINT32_C(0x00008000)
#define IMAGE_SCN_MEM_FARDATA               UINT32_C(0x00008000)
#define IMAGE_SCN_MEM_SYSHEAP               UINT32_C(0x00010000)
#define IMAGE_SCN_MEM_PURGEABLE             UINT32_C(0x00020000)
#define IMAGE_SCN_MEM_16BIT                 UINT32_C(0x00020000)
#define IMAGE_SCN_MEM_LOCKED                UINT32_C(0x00040000)
#define IMAGE_SCN_MEM_PRELOAD               UINT32_C(0x00080000)

#define IMAGE_SCN_ALIGN_1BYTES              UINT32_C(0x00100000)
#define IMAGE_SCN_ALIGN_2BYTES              UINT32_C(0x00200000)
#define IMAGE_SCN_ALIGN_4BYTES              UINT32_C(0x00300000)
#define IMAGE_SCN_ALIGN_8BYTES              UINT32_C(0x00400000)
#define IMAGE_SCN_ALIGN_16BYTES             UINT32_C(0x00500000)
#define IMAGE_SCN_ALIGN_32BYTES             UINT32_C(0x00600000)
#define IMAGE_SCN_ALIGN_64BYTES             UINT32_C(0x00700000)
#define IMAGE_SCN_ALIGN_128BYTES            UINT32_C(0x00800000)
#define IMAGE_SCN_ALIGN_256BYTES            UINT32_C(0x00900000)
#define IMAGE_SCN_ALIGN_512BYTES            UINT32_C(0x00A00000)
#define IMAGE_SCN_ALIGN_1024BYTES           UINT32_C(0x00B00000)
#define IMAGE_SCN_ALIGN_2048BYTES           UINT32_C(0x00C00000)
#define IMAGE_SCN_ALIGN_4096BYTES           UINT32_C(0x00D00000)
#define IMAGE_SCN_ALIGN_8192BYTES           UINT32_C(0x00E00000)
#define IMAGE_SCN_ALIGN_MASK                UINT32_C(0x00F00000)
#define IMAGE_SCN_ALIGN_SHIFT               20

#define IMAGE_SCN_LNK_NRELOC_OVFL           UINT32_C(0x01000000)
#define IMAGE_SCN_MEM_DISCARDABLE           UINT32_C(0x02000000)
#define IMAGE_SCN_MEM_NOT_CACHED            UINT32_C(0x04000000)
#define IMAGE_SCN_MEM_NOT_PAGED             UINT32_C(0x08000000)
#define IMAGE_SCN_MEM_SHARED                UINT32_C(0x10000000)
#define IMAGE_SCN_MEM_EXECUTE               UINT32_C(0x20000000)
#define IMAGE_SCN_MEM_READ                  UINT32_C(0x40000000)
#define IMAGE_SCN_MEM_WRITE                 UINT32_C(0x80000000)
/** @} */


/**
 * PE image base relocations block header.
 *
 * This found in IMAGE_DIRECTORY_ENTRY_BASERELOC. Each entry is follow
 * immediately by an array of 16-bit words, where the lower 12-bits are used
 * for the page offset and the upper 4-bits for the base relocation type
 * (IMAGE_REL_BASE_XXX).  The block should be padded with
 * IMAGE_REL_BASED_ABSOLUTE entries to ensure 32-bit alignment of this header.
 */
typedef struct _IMAGE_BASE_RELOCATION
{
    /** The RVA of the page/block the following ase relocations applies to. */
    uint32_t  VirtualAddress;
    /** The size of this relocation block, including this header. */
    uint32_t  SizeOfBlock;
} IMAGE_BASE_RELOCATION;
AssertCompileSize(IMAGE_BASE_RELOCATION, 8);
typedef IMAGE_BASE_RELOCATION *PIMAGE_BASE_RELOCATION;
typedef IMAGE_BASE_RELOCATION const *PCIMAGE_BASE_RELOCATION;

/** @name IMAGE_REL_BASED_XXX - PE base relocations.
 * Found in the IMAGE_DIRECTORY_ENTRY_BASERELOC data directory.
 * @{ */
#define IMAGE_REL_BASED_ABSOLUTE            UINT16_C(0x0)
#define IMAGE_REL_BASED_HIGH                UINT16_C(0x1)
#define IMAGE_REL_BASED_LOW                 UINT16_C(0x2)
#define IMAGE_REL_BASED_HIGHLOW             UINT16_C(0x3)
#define IMAGE_REL_BASED_HIGHADJ             UINT16_C(0x4)
#define IMAGE_REL_BASED_MIPS_JMPADDR        UINT16_C(0x5)
#define IMAGE_REL_BASED_MIPS_JMPADDR16      UINT16_C(0x9)
#define IMAGE_REL_BASED_IA64_IMM64          UINT16_C(0x9)
#define IMAGE_REL_BASED_DIR64               UINT16_C(0xa)
#define IMAGE_REL_BASED_HIGH3ADJ            UINT16_C(0xb)
/** @} */

/**
 * PE export directory entry.
 */
typedef struct _IMAGE_EXPORT_DIRECTORY
{
    uint32_t  Characteristics;
    uint32_t  TimeDateStamp;
    uint16_t  MajorVersion;
    uint16_t  MinorVersion;
    uint32_t  Name;
    uint32_t  Base;
    uint32_t  NumberOfFunctions;
    uint32_t  NumberOfNames;
    uint32_t  AddressOfFunctions;
    uint32_t  AddressOfNames;
    uint32_t  AddressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY;
AssertCompileSize(IMAGE_EXPORT_DIRECTORY, 40);
typedef IMAGE_EXPORT_DIRECTORY *PIMAGE_EXPORT_DIRECTORY;
typedef IMAGE_EXPORT_DIRECTORY const *PCIMAGE_EXPORT_DIRECTORY;


/**
 * PE import directory entry.
 */
typedef struct _IMAGE_IMPORT_DESCRIPTOR
{
    union
    {
        uint32_t  Characteristics;
        uint32_t  OriginalFirstThunk;
    } u;
    uint32_t  TimeDateStamp;
    uint32_t  ForwarderChain;
    uint32_t  Name;
    uint32_t  FirstThunk;
} IMAGE_IMPORT_DESCRIPTOR;
AssertCompileSize(IMAGE_IMPORT_DESCRIPTOR, 20);
typedef IMAGE_IMPORT_DESCRIPTOR *PIMAGE_IMPORT_DESCRIPTOR;
typedef IMAGE_IMPORT_DESCRIPTOR const *PCIMAGE_IMPORT_DESCRIPTOR;

/**
 * Something we currently don't make use of...
 */
typedef struct _IMAGE_IMPORT_BY_NAME
{
    uint16_t  Hint;
    uint8_t   Name[1];
} IMAGE_IMPORT_BY_NAME;
AssertCompileSize(IMAGE_IMPORT_BY_NAME, 4);
typedef IMAGE_IMPORT_BY_NAME *PIMAGE_IMPORT_BY_NAME;
typedef IMAGE_IMPORT_BY_NAME const *PCIMAGE_IMPORT_BY_NAME;


#if 0
/* The image_thunk_data32/64 structures are not very helpful except for getting RSI.
   keep them around till all the code has been converted. */
typedef struct _IMAGE_THUNK_DATA64
{
    union
    {
        uint64_t  ForwarderString;
        uint64_t  Function;
        uint64_t  Ordinal;
        uint64_t  AddressOfData;
    } u1;
} IMAGE_THUNK_DATA64;
typedef IMAGE_THUNK_DATA64 *PIMAGE_THUNK_DATA64;
typedef IMAGE_THUNK_DATA64 const *PCIMAGE_THUNK_DATA64;

typedef struct _IMAGE_THUNK_DATA32
{
    union
    {
        uint32_t  ForwarderString;
        uint32_t  Function;
        uint32_t  Ordinal;
        uint32_t  AddressOfData;
    } u1;
} IMAGE_THUNK_DATA32;
typedef IMAGE_THUNK_DATA32 *PIMAGE_THUNK_DATA32;
typedef IMAGE_THUNK_DATA32 const *PCIMAGE_THUNK_DATA32;
#endif

/** @name PE import directory macros.
 * @{ */
#define IMAGE_ORDINAL_FLAG32                UINT32_C(0x80000000)
#define IMAGE_ORDINAL32(ord)                ((ord) &  UINT32_C(0xffff))
#define IMAGE_SNAP_BY_ORDINAL32(ord)        (!!((ord) & IMAGE_ORDINAL_FLAG32))

#define IMAGE_ORDINAL_FLAG64                UINT64_C(0x8000000000000000)
#define IMAGE_ORDINAL64(ord)                ((ord) &  UINT32_C(0xffff))
#define IMAGE_SNAP_BY_ORDINAL64(ord)        (!!((ord) & IMAGE_ORDINAL_FLAG64))
/** @} */

/** @name Image load config directories
 * @{ */

/** @since Windows 10 (preview 9879) */
typedef struct _IMAGE_LOAD_CONFIG_CODE_INTEGRITY
{
    uint16_t  Flags;
    uint16_t  Catalog;
    uint32_t  CatalogOffset;
    uint32_t  Reserved;
} IMAGE_LOAD_CONFIG_CODE_INTEGRITY;
AssertCompileSize(IMAGE_LOAD_CONFIG_CODE_INTEGRITY, 12);
typedef IMAGE_LOAD_CONFIG_CODE_INTEGRITY *PIMAGE_LOAD_CONFIG_CODE_INTEGRITY;
typedef IMAGE_LOAD_CONFIG_CODE_INTEGRITY const *PCIMAGE_LOAD_CONFIG_CODE_INTEGRITY;

typedef struct _IMAGE_LOAD_CONFIG_DIRECTORY32_V1
{
    uint32_t  Size;
    uint32_t  TimeDateStamp;
    uint16_t  MajorVersion;
    uint16_t  MinorVersion;
    uint32_t  GlobalFlagsClear;
    uint32_t  GlobalFlagsSet;
    uint32_t  CriticalSectionDefaultTimeout;
    uint32_t  DeCommitFreeBlockThreshold;
    uint32_t  DeCommitTotalFreeThreshold;
    uint32_t  LockPrefixTable;
    uint32_t  MaximumAllocationSize;
    uint32_t  VirtualMemoryThreshold;
    uint32_t  ProcessHeapFlags;
    uint32_t  ProcessAffinityMask;
    uint16_t  CSDVersion;
    uint16_t  Reserved1;
    uint32_t  EditList;
    uint32_t  SecurityCookie;
} IMAGE_LOAD_CONFIG_DIRECTORY32_V1;
AssertCompileSize(IMAGE_LOAD_CONFIG_DIRECTORY32_V1, 0x40);
typedef IMAGE_LOAD_CONFIG_DIRECTORY32_V1 *PIMAGE_LOAD_CONFIG_DIRECTORY32_V1;
typedef IMAGE_LOAD_CONFIG_DIRECTORY32_V1 const *PCIMAGE_LOAD_CONFIG_DIRECTORY32_V1;

typedef struct _IMAGE_LOAD_CONFIG_DIRECTORY32_V2
{
    uint32_t  Size;
    uint32_t  TimeDateStamp;
    uint16_t  MajorVersion;
    uint16_t  MinorVersion;
    uint32_t  GlobalFlagsClear;
    uint32_t  GlobalFlagsSet;
    uint32_t  CriticalSectionDefaultTimeout;
    uint32_t  DeCommitFreeBlockThreshold;
    uint32_t  DeCommitTotalFreeThreshold;
    uint32_t  LockPrefixTable;
    uint32_t  MaximumAllocationSize;
    uint32_t  VirtualMemoryThreshold;
    uint32_t  ProcessHeapFlags;
    uint32_t  ProcessAffinityMask;
    uint16_t  CSDVersion;
    uint16_t  Reserved1;
    uint32_t  EditList;
    uint32_t  SecurityCookie;
    uint32_t  SEHandlerTable;
    uint32_t  SEHandlerCount;
} IMAGE_LOAD_CONFIG_DIRECTORY32_V2;
AssertCompileSize(IMAGE_LOAD_CONFIG_DIRECTORY32_V2, 0x48);
typedef IMAGE_LOAD_CONFIG_DIRECTORY32_V2 *PIMAGE_LOAD_CONFIG_DIRECTORY32_V2;
typedef IMAGE_LOAD_CONFIG_DIRECTORY32_V2 const *PCIMAGE_LOAD_CONFIG_DIRECTORY32_V2;

typedef struct _IMAGE_LOAD_CONFIG_DIRECTORY32_V3
{
    uint32_t  Size;
    uint32_t  TimeDateStamp;
    uint16_t  MajorVersion;
    uint16_t  MinorVersion;
    uint32_t  GlobalFlagsClear;
    uint32_t  GlobalFlagsSet;
    uint32_t  CriticalSectionDefaultTimeout;
    uint32_t  DeCommitFreeBlockThreshold;
    uint32_t  DeCommitTotalFreeThreshold;
    uint32_t  LockPrefixTable;
    uint32_t  MaximumAllocationSize;
    uint32_t  VirtualMemoryThreshold;
    uint32_t  ProcessHeapFlags;
    uint32_t  ProcessAffinityMask;
    uint16_t  CSDVersion;
    uint16_t  Reserved1;
    uint32_t  EditList;
    uint32_t  SecurityCookie;
    uint32_t  SEHandlerTable;
    uint32_t  SEHandlerCount;
    uint32_t  GuardCFCCheckFunctionPointer;
    uint32_t  Reserved2;
    uint32_t  GuardCFFunctionTable;
    uint32_t  GuardCFFunctionCount;
    uint32_t  GuardFlags;
} IMAGE_LOAD_CONFIG_DIRECTORY32_V3;
AssertCompileSize(IMAGE_LOAD_CONFIG_DIRECTORY32_V3, 0x5c);
typedef IMAGE_LOAD_CONFIG_DIRECTORY32_V3 *PIMAGE_LOAD_CONFIG_DIRECTORY32_V3;
typedef IMAGE_LOAD_CONFIG_DIRECTORY32_V3 const *PCIMAGE_LOAD_CONFIG_DIRECTORY32_V3;

/** @since Windows 10 (preview 9879) */
typedef struct _IMAGE_LOAD_CONFIG_DIRECTORY32_V4
{
    uint32_t  Size;
    uint32_t  TimeDateStamp;
    uint16_t  MajorVersion;
    uint16_t  MinorVersion;
    uint32_t  GlobalFlagsClear;
    uint32_t  GlobalFlagsSet;
    uint32_t  CriticalSectionDefaultTimeout;
    uint32_t  DeCommitFreeBlockThreshold;
    uint32_t  DeCommitTotalFreeThreshold;
    uint32_t  LockPrefixTable;
    uint32_t  MaximumAllocationSize;
    uint32_t  VirtualMemoryThreshold;
    uint32_t  ProcessHeapFlags;
    uint32_t  ProcessAffinityMask;
    uint16_t  CSDVersion;
    uint16_t  Reserved1;
    uint32_t  EditList;
    uint32_t  SecurityCookie;
    uint32_t  SEHandlerTable;
    uint32_t  SEHandlerCount;
    uint32_t  GuardCFCCheckFunctionPointer;
    uint32_t  Reserved2;
    uint32_t  GuardCFFunctionTable;
    uint32_t  GuardCFFunctionCount;
    uint32_t  GuardFlags;
    IMAGE_LOAD_CONFIG_CODE_INTEGRITY  CodeIntegrity;
} IMAGE_LOAD_CONFIG_DIRECTORY32_V4;
AssertCompileSize(IMAGE_LOAD_CONFIG_DIRECTORY32_V4, 0x68);
typedef IMAGE_LOAD_CONFIG_DIRECTORY32_V4 *PIMAGE_LOAD_CONFIG_DIRECTORY32_V4;
typedef IMAGE_LOAD_CONFIG_DIRECTORY32_V4 const *PCIMAGE_LOAD_CONFIG_DIRECTORY32_V4;

typedef IMAGE_LOAD_CONFIG_DIRECTORY32_V4   IMAGE_LOAD_CONFIG_DIRECTORY32;
typedef PIMAGE_LOAD_CONFIG_DIRECTORY32_V4  PIMAGE_LOAD_CONFIG_DIRECTORY32;
typedef PCIMAGE_LOAD_CONFIG_DIRECTORY32_V4 PCIMAGE_LOAD_CONFIG_DIRECTORY32;

/* No _IMAGE_LOAD_CONFIG_DIRECTORY64_V1 exists. */

typedef struct _IMAGE_LOAD_CONFIG_DIRECTORY64_V2
{
    uint32_t  Size;
    uint32_t  TimeDateStamp;
    uint16_t  MajorVersion;
    uint16_t  MinorVersion;
    uint32_t  GlobalFlagsClear;
    uint32_t  GlobalFlagsSet;
    uint32_t  CriticalSectionDefaultTimeout;
    uint64_t  DeCommitFreeBlockThreshold;
    uint64_t  DeCommitTotalFreeThreshold;
    uint64_t  LockPrefixTable;
    uint64_t  MaximumAllocationSize;
    uint64_t  VirtualMemoryThreshold;
    uint64_t  ProcessAffinityMask;
    uint32_t  ProcessHeapFlags;
    uint16_t  CSDVersion;
    uint16_t  Reserved1;
    uint64_t  EditList;
    uint64_t  SecurityCookie;
    uint64_t  SEHandlerTable;
    uint64_t  SEHandlerCount;
} IMAGE_LOAD_CONFIG_DIRECTORY64_V2;
AssertCompileSize(IMAGE_LOAD_CONFIG_DIRECTORY64_V2, 0x70);
typedef IMAGE_LOAD_CONFIG_DIRECTORY64_V2 *PIMAGE_LOAD_CONFIG_DIRECTORY64_V2;
typedef IMAGE_LOAD_CONFIG_DIRECTORY64_V2 const *PCIMAGE_LOAD_CONFIG_DIRECTORY64_V2;

#pragma pack(4) /* Why not 8 byte alignment, baka microsofties?!? */
typedef struct _IMAGE_LOAD_CONFIG_DIRECTORY64_V3
{
    uint32_t  Size;
    uint32_t  TimeDateStamp;
    uint16_t  MajorVersion;
    uint16_t  MinorVersion;
    uint32_t  GlobalFlagsClear;
    uint32_t  GlobalFlagsSet;
    uint32_t  CriticalSectionDefaultTimeout;
    uint64_t  DeCommitFreeBlockThreshold;
    uint64_t  DeCommitTotalFreeThreshold;
    uint64_t  LockPrefixTable;
    uint64_t  MaximumAllocationSize;
    uint64_t  VirtualMemoryThreshold;
    uint64_t  ProcessAffinityMask;
    uint32_t  ProcessHeapFlags;
    uint16_t  CSDVersion;
    uint16_t  Reserved1;
    uint64_t  EditList;
    uint64_t  SecurityCookie;
    uint64_t  SEHandlerTable;
    uint64_t  SEHandlerCount;
    uint64_t  GuardCFCCheckFunctionPointer;
    uint64_t  Reserved2;
    uint64_t  GuardCFFunctionTable;
    uint64_t  GuardCFFunctionCount;
    uint32_t  GuardFlags;
} IMAGE_LOAD_CONFIG_DIRECTORY64_V3;
#pragma pack()
AssertCompileSize(IMAGE_LOAD_CONFIG_DIRECTORY64_V3, 0x94);
typedef IMAGE_LOAD_CONFIG_DIRECTORY64_V3 *PIMAGE_LOAD_CONFIG_DIRECTORY64_V3;
typedef IMAGE_LOAD_CONFIG_DIRECTORY64_V3 const *PCIMAGE_LOAD_CONFIG_DIRECTORY64_V3;

/** @since  Windows 10 (Preview (9879). */
typedef struct _IMAGE_LOAD_CONFIG_DIRECTORY64_V4
{
    uint32_t  Size;
    uint32_t  TimeDateStamp;
    uint16_t  MajorVersion;
    uint16_t  MinorVersion;
    uint32_t  GlobalFlagsClear;
    uint32_t  GlobalFlagsSet;
    uint32_t  CriticalSectionDefaultTimeout;
    uint64_t  DeCommitFreeBlockThreshold;
    uint64_t  DeCommitTotalFreeThreshold;
    uint64_t  LockPrefixTable;
    uint64_t  MaximumAllocationSize;
    uint64_t  VirtualMemoryThreshold;
    uint64_t  ProcessAffinityMask;
    uint32_t  ProcessHeapFlags;
    uint16_t  CSDVersion;
    uint16_t  Reserved1;
    uint64_t  EditList;
    uint64_t  SecurityCookie;
    uint64_t  SEHandlerTable;
    uint64_t  SEHandlerCount;
    uint64_t  GuardCFCCheckFunctionPointer;
    uint64_t  Reserved2;
    uint64_t  GuardCFFunctionTable;
    uint64_t  GuardCFFunctionCount;
    uint32_t  GuardFlags;
    IMAGE_LOAD_CONFIG_CODE_INTEGRITY  CodeIntegrity;
} IMAGE_LOAD_CONFIG_DIRECTORY64_V4;
AssertCompileSize(IMAGE_LOAD_CONFIG_DIRECTORY64_V4, 0xa0);
typedef IMAGE_LOAD_CONFIG_DIRECTORY64_V4 *PIMAGE_LOAD_CONFIG_DIRECTORY64_V4;
typedef IMAGE_LOAD_CONFIG_DIRECTORY64_V4 const *PCIMAGE_LOAD_CONFIG_DIRECTORY64_V4;

typedef IMAGE_LOAD_CONFIG_DIRECTORY64_V4   IMAGE_LOAD_CONFIG_DIRECTORY64;
typedef PIMAGE_LOAD_CONFIG_DIRECTORY64_V4  PIMAGE_LOAD_CONFIG_DIRECTORY64;
typedef PCIMAGE_LOAD_CONFIG_DIRECTORY64_V4 PCIMAGE_LOAD_CONFIG_DIRECTORY64;

/** @} */


/**
 * PE certificate directory.
 *
 * Found in IMAGE_DIRECTORY_ENTRY_SECURITY.
 */
typedef struct WIN_CERTIFICATE
{
    uint32_t    dwLength;
    uint16_t    wRevision;
    uint16_t    wCertificateType;
    uint8_t     bCertificate[8];
} WIN_CERTIFICATE;
AssertCompileSize(WIN_CERTIFICATE, 16);
typedef WIN_CERTIFICATE *PWIN_CERTIFICATE;
typedef WIN_CERTIFICATE const *PCWIN_CERTIFICATE;

/** @name WIN_CERT_REVISION_XXX - Certificate data directory revision.
 * Used WIN_CERTIFICATE::wRevision found in the
 * IMAGE_DIRECTORY_ENTRY_SECURITY data directory. */
#define WIN_CERT_REVISION_1_0               UINT16_C(0x0100)
#define WIN_CERT_REVISION_2_0               UINT16_C(0x0200)
/** @} */

/** @name WIN_CERT_TYPE_XXX - Signature type.
 * Used by WIN_CERTIFICATE::wCertificateType.
 * @{ */
#define WIN_CERT_TYPE_X509                  UINT16_C(1)
#define WIN_CERT_TYPE_PKCS_SIGNED_DATA      UINT16_C(2)
#define WIN_CERT_TYPE_RESERVED_1            UINT16_C(3)
#define WIN_CERT_TYPE_TS_STACK_SIGNED       UINT16_C(4)
#define WIN_CERT_TYPE_EFI_PKCS115           UINT16_C(0x0ef0)
#define WIN_CERT_TYPE_EFI_GUID              UINT16_C(0x0ef1)
/** @} */

/** The alignment of the certificate table.
 * @remarks Found thru signtool experiments.  */
#define WIN_CERTIFICATE_ALIGNMENT           UINT32_C(8)


/**
 * Debug directory.
 *
 * Found in IMAGE_DIRECTORY_ENTRY_DEBUG.
 */
typedef struct _IMAGE_DEBUG_DIRECTORY
{
    uint32_t  Characteristics;
    uint32_t  TimeDateStamp;
    uint16_t  MajorVersion;
    uint16_t  MinorVersion;
    uint32_t  Type;
    uint32_t  SizeOfData;
    uint32_t  AddressOfRawData;
    uint32_t  PointerToRawData;
} IMAGE_DEBUG_DIRECTORY;
AssertCompileSize(IMAGE_DEBUG_DIRECTORY, 28);
typedef IMAGE_DEBUG_DIRECTORY *PIMAGE_DEBUG_DIRECTORY;
typedef IMAGE_DEBUG_DIRECTORY const *PCIMAGE_DEBUG_DIRECTORY;

/** @name IMAGE_DEBUG_TYPE_XXX - Debug format types.
 * Used by IMAGE_DEBUG_DIRECTORY::Type.
 * @{  */
#define IMAGE_DEBUG_TYPE_UNKNOWN            UINT32_C(0x0)
#define IMAGE_DEBUG_TYPE_COFF               UINT32_C(0x1)
#define IMAGE_DEBUG_TYPE_CODEVIEW           UINT32_C(0x2)
#define IMAGE_DEBUG_TYPE_FPO                UINT32_C(0x3)
#define IMAGE_DEBUG_TYPE_MISC               UINT32_C(0x4)
#define IMAGE_DEBUG_TYPE_EXCEPTION          UINT32_C(0x5)
#define IMAGE_DEBUG_TYPE_FIXUP              UINT32_C(0x6)
#define IMAGE_DEBUG_TYPE_OMAP_TO_SRC        UINT32_C(0x7)
#define IMAGE_DEBUG_TYPE_OMAP_FROM_SRC      UINT32_C(0x8)
#define IMAGE_DEBUG_TYPE_BORLAND            UINT32_C(0x9)
#define IMAGE_DEBUG_TYPE_RESERVED10         UINT32_C(0x10)
/** @} */

/** @name IMAGE_DEBUG_MISC_XXX - Misc debug data type.
 * Used by IMAGE_DEBUG_MISC::DataType.
 * @{ */
#define IMAGE_DEBUG_MISC_EXENAME            UINT32_C(1)
/** @} */


/**
 * The format of IMAGE_DEBUG_TYPE_MISC debug info.
 */
typedef struct _IMAGE_DEBUG_MISC
{
    uint32_t   DataType;
    uint32_t   Length;
    uint8_t    Unicode;
    uint8_t    Reserved[3];
    uint8_t    Data[1];
} IMAGE_DEBUG_MISC;
AssertCompileSize(IMAGE_DEBUG_MISC, 16);
typedef IMAGE_DEBUG_MISC *PIMAGE_DEBUG_MISC;
typedef IMAGE_DEBUG_MISC const *PCIMAGE_DEBUG_MISC;



/**
 * The header of a .DBG file (NT4).
 */
typedef struct _IMAGE_SEPARATE_DEBUG_HEADER
{
    uint16_t    Signature;              /**< 0x00 */
    uint16_t    Flags;                  /**< 0x02 */
    uint16_t    Machine;                /**< 0x04 */
    uint16_t    Characteristics;        /**< 0x06 */
    uint32_t    TimeDateStamp;          /**< 0x08 */
    uint32_t    CheckSum;               /**< 0x0c */
    uint32_t    ImageBase;              /**< 0x10 */
    uint32_t    SizeOfImage;            /**< 0x14 */
    uint32_t    NumberOfSections;       /**< 0x18 */
    uint32_t    ExportedNamesSize;      /**< 0x1c */
    uint32_t    DebugDirectorySize;     /**< 0x20 */
    uint32_t    SectionAlignment;       /**< 0x24 */
    uint32_t    Reserved[2];            /**< 0x28 */
} IMAGE_SEPARATE_DEBUG_HEADER;          /* size: 0x30 */
AssertCompileSize(IMAGE_SEPARATE_DEBUG_HEADER, 0x30);
typedef IMAGE_SEPARATE_DEBUG_HEADER *PIMAGE_SEPARATE_DEBUG_HEADER;
typedef IMAGE_SEPARATE_DEBUG_HEADER const *PCIMAGE_SEPARATE_DEBUG_HEADER;

/** The signature of a IMAGE_SEPARATE_DEBUG_HEADER. */
#define IMAGE_SEPARATE_DEBUG_SIGNATURE      UINT16_C(0x4944)


/**
 * The format of IMAGE_DEBUG_TYPE_COFF debug info.
 */
typedef struct _IMAGE_COFF_SYMBOLS_HEADER
{
    uint32_t    NumberOfSymbols;
    uint32_t    LvaToFirstSymbol;
    uint32_t    NumberOfLinenumbers;
    uint32_t    LvaToFirstLinenumber;
    uint32_t    RvaToFirstByteOfCode;
    uint32_t    RvaToLastByteOfCode;
    uint32_t    RvaToFirstByteOfData;
    uint32_t    RvaToLastByteOfData;
} IMAGE_COFF_SYMBOLS_HEADER;
AssertCompileSize(IMAGE_COFF_SYMBOLS_HEADER, 0x20);
typedef IMAGE_COFF_SYMBOLS_HEADER *PIMAGE_COFF_SYMBOLS_HEADER;
typedef IMAGE_COFF_SYMBOLS_HEADER const *PCIMAGE_COFF_SYMBOLS_HEADER;


/**
 * Line number format of IMAGE_DEBUG_TYPE_COFF debug info.
 *
 * @remarks This has misaligned members.
 */
#pragma pack(2)
typedef struct _IMAGE_LINENUMBER
{
    union
    {
        uint32_t    VirtualAddress;
        uint32_t    SymbolTableIndex;
    } Type;
    uint16_t    Linenumber;
} IMAGE_LINENUMBER;
#pragma pack()
AssertCompileSize(IMAGE_LINENUMBER, 6);
typedef IMAGE_LINENUMBER *PIMAGE_LINENUMBER;
typedef IMAGE_LINENUMBER const *PCIMAGE_LINENUMBER;


/** The size of a IMAGE_SYMBOL & IMAGE_AUX_SYMBOL structure. */
#define IMAGE_SIZE_OF_SYMBOL                18
/** The size of a IMAGE_SYMBOL_EX & IMAGE_AUX_SYMBOL_EX structure. */
#define IMAGE_SIZE_OF_SYMBOL_EX             20

/**
 * COFF symbol.
 */
#pragma pack(2)
typedef struct _IMAGE_SYMBOL
{
    union
    {
        uint8_t         ShortName[8];
        struct
        {
            uint32_t    Short;
            uint32_t    Long;
        } Name;
        uint32_t        LongName[2];
    } N;

    uint32_t    Value;
    int16_t     SectionNumber;
    uint16_t    Type;
    uint8_t     StorageClass;
    uint8_t     NumberOfAuxSymbols;
} IMAGE_SYMBOL;
#pragma pack()
AssertCompileSize(IMAGE_SYMBOL, IMAGE_SIZE_OF_SYMBOL);
typedef IMAGE_SYMBOL *PIMAGE_SYMBOL;
typedef IMAGE_SYMBOL const *PCIMAGE_SYMBOL;

/**
 * COFF auxiliary symbol token defintion (whatever that is).
 */
#pragma pack(2)
typedef struct IMAGE_AUX_SYMBOL_TOKEN_DEF
{
    uint8_t     bAuxType;
    uint8_t     bReserved;
    uint32_t    SymbolTableIndex;
    uint8_t     rgbReserved[12];
} IMAGE_AUX_SYMBOL_TOKEN_DEF;
#pragma pack()
AssertCompileSize(IMAGE_AUX_SYMBOL_TOKEN_DEF, IMAGE_SIZE_OF_SYMBOL);
typedef IMAGE_AUX_SYMBOL_TOKEN_DEF *PIMAGE_AUX_SYMBOL_TOKEN_DEF;
typedef IMAGE_AUX_SYMBOL_TOKEN_DEF const *PCIMAGE_AUX_SYMBOL_TOKEN_DEF;

/**
 * COFF auxiliary symbol.
 */
#pragma pack(1)
typedef union _IMAGE_AUX_SYMBOL
{
    struct
    {
        uint32_t    TagIndex;
        union
        {
            struct
            {
                uint16_t    Linenumber;
                uint16_t    Size;
            } LnSz;
        } Misc;
        union
        {
            struct
            {
                uint32_t    PointerToLinenumber;
                uint32_t    PointerToNextFunction;
            } Function;
            struct
            {
                uint16_t    Dimension[4];
            } Array;
        } FcnAry;
        uint16_t    TvIndex;
    } Sym;

    struct
    {
        uint8_t     Name[IMAGE_SIZE_OF_SYMBOL];
    } File;

    struct
    {
        uint32_t    Length;
        uint16_t    NumberOfRelocations;
        uint16_t    NumberOfLinenumbers;
        uint32_t    CheckSum;
        uint16_t    Number;
        uint8_t     Selection;
        uint8_t     bReserved;
        uint16_t    HighNumber;
    } Section;

    IMAGE_AUX_SYMBOL_TOKEN_DEF TokenDef;
    struct
    {
        uint32_t    crc;
        uint8_t     rgbReserved[14];
    } CRC;
} IMAGE_AUX_SYMBOL;
#pragma pack()
AssertCompileSize(IMAGE_AUX_SYMBOL, IMAGE_SIZE_OF_SYMBOL);
typedef IMAGE_AUX_SYMBOL *PIMAGE_AUX_SYMBOL;
typedef IMAGE_AUX_SYMBOL const *PCIMAGE_AUX_SYMBOL;


/**
 * Extended COFF symbol.
 */
typedef struct _IMAGE_SYMBOL_EX
{
    union
    {
        uint8_t         ShortName[8];
        struct
        {
            uint32_t    Short;
            uint32_t    Long;
        } Name;
        uint32_t        LongName[2];
    } N;

    uint32_t    Value;
    int32_t     SectionNumber;          /* The difference from IMAGE_SYMBOL */
    uint16_t    Type;
    uint8_t     StorageClass;
    uint8_t     NumberOfAuxSymbols;
} IMAGE_SYMBOL_EX;
AssertCompileSize(IMAGE_SYMBOL_EX, IMAGE_SIZE_OF_SYMBOL_EX);
typedef IMAGE_SYMBOL_EX *PIMAGE_SYMBOL_EX;
typedef IMAGE_SYMBOL_EX const *PCIMAGE_SYMBOL_EX;

/**
 * Extended COFF auxiliary symbol.
 */
typedef union _IMAGE_AUX_SYMBOL_EX
{
    struct
    {
        uint32_t    WeakDefaultSymIndex;
        uint32_t    WeakSearchType;
        uint8_t     rgbReserved[12];
    } Sym;

    struct
    {
        uint8_t     Name[IMAGE_SIZE_OF_SYMBOL_EX];
    } File;

    struct
    {
        uint32_t    Length;
        uint16_t    NumberOfRelocations;
        uint16_t    NumberOfLinenumbers;
        uint32_t    CheckSum;
        uint16_t    Number;
        uint8_t     Selection;
        uint8_t     bReserved;
        uint16_t    HighNumber;
        uint8_t     rgbReserved[2];
    } Section;

    IMAGE_AUX_SYMBOL_TOKEN_DEF TokenDef;

    struct
    {
        uint32_t    crc;
        uint8_t     rgbReserved[16];
    } CRC;
} IMAGE_AUX_SYMBOL_EX;
AssertCompileSize(IMAGE_AUX_SYMBOL_EX, IMAGE_SIZE_OF_SYMBOL_EX);
typedef IMAGE_AUX_SYMBOL_EX *PIMAGE_AUX_SYMBOL_EX;
typedef IMAGE_AUX_SYMBOL_EX const *PCIMAGE_AUX_SYMBOL_EX;

/** @name Special COFF section numbers.
 * Used by IMAGE_SYMBOL::SectionNumber and IMAGE_SYMBOL_EX::SectionNumber
 * @{ */
#define IMAGE_SYM_UNDEFINED                 INT16_C(0)
#define IMAGE_SYM_ABSOLUTE                  INT16_C(-1)
#define IMAGE_SYM_DEBUG                     INT16_C(-2)
/** @} */

/** @name IMAGE_SYM_CLASS_XXX - COFF symbol storage classes.
 * @{ */
#define IMAGE_SYM_CLASS_END_OF_FUNCTION     UINT8_C(0xff) /* -1 */
#define IMAGE_SYM_CLASS_NULL                UINT8_C(0)
#define IMAGE_SYM_CLASS_AUTOMATIC           UINT8_C(1)
#define IMAGE_SYM_CLASS_EXTERNAL            UINT8_C(2)
#define IMAGE_SYM_CLASS_STATIC              UINT8_C(3)
#define IMAGE_SYM_CLASS_REGISTER            UINT8_C(4)
#define IMAGE_SYM_CLASS_EXTERNAL_DEF        UINT8_C(5)
#define IMAGE_SYM_CLASS_LABEL               UINT8_C(6)
#define IMAGE_SYM_CLASS_UNDEFINED_LABEL     UINT8_C(7)
#define IMAGE_SYM_CLASS_MEMBER_OF_STRUCT    UINT8_C(8)
#define IMAGE_SYM_CLASS_ARGUMENT            UINT8_C(9)
#define IMAGE_SYM_CLASS_STRUCT_TAG          UINT8_C(10)
#define IMAGE_SYM_CLASS_MEMBER_OF_UNION     UINT8_C(11)
#define IMAGE_SYM_CLASS_UNION_TAG           UINT8_C(12)
#define IMAGE_SYM_CLASS_TYPE_DEFINITION     UINT8_C(13)
#define IMAGE_SYM_CLASS_UNDEFINED_STATIC    UINT8_C(14)
#define IMAGE_SYM_CLASS_ENUM_TAG            UINT8_C(15)
#define IMAGE_SYM_CLASS_MEMBER_OF_ENUM      UINT8_C(16)
#define IMAGE_SYM_CLASS_REGISTER_PARAM      UINT8_C(17)
#define IMAGE_SYM_CLASS_BIT_FIELD           UINT8_C(18)
#define IMAGE_SYM_CLASS_FAR_EXTERNAL        UINT8_C(68)
#define IMAGE_SYM_CLASS_BLOCK               UINT8_C(100)
#define IMAGE_SYM_CLASS_FUNCTION            UINT8_C(101)
#define IMAGE_SYM_CLASS_END_OF_STRUCT       UINT8_C(102)
#define IMAGE_SYM_CLASS_FILE                UINT8_C(103)
#define IMAGE_SYM_CLASS_SECTION             UINT8_C(104)
#define IMAGE_SYM_CLASS_WEAK_EXTERNAL       UINT8_C(105)
#define IMAGE_SYM_CLASS_CLR_TOKEN           UINT8_C(107)
/** @} */

/** @name IMAGE_SYM_TYPE_XXX - COFF symbol base types
 * @{ */
#define IMAGE_SYM_TYPE_NULL                 UINT16_C(0x0000)
#define IMAGE_SYM_TYPE_VOID                 UINT16_C(0x0001)
#define IMAGE_SYM_TYPE_CHAR                 UINT16_C(0x0002)
#define IMAGE_SYM_TYPE_SHORT                UINT16_C(0x0003)
#define IMAGE_SYM_TYPE_INT                  UINT16_C(0x0004)
#define IMAGE_SYM_TYPE_LONG                 UINT16_C(0x0005)
#define IMAGE_SYM_TYPE_FLOAT                UINT16_C(0x0006)
#define IMAGE_SYM_TYPE_DOUBLE               UINT16_C(0x0007)
#define IMAGE_SYM_TYPE_STRUCT               UINT16_C(0x0008)
#define IMAGE_SYM_TYPE_UNION                UINT16_C(0x0009)
#define IMAGE_SYM_TYPE_ENUM                 UINT16_C(0x000a)
#define IMAGE_SYM_TYPE_MOE                  UINT16_C(0x000b)
#define IMAGE_SYM_TYPE_BYTE                 UINT16_C(0x000c)
#define IMAGE_SYM_TYPE_WORD                 UINT16_C(0x000d)
#define IMAGE_SYM_TYPE_UINT                 UINT16_C(0x000e)
#define IMAGE_SYM_TYPE_DWORD                UINT16_C(0x000f)
#define IMAGE_SYM_TYPE_PCODE                UINT16_C(0x8000)
/** @} */

/** @name IMAGE_SYM_DTYPE_XXX - COFF symbol complex types
 * @{ */
#define IMAGE_SYM_DTYPE_NULL                UINT16_C(0x0)
#define IMAGE_SYM_DTYPE_POINTER             UINT16_C(0x1)
#define IMAGE_SYM_DTYPE_FUNCTION            UINT16_C(0x2)
#define IMAGE_SYM_DTYPE_ARRAY               UINT16_C(0x3)
/** @} */

/** @name COFF Symbol type masks and shift counts.
 * @{ */
#define N_BTMASK                            UINT16_C(0x000f)
#define N_TMASK                             UINT16_C(0x0030)
#define N_TMASK1                            UINT16_C(0x00c0)
#define N_TMASK2                            UINT16_C(0x00f0)
#define N_BTSHFT                            4
#define N_TSHIFT                            2
/** @} */

/** @name COFF Symbol type macros.
 * @{  */
#define BTYPE(a_Type)                       ( (a_Type) & N_BTMASK )
#define ISPTR(a_Type)                       ( ((a_Type) & N_TMASK) == (IMAGE_SYM_DTYPE_POINTER  << N_BTSHFT) )
#define ISFCN(a_Type)                       ( ((a_Type) & N_TMASK) == (IMAGE_SYM_DTYPE_FUNCTION << N_BTSHFT) )
#define ISARY(a_Type)                       ( ((a_Type) & N_TMASK) == (IMAGE_SYM_DTYPE_ARRAY    << N_BTSHFT) )
#define ISTAG(a_StorageClass)               (    (a_StorageClass) == IMAGE_SYM_CLASS_STRUCT_TAG \
                                              || (a_StorageClass) == IMAGE_SYM_CLASS_UNION_TAG \
                                              || (a_StorageClass) == IMAGE_SYM_CLASS_ENUM_TAG )
/** @} */


/**
 * COFF relocation table entry.
 *
 * @note The size of the structure is not a multiple of the largest member
 *       (uint32_t), so odd relocation table entry members will have
 *       misaligned uint32_t members.
 */
#pragma pack(1)
typedef struct _IMAGE_RELOCATION
{
    union
    {
        uint32_t    VirtualAddress;
        uint32_t    RelocCount;
    } u;
    uint32_t        SymbolTableIndex;
    uint16_t        Type;
} IMAGE_RELOCATION;
#pragma pack()
/** The size of a COFF relocation entry.   */
#define IMAGE_SIZEOF_RELOCATION 10
AssertCompileSize(IMAGE_RELOCATION, IMAGE_SIZEOF_RELOCATION);
typedef IMAGE_RELOCATION *PIMAGE_RELOCATION;
typedef IMAGE_RELOCATION const *PCIMAGE_RELOCATION;


/** @name IMAGE_REL_AMD64_XXX - COFF relocations for AMD64 CPUs.
 * Used by IMAGE_RELOCATION::Type.
 * @{ */
#define IMAGE_REL_AMD64_ABSOLUTE            UINT16_C(0x0000)
#define IMAGE_REL_AMD64_ADDR64              UINT16_C(0x0001)
#define IMAGE_REL_AMD64_ADDR32              UINT16_C(0x0002)
#define IMAGE_REL_AMD64_ADDR32NB            UINT16_C(0x0003)
#define IMAGE_REL_AMD64_REL32               UINT16_C(0x0004)
#define IMAGE_REL_AMD64_REL32_1             UINT16_C(0x0005)
#define IMAGE_REL_AMD64_REL32_2             UINT16_C(0x0006)
#define IMAGE_REL_AMD64_REL32_3             UINT16_C(0x0007)
#define IMAGE_REL_AMD64_REL32_4             UINT16_C(0x0008)
#define IMAGE_REL_AMD64_REL32_5             UINT16_C(0x0009)
#define IMAGE_REL_AMD64_SECTION             UINT16_C(0x000a)
#define IMAGE_REL_AMD64_SECREL              UINT16_C(0x000b)
#define IMAGE_REL_AMD64_SECREL7             UINT16_C(0x000c)
#define IMAGE_REL_AMD64_TOKEN               UINT16_C(0x000d)
#define IMAGE_REL_AMD64_SREL32              UINT16_C(0x000e)
#define IMAGE_REL_AMD64_PAIR                UINT16_C(0x000f)
#define IMAGE_REL_AMD64_SSPAN32             UINT16_C(0x0010)
/** @} */

/** @name ARM IMAGE_REL_ARM_XXX - COFF relocations for ARM CPUs.
 * Used by IMAGE_RELOCATION::Type.
 * @{ */
#define IMAGE_REL_ARM_ABSOLUTE              UINT16_C(0x0000)
#define IMAGE_REL_ARM_ADDR32                UINT16_C(0x0001)
#define IMAGE_REL_ARM_ADDR32NB              UINT16_C(0x0002)
#define IMAGE_REL_ARM_BRANCH24              UINT16_C(0x0003)
#define IMAGE_REL_ARM_BRANCH11              UINT16_C(0x0004)
#define IMAGE_REL_ARM_TOKEN                 UINT16_C(0x0005)
#define IMAGE_REL_ARM_BLX24                 UINT16_C(0x0008)
#define IMAGE_REL_ARM_BLX11                 UINT16_C(0x0009)
#define IMAGE_REL_ARM_SECTION               UINT16_C(0x000e)
#define IMAGE_REL_ARM_SECREL                UINT16_C(0x000f)
#define IMAGE_REL_ARM_MOV32A                UINT16_C(0x0010)
#define IMAGE_REL_ARM_MOV32T                UINT16_C(0x0011)
#define IMAGE_REL_ARM_BRANCH20T             UINT16_C(0x0012)
#define IMAGE_REL_ARM_BRANCH24T             UINT16_C(0x0014)
#define IMAGE_REL_ARM_BLX23T                UINT16_C(0x0015)
/** @} */

/** @name IMAGE_REL_ARM64_XXX - COFF relocations for ARMv8 CPUs (64-bit).
 * Used by IMAGE_RELOCATION::Type.
 * @{ */
#define IMAGE_REL_ARM64_ABSOLUTE            UINT16_C(0x0000)
#define IMAGE_REL_ARM64_ADDR32              UINT16_C(0x0001)
#define IMAGE_REL_ARM64_ADDR32NB            UINT16_C(0x0002)
#define IMAGE_REL_ARM64_BRANCH26            UINT16_C(0x0003)
#define IMAGE_REL_ARM64_PAGEBASE_REL21      UINT16_C(0x0004)
#define IMAGE_REL_ARM64_REL21               UINT16_C(0x0005)
#define IMAGE_REL_ARM64_PAGEOFFSET_12A      UINT16_C(0x0006)
#define IMAGE_REL_ARM64_PAGEOFFSET_12L      UINT16_C(0x0007)
#define IMAGE_REL_ARM64_SECREL              UINT16_C(0x0008)
#define IMAGE_REL_ARM64_SECREL_LOW12A       UINT16_C(0x0009)
#define IMAGE_REL_ARM64_SECREL_HIGH12A      UINT16_C(0x000a)
#define IMAGE_REL_ARM64_SECREL_LOW12L       UINT16_C(0x000b)
#define IMAGE_REL_ARM64_TOKEN               UINT16_C(0x000c)
#define IMAGE_REL_ARM64_SECTION             UINT16_C(0x000d)
#define IMAGE_REL_ARM64_ADDR64              UINT16_C(0x000e)
/** @} */

/** @name IMAGE_REL_SH3_XXX - COFF relocation for Hitachi SuperH CPUs.
 * Used by IMAGE_RELOCATION::Type.
 * @{ */
#define IMAGE_REL_SH3_ABSOLUTE              UINT16_C(0x0000)
#define IMAGE_REL_SH3_DIRECT16              UINT16_C(0x0001)
#define IMAGE_REL_SH3_DIRECT32              UINT16_C(0x0002)
#define IMAGE_REL_SH3_DIRECT8               UINT16_C(0x0003)
#define IMAGE_REL_SH3_DIRECT8_WORD          UINT16_C(0x0004)
#define IMAGE_REL_SH3_DIRECT8_LONG          UINT16_C(0x0005)
#define IMAGE_REL_SH3_DIRECT4               UINT16_C(0x0006)
#define IMAGE_REL_SH3_DIRECT4_WORD          UINT16_C(0x0007)
#define IMAGE_REL_SH3_DIRECT4_LONG          UINT16_C(0x0008)
#define IMAGE_REL_SH3_PCREL8_WORD           UINT16_C(0x0009)
#define IMAGE_REL_SH3_PCREL8_LONG           UINT16_C(0x000a)
#define IMAGE_REL_SH3_PCREL12_WORD          UINT16_C(0x000b)
#define IMAGE_REL_SH3_STARTOF_SECTION       UINT16_C(0x000c)
#define IMAGE_REL_SH3_SIZEOF_SECTION        UINT16_C(0x000d)
#define IMAGE_REL_SH3_SECTION               UINT16_C(0x000e)
#define IMAGE_REL_SH3_SECREL                UINT16_C(0x000f)
#define IMAGE_REL_SH3_DIRECT32_NB           UINT16_C(0x0010)
#define IMAGE_REL_SH3_GPREL4_LONG           UINT16_C(0x0011)
#define IMAGE_REL_SH3_TOKEN                 UINT16_C(0x0012)
#define IMAGE_REL_SHM_PCRELPT               UINT16_C(0x0013)
#define IMAGE_REL_SHM_REFLO                 UINT16_C(0x0014)
#define IMAGE_REL_SHM_REFHALF               UINT16_C(0x0015)
#define IMAGE_REL_SHM_RELLO                 UINT16_C(0x0016)
#define IMAGE_REL_SHM_RELHALF               UINT16_C(0x0017)
#define IMAGE_REL_SHM_PAIR                  UINT16_C(0x0018)
#define IMAGE_REL_SHM_NOMODE                UINT16_C(0x8000)
/** @} */

/** @name IMAGE_REL_PPC_XXX - COFF relocations for IBM PowerPC CPUs.
 * Used by IMAGE_RELOCATION::Type.
 * @{ */
#define IMAGE_REL_PPC_ABSOLUTE              UINT16_C(0x0000)
#define IMAGE_REL_PPC_ADDR64                UINT16_C(0x0001)
#define IMAGE_REL_PPC_ADDR32                UINT16_C(0x0002)
#define IMAGE_REL_PPC_ADDR24                UINT16_C(0x0003)
#define IMAGE_REL_PPC_ADDR16                UINT16_C(0x0004)
#define IMAGE_REL_PPC_ADDR14                UINT16_C(0x0005)
#define IMAGE_REL_PPC_REL24                 UINT16_C(0x0006)
#define IMAGE_REL_PPC_REL14                 UINT16_C(0x0007)
#define IMAGE_REL_PPC_ADDR32NB              UINT16_C(0x000a)
#define IMAGE_REL_PPC_SECREL                UINT16_C(0x000b)
#define IMAGE_REL_PPC_SECTION               UINT16_C(0x000c)
#define IMAGE_REL_PPC_SECREL16              UINT16_C(0x000f)
#define IMAGE_REL_PPC_REFHI                 UINT16_C(0x0010)
#define IMAGE_REL_PPC_REFLO                 UINT16_C(0x0011)
#define IMAGE_REL_PPC_PAIR                  UINT16_C(0x0012)
#define IMAGE_REL_PPC_SECRELLO              UINT16_C(0x0013)
#define IMAGE_REL_PPC_GPREL                 UINT16_C(0x0015)
#define IMAGE_REL_PPC_TOKEN                 UINT16_C(0x0016)
/** @} */

/** @name IMAGE_REL_I386_XXX - COFF relocations for x86 CPUs.
 * Used by IMAGE_RELOCATION::Type.
 * @{ */
#define IMAGE_REL_I386_ABSOLUTE             UINT16_C(0x0000)
#define IMAGE_REL_I386_DIR16                UINT16_C(0x0001)
#define IMAGE_REL_I386_REL16                UINT16_C(0x0002)
#define IMAGE_REL_I386_DIR32                UINT16_C(0x0006)
#define IMAGE_REL_I386_DIR32NB              UINT16_C(0x0007)
#define IMAGE_REL_I386_SEG12                UINT16_C(0x0009)
#define IMAGE_REL_I386_SECTION              UINT16_C(0x000A)
#define IMAGE_REL_I386_SECREL               UINT16_C(0x000B)
#define IMAGE_REL_I386_TOKEN                UINT16_C(0x000C)
#define IMAGE_REL_I386_SECREL7              UINT16_C(0x000D)
#define IMAGE_REL_I386_REL32                UINT16_C(0x0014)
/** @} */

/** @name IMAGE_REL_IA64_XXX - COFF relocations for "Itanic" CPUs.
 * @{ */
#define IMAGE_REL_IA64_ABSOLUTE             UINT16_C(0x0000)
#define IMAGE_REL_IA64_IMM14                UINT16_C(0x0001)
#define IMAGE_REL_IA64_IMM22                UINT16_C(0x0002)
#define IMAGE_REL_IA64_IMM64                UINT16_C(0x0003)
#define IMAGE_REL_IA64_DIR32                UINT16_C(0x0004)
#define IMAGE_REL_IA64_DIR64                UINT16_C(0x0005)
#define IMAGE_REL_IA64_PCREL21B             UINT16_C(0x0006)
#define IMAGE_REL_IA64_PCREL21M             UINT16_C(0x0007)
#define IMAGE_REL_IA64_PCREL21F             UINT16_C(0x0008)
#define IMAGE_REL_IA64_GPREL22              UINT16_C(0x0009)
#define IMAGE_REL_IA64_LTOFF22              UINT16_C(0x000a)
#define IMAGE_REL_IA64_SECTION              UINT16_C(0x000b)
#define IMAGE_REL_IA64_SECREL22             UINT16_C(0x000c)
#define IMAGE_REL_IA64_SECREL64I            UINT16_C(0x000d)
#define IMAGE_REL_IA64_SECREL32             UINT16_C(0x000e)
#define IMAGE_REL_IA64_DIR32NB              UINT16_C(0x0010)
#define IMAGE_REL_IA64_SREL14               UINT16_C(0x0011)
#define IMAGE_REL_IA64_SREL22               UINT16_C(0x0012)
#define IMAGE_REL_IA64_SREL32               UINT16_C(0x0013)
#define IMAGE_REL_IA64_UREL32               UINT16_C(0x0014)
#define IMAGE_REL_IA64_PCREL60X             UINT16_C(0x0015)
#define IMAGE_REL_IA64_PCREL60B             UINT16_C(0x0016)
#define IMAGE_REL_IA64_PCREL60F             UINT16_C(0x0017)
#define IMAGE_REL_IA64_PCREL60I             UINT16_C(0x0018)
#define IMAGE_REL_IA64_PCREL60M             UINT16_C(0x0019)
#define IMAGE_REL_IA64_IMMGPREL64           UINT16_C(0x001a)
#define IMAGE_REL_IA64_TOKEN                UINT16_C(0x001b)
#define IMAGE_REL_IA64_GPREL32              UINT16_C(0x001c)
#define IMAGE_REL_IA64_ADDEND               UINT16_C(0x001f)
/** @} */

/** @name IMAGE_REL_MIPS_XXX - COFF relocations for MIPS CPUs.
 * Used by IMAGE_RELOCATION::Type.
 * @{ */
#define IMAGE_REL_MIPS_ABSOLUTE             UINT16_C(0x0000)
#define IMAGE_REL_MIPS_REFHALF              UINT16_C(0x0001)
#define IMAGE_REL_MIPS_REFWORD              UINT16_C(0x0002)
#define IMAGE_REL_MIPS_JMPADDR              UINT16_C(0x0003)
#define IMAGE_REL_MIPS_REFHI                UINT16_C(0x0004)
#define IMAGE_REL_MIPS_REFLO                UINT16_C(0x0005)
#define IMAGE_REL_MIPS_GPREL                UINT16_C(0x0006)
#define IMAGE_REL_MIPS_LITERAL              UINT16_C(0x0007)
#define IMAGE_REL_MIPS_SECTION              UINT16_C(0x000a)
#define IMAGE_REL_MIPS_SECREL               UINT16_C(0x000b)
#define IMAGE_REL_MIPS_SECRELLO             UINT16_C(0x000c)
#define IMAGE_REL_MIPS_SECRELHI             UINT16_C(0x000d)
#define IMAGE_REL_MIPS_JMPADDR16            UINT16_C(0x0010)
#define IMAGE_REL_MIPS_REFWORDNB            UINT16_C(0x0022)
#define IMAGE_REL_MIPS_PAIR                 UINT16_C(0x0025)
/** @} */

/** @name IMAGE_REL_M32R_XXX - COFF relocations for Mitsubishi M32R CPUs.
 * Used by IMAGE_RELOCATION::Type.
 * @{ */
#define IMAGE_REL_M32R_ABSOLUTE             UINT16_C(0x0000)
#define IMAGE_REL_M32R_ADDR32               UINT16_C(0x0001)
#define IMAGE_REL_M32R_ADDR32NB             UINT16_C(0x0002)
#define IMAGE_REL_M32R_ADDR24               UINT16_C(0x0003)
#define IMAGE_REL_M32R_GPREL16              UINT16_C(0x0004)
#define IMAGE_REL_M32R_PCREL24              UINT16_C(0x0005)
#define IMAGE_REL_M32R_PCREL16              UINT16_C(0x0006)
#define IMAGE_REL_M32R_PCREL8               UINT16_C(0x0007)
#define IMAGE_REL_M32R_REFHALF              UINT16_C(0x0008)
#define IMAGE_REL_M32R_REFHI                UINT16_C(0x0009)
#define IMAGE_REL_M32R_REFLO                UINT16_C(0x000a)
#define IMAGE_REL_M32R_PAIR                 UINT16_C(0x000b)
#define IMAGE_REL_M32R_SECTION              UINT16_C(0x000c)
#define IMAGE_REL_M32R_SECREL               UINT16_C(0x000d)
#define IMAGE_REL_M32R_TOKEN                UINT16_C(0x000e)
/** @} */


/** @} */

#endif

