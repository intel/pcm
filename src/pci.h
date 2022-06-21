// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2018, Intel Corporation
// written by Roman Dementiev
//            Pat Fay
//            Jim Harris (FreeBSD)


#ifndef CPUCounters_PCI_H
#define CPUCounters_PCI_H

/*!     \file pci.h
        \brief Low level interface to access PCI configuration space

*/

#include "types.h"

#ifdef _MSC_VER
#include "windows.h"
#else
#include <unistd.h>
#endif

#ifdef __APPLE__
#include "PCIDriverInterface.h"
#endif

#include <vector>

namespace pcm {

class PciHandle
{
#ifdef _MSC_VER
    HANDLE hDriver;
#else
    int32 fd;
#endif

    uint32 bus;
    uint32 device;
    uint32 function;
#ifdef _MSC_VER
    DWORD pciAddress;
#endif

    friend class PciHandleM;
    friend class PciHandleMM;

    PciHandle();                                // forbidden
    PciHandle(const PciHandle &);               // forbidden
    PciHandle & operator = (const PciHandle &); // forbidden

public:
    PciHandle(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_);

    static bool exists(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_);

    int32 read32(uint64 offset, uint32 * value);
    int32 write32(uint64 offset, uint32 value);

    int32 read64(uint64 offset, uint64 * value);

    virtual ~PciHandle();

protected:
    static int openMcfgTable();
};

#ifdef _MSC_VER
typedef PciHandle PciHandleType;
#elif __APPLE__
// This may need to change if it can be implemented for OSX
typedef PciHandle PciHandleType;
#elif defined(__FreeBSD__) || defined(__DragonFly__)
typedef PciHandle PciHandleType;
#else

// read/write PCI config space using physical memory
class PciHandleM
{
#ifdef _MSC_VER

#else
    int32 fd;
#endif

    uint32 bus;
    uint32 device;
    uint32 function;
    uint64 base_addr;

    PciHandleM() = delete;             // forbidden
    PciHandleM(PciHandleM &) = delete; // forbidden
    PciHandleM & operator = (PciHandleM &) = delete; // forbidden

public:
    PciHandleM(uint32 bus_, uint32 device_, uint32 function_);

    static bool exists(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_);

    int32 read32(uint64 offset, uint32 * value);
    int32 write32(uint64 offset, uint32 value);

    int32 read64(uint64 offset, uint64 * value);

    virtual ~PciHandleM();
};

#ifndef _MSC_VER

// read/write PCI config space using physical memory using mmapped file I/O
class PciHandleMM
{
    int32 fd;
    char * mmapAddr;

    uint32 bus;
    uint32 device;
    uint32 function;
    uint64 base_addr;

#ifdef __linux__
    static MCFGHeader mcfgHeader;
    static std::vector<MCFGRecord> mcfgRecords;
    static void readMCFG();
#endif

    PciHandleMM();             // forbidden
    PciHandleMM(PciHandleM &); // forbidden

public:
    PciHandleMM(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_);

    static bool exists(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_);

    int32 read32(uint64 offset, uint32 * value);
    int32 write32(uint64 offset, uint32 value);

    int32 read64(uint64 offset, uint64 * value);

    virtual ~PciHandleMM();

#ifdef __linux__
    static const std::vector<MCFGRecord> & getMCFGRecords();
#endif
};

#ifdef PCM_USE_PCI_MM_LINUX
#define PciHandleType PciHandleMM
#else
#define PciHandleType PciHandle
#endif

#endif //  _MSC_VER

#endif

} // namespace pcm

#endif
