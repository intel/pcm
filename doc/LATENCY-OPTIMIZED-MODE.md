# Latency Optimized Mode in Intel® Xeon® 6 Processors

Intel® Xeon® 6 Processors (previously codenamed Granite Rapids and Sierra Forest/Birch Stream platform) introduce a new power management mechanism called Efficiency Latency Control (ELC), designed to optimize performance per watt. This feature allows hardware power management algorithms to balance the trade-off between latency and power consumption. For latency-sensitive workloads, further tuning can be performed to achieve the desired performance.

The hardware monitors the average CPU utilization across all cores at regular intervals to determine an appropriate uncore frequency. While this approach generally results in optimal performance per watt, some workloads may achieve higher performance at the expense of increased power consumption. For instance, an application that intermittently performs memory reads on an otherwise idle system may experience delays if the hardware lowers the uncore frequency, causing a lag in ramping up to the required performance levels. To verify this, the uncore frequencies can be monitored using the pcm utility:

![Uncore Frequency Statistics DEFAULT](https://github.com/user-attachments/assets/108c7350-3fc2-4056-aeaf-ecc7c25da6bc)

The screenshot above presents real-time data on uncore frequency statistics, measured in GHz, from a dual-socket platform (represented by two rows). Each socket includes five dies (organized into five columns). The first three dies contain CORes (COR), Last Level Cache (LLC), and Memory controllers (M), collectively referred to as CORLLCM. The final two dies are IO dies.

The ELC control has parameters that can be adjusted either through BIOS or software tools. The default parameter configuration is optimized for performance per watt, ensuring power efficiency. The alternative configuration, known as Latency Optimized Mode, prioritizes maximum performance.
Below are the PCM statistics from a system operating in Latency Optimized Mode:

![Uncore Frequency Statistics Latency Optimized Mode](https://github.com/user-attachments/assets/70310bbc-725b-4450-af7a-1db2c04291dd)

## BIOS Options for Latency Optimized Mode

The BIOS option for selecting the Default or Latency Optimized Mode can typically be located in the following menus, depending on the BIOS version and OEM vendor:
- **Socket Configuration** -> **Advanced Power Management** -> **CPU – Advanced PM Tuning** -> **Latency Optimized Mode** (Disabled or Enabled)
- **System Utilities** -> **System Configuration** -> **BIOS/Platform Configuration (RBSU)** -> **Power and Performance Options** -> **Advanced Power Options** -> **Efficiency Latency Control** (Default or Latency Optimized mode)

Should this BIOS option be unavailable or if there is a preference to change the mode during runtime, the PCM repository provides scripts for changing this mode.

|Platform	         |Script Type|	URL                                                                |
|------------------|-----------|---------------------------------------------------------------------|
|Linux/FreeBSD/UNIX|bash       | https://github.com/intel/pcm/blob/master/scripts/bhs-power-mode.sh  |
|Windows	         |powershell | https://github.com/intel/pcm/blob/master/scripts/bhs-power-mode.ps1 |

The scripts require the pcm-tpmi utility. There are several methods to obtain this utility:
- **Download or install precompiled PCM binaries:** Please refer to the following link: [Downloading Pre-Compiled PCM Tools](https://github.com/intel/pcm?tab=readme-ov-file#downloading-pre-compiled-pcm-tools)
- **Compile the utility:** Follow the instructions in the "Building PCM Tools" section available at: [Building PCM Tools](https://github.com/intel/pcm?tab=readme-ov-file#building-pcm-tools)
   * For Linux/FreeBSD: Copy the pcm-tpmi utility from PCM’s source 'build/bin' directory to `/usr/local/bin/` or execute `make install` in the 'build' directory.

For Windows: Copy the pcm-tpmi utility to the current directory.

Once the pcm-tpmi binary is correctly placed, you can set the Latency Optimized Mode.

### Setting Latency Optimized Mode

Linux/FreeBSD/UNIX:
```
bash bhs-power-mode.sh --latency-optimized-mode
```
Windows:
```
.\bhs-power-mode.ps1 --latency-optimized-mode
```

### Restoring the Default Mode

Linux/FreeBSD/UNIX:
```
bash bhs-power-mode.sh --default
```

Windows:
```
.\bhs-power-mode.ps1 --default
```


