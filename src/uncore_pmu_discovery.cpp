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
                        uint32 id:16;
                        uint32 version:4;
                        uint32 next:12;
                    } fields;
                    uint32 value;
                } header;
                uint64 offset = 0x100;
                do
                {
                    if (offset == 0 || h.read32(offset, &header.value) != sizeof(uint32) || header.value == 0)
                    {
                        return;
                    }
                    // std::cout << "offset " << offset << "\n";
                    if (header.fields.id == 0x23) // UNCORE_EXT_CAP_ID_DISCOVERY
                    {
                        // std::cout << "found UNCORE_EXT_CAP_ID_DISCOVERY\n";
                        uint32 entryID = 0;
                        constexpr auto UNCORE_DISCOVERY_DVSEC_OFFSET = 8;
                        if (h.read32(offset + UNCORE_DISCOVERY_DVSEC_OFFSET, &entryID) == sizeof(uint32)) // read at UNCORE_DISCOVERY_DVSEC_OFFSET
                        {
                            entryID &= 0xffff; // apply UNCORE_DISCOVERY_DVSEC_ID_MASK
                            if (entryID == 1) // UNCORE_DISCOVERY_DVSEC_ID_PMON
                            {
                                // std::cout << "found UNCORE_DISCOVERY_DVSEC_ID_PMON\n";
                                uint32 bir = 0;
                                if (h.read32(offset + UNCORE_DISCOVERY_DVSEC_OFFSET + 4, &bir) == sizeof(uint32)) // read "bir" value (2:0)
                                {
                                    bir &= 7;
                                    auto barOffset = 0x10 + bir * 4;
                                    uint32 bar = 0;
                                    if (h.read32(barOffset, &bar) == sizeof(uint32) && bar != 0) // read bar
                                    {
                                        bar &= ~4095;
                                        processTables(bar);
                                        return;
                                    }
                                    else
                                    {
                                        std::cerr << "Error: can't read bar from offset " << barOffset << " \n";
                                    }
                                }
                                else
                                {
                                    std::cerr << "Error: can't read bir\n";
                                }
                            }
                        }
                    }
                    offset = header.fields.next & ~3;
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
