--------------------------------------------------------------------------------
Processor Counter Monitor (PCM)
--------------------------------------------------------------------------------

[PCM Tools](#pcm-tools) | [Building PCM](#building-pcm-tools) | [Downloading Pre-Compiled PCM](#downloading-pre-compiled-pcm-tools) | [FAQ](#frequently-asked-questions-faq) | [API Documentation](#pcm-api-documentation) | [Environment Variables](#pcm-environment-variables) | [Compilation Options](#custom-compilation-options)

Processor Counter Monitor (PCM) is an application programming interface (API) and a set of tools based on the API to monitor performance and energy metrics of Intel&reg; Core&trade;, Xeon&reg;, Atom&trade; and Xeon Phi&trade; processors. PCM works on Linux, Windows, Mac OS X, FreeBSD, DragonFlyBSD and ChromeOS operating systems.

*Github repository statistics:* ![Custom badge](https://img.shields.io/endpoint?url=https%3A%2F%2Fhetthbszh0.execute-api.us-east-2.amazonaws.com%2Fdefault%2Fpcm-clones) ![Custom badge](https://img.shields.io/endpoint?url=https%3A%2F%2F5urjfrshcd.execute-api.us-east-2.amazonaws.com%2Fdefault%2Fpcm-yesterday-clones) ![Custom badge](https://img.shields.io/endpoint?url=https%3A%2F%2Fcsqqh18g3l.execute-api.us-east-2.amazonaws.com%2Fdefault%2Fpcm-today-clones)

*Code quality:* [![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/opcm/pcm.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/opcm/pcm/context:cpp) [![Language grade: Python](https://img.shields.io/lgtm/grade/python/g/opcm/pcm.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/opcm/pcm/context:python)

--------------------------------------------------------------------------------
Current Build Status
--------------------------------------------------------------------------------

- Linux and OSX: [![Build Status](https://travis-ci.com/opcm/pcm.svg?branch=master)](https://travis-ci.com/opcm/pcm)
- Windows: [![Build status](https://ci.appveyor.com/api/projects/status/github/opcm/pcm?branch=master&svg=true)](https://ci.appveyor.com/project/opcm/pcm)
- FreeBSD: [![Build Status](https://api.cirrus-ci.com/github/opcm/pcm.svg)](https://cirrus-ci.com/github/opcm/pcm)
- Docker Hub: [![Build status](https://img.shields.io/docker/cloud/build/opcm/pcm.svg)](doc/DOCKER_README.md)

--------------------------------------------------------------------------------
PCM Tools
--------------------------------------------------------------------------------

PCM provides a number of command-line utilities for real-time monitoring:

- **pcm** : basic processor monitoring utility (instructions per cycle, core frequency (including Intel(r) Turbo Boost Technology), memory and Intel(r) Quick Path Interconnect bandwidth, local and remote memory bandwidth, cache misses, core and CPU package sleep C-state residency, core and CPU package thermal headroom, cache utilization, CPU and memory energy consumption)
![pcm output](https://raw.githubusercontent.com/wiki/opcm/pcm/pcm.x.jpg)
- **pcm-sensor-server** : pcm collector exposing metrics over http in JSON or Prometheus (exporter text based) format ([how-to](doc/PCM-EXPORTER.md)). Also available as a [docker container](doc/DOCKER_README.md). More info about Global PCM events is [here](doc/PCM-SENSOR-SERVER-README.md).
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
- **pcm-raw**: [program arbitrary **core** and **uncore** events by specifying raw register event ID encoding](doc/PCM_RAW_README.md)
- **pcm-bw-histogram**: collect memory bandwidth utilization histogram

Graphical front ends:
- **pcm Grafana dashboard** :  front-end for Grafana (in [scripts/grafana](scripts/grafana) directory). Full Grafana Readme is [here](scripts/grafana/README.md)
![pcm grafana output](https://raw.githubusercontent.com/wiki/opcm/pcm/pcm-dashboard.png)
- **pcm-sensor** :  front-end for KDE KSysGuard
- **pcm-service** :  front-end for Windows perfmon

There are also utilities for reading/writing model specific registers (**pcm-msr**), PCI configuration registers (**pcm-pcicfg**) and memory mapped registers (**pcm-mmio**) supported on Linux, Windows, Mac OS X and FreeBSD.

And finally a daemon that stores core, memory and QPI counters in shared memory that can be be accessed by non-root users.

--------------------------------------------------------------------------------
Building PCM Tools
--------------------------------------------------------------------------------

Install cmake then:

```
mkdir build
cd build
cmake ..
cmake --build .
```
You will get all the utilities (pcm, pcm-memory, etc) in `build/bin` directory.
'--parallel' can be used for faster building:
```
cmake --build . --parallel
```
Debug is default on Windows. Specify config to build Release:
```
cmake --build . --config Release
```
On Windows and MacOs additional drivers are required. Please find instructions here: [WINDOWS_HOWTO.md](doc/WINDOWS_HOWTO.md) and [MAC_HOWTO.txt](doc/MAC_HOWTO.txt).

FreeBSD/DragonFlyBSD-specific details can be found in [FREEBSD_HOWTO.txt](doc/FREEBSD_HOWTO.txt)


--------------------------------------------------------------------------------
Downloading Pre-Compiled PCM Tools
--------------------------------------------------------------------------------

- Linux:
  * openSUSE: `sudo zypper install pcm`
  * RHEL8.5 or later: `sudo dnf install pcm` 
  * Fedora: `sudo yum install pcm`
  * RPMs and DEBs with the *latest* PCM version for RHEL/SLE/Ubuntu/Debian/openSUSE/etc distributions (binary and source) are available [here](https://software.opensuse.org/download/package?package=pcm&project=home%3Aopcm)
- Windows: download PCM binaries as [appveyor build service](https://ci.appveyor.com/project/opcm/pcm/history) artifacts and required Visual C++ Redistributable from [www.microsoft.com](https://www.microsoft.com/en-us/download/details.aspx?id=48145). Additional drivers are needed, see [WINDOWS_HOWTO.md](doc/WINDOWS_HOWTO.md).
- Docker: see [instructions on how to use pcm-sensor-server pre-compiled container from docker hub](doc/DOCKER_README.md).

--------------------------------------------------------------------------------
Frequently Asked Questions (FAQ)
--------------------------------------------------------------------------------

PCM's frequently asked questions (FAQ) are located [here](doc/FAQ.md).

--------------------------------------------------------------------------------
PCM API documentation
--------------------------------------------------------------------------------

PCM API documentation is embedded in the source code and can be generated into html format from source using Doxygen (www.doxygen.org).

--------------------------------------------------------------------------------
PCM environment variables
--------------------------------------------------------------------------------

The list of PCM environment variables is located [here](doc/ENVVAR_README.md)

--------------------------------------------------------------------------------
Custom compilation options
--------------------------------------------------------------------------------
The list of custom compilation options is located [here](doc/CUSTOM-COMPILE-OPTIONS.md)

--------------------------------------------------------------------------------
Packaging
--------------------------------------------------------------------------------
Packaging with CPack is supported on Debian and Redhat/SUSE system families.
To create DEB of RPM package need to call cpack after building in build folder:
```
cd build
cpack
```
This creates package:
- "pcm-VERSION-Linux.deb" on Debian family systems;
- "pcm-VERSION-Linux.rpm" on Redhat/SUSE-family systems.
Packages contain pcm-\* binaries and required for usage opCode-\* files.

