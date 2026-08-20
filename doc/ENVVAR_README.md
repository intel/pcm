`PCM_NO_PERF=1` : don't use Linux perf events API to program *core* PMUs (default is to use it)

`PCM_USE_UNCORE_PERF=1` :  use Linux perf events API to program *uncore* PMUs (default is *not* to use it)

`PCM_NO_RDT=1` : don't use RDT metrics for a better interoperation with pqos utility (https://github.com/intel/intel-cmt-cat)

`PCM_USE_RESCTRL=1` : use Linux resctrl driver for RDT metrics

`PCM_PRINT_TOPOLOGY=1` : print detailed CPU topology

`PCM_KEEP_NMI_WATCHDOG=1` : don't disable NMI watchdog (reducing the core metrics set)

`PCM_NO_MAIN_EXCEPTION_HANDLER=1` :  don't catch exceptions in the main function of pcm tools (a debugging option)

`PCM_ENFORCE_MBM=1` :  force-enable Memory Bandwidth Monitoring (MBM) metrics (LocalMemoryBW = LMB) and (RemoteMemoryBW = RMB) on processors with RDT/MBM errata

`PCM_NO_TPMI_RAPL=1` :  don't use the architectural RAPL TPMI interface for the package (CPU socket), DRAM and system (platform) energy/power metrics, use the legacy RAPL MSRs instead. By default PCM uses the RAPL TPMI interface whenever it is available, i.e. on server processors exposing the RAPL TPMI feature (TPMI ID 0) either through the Linux TPMI driver (debugfs) or through direct MMIO access, and falls back to the legacy RAPL MSRs otherwise. The package, DRAM and system energy metrics fall back to the MSRs independently of each other if their RAPL TPMI domains (package, memory and system) are not found. The RAPL TPMI interface is described in https://github.com/intel/tpmi_power_management/blob/main/RAPL_TPMI_public_disclosure_FINAL-rev3.pdf

`PCM_NO_TPMI_DRIVER=1` :  don't use the Linux TPMI driver (debugfs) to access TPMI registers, access them through MMIO instead

`PCM_QUIET=1` :  enable quiet mode for PCM initialization. In quiet mode, only error messages are output during PCM initialization, suppressing informational output such as processor information and topology details

`PCM_DEBUG_LEVEL=x` :  x is an integer defining debug output level. level = 0 (default): minimal or no debug info, > 0 increases verbosity
