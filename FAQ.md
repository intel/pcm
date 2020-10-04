
**Q1:** pcm-iio Tool outputs "Server CPU is required for this tool! Program aborted". Is there a way I can monitor my PCIe link bandwidth using one of the PCM utilities on client CPU?

**A1:** The "IO" metric in pcm.x is the closest capability to monitor I/O PCIe traffic on client CPUs.

**Q2:** PCM reports "ERROR: QPI LL monitoring device (...) is missing". How to fix it?

**A2:** It is likely a BIOS issue. See details [here](https://software.intel.com/content/www/us/en/develop/articles/bios-preventing-access-to-qpi-performance-counters.html)

**Q3:** Does PCM work inside a virtual machine?

**A3:** PCM does not work inside a virtual machine because it needs access to low-level registers which is typically forbidden inside a virtual machine.

**Q4:** Does PCM work inside a docker container?

**A4:** yes, it does. An example of how to run PCM inside a docker container is located [here](https://github.com/opcm/pcm/blob/master/DOCKER_README.md). The recipe works also for other PCM utilities besides pcm-sensor-server.

**Q5:** pcm-power reports "Unsupported processor model". Can you add support for pcm-power for my CPU?

**A5:** most likely you have a client CPU which does not have required hardware performance monitoring units. PCM-power can not work without them.

**Q6:** pcm-memory reports that the CPU is not supported. Can you add support for pcm-memory for my CPU?

**A6:** most likely you have a client CPU which does not have required hardware performance monitoring units. PCM-memory can not work without them.
