// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2024, Intel Corporation

/*!     \file cputopology.cpp
        \brief CPU core topology discovery independent of the PCM object
*/

#include "cputopology.h"
#include "utils.h"
#include "debug.h"

#if defined (__FreeBSD__) || defined(__DragonFly__)
#include <sys/param.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/ioccom.h>
#include <sys/cpuctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#ifdef _MSC_VER
#include <intrin.h>
#include <windows.h>
#include <comdef.h>
#include <tchar.h>
#endif

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace pcm
{

#if defined(__FreeBSD__) || defined(__DragonFly__)
void pcm_cpuid_bsd(int leaf, PCM_CPUID_INFO& info, int core)
{
    cpuctl_cpuid_args_t cpuid_args_freebsd;
    char cpuctl_name[64];

    snprintf(cpuctl_name, 64, "/dev/cpuctl%d", core);
    auto fd = ::open(cpuctl_name, O_RDWR);

    cpuid_args_freebsd.level = leaf;

    ::ioctl(fd, CPUCTL_CPUID, &cpuid_args_freebsd);
    for (int i = 0; i < 4; ++i)
    {
        info.array[i] = cpuid_args_freebsd.data[i];
    }
    ::close(fd);
}
#endif

bool CPUTopology::discover()
{
    PCM_CPUID_INFO cpuinfo;
    pcm_cpuid(0, cpuinfo);
    const uint32 max_cpuid = cpuinfo.array[0];
    bool hybrid = false;
    if (max_cpuid >= 0x7)
    {
        pcm_cpuid(7, 0, cpuinfo);
        hybrid = (cpuinfo.reg.edx & (1 << 15)) ? true : false;
    }
    return discover(max_cpuid, hybrid);
}

bool CPUTopology::discover(const uint32 max_cpuid, const bool hybrid)
{
    PCM_CPUID_INFO cpuid_args;
    uint32 smtMaskWidth = 0;
    uint32 coreMaskWidth = 0;
    uint32 l2CacheMaskShift = 0;
    uint32 l3CacheMaskShift = 0;

    struct domain
    {
        TopologyEntry::DomainTypeID type = TopologyEntry::DomainTypeID::InvalidDomainTypeID;
        unsigned levelShift = 0, nextLevelShift = 0, width = 0;
    };
    std::unordered_map<int, domain> topologyDomainMap;
    {
        const int32 maxTopoDomainAff = 1<<16;
        int32 topoDomainAff = -1;

        for (int32 core = 0; core < maxTopoDomainAff; ++core)
        {
            try {
                TemporalThreadAffinity _(core);
                topoDomainAff = core;
            }
            catch (...)
            {
            }
            if (topoDomainAff != -1) break;
        }

        TemporalThreadAffinity _(topoDomainAff);

        if (initCoreMasks(smtMaskWidth, coreMaskWidth, l2CacheMaskShift, l3CacheMaskShift) == false)
        {
            std::cerr << "ERROR: Major problem? No leaf 0 under cpuid function 11.\n";
            return false;
        }

        int subleaf = 0;

        std::vector<domain> topologyDomains;
        if (max_cpuid >= 0x1F)
        {
            subleaf = 0;
            do
            {
                pcm_cpuid(0x1F, subleaf, cpuid_args);
                domain d;
                d.type = (TopologyEntry::DomainTypeID)extract_bits_32(cpuid_args.reg.ecx, 8, 15);
               DBG(1 , "pcm_cpuid 0x1F cpuid_args.reg.ecx = " , cpuid_args.reg.ecx , " d.type = ", d.type);
                if (d.type == TopologyEntry::DomainTypeID::InvalidDomainTypeID)
                {
                    break;
                }
                d.nextLevelShift = extract_bits_32(cpuid_args.reg.eax, 0, 4);
                d.levelShift = topologyDomains.empty() ? 0 : topologyDomains.back().nextLevelShift;
                d.width = d.nextLevelShift - d.levelShift;
                topologyDomains.push_back(d);
                ++subleaf;
            } while (true);

            if (topologyDomains.size())
            {
                domain d;
                d.type = TopologyEntry::DomainTypeID::SocketPackageDomain;
                d.levelShift = topologyDomains.back().nextLevelShift;
                d.nextLevelShift = 32;
                d.width = d.nextLevelShift - d.levelShift;
                topologyDomains.push_back(d);
            }
            for (size_t l = 0; l < topologyDomains.size(); ++l)
            {
                topologyDomainMap[topologyDomains[l].type] = topologyDomains[l];
                DBG(1 , "Topology level: " , l ,
                                      " type: " , topologyDomains[l].type ,
                                      " (" , TopologyEntry::getDomainTypeStr(topologyDomains[l].type) , ")" ,
                                      " width: " , topologyDomains[l].width ,
                                      " levelShift: " , topologyDomains[l].levelShift ,
                                      " nextLevelShift: " , topologyDomains[l].nextLevelShift);
            }
        }
    }

    auto populateEntry = [&topologyDomainMap,&smtMaskWidth, &coreMaskWidth, &l2CacheMaskShift, &l3CacheMaskShift](TopologyEntry& entry)
    {
        auto getAPICID = [&](const uint32 leaf)
        {
            PCM_CPUID_INFO cpuid_args;
#if defined(__FreeBSD__) || defined(__DragonFly__)
            pcm_cpuid_bsd(leaf, cpuid_args, entry.os_id);
#else
            pcm_cpuid(leaf, 0x0, cpuid_args);
#endif
            return cpuid_args.array[3];
        };
        if (topologyDomainMap.size())
        {
            auto getID = [&topologyDomainMap](const int apic_id, const TopologyEntry::DomainTypeID t)
            {
                const auto di = topologyDomainMap.find(t);
                if (di != topologyDomainMap.end())
                {
                    const auto & d = di->second;
                    return extract_bits_32(apic_id, d.levelShift, d.nextLevelShift - 1);
                }
                return 0U;
            };
            entry.tile_id = extract_bits_32(getAPICID(0xb), l2CacheMaskShift, 31);
            const int apic_id = getAPICID(0x1F);
            entry.thread_id = getID(apic_id, TopologyEntry::DomainTypeID::LogicalProcessorDomain);
            entry.core_id = getID(apic_id, TopologyEntry::DomainTypeID::CoreDomain);
            entry.module_id = getID(apic_id, TopologyEntry::DomainTypeID::ModuleDomain);
            if (entry.tile_id == 0)
            {
                entry.tile_id = getID(apic_id, TopologyEntry::DomainTypeID::TileDomain);
            }
            entry.die_id = getID(apic_id, TopologyEntry::DomainTypeID::DieDomain);
            entry.die_grp_id = getID(apic_id, TopologyEntry::DomainTypeID::DieGrpDomain);
            entry.socket_id = getID(apic_id, TopologyEntry::DomainTypeID::SocketPackageDomain);

            auto getDomain = [&topologyDomainMap](const TopologyEntry::DomainTypeID t)
            {
                auto di = topologyDomainMap.find(t);
                if (di != topologyDomainMap.end())
                {
                    return di->second;
                }
                throw std::runtime_error("DomainType not found");
            };
            domain d1 = getDomain( TopologyEntry::DomainTypeID::CoreDomain );
            domain d2 = getDomain( TopologyEntry::DomainTypeID::SocketPackageDomain );
            entry.socket_unique_core_id = extract_bits_32( apic_id, d1.levelShift, d2.levelShift - 1 );
        }
        else
        {
            fillEntry(entry, smtMaskWidth, coreMaskWidth, l2CacheMaskShift, getAPICID(0xb));
        }
        entry.l3_cache_id = extract_bits_32(getAPICID(0xb), l3CacheMaskShift, 31);
    };

    auto populateHybridEntry = [&hybrid](TopologyEntry& entry, int core) -> bool
    {
        if (hybrid == false) return true;
        PCM_CPUID_INFO cpuid_args;
#if defined(__FreeBSD__) || defined(__DragonFly__)
        pcm_cpuid_bsd(0x1a, cpuid_args, core);
#elif defined (_MSC_VER) || defined(__linux__)
        pcm_cpuid(0x1a, 0x0, cpuid_args);
        (void)core;
#else
        std::cerr << "PCM Error: Hybrid processors are not supported for your OS\n";
        (void)core;
        return false;
#endif
        entry.native_cpu_model = extract_bits_32(cpuid_args.reg.eax, 0, 23);
        entry.core_type = (TopologyEntry::CoreType) extract_bits_32(cpuid_args.reg.eax, 24, 31);
        return true;
    };

#ifdef _MSC_VER
// version for Windows 7 and later version

    char * slpi = new char[sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)];
    DWORD len = (DWORD)sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
    BOOL res = GetLogicalProcessorInformationEx(RelationAll, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)slpi, &len);

    while (res == FALSE)
    {
        deleteAndNullifyArray(slpi);

        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            slpi = new char[len];
            res = GetLogicalProcessorInformationEx(RelationAll, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)slpi, &len);
        }
        else
        {
            tcerr << "Error in Windows function 'GetLogicalProcessorInformationEx': " <<
                GetLastError() << " ";
            const TCHAR * strError = _com_error(GetLastError()).ErrorMessage();
            if (strError) tcerr << strError;
            tcerr << "\n";
            return false;
        }
    }

    char * base_slpi = slpi;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pi = NULL;

    for ( ; slpi < base_slpi + len; slpi += (DWORD)pi->Size)
    {
        pi = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)slpi;
        if (pi->Relationship == RelationProcessorCore)
        {
            const auto current_threads_per_core = (pi->Processor.Flags == LTP_PC_SMT) ? 2 : 1;
            DBG(3, "thr per core: " , current_threads_per_core );
            num_cores += current_threads_per_core;
        }
    }

    num_online_cores = num_cores;

    if (num_cores != GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))
    {
        std::cerr << "Error in processor group size counting: " << num_cores << "!=" << GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) << "\n";
        std::cerr << "Make sure your binary is compiled for 64-bit: using 'x64' platform configuration.\n";
        return false;
    }

    for (int i = 0; i < (int)num_cores; i++)
    {
        ThreadGroupTempAffinity affinity(i);

        TopologyEntry entry;
        entry.os_id = i;

        populateEntry(entry);
        if (populateHybridEntry(entry, i) == false)
        {
            return false;
        }

        topology.push_back(entry);
        socketIdMap[entry.socket_id] = 0;
    }

    deleteAndNullifyArray(base_slpi);

