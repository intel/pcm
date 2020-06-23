/*
Copyright (c) 2009-2013, Intel Corporation
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
#include "client_bw.h"
#include "pci.h"

namespace pcm {

#define PCM_CLIENT_IMC_BAR_OFFSET       (0x0048)
#define PCM_CLIENT_IMC_DRAM_IO_REQESTS  (0x5048)
#define PCM_CLIENT_IMC_DRAM_DATA_READS  (0x5050)
#define PCM_CLIENT_IMC_DRAM_DATA_WRITES (0x5054)
#define PCM_CLIENT_IMC_MMAP_SIZE        (0x6000)
#define PCM_CLIENT_IMC_EVENT_BASE       (0x5000)

ClientBW::ClientBW()
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
    const auto startAddr = imcbar & (~(4096ULL - 1ULL)); // round down to 4K
    mmioRange = std::make_shared<MMIORange>(startAddr + PCM_CLIENT_IMC_EVENT_BASE, PCM_CLIENT_IMC_MMAP_SIZE - PCM_CLIENT_IMC_EVENT_BASE);
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

} // namespace pcm