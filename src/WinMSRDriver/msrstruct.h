// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation

/*
   written by Roman Dementiev
*/

#ifndef MSR_STRUCT_HEADER
#define MSR_STRUCT_HEADER


#ifndef CTL_CODE
#include <WinIoCtl.h>
#endif

#define MSR_DEV_TYPE 50000

#define IO_CTL_MSR_READ     CTL_CODE(MSR_DEV_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_MSR_WRITE    CTL_CODE(MSR_DEV_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_PCICFG_READ  CTL_CODE(MSR_DEV_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_PCICFG_WRITE CTL_CODE(MSR_DEV_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_MMAP_SUPPORT CTL_CODE(MSR_DEV_TYPE, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_MMAP         CTL_CODE(MSR_DEV_TYPE, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_MUNMAP       CTL_CODE(MSR_DEV_TYPE, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_PMU_ALLOC_SUPPORT CTL_CODE(MSR_DEV_TYPE, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_PMU_ALLOC         CTL_CODE(MSR_DEV_TYPE, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_PMU_FREE          CTL_CODE(MSR_DEV_TYPE, 0x809, METHOD_BUFFERED, FILE_ANY_ACCESS)

struct MSR_Request
{
    int core_id;
    ULONG64 msr_address;
    ULONG64 write_value;     /* value to write if write requet
                                 ignored if read request */
};

struct PCICFG_Request
{
    ULONG bus, dev, func, reg, bytes;
    // "bytes" can be only 4 or 8
    /* value to write if write request ignored if read request */
    ULONG64 write_value;
};

struct MMAP_Request
{
    LARGE_INTEGER address;
    SIZE_T size;
};


#endif
