# NUMA Node Location API for PCI Devices

## Overview

The `getNUMANode()` API allows you to retrieve the NUMA (Non-Uniform Memory Access) node location of a PCI device identified by its segment:bus:device:function coordinates.

## Background

- **PciHandle** and **PciHandleMM** classes are abstractions of PCI configuration space registers
- Each PCI device has a unique location: `segment:bus:device:function`
- **segment** is also known as **group number** or **domain** (synonyms: groupnr, groupnr_)

## API Usage

### Method Signature

```cpp
int32 PciHandle::getNUMANode() const;
int32 PciHandleMM::getNUMANode() const;
```

### Return Value

- **>= 0**: The NUMA node ID where the PCI device is located
- **-1**: NUMA information not available or not applicable

### Example

```cpp
#include "pci.h"

using namespace pcm;

// Open a PCI device at segment 0, bus 0, device 0, function 0
PciHandleType handle(0, 0, 0, 0);

// Get the NUMA node
int32 numa_node = handle.getNUMANode();

if (numa_node >= 0) {
    std::cout << "Device is on NUMA node: " << numa_node << "\n";
} else {
    std::cout << "NUMA information not available\n";
}
```

## Platform-Specific Implementation

### Linux

- **Method**: Reads from `/sys/bus/pci/devices/<domain>:<bus>:<device>.<function>/numa_node`
- **Fallback**: Also tries `/pcm/sys/bus/pci/devices/...` path
- **Return**: 
  - NUMA node ID (typically 0, 1, 2, ...) if available
  - -1 if the file doesn't exist or can't be read

### Windows

- **Method**: Reads SRAT (System Resource Affinity Table) from ACPI firmware using `GetSystemFirmwareTable` API
- **Implementation**: 
  - Parses SRAT table to extract PCI Device Affinity structures (type 2)
  - Builds a mapping from PCI device location (segment:bus:device:function) to NUMA node (proximity domain)
  - Caches the mapping on first call for performance
- **Return**: 
  - NUMA node ID (proximity domain) if device is found in SRAT table
  - -1 if SRAT table is not available or device is not listed
- **Requirements**: Windows Vista or later (for `GetSystemFirmwareTable` API)

### FreeBSD / DragonFly

- **Method**: Queries system via `sysctlbyname()` for NUMA domain information
- **Implementation**:
  - First checks if NUMA is enabled via `vm.ndomains` sysctl
  - Attempts to query PCI device-specific NUMA domain using multiple sysctl path formats
  - Tries: `hw.pci.X.Y.Z.W.numa_domain` and `hw.pci.X:Y:Z.W.numa_domain`
- **Return**:
  - NUMA node ID if available and system has NUMA enabled
  - -1 if NUMA is disabled, not supported, or device affinity information unavailable
- **Note**: FreeBSD doesn't have a standardized sysctl path for PCI device NUMA affinity across all versions

### macOS

- **Method**: Returns -1 (macOS typically doesn't expose NUMA for PCI devices)
- **Return**: -1 (not applicable)

## Use Cases

1. **Performance Optimization**: Place processing threads on the same NUMA node as the device
2. **Memory Allocation**: Allocate buffers on the same NUMA node for optimal DMA performance
3. **System Topology Discovery**: Map out the relationship between PCI devices and NUMA nodes
4. **Monitoring and Analytics**: Identify cross-NUMA traffic patterns

## Building the Example

```bash
cd examples
g++ -std=c++11 -I../src numa_node_example.cpp -o numa_node_example -L../build/lib -lpcm -lpthread
LD_LIBRARY_PATH=../build/lib ./numa_node_example
```

## Notes

- Requires appropriate permissions to access PCI configuration space
- On Linux, run with `sudo` or ensure `/sys/bus/pci` is accessible
- The NUMA node value is read at runtime and not cached
- A return value of -1 doesn't indicate an error; it means NUMA information is not available

## Related APIs

- `PciHandle::read32()` - Read 32-bit value from PCI configuration space
- `PciHandle::write32()` - Write 32-bit value to PCI configuration space
- `PciHandle::read64()` - Read 64-bit value from PCI configuration space
- `PciHandle::exists()` - Check if a PCI device exists

## See Also

- Linux kernel documentation: `Documentation/ABI/testing/sysfs-bus-pci`
- ACPI SRAT (System Resource Affinity Table) specification
- PCI Express Base Specification
