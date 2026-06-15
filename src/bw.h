// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012-2022, Intel Corporation
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
        virtual uint64 getGtRequests() { return 0; }
        virtual uint64 getIaRequests() { return 0; }
        virtual uint64 getIoRequests() { return 0; }
        virtual uint64 getPMMReads() { return 0; }
        virtual uint64 getPMMWrites() { return 0; }
        virtual ~FreeRunningBWCounters() {}
    };

    class TGLClientBW : public FreeRunningBWCounters
    {
        std::array<std::array<std::shared_ptr<MMIORange>, 2>, 2> mmioRange;
    public:
        TGLClientBW();

        uint64 getImcReads() override;
        uint64 getImcWrites() override;
    };

    class ADLClientBW : public FreeRunningBWCounters
    {
        std::shared_ptr<MMIORange> mmioRange;
    public:
        ADLClientBW();

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
        uint64 getGtRequests() override;
        uint64 getIaRequests() override;
        uint64 getIoRequests() override;
    };

std::vector<size_t> getServerMemBars(const uint32 numIMC, const uint32 root_segment_ubox0, const uint32 root_bus_ubox0);
size_t getServerSCFBar(const uint32 root_segment_ubox0, const uint32 root_bus_ubox0);

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