#else
    // for Linux, FreeBSD and DragonFlyBSD

    TopologyEntry entry;

#ifdef __linux__
    num_cores = readMaxFromSysFS("/sys/devices/system/cpu/present");
    if(num_cores == -1)
    {
      std::cerr << "Cannot read number of present cores\n";
      return false;
    }
    ++num_cores;

    // open /proc/cpuinfo
    FILE * f_cpuinfo = fopen("/proc/cpuinfo", "r");
    if (!f_cpuinfo)
    {
        std::cerr << "Cannot open /proc/cpuinfo file.\n";
        return false;
    }

    // map with key=pkg_apic_id (not necessarily zero based or sequential) and
    // associated value=socket_id that should be 0 based and sequential
    std::map<int, int> found_pkg_ids;
    topology.resize(num_cores);
    char buffer[1024];
    while (0 != fgets(buffer, 1024, f_cpuinfo))
    {
        if (strncmp(buffer, "processor", sizeof("processor") - 1) == 0)
        {
            pcm_sscanf(buffer) >> s_expect("processor\t: ") >> entry.os_id;
            DBG(3, "os_core_id: " , entry.os_id );
            try {
                TemporalThreadAffinity _(entry.os_id);

                populateEntry(entry);
                if (populateHybridEntry(entry, entry.os_id) == false)
                {
                    return false;
                }

                topology[entry.os_id] = entry;
                socketIdMap[entry.socket_id] = 0;
                ++num_online_cores;
            }
            catch (std::exception &)
            {
                std::cerr << "Marking core " << entry.os_id << " offline\n";
            }
        }
    }
    fclose(f_cpuinfo);

#elif defined(__FreeBSD__) || defined(__DragonFly__)

    size_t size = sizeof(num_cores);

    if(0 != sysctlbyname("hw.ncpu", &num_cores, &size, NULL, 0))
    {
        std::cerr << "Unable to get hw.ncpu from sysctl.\n";
        return false;
    }
    num_online_cores = num_cores;

    if (modfind("cpuctl") == -1)
    {
        std::cerr << "cpuctl(4) not loaded.\n";
        return false;
    }

    for (int i = 0; i < num_cores; i++)
    {
        entry.os_id = i;

        populateEntry(entry);
        if (populateHybridEntry(entry, i) == false)
        {
            return false;
        }

        topology.push_back(entry);
        socketIdMap[entry.socket_id] = 0;
    }

#endif

#endif //end of ifdef _MSC_VER

    if(num_cores == 0) {
        num_cores = (int32)topology.size();
    }

    return true;
}

} // namespace pcm
