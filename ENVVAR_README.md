`PCM_NO_PERF=1` : don't use Linux perf events API to program *core* PMUs (default is to use it)

`PCM_USE_UNCORE_PERF=1` :  use Linux perf events API to program *uncore* PMUs (default is *not* to use it)

`PCM_NO_RDT=1` : don't use RDT metrics for a better interoperation with pqos utility (https://github.com/intel/intel-cmt-cat)
