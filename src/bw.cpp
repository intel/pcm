// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Roman Dementiev,
//            Patrick Konsor
//

#include <iostream>
#include "bw.h"
#include "pci.h"
#include "utils.h"
#include <assert.h>

namespace pcm {

    constexpr auto PCM_CLIENT_IMC_BAR_OFFSET = 0x0048;
    constexpr auto PCM_TGL_IMC_STEP = 0x10000;
    unsigned int PCM_TGL_IMC_DRAM_DATA_READS[2]  = { 0x5058, 0xd858 };
    unsigned int PCM_TGL_IMC_DRAM_DATA_WRITES[2] = { 0x50A0, 0xd8A0 };
    unsigned int PCM_TGL_IMC_MMAP_SIZE[2]        = { 0x5000 + 0x1000, 0xd000 + 0x1000 };
    unsigned int PCM_TGL_IMC_EVENT_BASE[2]       = { 0x5000,          0xd000 };

    uint64 getClientIMCStartAddr()
    {
        PciHandleType imcHandle(0, 0, 0, 0); // memory controller device coordinates: domain 0, bus 0, device 0, function 0
        uint64 imcbar = 0;
        imcHandle.read64(PCM_CLIENT_IMC_BAR_OFFSET, &imcbar);
        // std::cout << "DEBUG: imcbar=" << std::hex << imcbar << "\n" << std::dec << std::flush;
        if (!imcbar)
        {
            std::cerr << "ERROR: imcbar is zero.\n";
            throw std::exception();
        }
        return imcbar & (~(4096ULL - 1ULL)); // round down to 4K
    }

    TGLClientBW::TGLClientBW()
    {
        const auto startAddr = getClientIMCStartAddr();
        for (size_t i = 0; i < mmioRange.size(); ++i)
        {
            for (size_t model = 0; model < mmioRange[i].size(); ++model)
            {
                mmioRange[i][model] = std::make_shared<MMIORange>(startAddr + i * PCM_TGL_IMC_STEP +  PCM_TGL_IMC_EVENT_BASE[model], PCM_TGL_IMC_MMAP_SIZE[model] - PCM_TGL_IMC_EVENT_BASE[model]);
            }
        }
    }

    uint64 TGLClientBW::getImcReads()
    {
        uint64 result = 0;
        for (auto & r : mmioRange)
            for (size_t model = 0; model < r.size(); ++model)
            {
                result += r[model]->read64(PCM_TGL_IMC_DRAM_DATA_READS[model] - PCM_TGL_IMC_EVENT_BASE[model]);
            }
        return result;
    }

