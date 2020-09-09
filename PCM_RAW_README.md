--------------------------------------------------------------------------------
PCM Raw Utility
--------------------------------------------------------------------------------

Disclaimer: in contrast to other PCM utilities this one is for expert usage only.

pcm-raw allows to collect arbitrary core and uncore PMU events by providing raw PMU event ID encoding. It can become handy if other low-level PMU tools (e.g. emon, Linux perf) can not be used for some reason. For example:
- emon kernel driver is not compatible with the currently used Linux kernel or operating system
- loading emon Linux kernel driver is forbidden due to system administration policies
- Linux kernel is too old to support modern processor PMU and can not be upgraded

Currently supported PMUs: core, m3upi, upi(ll)/qpi(ll), imc, m2m, pcu, cha/cbo, iio, ubox

Current limitations:
- event multiplexing not supported

Recommended usage (as priviliged/root user):
1. Install VTune which also contains emon (emon/sep driver installation is not needed): [free download](https://software.intel.com/content/www/us/en/develop/tools/vtune-profiler.html)
2. Run emon with `--dry-run -m` options to obtain raw PMU event encodings for event of interest. For example:
```
# emon -C BR_MISP_RETIRED.ALL_BRANCHES,UNC_CHA_CLOCKTICKS,UNC_IIO_DATA_REQ_OF_CPU.MEM_WRITE.PART0,UNC_UPI_TxL_FLITS.NON_DATA --dry-run -m
Event Set 0
        BR_MISP_RETIRED.ALL_BRANCHES (PerfEvtSel0 (0x186) = 0x00000000004300c5)
          CC=ALL PC=0x0 UMASK=0x0 E=0x1 INT=0x0 INV=0x0 CMASK=0x0 AMT=0x0
cha Uncore Event Set 0
        UNC_CHA_CLOCKTICKS (CHA Counter 0 (0xe01) = 0x0000000000400000)

qpill Uncore Event Set 0
        UNC_UPI_TxL_FLITS.NON_DATA (QPILL Counter 0 (0x350) = 0x0000000000409702)

iio Uncore Event Set 0
        UNC_IIO_DATA_REQ_OF_CPU.MEM_WRITE.PART0 (IIO Counter 0 (0xa48) = 0x0000701000400183)
```
3. Run pcm-raw by specifying the obtained raw event encodings to collect into csv file. Example:
```
pcm-raw.x -e core/config=0x00000000004300c5,name=BR_MISP_RETIRED.ALL_BRANCHES -e cha/config=0x0000000000400000,name=UNC_CHA_CLOCKTICKS -e qpi/config=0x0000000000409702,name=UNC_UPI_TxL_FLITS.NON_DATA -e iio/config=0x0000701000400183,name=UNC_IIO_DATA_REQ_OF_CPU.MEM_WRITE.PART0 -csv=out.csv 
```
4. View/process the csv file using your favorite method. For example just open it in Excel.
