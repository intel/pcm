// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2022, Intel Corporation
// written by Roman Dementiev

#ifdef __linux__

#include "resctrl.h"
#include "cpucounters.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <iostream>
#include <cstdlib>

namespace pcm
{
    bool Resctrl::isMounted()
    {
        struct stat st;
        if (stat("/sys/fs/resctrl/mon_groups", &st) < 0)
        {
            return false;
        }
        return true;
    }
    void Resctrl::init()
    {
        if (isMounted() == false)
        {
            std::cerr << "ERROR: /sys/fs/resctrl is not mounted\n";
            std::cerr << "ERROR: RDT metrics (L3OCC,LMB,RMB) will not be available\n";
            std::cerr << "Mount it to make it work: mount -t resctrl resctrl /sys/fs/resctrl\n";
            return;
        }
        const auto numCores = pcm.getNumCores();
        for (unsigned int c = 0; c < numCores; ++c)
        {
            if (pcm.isCoreOnline(c))
            {
                const auto C = std::to_string(c);
                const auto dir = std::string(PCMPath) + C;
                struct stat st;
                if (stat(dir.c_str(), &st) < 0 && mkdir(dir.c_str(), 0700) < 0)
                {
                    std::cerr << "INFO: can't create directory " << dir << " error: " << strerror(errno) << "\n";
                    const auto containerDir = std::string("/pcm") + dir;
                    if (stat(containerDir.c_str(), &st) < 0 && mkdir(containerDir.c_str(), 0700) < 0)
                    {
                        std::cerr << "INFO: can't create directory " << containerDir << " error: " << strerror(errno) << "\n";
                        std::cerr << "ERROR: RDT metrics (L3OCC,LMB,RMB) will not be available\n";
                        break;
                    }
                }
                const auto cpus_listFilename = dir + "/cpus_list";
                writeSysFS(cpus_listFilename.c_str(), C, false);
                auto generateMetricFiles = [&dir, c] (PCM & pcm, const std::string & metric, FileMapType & fileMap)
                {
                    auto getMetricFilename = [] (const std::string & dir, const uint64 s, const std::string & metric)
                    {
                        std::ostringstream ostr;
                        ostr << dir << "/mon_data/mon_L3_" << std::setfill('0') << std::setw(2) << s << "/" << metric;
                        return ostr.str();
                    };
                    for (uint64 s = 0; s < pcm.getNumSockets(); ++s)
                    {
                        fileMap[c].push_back(getMetricFilename(dir, s, metric));
                    }
                };
                if (pcm.L3CacheOccupancyMetricAvailable())
                {
                    generateMetricFiles(pcm, "llc_occupancy", L3OCC);
                }
                if (pcm.CoreLocalMemoryBWMetricAvailable())
                {
                    generateMetricFiles(pcm, "mbm_local_bytes", MBL);
                }
                if (pcm.CoreRemoteMemoryBWMetricAvailable())
                {
                    generateMetricFiles(pcm, "mbm_total_bytes", MBT);
                }
            }
        }
    }
    void Resctrl::cleanup()
    {
        const auto numCores = pcm.getNumCores();
        for (unsigned int c = 0; c < numCores; ++c)
        {
            if (pcm.isCoreOnline(c))
            {
                const auto dir = std::string(PCMPath) + std::to_string(c);
                rmdir(dir.c_str());
                const auto containerDir = std::string("/pcm") + dir;
                rmdir(containerDir.c_str());
            }
        }
    }
    size_t Resctrl::getMetric(const Resctrl::FileMapType & fileMap, int core)
    {
        auto files = fileMap.find(core);
        if (files == fileMap.end())
        {
            return 0ULL;
        }
        size_t result = 0;
        for (auto& f : files->second)
        {
            const auto data = readSysFS(f.c_str(), false);
            if (data.empty() == false)
            {
                result += atoll(data.c_str());
            }
            else
            {
                static std::mutex lock;
                std::lock_guard<std::mutex> _(lock);
                std::cerr << "Error reading " << f << ". Error: " << strerror(errno) << "\n";
                if (errno == 24)
                {
                    std::cerr << PCM_ULIMIT_RECOMMENDATION;
                }
            }
        }
        return result;
    }
    size_t Resctrl::getL3OCC(int core)
    {
        return getMetric(L3OCC, core);
    }
    size_t Resctrl::getMBL(int core)
    {
        return getMetric(MBL, core);
    }
    size_t Resctrl::getMBT(int core)
    {
        return getMetric(MBT, core);
    }
};

 #endif // __linux__
