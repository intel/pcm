// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2024, Intel Corporation

/*!     \file cputopology.h
        \brief CPU core topology discovery independent of the PCM object
*/

#pragma once

#ifndef PCM_API
#define PCM_API
#endif

#include "types.h"
#include "topologyentry.h"

#include <map>
#include <vector>
#include <stdexcept>

namespace pcm
{

#if defined(__FreeBSD__) || defined(__DragonFly__)
void pcm_cpuid_bsd(int leaf, PCM_CPUID_INFO& info, int core);
#endif

/*! \brief Discovers the CPU core topology without creating a PCM object

    Enumerates all cores in the system (with their APIC-derived thread/core/module/
    tile/die/socket ids), the total number of cores, the number of online cores and
    the map of package/socket APIC ids. It has no side effects (does not program any
    PMU or access MSRs) and can be used by the PCM object as well as by simple
    utilities like pcm-msr that only need topology information.
*/
class PCM_API CPUTopology
{
public:
    std::vector<TopologyEntry> topology;
    int32 num_cores = 0;
    int32 num_online_cores = 0;
    //! map with key = pkg apic id (not necessarily zero based or sequential) and
    //! value initialized to 0 (can be remapped to a logical socket id by the caller)
    std::map<uint32, uint32> socketIdMap;

    //! \brief discovers the topology using the supplied max cpuid leaf and hybrid flag
    //! \throw std::runtime_error if the topology can not be discovered
    CPUTopology(const uint32 max_cpuid, const bool hybrid)
    {
        if (discover(max_cpuid, hybrid) == false)
        {
            throw std::runtime_error("Cannot discover CPU topology");
        }
    }

    //! \brief discovers the topology, detecting the max cpuid leaf and hybrid flag automatically
    //! \throw std::runtime_error if the topology can not be discovered
    CPUTopology()
    {
        if (discover() == false)
        {
            throw std::runtime_error("Cannot discover CPU topology");
        }
    }

    int32 getNumCores() const { return num_cores; }
    int32 getNumOnlineCores() const { return num_online_cores; }

    //! \brief checks if the core is online
    bool isCoreOnline(const int32 os_core_id) const
    {
        return (topology[os_core_id].os_id != -1) && (topology[os_core_id].core_id != -1) && (topology[os_core_id].socket_id != -1);
    }

private:
    bool discover(const uint32 max_cpuid, const bool hybrid);
    bool discover();
};

} // namespace pcm
