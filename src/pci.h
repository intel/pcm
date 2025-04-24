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
#include "debug.h"
#include "utils.h"

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

#if defined(__FreeBSD__) || defined(__DragonFly__)
    uint32 groupnr;
#endif
    uint32 bus;
    uint32 device;
    uint32 function;
#ifdef _MSC_VER
    DWORD pciAddress;
#endif

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
#elif defined(__linux__)

// read/write PCI config space using physical memory using mmapped file I/O
class PciHandleMM
{
    int32 fd;
    char * mmapAddr;

    uint32 bus;
    uint32 device;
    uint32 function;
    uint64 base_addr;

    static MCFGHeader mcfgHeader;
    static std::vector<MCFGRecord> mcfgRecords;
    static void readMCFG();

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

    static const std::vector<MCFGRecord> & getMCFGRecords();
};

#ifdef PCM_USE_PCI_MM_LINUX
#define PciHandleType PciHandleMM
#else
#define PciHandleType PciHandle
#endif

#else
#error "Platform not supported"
#endif

template <class F>
inline void forAllIntelDevices(F f, int requestedDevice = -1, int requestedFunction = -1)
{
    std::vector<MCFGRecord> mcfg;
    getMCFGRecords(mcfg);

    auto probe = [&f](const uint32 group, const uint32 bus, const uint32 device, const uint32 function)
    {
        DBG(3, "Probing " , std::hex , group , ":" , bus , ":" , device , ":" , function , " " , std::dec);
        uint32 value = 0;
        try
        {
            PciHandleType h(group, bus, device, function);
            h.read32(0, &value);

        } catch(...)
        {
            // invalid bus:device:function
            return;
        }
        const uint32 vendor_id = value & 0xffff;
        const uint32 device_id = (value >> 16) & 0xffff;
        DBG(3, "Found dev " , std::hex , vendor_id , ":" , device_id , std::dec);
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

union VSEC {
    struct {
        uint64 cap_id:16;
        uint64 cap_version:4;
        uint64 cap_next:12;
        uint64 vsec_id:16;
        uint64 vsec_version:4;
        uint64 vsec_length:12;
        uint64 entryID:16;
        uint64 NumEntries:8;
        uint64 EntrySize:8;
        uint64 tBIR:3;
        uint64 Address:29;
    } fields;
    uint64 raw_value64[2];
    uint32 raw_value32[4];
};

template <class MatchFunc, class ProcessFunc>
void processDVSEC(MatchFunc matchFunc, ProcessFunc processFunc)
{
    forAllIntelDevices([&](const uint32 group, const uint32 bus, const uint32 device, const uint32 function, const uint32 device_id)
    {
        DBG(2, "Intel device scan.found " , std::hex , group , ":" , bus , " : " , device , " : " , function , " " , device_id);
        uint32 status{0};
        PciHandleType h(group, bus, device, function);
        h.read32(4, &status); // read status
        if (status & 0x100000) // has capability list
        {
            DBG(2, "Intel device scan. found ", std::hex , group , ":" , bus , ":" , device , ":" , function , " " , device_id , " with capability list");
            VSEC header;
            uint64 offset = 0x100;
            do
            {
                if (offset == 0 || h.read32(offset, &header.raw_value32[0]) != sizeof(uint32) || header.raw_value32[0] == 0)
                {
                    return;
                }
                if (h.read64(offset, &header.raw_value64[0]) != sizeof(uint64) || h.read64(offset + sizeof(uint64), &header.raw_value64[1]) != sizeof(uint64))
                {
                    return;
                }
                DBG(2, "offset 0x" , std::hex , offset , " cap_id: 0x" , header.fields.cap_id , " vsec_id: 0x", header.fields.vsec_id, " entryID: 0x" , std::hex , header.fields.entryID , std::dec);
                if (matchFunc(header))
                {
                    DBG(2, ".... found match");
                    auto barOffset = 0x10 + header.fields.tBIR * 4;
                    uint32 bar = 0;
                    if (h.read32(barOffset, &bar) == sizeof(uint32) && bar != 0) // read bar
                    {
                        bar &= ~4095;
                        processFunc(bar, header);
                    }
                    else
                    {
                        std::cerr << "Error: can't read bar from offset " << barOffset << " \n";
                    }
                }
                const uint64 lastOffset = offset;
                offset = header.fields.cap_next & ~3;
                if (lastOffset == offset) // the offset did not change
                {
                    DBG(2, " lastOffset == offset ", lastOffset , "==", offset);
                    return; // deadlock protection
                }
            } while (1);
        }
    });
}

} // namespace pcm

#endif
