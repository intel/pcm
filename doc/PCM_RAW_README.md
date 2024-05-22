--------------------------------------------------------------------------------
PCM Raw Utility
--------------------------------------------------------------------------------

Disclaimer: in contrast to other PCM utilities this one is for expert usage only.

*pcm-raw* allows to collect arbitrary core and uncore PMU events by providing raw PMU event ID encoding. It can become handy if other low-level PMU tools (e.g. emon, Linux perf) can not be used for some reason. For example:
- emon kernel driver is not compatible with the currently used Linux kernel or operating system
- loading emon Linux kernel driver is forbidden due to system administration policies
- Linux kernel is too old to support modern processor PMU and can not be upgraded

Currently supported PMUs: core, m3upi, upi(ll)/qpi(ll), imc, m2m, pcu, cha/cbo, iio, ubox

Recommended usage (as privileged/root user):
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
3. Run *pcm-raw* by specifying the obtained raw event encodings to collect into csv file. Example:
```
pcm-raw -e core/config=0x00000000004300c5,name=BR_MISP_RETIRED.ALL_BRANCHES -e cha/config=0x0000000000400000,name=UNC_CHA_CLOCKTICKS -e qpi/config=0x0000000000409702,name=UNC_UPI_TxL_FLITS.NON_DATA -e iio/config=0x0000701000400183,name=UNC_IIO_DATA_REQ_OF_CPU.MEM_WRITE.PART0 -csv=out.csv
```
4. View/process the csv file using your favorite method. For example just open it in Excel.

--------------------------------------------------------------------------------
Collecting Register Values
--------------------------------------------------------------------------------

pcm-raw supports collecting raw MSR and PCICFG (CSR) register values. The syntax is described below:

Model Specific Registers (MSRs):

```
package_msr/config=<msr_address>,config1=<static_or_freerun>[,name=<name>]
```

static_or_freerun encoding:
* 0 : static (last value reported in csv)
* 1 : freerun (delta to last value reported in csv)

Examples:
```
package_msr/config=0x34,config1=0,name=SMI_COUNT
thread_msr/config=0x10,config1=1,name=TSC_DELTA
thread_msr/config=0x10,config1=0,name=TSC
```

If the name is not specified the first event will show up as package_msr:0x34:static, with the name it will show up as SMI_COUNT in csv.

PCI Configuration Registers - PCICFG (CSR):

```
pcicfg/config=<dev_id>,config1=<offset>,config2=<static_or_freerun>,width=<width>[,name=<name>]
```

* dev_id: Intel PCI device id where the register is located
* offset: offset of the register
* static_or_freerun: same syntax as for MSR registers
* width: register width in bits (16,32,64) 

Example:

```
pcicfg/config=0xe20,config1=0x180,config2=0x0,width=32,name=CHANERR_INT
```
From: https://www.intel.la/content/dam/www/public/us/en/documents/datasheets/xeon-e7-v2-datasheet-vol-2.pdf

MMIO Registers:

```
mmio/config=<device_id>,config1=<offset>,config2=<static_or_freerun>,config3=<membar_bits1>[,config4=<membar_bits2>],width=<width>[,name=<NAME>]
```

The MEMBAR is computed by logically ORing the result of membar_bits1 and membar_bits1 computation described below (PCICFG read + bit extraction and shift). The final MMIO register address = MEMBAR + offset.

* width: register width in bits (16,32,64) 
* dev_id: Intel PCI device id where the membar address registers are located
* membar_bits1: mmioBase register bits to compute membar (base address)
  - bits 0-15 : PCICFG register offset to read membar1 bits
  - bits 16-23: source position of membar bits in the PCICFG register 
  - bits 24-31: number of bits
  - bits 32-39: destination bit position in the membar
* membar_bits2: mmioBase register bits to compute membar (base address), can be zero if only membar_bits1 is sufficient for locating the register.
  - bits 0-15 : PCICFG register offset to read membar2 bits
  - bits 16-23: source position of membar bits in the PCICFG register 
  - bits 24-31: number of bits
  - bits 32-39: destination bit position in the membar
