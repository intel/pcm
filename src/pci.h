// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
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

    PciHandleMM() = delete;             // forbidden
    PciHandleMM(const PciHandleMM &) = delete; // forbidden
    PciHandleMM & operator = (const PciHandleMM &) = delete;

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

inline void getMCFGRecords(std::vector<MCFGRecord> & mcfg)
{
    #ifdef __linux__
    mcfg = PciHandleMM::getMCFGRecords();
    #else
    MCFGRecord segment;
    segment.PCISegmentGroupNumber = 0;
    segment.startBusNumber = 0;
    segment.endBusNumber = 0xff;
    mcfg.push_back(segment);
    #endif
}

template <class F>
inline void forAllIntelDevices(F f, int requestedDevice = -1, int requestedFunction = -1)
{
    std::vector<MCFGRecord> mcfg;
    getMCFGRecords(mcfg);

    auto probe = [&f](const uint32 group, const uint32 bus, const uint32 device, const uint32 function)
    {
        uint32 value = 0;
        try
        {
            PciHandleType h(group, bus, device, function);
            h.read32(0, &value);

        } catch(...)
        {
            // invalid bus:devicei:function
            return;
        }
        const uint32 vendor_id = value & 0xffff;
        const uint32 device_id = (value >> 16) & 0xffff;
        if (vendor_id != PCM_INTEL_PCI_VENDOR_ID)
        {
            return;
        }

        f(group, bus, device, function, device_id);
    };

    for (uint32 s = 0; s < (uint32)mcfg.size(); ++s)
    {
        const auto group = mcfg[s].PCISegmentGroupNumber;
        for (uint32 bus = (uint32)mcfg[s].startBusNumber; bus <= (uint32)mcfg[s].endBusNumber; ++bus)
        {
            auto forAllFunctions = [requestedFunction,&probe](const uint32 group, const uint32 bus, const uint32 device)
            {
                if (requestedFunction < 0)
                {
                    for (uint32 function = 0 ; function < 8; ++function)
                    {
                        probe(group, bus, device, function);
                    }
                }
                else
                {
                    probe(group, bus, device, requestedFunction);
                }
            };
            if (requestedDevice < 0)
            {
                for (uint32 device = 0 ; device < 32; ++device)
                {
                    forAllFunctions(group, bus, device);
                }
            }
            else
            {
                forAllFunctions(group, bus, requestedDevice);
            }
        }
    }
}

} // namespace pcm

#endif
