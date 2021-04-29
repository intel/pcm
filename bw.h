/*
Copyright (c) 2012-2019, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev
//

#pragma once

/*!     \file bw.h
        \brief Interfaces to access free-running bandwidth counters

*/

#include <memory>
#include <vector>
#include <array>
#include "mmio.h"

namespace pcm {

    class FreeRunningBWCounters
    {
    public:
        virtual uint64 getImcReads() { return 0; }
        virtual uint64 getImcWrites() { return 0; }
        virtual uint64 getIoRequests() { return 0; }
        virtual uint64 getPMMReads() { return 0; }
        virtual uint64 getPMMWrites() { return 0; }
        virtual ~FreeRunningBWCounters() {}
    };

    class TGLClientBW : public FreeRunningBWCounters
    {
        std::array<std::shared_ptr<MMIORange>, 2> mmioRange;
        int model;
    public:
        TGLClientBW();

        uint64 getImcReads() override;
        uint64 getImcWrites() override;
    };

    class ClientBW : public FreeRunningBWCounters
    {
        std::shared_ptr<MMIORange> mmioRange;
    public:
        ClientBW();

        uint64 getImcReads() override;
        uint64 getImcWrites() override;
        uint64 getIoRequests() override;
    };

std::vector<size_t> getServerMemBars(const uint32 numIMC, const uint32 root_segment_ubox0, const uint32 root_bus_ubox0);

class ServerBW
{
    std::vector<std::shared_ptr<MMIORange> > mmioRanges;

    ServerBW();
public:
    ServerBW(const uint32 numIMC, const uint32 root_segment_ubox0, const uint32 root_bus_ubox0);

    uint64 getImcReads();
    uint64 getImcWrites();
    uint64 getPMMReads();
    uint64 getPMMWrites();
};

} // namespace pcm