* offset: offset of the MMIO register relative to the membar
* static_or_freerun: same syntax as for MSR registers

Example (Icelake server iMC PMON MMIO register read):

```
mmio/config=0x3451,config1=0x22808,config2=1,config3=0x171D0000D0,config4=0x0c0b0000d8,width=64
```

--------------------------------------------------------------------------------
Collecting Events By Names From Event Lists (https://github.com/intel/perfmon/)
--------------------------------------------------------------------------------

pcm-raw can also automatically lookup the events from the json event lists (https://github.com/intel/perfmon/) and translate them to raw encodings itself. To make this work you need to checkout PCM with simdjson submodule:

* use git clone --recursive flag when cloning pcm repository, or
* update submodule with command `git submodule update --init --recursive`, or
* download simdjson library in the PCM source directory and recompile PCM:

1. change to PCM 'src/' directory
2. git clone https://github.com/simdjson/simdjson.git
3. re-compile pcm

Example of usage (on Intel Xeon Scalable processor):

```
pcm-raw -tr -e INST_RETIRED.ANY -e CPU_CLK_UNHALTED.THREAD -e CPU_CLK_UNHALTED.REF_TSC -e LD_BLOCKS.STORE_FORWARD -e UNC_CHA_CLOCKTICKS -e UNC_M_CAS_COUNT.RD
```

or with event groups specified in event_file.txt (with event multiplexing):

```
pcm-raw -tr -el event_file.txt
```

where event_file.txt contains event groups separated by a semicolon:

```
# group 1
INST_RETIRED.ANY
CPU_CLK_UNHALTED.REF_TSC
CPU_CLK_UNHALTED.THREAD
DTLB_LOAD_MISSES.STLB_HIT
L1D_PEND_MISS.PENDING_CYCLES_ANY
MEM_INST_RETIRED.LOCK_LOADS
UOPS_EXECUTED.X87
UNC_CHA_DIR_LOOKUP.SNP
UNC_CHA_DIR_LOOKUP.NO_SNP
UNC_M_CAS_COUNT.RD
UNC_M_CAS_COUNT.WR
UNC_UPI_CLOCKTICKS
UNC_UPI_TxL_FLITS.ALL_DATA
UNC_UPI_TxL_FLITS.NON_DATA
UNC_UPI_L1_POWER_CYCLES
;
# group 2
INST_RETIRED.ANY
CPU_CLK_UNHALTED.REF_TSC
CPU_CLK_UNHALTED.THREAD
OFFCORE_REQUESTS_BUFFER.SQ_FULL
MEM_LOAD_L3_HIT_RETIRED.XSNP_HIT
MEM_LOAD_L3_HIT_RETIRED.XSNP_HITM
MEM_LOAD_L3_HIT_RETIRED.XSNP_MISS
UNC_CHA_DIR_UPDATE.HA
UNC_CHA_DIR_UPDATE.TOR
UNC_M2M_DIRECTORY_UPDATE.ANY
UNC_M_CAS_COUNT.RD
UNC_M_CAS_COUNT.WR
UNC_M_PRE_COUNT.PAGE_MISS
UNC_UPI_TxL0P_POWER_CYCLES
UNC_UPI_RxL0P_POWER_CYCLES
UNC_UPI_RxL_FLITS.ALL_DATA
UNC_UPI_RxL_FLITS.NON_DATA
;
```

Sample csv output (date,time,event_name,milliseconds_between_samples,TSC_cycles_between_samples,unit0_event_count,unit1_event_count,unit2_event_count,...):

```
2021-09-27,00:07:40.507,UNC_CHA_DIR_LOOKUP.SNP,1000,2102078418,76,70,56,91,88,75,76,158,74,60,77,81,75,74,71,95,99,95,125,87,68,136,54,91,65,84,69,46,75,100,92,68,67,70,68,80,72,88,80,76,130,71,102,98,79,73,71,109
2021-09-27,00:07:40.507,UNC_CHA_DIR_LOOKUP.NO_SNP,1000,2102078418,1218,1280,1187,1310,1268,1287,1282,1331,1265,1267,1300,1270,1258,1307,1289,1300,1410,1378,1312,1316,1367,1337,1332,1317,1584,1519,1569,1557,1483,1537,1545,1520,1562,1527,1575,1540,1530,1581,1476,1525,1610,1680,1581,1657,1565,1613,1596,1600
2021-09-27,00:07:40.507,INST_RETIRED.ANY,1000,2102078418,705400,44587,45923,238392,53910,69547,46644,46172,44740,44732,45692,44864,46105,45352,45057,217052,46511,46671,46893,46459,53739,47021,114133,46339,61649,59027,142096,48048,98178,48288,162122,474329,48046,49795,78239,425635,105512,69933,49827,48913,71549,48451,294858,312316,149586,540477,49115,55144,46788,61681,82964,81127,116227,85776,453369,145979,81007,82269,83580,73595,73355,73751,72599,47169,47767,48191,48131,48359,48621,67664,48227,532184,49686,48704,324264,48539,48795,48609,60275,518368,116077,163734,526815,50650,140337,666605,47935,1368049,47243,337542,47153,46882,46925,62373,70186,466927
2021-09-27,00:07:40.507,CPU_CLK_UNHALTED.REF_TSC,1000,2102078418,3618636,384720,589092,2143512,766752,724164,803124,627312,541548,538188,534324,509964,535164,527436,529284,1366176,488124,491820,533148,543900,608580,577920,1145172,602196,919632,824544,1429344,692916,1092756,700644,1298640,2487156,736344,841344,1324008,1855476,1260084,1104768,658308,5805324,851424,766080,1909740,2170392,1313592,3986892,683844,986832,659064,642432,682668,772128,1076628,710220,2514876,1085112,715344,700812,676452,594468,577668,590856,574056,597996,525336,551460,548520,561624,569352,741468,623196,3124212,592032,596400,2265312,556584,593124,546756,766752,2547216,1047396,1280160,2704884,525336,1200444,3255000,497700,13643700,481572,1601040,515592,523740,503664,854280,603120,2305128
2021-09-27,00:07:40.507,CPU_CLK_UNHALTED.THREAD,1000,2102078418,1723000,183219,280560,1020631,365140,344897,382467,298699,257868,256243,254471,242757,254794,251172,252091,650377,232442,234209,253807,259024,289817,275179,545244,286717,437888,392646,680513,329759,520244,333662,618356,1184347,350594,400648,630580,1517122,599939,525847,313441,2765951,405441,364827,909395,1033366,625655,1898427,325614,881026,312798,305884,325245,367890,512845,338440,1197524,516836,341497,334581,322975,283138,275031,281300,273347,284616,250171,262581,261182,267455,271097,353013,296757,1487751,282516,283651,1076725,265489,282845,260889,365411,1212743,498705,611118,1287439,360493,571158,1549944,236616,6499483,229820,762766,245338,248648,239640,406676,287582,1714659
2021-09-27,00:07:40.507,DTLB_LOAD_MISSES.STLB_HIT,1000,2102078418,10093,1178,1186,2593,1184,1356,1182,1201,1187,1200,1191,1179,1189,1179,1177,1444,1218,1205,1158,1183,1216,1190,1789,1184,1388,1347,2207,1384,1566,1352,1541,3221,1374,1398,1580,11223,1690,1427,1398,1356,1531,1388,3429,3567,2136,2639,1354,1393,1181,1188,1457,1456,1801,1437,4698,1697,1426,1434,1418,1452,1396,1394,1434,1164,1349,1349,1356,1318,1354,1528,1349,18546,1168,1160,8935,1166,1172,1167,1194,4432,1801,2341,3152,1190,1777,4328,1178,4396,1170,1939,1199,1150,1158,1197,1187,12441
2021-09-27,00:07:40.507,L1D_PEND_MISS.PENDING_CYCLES_ANY,1000,2102078418,682630,81530,114229,363299,169260,134931,441644,183870,89947,95379,98135,81156,75366,77990,78734,178321,52738,53883,57241,56306,65514,94824,152070,227164,87723,80980,300491,70675,148506,70130,173723,628031,142178,161405,503099,383743,255465,317627,67134,1509172,105102,242908,300344,336683,157280,555052,84017,615357,526290,88531,117674,387708,192129,157226,451213,201430,103646,106302,112452,86251,83203,82880,80239,189044,72389,73820,75135,70746,84963,106517,168907,249006,117006,109389,320326,98291,168531,100734,206075,647276,167155,154684,495947,359092,257614,322235,78189,1473756,148139,278653,308380,343576,166510,556816,90475,306546
2021-09-27,00:07:40.507,MEM_INST_RETIRED.LOCK_LOADS,1000,2102078418,3462,231,235,1159,259,277,239,237,236,238,236,239,238,237,237,1114,237,237,238,237,265,237,555,237,277,278,542,237,431,240,389,906,239,238,385,3973,435,280,238,238,401,238,847,1238,604,1948,238,238,235,275,266,267,428,277,1287,399,271,277,272,239,240,239,239,237,237,237,237,237,238,347,237,4266,238,238,1174,238,238,238,270,1361,526,697,1101,238,615,2172,238,4276,236,642,236,237,236,275,299,2842
2021-09-27,00:07:40.507,UOPS_EXECUTED.X87,1000,2102078418,1152,12,13,496,46,17,27,12,11,13,10,14,11,27,12,1591,11,10,11,13,23,11,257,12,64,52,216,31,231,31,1668,5944,31,30,85,710,101,54,34,41,100,33,1852,1561,423,2348,28,46,14,23,155,57,82,172,2776,281,19,52,107,18,36,18,19,14,11,10,10,10,26,57,10,108,31,33,151,31,32,30,63,3700,361,509,4610,31,396,1814,31,5607,33,4175,31,30,32,47,78,471
2021-09-27,00:07:40.507,UNC_M_CAS_COUNT.RD,1000,2102078418,37565,33584,0,0,0,0,40306,0,37373,0,0,0
2021-09-27,00:07:40.507,UNC_M_CAS_COUNT.WR,1000,2102078418,58994,53007,0,0,0,0,25088,0,21901,0,0,0
2021-09-27,00:07:40.507,UNC_UPI_CLOCKTICKS,1000,2102078418,1300347171,1300351441,1200328715,1300297715,1300303139,1200283803
2021-09-27,00:07:40.507,UNC_UPI_TxL_FLITS.ALL_DATA,1000,2102078418,132768,150840,0,285147,269190,0
2021-09-27,00:07:40.507,UNC_UPI_TxL_FLITS.NON_DATA,1000,2102078418,298203,319302,0,293389,264875,0
2021-09-27,00:07:40.507,UNC_UPI_L1_POWER_CYCLES,1000,2102078418,0,0,1200328715,0,0,1200283803
```
The unit can be logical core, memory channel, CHA, etc, depending on the event type.

--------------------------------------------------------------------------------
Low-level access to Intel PMT telemetry data
--------------------------------------------------------------------------------

pcm-raw can read raw telemetry data from Intel PMT (https://github.com/intel/Intel-PMT/).

Syntax for a PMT raw telemetry counter:

```
pmt/config=<uniqueid>,config1=<sampleID>,config2=<sampleType>,config3=<lsb,config4=<msb>[,name=<description>]

```

The fields are the values for the counter from the Intel PMT aggregator XML:

* uniqueid : Intel PMT Telemetry unique identifier
* sampleID : sample ID of the counter
* sampleType counter encoding:
  -  0        : Snapshot (last value reported in csv)
  - non-zero  : Counter (delta to last value reported in csv)
* lsb : lsb field
* msb : msb field

Example for https://github.com/intel/Intel-PMT/blob/868049006ad2770a75e5fc7526fd0c4b22438e27/xml/SPR/OOBMSM/CORE/spr_aggregator.xml#L15428:
```
pmt/config=0x87b6fef1,config1=770,config2=0,config3=32,config4=63,name="Temperature_histogram_range_5_(50.5-57.5C)_counter_for_core_0"
```

Current limitations: this feature (PMT access) is currently only available on Linux (with Intel PMT Linux driver).
