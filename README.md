--------------------------------------------------------------------------------
Intel&reg; Performance Counter Monitor (Intel&reg; PCM)
--------------------------------------------------------------------------------

[![CodeQL](https://github.com/intel/pcm/actions/workflows/codeql.yml/badge.svg?branch=master)](https://github.com/intel/pcm/security/code-scanning/tools/CodeQL/status)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/intel/pcm/badge)](https://securityscorecards.dev/viewer/?uri=github.com/intel/pcm)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8652/badge)](https://www.bestpractices.dev/projects/8652)

[PCM Tools](#pcm-tools) | [Building PCM](#building-pcm-tools) | [Downloading Pre-Compiled PCM](#downloading-pre-compiled-pcm-tools) | [FAQ](#frequently-asked-questions-faq) | [API Documentation](#pcm-api-documentation) | [Environment Variables](#pcm-environment-variables) | [Compilation Options](#custom-compilation-options)

Intel&reg; Performance Counter Monitor (Intel&reg; PCM) is an application programming interface (API) and a set of tools based on the API to monitor performance and energy metrics of Intel&reg; Core&trade;, Xeon&reg;, Atom&trade; and Xeon Phi&trade; processors. PCM works on Linux, Windows, Mac OS X, FreeBSD, DragonFlyBSD and ChromeOS operating systems.

*Github repository statistics:* ![Custom badge](https://img.shields.io/endpoint?url=https%3A%2F%2Fhetthbszh0.execute-api.us-east-2.amazonaws.com%2Fdefault%2Fpcm-clones) ![Custom badge](https://img.shields.io/endpoint?url=https%3A%2F%2F5urjfrshcd.execute-api.us-east-2.amazonaws.com%2Fdefault%2Fpcm-yesterday-clones) ![Custom badge](https://img.shields.io/endpoint?url=https%3A%2F%2Fcsqqh18g3l.execute-api.us-east-2.amazonaws.com%2Fdefault%2Fpcm-today-clones)

We welcome bug reports and enhancement requests, which can be submitted via the "Issues" section on GitHub. For those interested in contributing to the code, please refer to the guidelines outlined in the CONTRIBUTING.md file.

--------------------------------------------------------------------------------
Current Build Status
--------------------------------------------------------------------------------

- Linux: [![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/intel/pcm/linux_make.yml?branch=master)](https://github.com/intel/pcm/actions/workflows/linux_make.yml?query=branch%3Amaster)
- Windows: [![Build status](https://ci.appveyor.com/api/projects/status/github/intel/pcm?branch=master&svg=true)](https://ci.appveyor.com/project/opcm/pcm)
- FreeBSD: [![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/intel/pcm/freebsd_build.yml?branch=master)](https://github.com/intel/pcm/actions/workflows/freebsd_build.yml?query=branch%3Amaster)
- OS X: [![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/intel/pcm/macosx_build.yml?branch=master)](https://github.com/intel/pcm/actions/workflows/macosx_build.yml?query=branch%3Amaster)
- Docker container: [![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/intel/pcm/docker.yml?branch=master)](doc/DOCKER_README.md)

--------------------------------------------------------------------------------
PCM Tools
--------------------------------------------------------------------------------

PCM provides a number of command-line utilities for real-time monitoring:

- **pcm** : basic processor monitoring utility (instructions per cycle, core frequency (including Intel(r) Turbo Boost Technology), memory and Intel(r) Quick Path Interconnect bandwidth, local and remote memory bandwidth, cache misses, core and CPU package sleep C-state residency, core and CPU package thermal headroom, cache utilization, CPU and memory energy consumption)

![pcm output](https://github.com/intel/pcm/assets/25432609/88485ff5-dc7c-4a1c-974f-8396f03829dc)

- **pcm-sensor-server** : pcm collector exposing metrics over http in JSON or Prometheus (exporter text based) format ([how-to](doc/PCM-EXPORTER.md)). Also available as a [docker container](doc/DOCKER_README.md). More info about Global PCM events is [here](doc/PCM-SENSOR-SERVER-README.md).
- **pcm-memory** : monitor memory bandwidth (per-channel and per-DRAM DIMM rank)
![pcm-memory output](https://raw.githubusercontent.com/wiki/intel/pcm/pcm-memory.x.JPG)
- **pcm-accel** : [monitor Intel® In-Memory Analytics Accelerator (Intel® IAA), Intel® Data Streaming Accelerator (Intel® DSA) and Intel® QuickAssist Technology (Intel® QAT)  accelerators](doc/PCM_ACCEL_README.md)
![image](https://user-images.githubusercontent.com/25432609/218480696-42ade94f-e0c3-4000-9dd8-39a0e75a210e.png)

- **pcm-latency** : monitor L1 cache miss and DDR/PMM memory latency
- **pcm-pcie** : monitor PCIe bandwidth per-socket
- **pcm-iio** : monitor PCIe bandwidth per PCIe device

![pcm-iio output](https://raw.githubusercontent.com/wiki/intel/pcm/pcm-iio.png)
- **pcm-numa** : monitor local and remote memory accesses
- **pcm-power** : monitor sleep and energy states of processor, Intel(r) Quick Path Interconnect, DRAM memory, reasons of CPU frequency throttling and other energy-related metrics
- **pcm-tsx**: monitor performance metrics for Intel(r) Transactional Synchronization Extensions
- **pcm-core** and **pmu-query**: query and monitor arbitrary processor core events
- **pcm-raw**: [program arbitrary **core** and **uncore** events by specifying raw register event ID encoding](doc/PCM_RAW_README.md)
- **pcm-bw-histogram**: collect memory bandwidth utilization histogram

Graphical front ends:
- **pcm Grafana dashboard** :  front-end for Grafana (in [scripts/grafana](scripts/grafana) directory). Full Grafana Readme is [here](scripts/grafana/README.md)
![pcm grafana output](https://raw.githubusercontent.com/wiki/intel/pcm/pcm-dashboard.png)
- **pcm-sensor** :  front-end for KDE KSysGuard
- **pcm-service** :  front-end for Windows perfmon

There are also utilities for reading/writing model specific registers (**pcm-msr**), PCI configuration registers (**pcm-pcicfg**), memory mapped registers (**pcm-mmio**) and TPMI registers (**pcm-tpmi**) supported on Linux, Windows, Mac OS X and FreeBSD.

And finally a daemon that stores core, memory and QPI counters in shared memory that can be be accessed by non-root users.

--------------------------------------------------------------------------------
Building PCM Tools
--------------------------------------------------------------------------------

Clone PCM repository with submodules:

```
git clone --recursive https://github.com/intel/pcm
```

or clone the repository first, and then update submodules with:

```
git submodule update --init --recursive
```

Install cmake (and libasan on Linux) then:

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
On Windows and MacOs additional drivers and steps are required. Please find instructions here: [WINDOWS_HOWTO.md](doc/WINDOWS_HOWTO.md) and [MAC_HOWTO.txt](doc/MAC_HOWTO.txt).

FreeBSD/DragonFlyBSD-specific details can be found in [FREEBSD_HOWTO.txt](doc/FREEBSD_HOWTO.txt)

![pcm-build-run-2](https://user-images.githubusercontent.com/25432609/205663554-c4fa1724-6286-495a-9dbd-0104de3f535f.gif)

--------------------------------------------------------------------------------
Downloading Pre-Compiled PCM Tools
--------------------------------------------------------------------------------

- Linux:
  * Ubuntu/Debian: `sudo apt install pcm`
  * openSUSE: `sudo zypper install pcm`
  * RHEL8.5 or later: `sudo dnf install pcm` 
  * Fedora: `sudo yum install pcm`
  * RPMs and DEBs with the *latest* PCM version for RHEL/SLE/Ubuntu/Debian/openSUSE/etc distributions (binary and source) are available [here](https://software.opensuse.org/download/package?package=pcm&project=home%3Aopcm)
- Windows: download PCM binaries as [appveyor build service](https://ci.appveyor.com/project/opcm/pcm/history) artifacts and required Visual C++ Redistributable from [www.microsoft.com](https://www.microsoft.com/en-us/download/details.aspx?id=48145). Additional steps and drivers are required, see [WINDOWS_HOWTO.md](doc/WINDOWS_HOWTO.md).
- Docker: see [instructions on how to use pcm-sensor-server pre-compiled container from docker hub](doc/DOCKER_README.md).

--------------------------------------------------------------------------------
Executing PCM tools under non-root user on Linux
--------------------------------------------------------------------------------

Executing PCM tools under an unprivileged user on a Linux operating system is feasible. However, there are certain prerequisites that need to be met, such as having Linux perf_event support for your processor in the Linux kernel version you are currently running. To successfully run the PCM tools, you need to set the `/proc/sys/kernel/perf_event_paranoid` setting to -1 as root once:

```
echo -1 > /proc/sys/kernel/perf_event_paranoid
```

and configure two specific environment variables when running the tools under a non-root user:

```
export PCM_NO_MSR=1
export PCM_KEEP_NMI_WATCHDOG=1
```

For instance, you can execute the following commands to set the environment variables and run pcm:

```
export PCM_NO_MSR=1
export PCM_KEEP_NMI_WATCHDOG=1
pcm
```

or (to run the pcm sensor server as non-root):

```
PCM_NO_MSR=1 PCM_KEEP_NMI_WATCHDOG=1 pcm-sensor-server
```

Please keep in mind that when executing PCM tools under an unprivileged user on Linux, certain PCM metrics may be unavailable. This limitation specifically affects metrics that rely solely on direct MSR (Model-Specific Register) register access. Due to the restricted privileges of the user, accessing these registers is not permitted, resulting in the absence of corresponding metrics.

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
