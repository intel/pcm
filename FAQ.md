
## Q1

pcm-iio Tool outputs "Server CPU is required for this tool! Program aborted". Is there a way I can monitor my PCIe link bandwidth using one of the PCM utilities on client CPU?

Answer: The "IO" metric in pcm.x is the closest capability to monitor I/O PCIe traffic on client CPUs.

## Q2

PCM reports "ERROR: QPI LL monitoring device (...) is missing". How to fix it?

Answer: It is likely a BIOS issue. See details [here](https://software.intel.com/content/www/us/en/develop/articles/bios-preventing-access-to-qpi-performance-counters.html)

## Q3

Does PCM work inside a virtual machine?

Answer: PCM works inside virtual machines which support vPMU (with a limited set of metrics supported by vPMU). For example on AWS instances this is indicated by the presence of [arch_perfmon](https://instaguide.io/info.html?type=c5.18xlarge) flag: https://instaguide.io/ . Enabling vPMU in hypervisors is documented [in Hardware Event-Based Sampling section](https://software.intel.com/content/www/us/en/develop/documentation/vtune-help/top/set-up-analysis-target/on-virtual-machine.html).

## Q4

Does PCM work inside a docker container?

Answer: yes, it does. An example of how to run PCM inside a docker container is located [here](https://github.com/opcm/pcm/blob/master/DOCKER_README.md). The recipe works also for other PCM utilities besides pcm-sensor-server.

## Q5

pcm-power reports "Unsupported processor model". Can you add support for pcm-power for my CPU?

Answer: most likely you have a client CPU which does not have required hardware performance monitoring units. PCM-power can not work without them.

## Q6

pcm-memory reports that the CPU is not supported. Can you add support for pcm-memory for my CPU?

Answer: most likely you have a client CPU which does not have required hardware performance monitoring units. PCM-memory can not work without them.

## Q7

Can PCM be used for measuring energy, CPU cycles, etc for a particular process or does it measure for the system as a whole?

Answer: PCM supports measurement for the whole system, per processor, per physical or per logical core. If you need monitoring per-process or user per-thread you can pin your process and/or thread to certain cores and read PCM data for these cores. But keep in mind that the OS can also schedule other processes or threads on this core and this may disturb your measurements. For a precise per-process or per-thread measurement the Intel VTune profiler or Linux perf profiler should be used.

## Q8

PCM reports

```
opening /dev/mem failed: errno is 1 (Operation not permitted)
Can not read memory controller counter information from PCI configuration space. Access to memory bandwidth counters is not possible.
You must be root to access these SandyBridge/IvyBridge/Haswell counters in PCM. 
Secure Boot detected. Using Linux perf for uncore PMU programming.
```

How to fix it?

Answer: Linux disables access to /dev/mem because Secure Boot is enabled in the BIOS. Disable Secure Boot in the BIOS to enable memory controller statistics (e.g. memory read and write bandwidth).

## Q9

PCM reports
```
Linux Perf: Error on programming ...
```
How to fix it?

**Answer:** It is an issue with the Linux kernel perf driver. As a workaround upgrade your Linux kernel to the latest available/possible or run PCM with its own programming logic:

```
export PCM_NO_PERF=1
pcm.x -r
```
