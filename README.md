--------------------------------------------------------------------------------
Processor Counter Monitor (PCM)
--------------------------------------------------------------------------------

[PCM Tools](#pcm-tools) | [Building PCM](#building-pcm-tools) | [Downloading Pre-Compiled PCM](#downloading-pre-compiled-pcm-tools) | [FAQ](#frequently-asked-questions-faq) | [API Documentation](#pcm-api-documentation) | [Environment Variables](#pcm-environment-variables) | [Compilation Options](#custom-compilation-options)

Processor Counter Monitor (PCM) is an application programming interface (API) and a set of tools based on the API to monitor performance and energy metrics of Intel&reg; Core&trade;, Xeon&reg;, Atom&trade; and Xeon Phi&trade; processors. PCM works on Linux, Windows, Mac OS X, FreeBSD and DragonFlyBSD operating systems.

*Github repository statistics:* ![Custom badge](https://img.shields.io/endpoint?url=https%3A%2F%2Fhetthbszh0.execute-api.us-east-2.amazonaws.com%2Fdefault%2Fpcm-clones) ![Custom badge](https://img.shields.io/endpoint?url=https%3A%2F%2F5urjfrshcd.execute-api.us-east-2.amazonaws.com%2Fdefault%2Fpcm-yesterday-clones) ![Custom badge](https://img.shields.io/endpoint?url=https%3A%2F%2Fcsqqh18g3l.execute-api.us-east-2.amazonaws.com%2Fdefault%2Fpcm-today-clones)

--------------------------------------------------------------------------------
Current Build Status
--------------------------------------------------------------------------------

- Linux and OSX: [![Build Status](https://travis-ci.com/opcm/pcm.svg?branch=master)](https://travis-ci.com/opcm/pcm)
- Windows: [![Build status](https://ci.appveyor.com/api/projects/status/github/opcm/pcm?branch=master&svg=true)](https://ci.appveyor.com/project/opcm/pcm)
- FreeBSD: [![Build Status](https://api.cirrus-ci.com/github/opcm/pcm.svg)](https://cirrus-ci.com/github/opcm/pcm)
- Docker Hub: [![Build status](https://img.shields.io/docker/cloud/build/opcm/pcm.svg)](https://github.com/opcm/pcm/blob/master/DOCKER_README.md) [![pulls](https://img.shields.io/docker/pulls/opcm/pcm.svg)](https://github.com/opcm/pcm/blob/master/DOCKER_README.md)

--------------------------------------------------------------------------------
PCM Tools
--------------------------------------------------------------------------------

PCM provides a number of command-line utilities for real-time monitoring:

- **pcm** : basic processor monitoring utility (instructions per cycle, core frequency (including Intel(r) Turbo Boost Technology), memory and Intel(r) Quick Path Interconnect bandwidth, local and remote memory bandwidth, cache misses, core and CPU package sleep C-state residency, core and CPU package thermal headroom, cache utilization, CPU and memory energy consumption)
![pcm output](https://raw.githubusercontent.com/wiki/opcm/pcm/pcm.x.jpg)
- **pcm-sensor-server** : pcm collector exposing metrics over http in JSON or Prometheus (exporter text based) format. Also available as a [docker container](https://github.com/opcm/pcm/blob/master/DOCKER_README.md)
- **pcm-memory** : monitor memory bandwidth (per-channel and per-DRAM DIMM rank)
![pcm-memory output](https://raw.githubusercontent.com/wiki/opcm/pcm/pcm-memory.x.JPG)
- **pcm-latency** : monitor L1 cache miss and DDR/PMM memory latency
- **pcm-pcie** : monitor PCIe bandwidth per-socket
- **pcm-iio** : monitor PCIe bandwidth per PCIe device
![pcm-iio output](https://raw.githubusercontent.com/wiki/opcm/pcm/pcm-iio.png)
- **pcm-numa** : monitor local and remote memory accesses
- **pcm-power** : monitor sleep and energy states of processor, Intel(r) Quick Path Interconnect, DRAM memory, reasons of CPU frequency throttling and other energy-related metrics
- **pcm-tsx**: monitor performance metrics for Intel(r) Transactional Synchronization Extensions
- **pcm-core** and **pmu-query**: query and monitor arbitrary processor core events
- **pcm-raw**(NEW): [program arbitrary **core** and **uncore** events by specifying raw register event ID encoding](https://github.com/opcm/pcm/blob/master/PCM_RAW_README.md)
- **pcm-bw-histogram**: collect memory bandwidth utilization histogram

Graphical front ends:
- **pcm Grafana dashboard** :  front-end for Grafana (in [grafana](https://github.com/opcm/pcm/tree/master/grafana) directory)
![pcm grafana output](https://raw.githubusercontent.com/wiki/opcm/pcm/pcm-dashboard.png)
- **pcm-sensor** :  front-end for KDE KSysGuard
- **pcm-service** :  front-end for Windows perfmon

There are also utilities for reading/writing model specific registers (**pcm-msr**), PCI configuration registers (**pcm-pcicfg**) and memory mapped registers (**pcm-mmio**) supported on Linux, Windows, Mac OS X and FreeBSD.

And finally a daemon that stores core, memory and QPI counters in shared memory that can be be accessed by non-root users.

--------------------------------------------------------------------------------
Building PCM Tools
--------------------------------------------------------------------------------

- Linux: just type 'make'. You will get all the utilities (pcm.x, pcm-memory.x, etc) built in the main PCM directory.
- FreeBSD/DragonFlyBSD: just type 'gmake'. You will get all the utilities (pcm.x, pcm-memory.x, etc) built in the main PCM directory. If the 'gmake' command is not available, you need to install GNU make from ports (for example with 'pkg install gmake').
- Windows: follow the steps in [WINDOWS_HOWTO.md](https://github.com/opcm/pcm/blob/master/WINDOWS_HOWTO.md) (will need to build or download additional drivers).
- Mac OS X: follow instructions in [MAC_HOWTO.txt](https://github.com/opcm/pcm/blob/master/MAC_HOWTO.txt)

--------------------------------------------------------------------------------
Downloading Pre-Compiled PCM Tools
--------------------------------------------------------------------------------

- Linux: precompiled RPMs (binary and source) and DEBs are available [here](https://download.opensuse.org/repositories/home:/opcm/)
- Windows: download PCM binaries as [appveyor build service](https://ci.appveyor.com/project/opcm/pcm/history) artifacts and required Visual C++ Redistributable from [www.microsoft.com](https://www.microsoft.com/en-us/download/details.aspx?id=48145). Additional drivers are needed, see [WINDOWS_HOWTO.md](https://github.com/opcm/pcm/blob/master/WINDOWS_HOWTO.md).
- Docker: see [instructions on how to use pcm-sensor-server pre-compiled container from docker hub](https://github.com/opcm/pcm/blob/master/DOCKER_README.md).

--------------------------------------------------------------------------------
Frequently Asked Questions (FAQ)
--------------------------------------------------------------------------------

PCM's frequently asked questions (FAQ) are located [here](https://github.com/opcm/pcm/blob/master/FAQ.md).

--------------------------------------------------------------------------------
PCM API documentation
--------------------------------------------------------------------------------

PCM API documentation is embedded in the source code and can be generated into html format from source using Doxygen (www.doxygen.org).

--------------------------------------------------------------------------------
PCM environment variables
--------------------------------------------------------------------------------

The list of PCM environment variables is located [here](https://github.com/opcm/pcm/blob/master/ENVVAR_README.md)

--------------------------------------------------------------------------------
Custom compilation options
--------------------------------------------------------------------------------
The list of custom compilation options is located [here](https://github.com/opcm/pcm/blob/master/CUSTOM-COMPILE-OPTIONS.md)
