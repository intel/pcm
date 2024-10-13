// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2022, Intel Corporation

#include "uncore_pmu_discovery.h"
#include "pci.h"
#include "mmio.h"
#include "iostream"
#include "utils.h"

namespace pcm {

UncorePMUDiscovery::UncorePMUDiscovery()
{
    if (safe_getenv("PCM_NO_UNCORE_PMU_DISCOVERY") == std::string("1"))
    {
        return;
    }
    auto processTables = [this](const uint64 bar, const VSEC &)
    {
        constexpr size_t UncoreDiscoverySize = 3UL;
        union UncoreGlobalDiscovery {
            GlobalPMU pmu;
            uint64 table[UncoreDiscoverySize];
        };
        UncoreGlobalDiscovery global;
        mmio_memcpy(global.table, bar, UncoreDiscoverySize * sizeof(uint64), true);
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
            mmio_memcpy(unit.table, bar + (u+1) * step, UncoreDiscoverySize * sizeof(uint64), true);
            if (unit.table[0] == 0 && unit.table[1] == 0)
            {
                // invalid entry
                continue;
            }
            // unit.pmu.print();
            boxPMUMap[unit.pmu.boxType].push_back(unit.pmu);
        }
        boxPMUs.push_back(boxPMUMap);
    };
    try {
        processDVSEC([](const VSEC & vsec)
        {
            return vsec.fields.cap_id == 0x23 // UNCORE_EXT_CAP_ID_DISCOVERY
                && vsec.fields.entryID == 1; // UNCORE_DISCOVERY_DVSEC_ID_PMON
        }, processTables);

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
