
**Q:** pcm-iio Tool outputs "Server CPU is required for this tool! Program aborted". Is there a way I can monitor my PCIe link bandwidth using one of the PCM utilities?

**A:** The "IO" metric in pcm.x is the closest capability to monitor I/O PCIe traffic on client CPUs.

**Q:** PCM reports "ERROR: QPI LL monitoring device (...) is missing".

**A:** It is likely a BIOS issue. See details [here](https://software.intel.com/content/www/us/en/develop/articles/bios-preventing-access-to-qpi-performance-counters.html)

**Q:** Does PCM work inside a virtual machine.

**A:** PCM does not work inside a virtual machine because it needs access to low-level registers which is typically forbidden inside a virtual machine.

**Q:** Does PCM work inside a docker container.

**A:** yes, it does. An example of how to run PCM inside a docker container is located [here](https://github.com/opcm/pcm/blob/master/DOCKER_README.md). The recipe works also for other PCM utilities besides pcm-sensor-server.
