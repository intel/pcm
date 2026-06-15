// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Intel Corporation
// Example: How to map NUMA node IDs to CPU socket IDs

#include <iostream>
#include <iomanip>
#include "../src/cpucounters.h"

using namespace pcm;

int main()
{
    std::cout << "Example: Mapping NUMA node IDs to CPU socket IDs\n";
    std::cout << "==================================================\n\n";
    
    // Get PCM instance
    PCM * m = PCM::getInstance();
    
    // Initialize PCM
    PCM::ErrorCode status = m->program();
    if (status != PCM::Success)
    {
        std::cerr << "Error: Cannot access CPU counters\n";
        std::cerr << "Try running as root/administrator\n";
        std::cerr << "Error code: " << status << "\n";
        return 1;
    }
    
    std::cout << "System Information:\n";
    std::cout << "-------------------\n";
    std::cout << "Number of sockets: " << m->getNumSockets() << "\n";
    std::cout << "Number of cores:   " << m->getNumCores() << "\n";
    std::cout << "Number of online cores: " << m->getNumOnlineCores() << "\n\n";
    
    // Example: Map NUMA nodes to sockets
    std::cout << "NUMA Node to Socket Mapping:\n";
    std::cout << "----------------------------\n";
    
    // Try to map the first few NUMA nodes (typically 0-7 is sufficient)
    const uint32 max_numa_nodes_to_check = 8;
    bool found_any = false;
    
    for (uint32 numa_node = 0; numa_node < max_numa_nodes_to_check; ++numa_node)
    {
        int32 socket_id = m->mapNUMANodeToSocket(numa_node);
        
        if (socket_id >= 0)
        {
            std::cout << "  NUMA node " << numa_node 
                      << " -> Socket " << socket_id << "\n";
            found_any = true;
        }
    }
    
    if (!found_any)
    {
        std::cout << "  No NUMA node mappings available\n";
        std::cout << "\nNote: This is normal on:\n";
        std::cout << "  - Single-socket systems\n";
        std::cout << "  - Systems without NUMA support\n";
        std::cout << "  - macOS (not implemented)\n";
        std::cout << "  - FreeBSD without NUMA enabled (vm.ndomains <= 1)\n";
    }
    
    std::cout << "\n";
    
    // Example: Show relationship between cores and sockets
    std::cout << "Core to Socket Mapping (first 8 cores):\n";
    std::cout << "----------------------------------------\n";
    const uint32 cores_to_show = std::min((uint32)8, m->getNumCores());
    for (uint32 core = 0; core < cores_to_show; ++core)
    {
        int32 socket = m->getSocketId(core);
        std::cout << "  Core " << core << " -> Socket " << socket << "\n";
    }
    
    std::cout << "\n=== Example completed successfully ===\n";
    
    // Cleanup
    m->cleanup();
    
    return 0;
}