    uint64 TGLClientBW::getImcWrites()
    {
        uint64 result = 0;
        for (auto & r : mmioRange)
            for (size_t model = 0; model < r.size(); ++model)
            {
                result += r[model]->read64(PCM_TGL_IMC_DRAM_DATA_WRITES[model] - PCM_TGL_IMC_EVENT_BASE[model]);
            }
        return result;
    }

#define PCM_CLIENT_IMC_DRAM_GT_REQUESTS (0x5040)
#define PCM_CLIENT_IMC_DRAM_IA_REQUESTS (0x5044)
#define PCM_CLIENT_IMC_DRAM_IO_REQUESTS (0x5048)
#define PCM_CLIENT_IMC_DRAM_DATA_READS  (0x5050)
#define PCM_CLIENT_IMC_DRAM_DATA_WRITES (0x5054)
#define PCM_CLIENT_IMC_MMAP_SIZE        (0x6000)
#define PCM_CLIENT_IMC_EVENT_BASE       (0x5000)

ClientBW::ClientBW()
{
    mmioRange = std::make_shared<MMIORange>(getClientIMCStartAddr() + PCM_CLIENT_IMC_EVENT_BASE, PCM_CLIENT_IMC_MMAP_SIZE - PCM_CLIENT_IMC_EVENT_BASE);
}

uint64 ClientBW::getImcReads()
{
    return mmioRange->read32(PCM_CLIENT_IMC_DRAM_DATA_READS - PCM_CLIENT_IMC_EVENT_BASE);
}

uint64 ClientBW::getImcWrites()
{
    return mmioRange->read32(PCM_CLIENT_IMC_DRAM_DATA_WRITES - PCM_CLIENT_IMC_EVENT_BASE);
}

uint64 ClientBW::getGtRequests()
{
    return mmioRange->read32(PCM_CLIENT_IMC_DRAM_GT_REQUESTS - PCM_CLIENT_IMC_EVENT_BASE);
}

uint64 ClientBW::getIaRequests()
{
    return mmioRange->read32(PCM_CLIENT_IMC_DRAM_IA_REQUESTS - PCM_CLIENT_IMC_EVENT_BASE);
}

uint64 ClientBW::getIoRequests()
{
    return mmioRange->read32(PCM_CLIENT_IMC_DRAM_IO_REQUESTS - PCM_CLIENT_IMC_EVENT_BASE);
}

#define PCM_ADL_IMC_EVENT_BASE       (0xd000)
#define PCM_ADL_IMC_DRAM_DATA_READS  (0x858)
#define PCM_ADL_IMC_DRAM_DATA_WRITES (0x8A0)

ADLClientBW::ADLClientBW()
{
    mmioRange = std::make_shared<MMIORange>(getClientIMCStartAddr() + PCM_ADL_IMC_EVENT_BASE, 0x1000);
}

uint64 ADLClientBW::getImcReads()
{
    return mmioRange->read32(PCM_ADL_IMC_DRAM_DATA_READS);
}

uint64 ADLClientBW::getImcWrites()
{
    return mmioRange->read32(PCM_ADL_IMC_DRAM_DATA_WRITES);
}

#define PCM_SERVER_IMC_DRAM_DATA_READS  (0x2290)
#define PCM_SERVER_IMC_DRAM_DATA_WRITES (0x2298)
#define PCM_SERVER_IMC_PMM_DATA_READS   (0x22a0)
#define PCM_SERVER_IMC_PMM_DATA_WRITES  (0x22a8)
#define PCM_SERVER_IMC_MMAP_SIZE        (0x4000)

std::vector<size_t> getServerBars(const size_t regBase, const uint32 numIMC, const uint32 root_segment_ubox0, const uint32 root_bus_ubox0)
{
    std::vector<size_t> result;
    PciHandleType ubox0Handle(root_segment_ubox0, root_bus_ubox0, SERVER_UBOX0_REGISTER_DEV_ADDR, SERVER_UBOX0_REGISTER_FUNC_ADDR);
    uint32 mmioBase = 0;
    ubox0Handle.read32(0xd0, &mmioBase);
    // std::cout << "mmioBase is 0x" << std::hex << mmioBase << std::dec << std::endl;
    for (uint32 i = 0; i < numIMC; ++i)
    {
        uint32 memOffset = 0;
        ubox0Handle.read32(regBase + i * 4, &memOffset);
        // std::cout << "memOffset for imc "<<i<<" is 0x" << std::hex << memOffset << std::dec << std::endl;
        size_t memBar = ((size_t(mmioBase) & ((1ULL << 29ULL) - 1ULL)) << 23ULL) |
            ((size_t(memOffset) & ((1ULL << 11ULL) - 1ULL)) << 12ULL);
        // std::cout << "membar for imc "<<i<<" is 0x" << std::hex << memBar << std::dec << std::endl;
        if (memBar == 0)
        {
            std::cerr << "ERROR: bar " << i << " is zero." << std::endl;
            throw std::exception();
        }
        result.push_back(memBar);
    }
    return result;
}

size_t getServerSCFBar(const uint32 root_segment_ubox0, const uint32 root_bus_ubox0)
{
    std::vector<size_t> result = getServerBars(0xd4, 1, root_segment_ubox0, root_bus_ubox0);
    assert(result.size() == 1);
    return result[0];
}

std::vector<size_t> getServerMemBars(const uint32 numIMC, const uint32 root_segment_ubox0, const uint32 root_bus_ubox0)
{
    return getServerBars(0xd8, numIMC, root_segment_ubox0, root_bus_ubox0);
}

ServerBW::ServerBW(const uint32 numIMC, const uint32 root_segment_ubox0, const uint32 root_bus_ubox0)
{
    auto memBars = getServerMemBars(numIMC, root_segment_ubox0, root_bus_ubox0);
    for (auto & memBar: memBars)
    {
        mmioRanges.push_back(std::make_shared<MMIORange>(memBar, PCM_SERVER_IMC_MMAP_SIZE));
    }
}

uint64 ServerBW::getImcReads()
{
    uint64 result = 0;
    for (auto & mmio: mmioRanges)
    {
        // std::cout << "PCM_SERVER_IMC_DRAM_DATA_READS: " << mmio->read64(PCM_SERVER_IMC_DRAM_DATA_READS) << std::endl;
        result += mmio->read64(PCM_SERVER_IMC_DRAM_DATA_READS);
    }
    return result;
}

uint64 ServerBW::getImcWrites()
{
    uint64 result = 0;
    for (auto & mmio : mmioRanges)
    {
        result += mmio->read64(PCM_SERVER_IMC_DRAM_DATA_WRITES);
    }
    return result;
}

uint64 ServerBW::getPMMReads()
{
    uint64 result = 0;
    for (auto & mmio : mmioRanges)
    {
        result += mmio->read64(PCM_SERVER_IMC_PMM_DATA_READS);
    }
    return result;
}

uint64 ServerBW::getPMMWrites()
{
    uint64 result = 0;
    for (auto & mmio : mmioRanges)
    {
        result += mmio->read64(PCM_SERVER_IMC_PMM_DATA_WRITES);
    }
    return result;
}

} // namespace pcm
