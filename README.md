--------------------------------------------------------------------------------
Processor Counter Monitor (PCM)
--------------------------------------------------------------------------------

Processor Counter Monitor (PCM) is an application programming interface (API) and a set of tools based on the API to monitor performance and energy metrics of Intel&reg; Core&trade;, Xeon&reg;, Atom&trade; and Xeon Phi&trade; processors. PCM works on Linux, Windows, Mac OS X, FreeBSD and DragonFlyBSD operating systems.

--------------------------------------------------------------------------------
Current Build Status
--------------------------------------------------------------------------------

- Linux and OSX: [![Build Status](https://travis-ci.org/opcm/pcm.svg?branch=master)](https://travis-ci.org/opcm/pcm)
- Windows: [![Build status](https://ci.appveyor.com/api/projects/status/0ytkojay9r0o6sxy?svg=true)](https://ci.appveyor.com/project/opcm/pcm)

--------------------------------------------------------------------------------
PCM Tools
--------------------------------------------------------------------------------

PCM provides a number of command-line utilities for real-time monitoring:

- pcm : basic processor monitoring utility (instructions per cycle, core frequency (including Intel(r) Turbo Boost Technology), memory and Intel(r) Quick Path Interconnect bandwidth, local and remote memory bandwidth, cache misses, core and CPU package sleep C-state residency, core and CPU package thermal headroom, cache utilization, CPU and memory energy consumption)
- pcm-memory : monitor memory bandwidth (per-channel and per-DRAM DIMM rank)
- pcm-latency : monitor L1 cache miss and DDR/PMM memory latency
- pcm-pcie : monitor PCIe bandwidth per-socket
- pcm-iio : monitor PCIe bandwidth per PCIe device
- pcm-numa : monitor local and remote memory accesses
- pcm-power : monitor sleep and energy states of processor, Intel(r) Quick Path Interconnect, DRAM memory, reasons of CPU frequency throttling and other energy-related metrics
- pcm-tsx: monitor performance metrics for Intel(r) Transactional Synchronization Extensions
- pcm-core and pmu-query: query and monitor arbitrary processor core events

Graphical front ends:
- pcm-sensor :  front-end for KDE KSysGuard
- pcm-service :  front-end for Windows perfmon

There is also a utility for reading/writing Intel model specific registers (pcm-msr) supported on Linux, Windows, Mac OS X and FreeBDS.

And finally a daemon that stores core, memory and QPI counters in shared memory that can be be accessed by non-root users.

--------------------------------------------------------------------------------
PCM API documentation
--------------------------------------------------------------------------------

PCM API documentation is embedded in the source code and can be generated into html format from source using Doxygen (www.doxygen.org).

--------------------------------------------------------------------------------
Building the PCM Tools
--------------------------------------------------------------------------------

- Linux: just type 'make'. You will get all the utilities (pcm.x, pcm-memory.x, etc) built in the main PCM directory.
- FreeBSD/DragonFlyBSD: just type 'gmake'. You will get all the utilities (pcm.x, pcm-memory.x, etc) built in the main PCM directory. If the 'gmake' command is not available, you need to install GNU make from ports (for example with 'pkg install gmake').
- Windows: follow the steps in [WINDOWS_HOWTO.rtf](https://raw.githubusercontent.com/opcm/pcm/master/WINDOWS_HOWTO.rtf) (will will need to build or download additional drivers). You can also download PCM binaries from [appveyor build service](https://ci.appveyor.com/project/opcm/pcm/build/artifacts) and required Visual C++ Redistributable from [www.microsoft.com](https://www.microsoft.com/en-us/download/details.aspx?id=48145).
- Mac OS X: follow instructions in [MAC_HOWTO.txt](https://github.com/opcm/pcm/blob/master/MAC_HOWTO.txt)

