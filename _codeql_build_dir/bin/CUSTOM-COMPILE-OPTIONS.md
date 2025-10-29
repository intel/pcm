cpucounters.h : `#define PCM_HA_REQUESTS_READS_ONLY`
For the metric "LOCAL: number of local memory access in %“ (getLocalMemoryRequestRatio API) count only read accesses (local and all).

cpucounters.h : `#define PCM_DEBUG_TOPOLOGY`
print detailed CPU topology information

cpucounters.h : `#define PCM_UNCORE_PMON_BOX_CHECK_STATUS`
verify uncore PMU register state after programming

types.h : `#define PCM_DEBUG`
print some debug information

pci.h : `#define PCM_USE_PCI_MM_LINUX`
use /dev/mem (direct memory mapped I/O) for PCICFG register access on Linux. This might be required for accessing registers in extended configuration space (beyond 4K) in *pcm-pcicfg* utility. Recent Linux kernels also require to be booted with iomem=relaxed option to make this work.

