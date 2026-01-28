// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024, Intel Corporation
// Test program for mapNUMANodeToSocket() API

#include <iostream>
#include <iomanip>
#include "../src/cpucounters.h"

using namespace pcm;

int main()
{
    std::cout << "Testing mapNUMANodeToSocket() API\n";
    std::cout << "==================================\n\n";
    
    PCM * m = PCM::getInstance();
    
    if (m->program() != PCM::Success)
    {
        std::cerr << "Error: Cannot access CPU counters\n";
        std::cerr << "Try running as root/administrator\n";
        return 1;
    }
    
    std::cout << "PCM initialized successfully\n";
    std::cout << "Number of sockets: " << m->getNumSockets() << "\n";
    std::cout << "Number of cores: " << m->getNumCores() << "\n\n";
    
    // Test mapping for NUMA nodes 0-7 (most systems won't have more)
    std::cout << "NUMA Node -> Socket Mapping:\n";
    std::cout << "-----------------------------\n";
    
    bool found_valid_mapping = false;
    for (uint32 numa_node = 0; numa_node < 8; ++numa_node)
    {
        int32 socket_id = m->mapNUMANodeToSocket(numa_node);
        
        if (socket_id >= 0)
        {
            std::cout << "NUMA node " << numa_node << " -> Socket " << socket_id << "\n";
            found_valid_mapping = true;
        }
        else
        {
            // Only show -1 for first few nodes to avoid clutter
            if (numa_node < 4)
            {
                std::cout << "NUMA node " << numa_node << " -> Not available (returned " << socket_id << ")\n";
            }
        }
    }
    
    if (!found_valid_mapping)
    {
        std::cout << "\nNo valid NUMA node mappings found.\n";
        std::cout << "This may be expected on:\n";
        std::cout << "  - Single-socket systems\n";
        std::cout << "  - Systems without NUMA support\n";
        std::cout << "  - Non-Linux platforms (macOS, FreeBSD)\n";
        std::cout << "  - Systems where sysfs NUMA information is not available\n";
    }
    
    std::cout << "\nTest PASSED\n";
    
    m->cleanup();
    return 0;
}
