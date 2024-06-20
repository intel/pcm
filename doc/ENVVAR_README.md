`PCM_NO_PERF=1` : don't use Linux perf events API to program *core* PMUs (default is to use it)

`PCM_USE_UNCORE_PERF=1` :  use Linux perf events API to program *uncore* PMUs (default is *not* to use it)

`PCM_NO_RDT=1` : don't use RDT metrics for a better interoperation with pqos utility (https://github.com/intel/intel-cmt-cat)

`PCM_USE_RESCTRL=1` : use Linux resctrl driver for RDT metrics

`PCM_PRINT_TOPOLOGY=1` : print detailed CPU topology

`PCM_KEEP_NMI_WATCHDOG=1` : don't disable NMI watchdog (reducing the core metrics set)

`PCM_NO_MAIN_EXCEPTION_HANDLER=1` :  don't catch exceptions in the main function of pcm tools (a debugging option)

`PCM_ENFORCE_MBM=1` :  force-enable Memory Bandwidth Monitoring (MBM) metrics (LocalMemoryBW = LMB) and (RemoteMemoryBW = RMB) on processors with RDT/MBM errata
