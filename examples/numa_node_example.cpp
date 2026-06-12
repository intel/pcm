// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024, Intel Corporation
// Example: How to retrieve NUMA node location for PCI devices

#include <iostream>
#include <iomanip>
#include "pci.h"

using namespace pcm;

int main()
{
    std::cout << "Example: Retrieving NUMA node location for PCI devices\n";
    std::cout << "========================================================\n\n";
    
    // Example 1: Get NUMA node for a specific PCI device
    // Format: segment (or group):bus:device.function
    uint32 segment = 0;    // Also known as "domain" or "group"
    uint32 bus = 0;
    uint32 device = 0;
    uint32 function = 0;
    
    try
    {
        // Create a handle to the PCI device
        // On Linux: uses /proc/bus/pci/ or PciHandleMM for memory-mapped access
        // On Windows: uses Windows driver
        // On FreeBSD: uses /dev/pci
        // On macOS: uses PCIDriver
        PciHandleType handle(segment, bus, device, function);
        
        std::cout << "Successfully opened PCI device " 
                  << segment << ":" << bus << ":" << device << "." << function << "\n";
        
        // Get the NUMA node location
        int32 numa_node = handle.getNUMANode();
        
        std::cout << "NUMA node: ";
        if (numa_node >= 0)
        {
            std::cout << numa_node << "\n";
        }
        else
        {
            std::cout << "Not available (return value: " << numa_node << ")\n";
            std::cout << "Note: -1 means NUMA information is not available on this platform\n";
            std::cout << "      or the PCI device does not have NUMA node association.\n";
        }
        
        // You can also read PCI configuration space as usual
        uint32 vendor_device_id = 0;
        if (handle.read32(0, &vendor_device_id) == sizeof(uint32))
        {
            uint32 vendor_id = vendor_device_id & 0xFFFF;
            uint32 device_id = (vendor_device_id >> 16) & 0xFFFF;
            std::cout << "\nPCI Device Info:\n";
            std::cout << "  Vendor ID: 0x" << std::hex << std::setw(4) << std::setfill('0') 
                      << vendor_id << "\n";
            std::cout << "  Device ID: 0x" << std::setw(4) << std::setfill('0') 
                      << device_id << std::dec << "\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << "\nPossible reasons:\n";
        std::cerr << "  - PCI device does not exist\n";
        std::cerr << "  - Insufficient permissions (try running as root/administrator)\n";
        std::cerr << "  - PCI subsystem not available on this platform\n";
        return 1;
    }
    
    std::cout << "\n=== Example completed successfully ===\n";
    return 0;
}
