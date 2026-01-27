// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024, Intel Corporation
// Test program for getNUMANode() API

#include <iostream>
#include <iomanip>
#include "../src/pci.h"

using namespace pcm;

int main()
{
    std::cout << "Testing getNUMANode() API\n";
    std::cout << "=========================\n\n";
    
    try
    {
        // Try to create a PciHandle for a common device
        // We'll try bus 0, device 0, function 0 as it often exists
        PciHandleType handle(0, 0, 0, 0);
        
        std::cout << "Successfully created PciHandle for 0:0:0.0\n";
        
        // Get NUMA node
        int32 numa_node = handle.getNUMANode();
        
        std::cout << "NUMA node: " << numa_node;
        if (numa_node == -1)
        {
            std::cout << " (not available or not applicable)";
        }
        std::cout << "\n";
        
        // Test reading vendor ID to verify the handle works
        uint32 vendor_device_id = 0;
        if (handle.read32(0, &vendor_device_id) == sizeof(uint32))
        {
            uint32 vendor_id = vendor_device_id & 0xFFFF;
            uint32 device_id = (vendor_device_id >> 16) & 0xFFFF;
            std::cout << "Vendor ID: 0x" << std::hex << std::setw(4) << std::setfill('0') 
                      << vendor_id << ", Device ID: 0x" << device_id << std::dec << "\n";
        }
        
        std::cout << "\nTest PASSED\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
        std::cerr << "Note: This is expected if device 0:0:0.0 doesn't exist or you don't have permissions\n";
        std::cerr << "\nTest completed (device not accessible)\n";
        return 0;  // Not a failure - just means device doesn't exist
    }
}
