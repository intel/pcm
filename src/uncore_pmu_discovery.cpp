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
    const auto debug = (safe_getenv("PCM_DEBUG_PMU_DISCOVERY") == std::string("1"));

    auto processTables = [this, &debug](const uint64 bar, const VSEC & vsec)
    {
        try {
            DBG(2, "Uncore discovery detection. Reading from bar 0x", std::hex, bar, std::dec);
            constexpr size_t UncoreDiscoverySize = 3UL;
            union UncoreGlobalDiscovery {
                GlobalPMU pmu;
                uint64 table[UncoreDiscoverySize];
            };
            UncoreGlobalDiscovery global;
            mmio_memcpy(global.table, bar, UncoreDiscoverySize * sizeof(uint64), true);
            globalPMUs.push_back(global.pmu);
            if (debug)
            {
                std::cerr << "Read global.pmu from 0x" << std::hex << bar << std::dec << "\n";
                global.pmu.print();
                std::cout.flush();
            }
            union UncoreUnitDiscovery {
                BoxPMU pmu;
                uint64 table[UncoreDiscoverySize];
            };
            UncoreUnitDiscovery unit;
            const auto step = global.pmu.stride * 8;
            BoxPMUMap boxPMUMap;
            for (size_t u = 0; u < global.pmu.maxUnits; ++u)
            {
                mmio_memcpy(unit.table, bar + (u + 1) * step, UncoreDiscoverySize * sizeof(uint64), true);
                if (debug)
                {
                    std::cerr << "Read unit.pmu " << u << " from 0x" << std::hex << (bar + (u + 1) * step) << std::dec << "\n";
                    unit.pmu.print();
                    std::cout.flush();
                }
                if (unit.table[0] == 0 && unit.table[1] == 0)
                {
                    if (debug)
                    {
                        std::cerr << "Invalid entry\n";
                    }
                    // invalid entry
                    continue;
                }
                // unit.pmu.print();
                boxPMUMap[unit.pmu.boxType].push_back(unit.pmu);
            }
            boxPMUs.push_back(boxPMUMap);
        }
        catch (const std::exception & e)
        {
            std::cerr << "WARNING: enumeration of devices in UncorePMUDiscovery failed on bar 0x"
                << std::hex << bar << "\n" << e.what() << "\n" <<
                " CAP_ID: 0x" << vsec.fields.cap_id << "\n" <<
                " CAP_VERSION: 0x" << vsec.fields.cap_version << "\n" <<
                " CAP_NEXT: 0x" << vsec.fields.cap_next << "\n" <<
                " VSEC_ID: 0x" << vsec.fields.vsec_id << "\n" <<
                " VSEC_VERSION: 0x" << vsec.fields.vsec_version << "\n" <<
                " VSEC_LENGTH: 0x" << vsec.fields.vsec_length << "\n" <<
                " ENTRY_ID: 0x" << vsec.fields.entryID << "\n" <<
                " NUM_ENTRIES: 0x" << vsec.fields.NumEntries << "\n" <<
                " ENTRY_SIZE: 0x" << vsec.fields.EntrySize << "\n" <<
                " TBIR: 0x" << vsec.fields.tBIR << "\n" <<
                " ADDRESS: 0x" << vsec.fields.Address <<
                std::dec << "\n";
            std::cerr << "INFO: discovery has " << boxPMUs.size() << " entries\n";
        }
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
