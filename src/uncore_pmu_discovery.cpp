// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2022, Intel Corporation

#include "uncore_pmu_discovery.h"
#include "pci.h"
#include "mmio.h"
#include "iostream"
#include "utils.h"

namespace pcm {

constexpr auto UNCORE_DISCOVERY_MAP_SIZE = 0x80000;

UncorePMUDiscovery::UncorePMUDiscovery()
{
    if (safe_getenv("PCM_NO_UNCORE_PMU_DISCOVERY") == std::string("1"))
    {
        return;
    }
    unsigned socket = 0;
    auto processTables = [&socket,this](const uint64 bar)
    {
        constexpr size_t UncoreDiscoverySize = 3UL;
        union UncoreGlobalDiscovery {
            GlobalPMU pmu;
            uint64 table[UncoreDiscoverySize];
        };
        MMIORange range(bar, UNCORE_DISCOVERY_MAP_SIZE); // mmio range with UNCORE_DISCOVERY_MAP_SIZE bytes
        UncoreGlobalDiscovery global;
        auto copyTable = [&range,&UncoreDiscoverySize,&bar](uint64 * table, const size_t offset)
        {
            for (size_t i = 0; i < UncoreDiscoverySize; ++i)
            {
                const auto pos = offset + i * sizeof(uint64);
                assert(pos < UNCORE_DISCOVERY_MAP_SIZE);
                table[i] = range.read64(pos);
                if (table[i] == ~0ULL)
                {
                    std::cerr << "Failed to read memory at 0x" << std::hex << bar << " + 0x" << pos << std::dec << "\n";
                    throw std::exception();
                }
            }
        };
        copyTable(global.table, 0);
        globalPMUs.push_back(global.pmu);
        union UncoreUnitDiscovery {
            BoxPMU pmu;
            uint64 table[UncoreDiscoverySize];
        };
        UncoreUnitDiscovery unit;
        const auto step = global.pmu.stride * 8;
        BoxPMUMap boxPMUMap;
        for (size_t u = 0; u < global.pmu.maxUnits; ++u)
        {
            copyTable(unit.table, (u+1) * step);
            if (unit.table[0] == 0 && unit.table[1] == 0)
            {
                // invalid entry
                continue;
            }
            // unit.pmu.print();
            boxPMUMap[unit.pmu.boxType].push_back(unit.pmu);
        }
        boxPMUs.push_back(boxPMUMap);
        ++socket;
    };
    try {
    forAllIntelDevices(
        [&processTables](const uint32 group, const uint32 bus, const uint32 device, const uint32 function, const uint32 /* device_id */)
        {
            uint32 status{0};
            PciHandleType h(group, bus, device, function);
            h.read32(6, &status); // read status
            if (status & 0x10) // has capability list
            {
                // std::cout << "Intel device scan. found "<< std::hex << group << ":" << bus << ":" << device << ":" << function << " " << device_id << " with capability list\n" << std::dec;
                union {
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
                } header;

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
                    // std::cout << "offset 0x" << std::hex << offset << " header.fields.cap_id: 0x" << header.fields.cap_id << std::dec << "\n";
                    if (header.fields.cap_id == 0xb) // Vendor Specific Information
                    {
                        // std::cout << ".. found Vendor Specific Information ID 0x" << std::hex << header.fields.vsec_id << " " << std::dec << " len:" << header.fields.vsec_length << "\n";
                    }
                    else if (header.fields.cap_id == 0x23) // UNCORE_EXT_CAP_ID_DISCOVERY
                    {
                        // std::cout << ".. found UNCORE_EXT_CAP_ID_DISCOVERY entryID: 0x" << std::hex << header.fields.entryID << std::dec << "\n";
                        if (header.fields.entryID == 1) // UNCORE_DISCOVERY_DVSEC_ID_PMON
                        {
                            // std::cout << ".... found UNCORE_DISCOVERY_DVSEC_ID_PMON\n";
                            auto barOffset = 0x10 + header.fields.tBIR * 4;
                            uint32 bar = 0;
                            if (h.read32(barOffset, &bar) == sizeof(uint32) && bar != 0) // read bar
                            {
                                bar &= ~4095;
                                processTables(bar);
                            }
                            else
                            {
                                std::cerr << "Error: can't read bar from offset " << barOffset << " \n";
                            }
                        }
                    }
                    offset = header.fields.cap_next & ~3;
                } while (1);
            }
        });
    } catch (...)
    {
        std::cerr << "WARNING: enumeration of devices in UncorePMUDiscovery failed\n";
    }

    if (safe_getenv("PCM_PRINT_UNCORE_PMU_DISCOVERY") == std::string("1"))
    {
        for (size_t s = 0; s < boxPMUs.size(); ++s)
        {
            std::cout << "Socket " << s << " global PMU:\n";
            std::cout << "    ";
            globalPMUs[s].print();
            std::cout << "Socket " << s << " unit PMUs:\n";
            for (const auto & pmuType : boxPMUs[s])
            {
                const auto n = pmuType.second.size();
                std::cout << "   PMU type " << pmuType.first << " (" << n << " boxes)"<<  "\n";
                for (size_t i = 0; i < n ; ++i)
                {
                    std::cout << "        ";
                    pmuType.second[i].print();
                }
            }
        }
    }
}

} // namespace pcm
