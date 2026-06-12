## Purpose:

PCM-IIO tool monitors PCIe transactions with a breakdown per PCIe bus (IIO stack) and/or PCIe devices.

## Tool UI introduction:

Inbound (PCIe device DMA into system) metrics:

* IB write (inbound write): the number of bytes per second that the PCIe device requested to write to main memory through DMA
* IB read (inbound read): the number of bytes per second that the PCIe device requested to read from main memory through DMA

Outbound (CPU MMIO to the PCIe device) metrics:

* OB read (outbound read): the number of bytes per second that the CPU requested to read from the PCIe device through MMIO (memory-mapped I/O)
* OB write (outbound write): the number of bytes per second that the CPU requested to write to the PCIe device through MMIO (memory-mapped I/O)

IOMMU metrics:

* IOTLB Lookup: IOTLB lookups per second
* IOTLB Miss: IOTLB misses per second
* Ctxt Cache Hit: Context cache hits per second
* 256T Cache Hit: Second Level Page Walk Cache Hits to a 256T page per second
* 512G Cache Hit: Second Level Page Walk Cache Hits to a 512G page per second
* 1G Cache Hit: Second Level Page Walk Cache Hits to a 1G page per second
* 2M Cache Hit: Second Level Page Walk Cache Hits to a 2M page per second
* IOMMU Mem Access: IOMMU memory accesses per second

Sample output:

![image](https://github.com/user-attachments/assets/e8cce396-b210-49d5-ac95-dc43f9ae69d3)

## Event config file:

pcm-iio tool allows the user to customize the performance events with a config file as an advanced feature. The event config files are in opCode-x-y.txt files where x/y is cpu family is model id, for example 6/143 for Sapphire Rapids.

