/*
Copyright (c) 2009-2019, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev,
//            Patrick Konsor
//

#include <iostream>
#include "bw.h"
#include "pci.h"
#include "utils.h"

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
        // std::cout << "DEBUG: imcbar=" << std::hex << imcbar << "\n" << std::flush;
        if (!imcbar)
        {
            std::cerr << "ERROR: imcbar is zero.\n";
            throw std::exception();
        }
        return imcbar & (~(4096ULL - 1ULL)); // round down to 4K
    }

    TGLClientBW::TGLClientBW()
    {
        PCM_CPUID_INFO cpuinfo;
        pcm_cpuid(1, cpuinfo); // need to retrieve original cpu id (undo cpu model merging)
        model = ((cpuinfo.array[0]) & 0x10) >> 4;
        const auto startAddr = getClientIMCStartAddr();
        for (size_t i = 0; i < mmioRange.size(); ++i)
        {
            mmioRange[i] = std::make_shared<MMIORange>(startAddr + i * PCM_TGL_IMC_STEP +  PCM_TGL_IMC_EVENT_BASE[model], PCM_TGL_IMC_MMAP_SIZE[model] - PCM_TGL_IMC_EVENT_BASE[model]);
        }
    }

    uint64 TGLClientBW::getImcReads()
    {
        uint64 result = 0;
        for (auto r : mmioRange)
        {
            result += r->read64(PCM_TGL_IMC_DRAM_DATA_READS[model] - PCM_TGL_IMC_EVENT_BASE[model]);
        }
        return result;
    }

    uint64 TGLClientBW::getImcWrites()
    {
        uint64 result = 0;
        for (auto r : mmioRange)
        {
            result += r->read64(PCM_TGL_IMC_DRAM_DATA_WRITES[model] - PCM_TGL_IMC_EVENT_BASE[model]);
        }
        return result;
    }

#define PCM_CLIENT_IMC_DRAM_IO_REQESTS  (0x5048)
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

uint64 ClientBW::getIoRequests()
{
    return mmioRange->read32(PCM_CLIENT_IMC_DRAM_IO_REQESTS - PCM_CLIENT_IMC_EVENT_BASE);
}

#define PCM_SERVER_IMC_DRAM_DATA_READS  (0x2290)
#define PCM_SERVER_IMC_DRAM_DATA_WRITES (0x2298)
#define PCM_SERVER_IMC_PMM_DATA_READS   (0x22a0)
#define PCM_SERVER_IMC_PMM_DATA_WRITES  (0x22a8)
#define PCM_SERVER_IMC_MMAP_SIZE        (0x4000)

std::vector<size_t> getServerMemBars(const uint32 numIMC, const uint32 root_segment_ubox0, const uint32 root_bus_ubox0)
{
    std::vector<size_t> result;
    PciHandleType ubox0Handle(root_segment_ubox0, root_bus_ubox0, SERVER_UBOX0_REGISTER_DEV_ADDR, SERVER_UBOX0_REGISTER_FUNC_ADDR);
    uint32 mmioBase = 0;
    ubox0Handle.read32(0xd0, &mmioBase);
    for (uint32 i = 0; i < numIMC; ++i)
    {
        uint32 memOffset = 0;
        ubox0Handle.read32(0xd8 + i * 4, &memOffset);
        size_t memBar = ((mmioBase & ((1ULL << 29ULL) - 1ULL)) << 23ULL) |
            ((memOffset & ((1ULL << 11ULL) - 1ULL)) << 12ULL);
        if (memBar == 0)
        {
            std::cerr << "ERROR: memBar " << i << " is zero." << std::endl;
            throw std::exception();
        }
        result.push_back(memBar);
    }
    return result;
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