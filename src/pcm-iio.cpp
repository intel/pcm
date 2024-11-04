// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2017-2022, Intel Corporation

// written by Patrick Lu,
//            Aaron Cruz
//            and others
#include "cpucounters.h"

#ifdef _MSC_VER
    #include <windows.h>
    #include "windows/windriver.h"
#else
    #include <unistd.h>
#endif

#include <memory>
#include <fstream>
#include <stdlib.h>
#include <limits>
#include <stdexcept>      // std::length_error
#include <cstdint>
#include <numeric>
#include <algorithm>
#include <set>

#ifdef _MSC_VER
    #include "freegetopt/getopt.h"
#endif

#include "lspci.h"
#include "utils.h"
using namespace std;
using namespace pcm;

#define PCM_DELAY_DEFAULT 3.0 // in seconds

#define QAT_DID 0x18DA
#define NIS_DID 0x18D1
#define HQM_DID 0x270B

#define GRR_QAT_VRP_DID 0x5789 // Virtual Root Port to integrated QuickAssist (GRR QAT)
#define GRR_NIS_VRP_DID 0x5788 // VRP to Network Interface and Scheduler (GRR NIS)

#define ROOT_BUSES_OFFSET   0xCC
#define ROOT_BUSES_OFFSET_2 0xD0

#define SKX_SOCKETID_UBOX_DID 0x2014
#define SKX_UBOX_DEVICE_NUM   0x08
#define SKX_UBOX_FUNCTION_NUM 0x02
#define SKX_BUS_NUM_STRIDE    8
//the below LNID and GID applies to Skylake Server
#define SKX_UNC_SOCKETID_UBOX_LNID_OFFSET 0xC0
#define SKX_UNC_SOCKETID_UBOX_GID_OFFSET  0xD4

static const std::string iio_stack_names[6] = {
    "IIO Stack 0 - CBDMA/DMI      ",
    "IIO Stack 1 - PCIe0          ",
    "IIO Stack 2 - PCIe1          ",
    "IIO Stack 3 - PCIe2          ",
    "IIO Stack 4 - MCP0           ",
    "IIO Stack 5 - MCP1           "
};

static const std::string skx_iio_stack_names[6] = {
    "IIO Stack 0 - CBDMA/DMI      ",
    "IIO Stack 1 - PCIe0          ",
    "IIO Stack 2 - PCIe1          ",
    "IIO Stack 3 - PCIe2          ",
    "IIO Stack 4 - MCP0           ",
    "IIO Stack 5 - MCP1           "
};

static const std::string icx_iio_stack_names[6] = {
    "IIO Stack 0 - PCIe0          ",
    "IIO Stack 1 - PCIe1          ",
    "IIO Stack 2 - MCP            ",
    "IIO Stack 3 - PCIe2          ",
    "IIO Stack 4 - PCIe3          ",
    "IIO Stack 5 - CBDMA/DMI      "
};

static const std::string icx_d_iio_stack_names[6] = {
    "IIO Stack 0 - MCP            ",
    "IIO Stack 1 - PCIe0          ",
    "IIO Stack 2 - CBDMA/DMI      ",
    "IIO Stack 3 - PCIe2          ",
    "IIO Stack 4 - PCIe3          ",
    "IIO Stack 5 - PCIe1          "
};

static const std::string snr_iio_stack_names[5] = {
    "IIO Stack 0 - QAT            ",
    "IIO Stack 1 - CBDMA/DMI      ",
    "IIO Stack 2 - NIS            ",
    "IIO Stack 3 - HQM            ",
    "IIO Stack 4 - PCIe           "
};

#define ICX_CBDMA_DMI_SAD_ID 0
#define ICX_MCP_SAD_ID       3

#define ICX_PCH_PART_ID   0
#define ICX_CBDMA_PART_ID 3

#define SNR_ICX_SAD_CONTROL_CFG_OFFSET 0x3F4
#define SNR_ICX_MESH2IIO_MMAP_DID      0x09A2

#define ICX_VMD_PCI_DEVNO   0x00
#define ICX_VMD_PCI_FUNCNO  0x05

static const std::map<int, int> icx_sad_to_pmu_id_mapping = {
    { ICX_CBDMA_DMI_SAD_ID, 5 },
    { 1,                    0 },
    { 2,                    1 },
    { ICX_MCP_SAD_ID,       2 },
    { 4,                    3 },
    { 5,                    4 }
};

static const std::map<int, int> icx_d_sad_to_pmu_id_mapping = {
    { ICX_CBDMA_DMI_SAD_ID, 2 },
    { 1,                    5 },
    { 2,                    1 },
    { ICX_MCP_SAD_ID,       0 },
    { 4,                    3 },
    { 5,                    4 }
};

#define SNR_ACCELERATOR_PART_ID 4

#define SNR_ROOT_PORT_A_DID 0x334A

#define SNR_CBDMA_DMI_SAD_ID 0
#define SNR_PCIE_GEN3_SAD_ID 1
#define SNR_HQM_SAD_ID       2
#define SNR_NIS_SAD_ID       3
#define SNR_QAT_SAD_ID       4

static const std::map<int, int> snr_sad_to_pmu_id_mapping = {
    { SNR_CBDMA_DMI_SAD_ID, 1 },
    { SNR_PCIE_GEN3_SAD_ID, 4 },
    { SNR_HQM_SAD_ID      , 3 },
    { SNR_NIS_SAD_ID      , 2 },
    { SNR_QAT_SAD_ID      , 0 }
};

#define HQMV2_DID   0x2710 // Hardware Queue Manager v2
#define HQMV25_DID  0x2714 // Hardware Queue Manager v2.5
#define DSA_DID     0x0b25 // Data Streaming Accelerator (DSA)
#define IAX_DID     0x0cfe // In-Memory Database Analytics Accelerator (IAX)
#define QATV2_DID   0x4940 // QuickAssist (CPM) v2

#define SPR_DMI_PART_ID                7
#define SPR_XCC_HQM_PART_ID            5
#define SPR_MCC_HQM_PART_ID            4
#define SPR_XCC_QAT_PART_ID            4
#define SPR_MCC_QAT_PART_ID            5
#define SPR_SAD_CONTROL_CFG_OFFSET     SNR_ICX_SAD_CONTROL_CFG_OFFSET

#define SPR_PCU_CR3_DID 0x325b
#define SPR_PCU_CR3_REG_DEVICE 0x1e
#define SPR_PCU_CR3_REG_FUNCTION 0x03
#define SPR_CAPID4_OFFSET 0x94
#define SPR_CAPID4_GET_PHYSICAL_CHOP(capid4) ((capid4 >> 6) & 3)
#define SPR_PHYSICAL_CHOP_XCC 0b11
#define SPR_PHYSICAL_CHOP_MCC 0b01

#define SPR_XCC_DMI_PMON_ID         1
#define SPR_XCC_PCIE_GEN5_0_PMON_ID 2
#define SPR_XCC_PCIE_GEN5_1_PMON_ID 4
#define SPR_XCC_PCIE_GEN5_2_PMON_ID 6
#define SPR_XCC_PCIE_GEN5_3_PMON_ID 7
#define SPR_XCC_PCIE_GEN5_4_PMON_ID 9
#define SPR_XCC_IDX0_PMON_ID        0
#define SPR_XCC_IDX1_PMON_ID        3
#define SPR_XCC_IDX2_PMON_ID        5
#define SPR_XCC_IDX3_PMON_ID        8

const std::map<int, int> spr_xcc_sad_to_pmu_id_mapping = {
    { 0,  SPR_XCC_DMI_PMON_ID         },
    { 1,  SPR_XCC_PCIE_GEN5_0_PMON_ID },
    { 2,  SPR_XCC_PCIE_GEN5_1_PMON_ID },
    { 3,  SPR_XCC_PCIE_GEN5_2_PMON_ID },
    { 4,  SPR_XCC_PCIE_GEN5_3_PMON_ID },
    { 5,  SPR_XCC_PCIE_GEN5_4_PMON_ID },
    { 8,  SPR_XCC_IDX0_PMON_ID        },
    { 9,  SPR_XCC_IDX1_PMON_ID        },
    { 10, SPR_XCC_IDX2_PMON_ID        },
    { 11, SPR_XCC_IDX3_PMON_ID        }
};

#define SPR_MCC_DMI_PMON_ID         10
#define SPR_MCC_PCIE_GEN5_0_PMON_ID 0 // assumption
#define SPR_MCC_PCIE_GEN5_1_PMON_ID 1
#define SPR_MCC_PCIE_GEN5_2_PMON_ID 2
#define SPR_MCC_PCIE_GEN5_3_PMON_ID 4 // assumption
#define SPR_MCC_PCIE_GEN5_4_PMON_ID 5
#define SPR_MCC_IDX0_PMON_ID        3

const std::map<int, int> spr_mcc_sad_to_pmu_id_mapping = {
    { 0, SPR_MCC_PCIE_GEN5_0_PMON_ID },
    { 1, SPR_MCC_PCIE_GEN5_1_PMON_ID },
    { 2, SPR_MCC_PCIE_GEN5_2_PMON_ID },
    { 3, SPR_MCC_DMI_PMON_ID         },
    { 4, SPR_MCC_PCIE_GEN5_3_PMON_ID },
    { 5, SPR_MCC_PCIE_GEN5_4_PMON_ID },
    { 8, SPR_MCC_IDX0_PMON_ID        },
};

static const std::string spr_xcc_iio_stack_names[] = {
    "IIO Stack 0 - IDX0  ",
    "IIO Stack 1 - DMI   ",
    "IIO Stack 2 - PCIe0 ",
    "IIO Stack 3 - IDX1  ",
    "IIO Stack 4 - PCIe1 ",
    "IIO Stack 5 - IDX2  ",
    "IIO Stack 6 - PCIe2 ",
    "IIO Stack 7  - PCIe3",
    "IIO Stack 8  - IDX3 ",
    "IIO Stack 9  - PCIe4",
    "IIO Stack 10 - NONE ",
    "IIO Stack 11 - NONE ",
};

/*
 * SPR MCC has 7 I/O stacks but PMON block for DMI has ID number 10.
 * And just to follow such enumeration keep Stack 10 for DMI.
 */
static const std::string spr_mcc_iio_stack_names[] = {
    "IIO Stack 0 - PCIe0 ",
    "IIO Stack 1 - PCIe1 ",
    "IIO Stack 2 - PCIe2 ",
    "IIO Stack 3 - IDX0  ",
    "IIO Stack 4 - PCIe3 ",
    "IIO Stack 5 - PCIe4 ",
    "IIO Stack 6 - NONE  ",
    "IIO Stack 7 - NONE  ",
    "IIO Stack 8 - NONE  ",
    "IIO Stack 9 - NONE  ",
    "IIO Stack 10 - DMI  ",
};

// MS2IOSF stack IDs in CHA notation
#define GRR_PCH_DSA_GEN4_SAD_ID 0
#define GRR_DLB_SAD_ID          1
#define GRR_NIS_QAT_SAD_ID      2

#define GRR_PCH_DSA_GEN4_PMON_ID 2
#define GRR_DLB_PMON_ID          1
#define GRR_NIS_QAT_PMON_ID      0

// Stack 0 contains PCH, DSA and CPU PCIe Gen4 Complex
const std::map<int, int> grr_sad_to_pmu_id_mapping = {
    { GRR_PCH_DSA_GEN4_SAD_ID, GRR_PCH_DSA_GEN4_PMON_ID },
    { GRR_DLB_SAD_ID,          GRR_DLB_PMON_ID          },
    { GRR_NIS_QAT_SAD_ID,      GRR_NIS_QAT_PMON_ID      },
};

#define GRR_DLB_PART_ID 0
#define GRR_NIS_PART_ID 0
#define GRR_QAT_PART_ID 1

static const std::string grr_iio_stack_names[3] = {
    "IIO Stack 0 - NIS/QAT        ",
    "IIO Stack 1 - HQM            ",
    "IIO Stack 2 - PCH/DSA/PCIe   "
};

#define EMR_DMI_PMON_ID         7
#define EMR_PCIE_GEN5_0_PMON_ID 1
#define EMR_PCIE_GEN5_1_PMON_ID 2
#define EMR_PCIE_GEN5_2_PMON_ID 3
#define EMR_PCIE_GEN5_3_PMON_ID 8
#define EMR_PCIE_GEN5_4_PMON_ID 6
#define EMR_IDX0_PMON_ID        0
#define EMR_IDX1_PMON_ID        4
#define EMR_IDX2_PMON_ID        5
#define EMR_IDX3_PMON_ID        9

const std::map<int, int> emr_sad_to_pmu_id_mapping = {
    { 0,  EMR_DMI_PMON_ID         },
    { 1,  EMR_PCIE_GEN5_0_PMON_ID },
    { 2,  EMR_PCIE_GEN5_1_PMON_ID },
    { 3,  EMR_PCIE_GEN5_2_PMON_ID },
    { 4,  EMR_PCIE_GEN5_3_PMON_ID },
    { 5,  EMR_PCIE_GEN5_4_PMON_ID },
    { 8,  EMR_IDX0_PMON_ID        },
    { 9,  EMR_IDX1_PMON_ID        },
    { 10, EMR_IDX2_PMON_ID        },
    { 11, EMR_IDX3_PMON_ID        }
};

static const std::string emr_iio_stack_names[] = {
    "IIO Stack 0 - IDX0  ",
    "IIO Stack 1 - PCIe3 ",
    "IIO Stack 2 - PCIe0 ",
    "IIO Stack 3 - IDX1  ",
    "IIO Stack 4 - PCIe1 ",
    "IIO Stack 5 - IDX2  ",
    "IIO Stack 6 - PCIe2 ",
    "IIO Stack 7  - DMI",
    "IIO Stack 8  - IDX3 ",
    "IIO Stack 9  - PCIe4",
    "IIO Stack 10 - NONE ",
    "IIO Stack 11 - NONE ",
};

enum EagleStreamPlatformStacks
{
    esDMI = 0,
    esPCIe0,
    esPCIe1,
    esPCIe2,
    esPCIe3,
    esPCIe4,
    esDINO0,
    esDINO1,
    esDINO2,
    esDINO3,
    esEndOfList
};

const std::vector<int> spr_xcc_stacks_enumeration = {
    /* esDMI   */ SPR_XCC_DMI_PMON_ID,
    /* esPCIe0 */ SPR_XCC_PCIE_GEN5_0_PMON_ID,
    /* esPCIe1 */ SPR_XCC_PCIE_GEN5_1_PMON_ID,
    /* esPCIe2 */ SPR_XCC_PCIE_GEN5_2_PMON_ID,
    /* esPCIe3 */ SPR_XCC_PCIE_GEN5_3_PMON_ID,
    /* esPCIe4 */ SPR_XCC_PCIE_GEN5_4_PMON_ID,
    /* esDINO0 */ SPR_XCC_IDX0_PMON_ID,
    /* esDINO1 */ SPR_XCC_IDX1_PMON_ID,
    /* esDINO2 */ SPR_XCC_IDX2_PMON_ID,
    /* esDINO3 */ SPR_XCC_IDX3_PMON_ID,
};

const std::vector<int> spr_mcc_stacks_enumeration = {
    /* esDMI   */ SPR_MCC_DMI_PMON_ID,
    /* esPCIe0 */ SPR_MCC_PCIE_GEN5_0_PMON_ID,
    /* esPCIe1 */ SPR_MCC_PCIE_GEN5_1_PMON_ID,
    /* esPCIe2 */ SPR_MCC_PCIE_GEN5_2_PMON_ID,
    /* esPCIe3 */ SPR_MCC_PCIE_GEN5_3_PMON_ID,
    /* esPCIe4 */ SPR_MCC_PCIE_GEN5_4_PMON_ID,
    /* esDINO0 */ SPR_MCC_IDX0_PMON_ID,
};

const std::vector<int> emr_stacks_enumeration = {
    /* esDMI   */ EMR_DMI_PMON_ID,
    /* esPCIe0 */ EMR_PCIE_GEN5_0_PMON_ID,
    /* esPCIe1 */ EMR_PCIE_GEN5_1_PMON_ID,
    /* esPCIe2 */ EMR_PCIE_GEN5_2_PMON_ID,
    /* esPCIe3 */ EMR_PCIE_GEN5_3_PMON_ID,
    /* esPCIe4 */ EMR_PCIE_GEN5_4_PMON_ID,
    /* esDINO0 */ EMR_IDX0_PMON_ID,
    /* esDINO1 */ EMR_IDX1_PMON_ID,
    /* esDINO2 */ EMR_IDX2_PMON_ID,
    /* esDINO3 */ EMR_IDX3_PMON_ID,
};

enum class EagleStreamSupportedTypes
{
    esInvalid = -1,
    esSprXcc,
    esSprMcc,
    esEmrXcc
};

typedef EagleStreamSupportedTypes estype;

const std::map<estype, std::vector<int>> es_stacks_enumeration = {
    {estype::esSprXcc, spr_xcc_stacks_enumeration},
    {estype::esSprMcc, spr_mcc_stacks_enumeration},
    {estype::esEmrXcc, emr_stacks_enumeration    },
};

const std::map<estype, const std::string *> es_stack_names = {
    {estype::esSprXcc, spr_xcc_iio_stack_names},
    {estype::esSprMcc, spr_mcc_iio_stack_names},
    {estype::esEmrXcc, emr_iio_stack_names    },
};

const std::map<estype, std::map<int, int>> es_sad_to_pmu_id_mapping = {
    {estype::esSprXcc, spr_xcc_sad_to_pmu_id_mapping},
    {estype::esSprMcc, spr_mcc_sad_to_pmu_id_mapping},
    {estype::esEmrXcc, emr_sad_to_pmu_id_mapping    },
};

#define SRF_PE0_PMON_ID         3
#define SRF_PE1_PMON_ID         4
#define SRF_PE2_PMON_ID         2
#define SRF_PE3_PMON_ID         5
/*
 * There are platform configuration when FlexUPI stacks (stacks 5 and 6) are enabled as
 * PCIe stack and PCIe ports are disabled (ports 2 and 3) and vice sersa. See details here:
 * In these cases the PMON IDs are different.
 * So, defines with _FLEX_ are applicable for cases when FlexUPI stacks
 * are working as PCIe ports.
 */
#define SRF_PE4_PMON_ID             11
#define SRF_FLEX_PE4_PMON_ID        13
#define SRF_PE5_PMON_ID             12
#define SRF_FLEX_PE5_PMON_ID        10

#define SRF_PE6_PMON_ID             0
#define SRF_PE7_PMON_ID             7
#define SRF_PE8_PMON_ID             8
#define SRF_HC0_PMON_ID             1
#define SRF_HC1_PMON_ID             6
#define SRF_HC2_PMON_ID             9
#define SRF_HC3_PMON_ID             14

#define SRF_PE0_SAD_BUS_ID         2
#define SRF_PE1_SAD_BUS_ID         3
#define SRF_PE2_SAD_BUS_ID         1
#define SRF_PE3_SAD_BUS_ID         4
#define SRF_PE4_SAD_BUS_ID         29
#define SRF_FLEX_PE4_SAD_BUS_ID    SRF_PE4_SAD_BUS_ID
#define SRF_PE5_SAD_BUS_ID         26
#define SRF_FLEX_PE5_SAD_BUS_ID    SRF_PE5_SAD_BUS_ID
#define SRF_PE6_SAD_BUS_ID         0  // UPI0
#define SRF_PE7_SAD_BUS_ID         5  // UPI1
#define SRF_PE8_SAD_BUS_ID         28 // UPI2
#define SRF_UBOXA_SAD_BUS_ID       30
#define SRF_UBOXB_SAD_BUS_ID       31

const std::set<int> srf_pcie_stacks({
    SRF_PE0_SAD_BUS_ID,
    SRF_PE1_SAD_BUS_ID,
    SRF_PE2_SAD_BUS_ID,
    SRF_PE3_SAD_BUS_ID,
    SRF_PE4_SAD_BUS_ID,
    SRF_FLEX_PE4_SAD_BUS_ID,
    SRF_PE5_SAD_BUS_ID,
    SRF_FLEX_PE5_SAD_BUS_ID,
    SRF_PE6_SAD_BUS_ID,
    SRF_PE7_SAD_BUS_ID,
    SRF_PE8_SAD_BUS_ID,
});

#define SRF_HC0_SAD_BUS_ID         8
#define SRF_HC1_SAD_BUS_ID         12
#define SRF_HC2_SAD_BUS_ID         20
#define SRF_HC3_SAD_BUS_ID         16

const std::map<int, int> srf_sad_to_pmu_id_mapping = {
    { SRF_PE0_SAD_BUS_ID,      SRF_PE0_PMON_ID      },
    { SRF_PE1_SAD_BUS_ID,      SRF_PE1_PMON_ID      },
    { SRF_PE2_SAD_BUS_ID,      SRF_PE2_PMON_ID      },
    { SRF_PE3_SAD_BUS_ID,      SRF_PE3_PMON_ID      },
    { SRF_PE4_SAD_BUS_ID,      SRF_PE4_PMON_ID      },
    { SRF_FLEX_PE4_SAD_BUS_ID, SRF_FLEX_PE4_PMON_ID },
    { SRF_PE5_SAD_BUS_ID,      SRF_PE5_PMON_ID      },
    { SRF_FLEX_PE5_SAD_BUS_ID, SRF_FLEX_PE5_PMON_ID },
    { SRF_PE6_SAD_BUS_ID,      SRF_PE6_PMON_ID      },
    { SRF_PE7_SAD_BUS_ID,      SRF_PE7_PMON_ID      },
    { SRF_PE8_SAD_BUS_ID,      SRF_PE8_PMON_ID      },
    { SRF_HC0_SAD_BUS_ID,      SRF_HC0_PMON_ID      },
    { SRF_HC1_SAD_BUS_ID,      SRF_HC1_PMON_ID      },
    { SRF_HC2_SAD_BUS_ID,      SRF_HC2_PMON_ID      },
    { SRF_HC3_SAD_BUS_ID,      SRF_HC3_PMON_ID      },
};

#define SRF_DSA_IAX_PART_NUMBER 0
#define SRF_HQM_PART_NUMBER     5
#define SRF_QAT_PART_NUMBER     4

static const std::string srf_iio_stack_names[] = {
    "IIO Stack 0  - PCIe6     ", // SRF_PE6_PMON_ID      0
    "IIO Stack 1  - HCx0      ", // SRF_HC0_PMON_ID      1
    "IIO Stack 2  - PCIe2     ", // SRF_PE2_PMON_ID      2
    "IIO Stack 3  - PCIe0     ", // SRF_PE0_PMON_ID      3
    "IIO Stack 4  - PCIe1     ", // SRF_PE1_PMON_ID      4
    "IIO Stack 5  - PCIe3     ", // SRF_PE3_PMON_ID      5
    "IIO Stack 6  - HCx1      ", // SRF_HC1_PMON_ID      6
    "IIO Stack 7  - PCIe7     ", // SRF_PE7_PMON_ID      7
    "IIO Stack 8  - PCIe8     ", // SRF_PE8_PMON_ID      8
    "IIO Stack 9  - HCx3      ", // SRF_HC3_PMON_ID      9
    "IIO Stack 10 - Flex PCIe5", // SRF_FLEX_PE5_PMON_ID 10
    "IIO Stack 11 - PCIe4     ", // SRF_PE4_PMON_ID      11
    "IIO Stack 12 - PCIe5     ", // SRF_PE5_PMON_ID      12
    "IIO Stack 13 - Flex PCIe4", // SRF_FLEX_PE4_PMON_ID 13
    "IIO Stack 14 - HCx2      ", // SRF_HC2_PMON_ID      14
};

struct iio_counter : public counter {
  std::vector<result_content> data;
};

result_content results;

typedef struct
{
    PCM *m;
    iio_counter ctr;
    vector<struct iio_counter> ctrs;
} iio_evt_parse_context;

vector<string> combine_stack_name_and_counter_names(string stack_name, const map<string,std::pair<h_id,std::map<string,v_id>>> &nameMap)
{
    vector<string> v;
    vector<string> tmp(nameMap.size());
    v.push_back(stack_name);
    for (std::map<string,std::pair<h_id,std::map<string,v_id>>>::const_iterator iunit = nameMap.begin(); iunit != nameMap.end(); ++iunit) {
        string h_name = iunit->first;
        int h_id = (iunit->second).first;
        tmp[h_id] = h_name;
        //cout << "h_id:" << h_id << " name:" << h_name << "\n";
    }
    //XXX: How to simplify and just combine tmp & v?
    for (uint32_t i = 0; i < nameMap.size(); i++) {
        v.push_back(tmp[i]);
    }
    return v;
}

string build_pci_header(const PCIDB & pciDB, uint32_t column_width, const struct pci &p, int part = -1, uint32_t level = 0)
{
    string s = "|";
    char bdf_buf[32];
    char speed_buf[10];
    char vid_did_buf[10];
    char device_name_buf[128];

    snprintf(bdf_buf, sizeof(bdf_buf), "%04X:%02X:%02X.%1d", p.bdf.domainno, p.bdf.busno, p.bdf.devno, p.bdf.funcno);
    snprintf(speed_buf, sizeof(speed_buf), "Gen%1d x%-2d", p.link_speed, p.link_width);
    snprintf(vid_did_buf, sizeof(vid_did_buf), "%04X:%04X", p.vendor_id, p.device_id);
    snprintf(device_name_buf, sizeof(device_name_buf), "%s %s",
            (pciDB.first.count(p.vendor_id) > 0)?pciDB.first.at(p.vendor_id).c_str():"unknown vendor",
            (pciDB.second.count(p.vendor_id) > 0 && pciDB.second.at(p.vendor_id).count(p.device_id) > 0)?pciDB.second.at(p.vendor_id).at(p.device_id).c_str():"unknown device"
        );
    s += bdf_buf;
    s += '|';
    s += speed_buf;
    s += '|';
    s += vid_did_buf;
    s += " ";
    s += device_name_buf;

    if (!p.parts_no.empty()) {
        s += "; Part: ";
        for (auto& part : p.parts_no) {
            s += std::to_string(part) + ", ";
        }
        s += "\b\b ";
    }

    /* row with data */
    if (part >= 0) {
        s.insert(1,"P" + std::to_string(part) + " ");
        s += std::string(column_width - (s.size()-1), ' ');
    } else { /* row without data, just child pci device */
        s.insert(0, std::string(4*level, ' '));
    }

    return s;
}

void build_pci_tree(vector<string> &buffer, const PCIDB & pciDB, uint32_t column_width, const struct pci &p, int part, uint32_t level = 0)
{
    string row;
    for (const auto& child : p.child_pci_devs) {
        row = build_pci_header(pciDB, column_width, child, part, level);
        buffer.push_back(row);
        if (child.hasChildDevices())
            build_pci_tree(buffer, pciDB, column_width, child, part, level + 1);
    }
}

vector<string> build_display(vector<struct iio_stacks_on_socket>& iios, vector<struct iio_counter>& ctrs, const PCIDB& pciDB,
                             const map<string,std::pair<h_id,std::map<string,v_id>>> &nameMap)
{
    vector<string> buffer;
    vector<string> headers;
    vector<struct data> data;
    uint64_t header_width;
    string row;
    for (auto socket = iios.cbegin(); socket != iios.cend(); ++socket) {
        buffer.push_back("Socket" + std::to_string(socket->socket_id));
        for (auto stack = socket->stacks.cbegin(); stack != socket->stacks.cend(); ++stack) {
            auto stack_id = stack->iio_unit_id;
            headers = combine_stack_name_and_counter_names(stack->stack_name, nameMap);
            //Print first row
            row = std::accumulate(headers.begin(), headers.end(), string(" "), a_header_footer);
            header_width = row.size();
            buffer.push_back(row);
            //Print a_title
            row = std::accumulate(headers.begin(), headers.end(), string("|"), a_title);
            buffer.push_back(row);
            //Print deliminator
            row = std::accumulate(headers.begin(), headers.end(), string("|"), a_header_footer);
            buffer.push_back(row);
            //Print data
            std::map<uint32_t,map<uint32_t,struct iio_counter*>> v_sort;
            //re-organize data collection to be row wise
            for (std::vector<struct iio_counter>::iterator counter = ctrs.begin(); counter != ctrs.end(); ++counter) {
                v_sort[counter->v_id][counter->h_id] = &(*counter);
            }
            for (std::map<uint32_t,map<uint32_t,struct iio_counter*>>::const_iterator vunit = v_sort.cbegin(); vunit != v_sort.cend(); ++vunit) {
                map<uint32_t, struct iio_counter*> h_array = vunit->second;
                uint32_t vv_id = vunit->first;
                vector<uint64_t> h_data;
                string v_name = h_array[0]->v_event_name;
                for (map<uint32_t,struct iio_counter*>::const_iterator hunit = h_array.cbegin(); hunit != h_array.cend(); ++hunit) {
                    uint32_t hh_id = hunit->first;
                    uint64_t raw_data = hunit->second->data[0][socket->socket_id][stack_id][std::pair<h_id,v_id>(hh_id,vv_id)];
                    h_data.push_back(raw_data);
                }
                data = prepare_data(h_data, headers);
                row = "| " + v_name;
                row += string(abs(int(headers[0].size() - (row.size() - 1))), ' ');
                row += std::accumulate(data.begin(), data.end(), string("|"), a_data);
                buffer.push_back(row);
            }
            //Print deliminator
            row = std::accumulate(headers.begin(), headers.end(), string("|"), a_header_footer);
            buffer.push_back(row);
            //Print pcie devices
            for (const auto& part : stack->parts) {
                uint8_t level = 1;
                for (const auto& pci_device : part.child_pci_devs) {
                    row = build_pci_header(pciDB, (uint32_t)header_width, pci_device, -1, level);
                    buffer.push_back(row);
                    if (pci_device.hasChildDevices()) {
                        build_pci_tree(buffer, pciDB, (uint32_t)header_width, pci_device, -1, level + 1);
                    } else if (pci_device.header_type == 1) {
                            level++;
                    }
                }
            }
            //Print footer
            row = std::accumulate(headers.begin(), headers.end(), string(" "), a_header_footer);
            buffer.push_back(row);
        }
    }
    return buffer;
}

std::string get_root_port_dev(const bool show_root_port, int part_id,  const pcm::iio_stack *stack)
{
    char tmp[9] = "        ";
    std::string rp_pci;

    if (!show_root_port)
        return rp_pci;

    for (auto part = stack->parts.begin(); part != stack->parts.end(); part = std::next(part))
    {
        if (part->part_id == part_id)
        {
            std::snprintf(tmp, sizeof(tmp), "%02x:%02x.%x", part->root_pci_dev.bdf.busno,
                        part->root_pci_dev.bdf.devno, part->root_pci_dev.bdf.funcno);
            break;
        }
    }

    rp_pci.append(tmp);
    return rp_pci;

}

vector<string> build_csv(vector<struct iio_stacks_on_socket>& iios, vector<struct iio_counter>& ctrs,
                         const bool human_readable, const bool show_root_port, const std::string& csv_delimiter,
                         const map<string,std::pair<h_id,std::map<string,v_id>>> &nameMap)
{
    vector<string> result;
    vector<string> current_row;
    auto header = combine_stack_name_and_counter_names("Part", nameMap);
    header.insert(header.begin(), "Name");
    if (show_root_port)
        header.insert(header.begin(), "Root Port");
    header.insert(header.begin(), "Socket");
    result.push_back(build_csv_row(header, csv_delimiter));
    std::map<uint32_t,map<uint32_t,struct iio_counter*>> v_sort;
    //re-organize data collection to be row wise
    size_t max_name_width = 0;
    for (std::vector<struct iio_counter>::iterator counter = ctrs.begin(); counter != ctrs.end(); ++counter) {
        v_sort[counter->v_id][counter->h_id] = &(*counter);
        max_name_width = (std::max)(max_name_width, counter->v_event_name.size());
    }

    for (auto socket = iios.cbegin(); socket != iios.cend(); ++socket) {
        for (auto stack = socket->stacks.cbegin(); stack != socket->stacks.cend(); ++stack) {
            const std::string socket_name = "Socket" + std::to_string(socket->socket_id);

            std::string stack_name = stack->stack_name;
            if (!human_readable) {
                stack_name.erase(stack_name.find_last_not_of(' ') + 1);
            }

            const uint32_t stack_id = stack->iio_unit_id;
            //Print data
            int part_id;
            std::map<uint32_t,map<uint32_t,struct iio_counter*>>::const_iterator vunit;
            for (vunit = v_sort.cbegin(), part_id = 0;
                     vunit != v_sort.cend(); ++vunit, ++part_id) {
                map<uint32_t, struct iio_counter*> h_array = vunit->second;
                uint32_t vv_id = vunit->first;
                vector<uint64_t> h_data;
                string v_name = h_array[0]->v_event_name;
                if (human_readable) {
                    v_name += string(max_name_width - (v_name.size()), ' ');
                }

                current_row.clear();
                current_row.push_back(socket_name);
                if (show_root_port) {
                    auto pci_dev = get_root_port_dev(show_root_port, part_id, &(*stack));
                    current_row.push_back(pci_dev);
                }
                current_row.push_back(stack_name);
                current_row.push_back(v_name);
                for (map<uint32_t,struct iio_counter*>::const_iterator hunit = h_array.cbegin(); hunit != h_array.cend(); ++hunit) {
                    uint32_t hh_id = hunit->first;
                    uint64_t raw_data = hunit->second->data[0][socket->socket_id][stack_id][std::pair<h_id,v_id>(hh_id,vv_id)];
                    current_row.push_back(human_readable ? unit_format(raw_data) : std::to_string(raw_data));
                }
                result.push_back(build_csv_row(current_row, csv_delimiter));
            }
        }
    }
    return result;
}

class IPlatformMapping {
private:
    uint32_t m_sockets;
    uint32_t m_model;
protected:
    void probeDeviceRange(std::vector<struct pci> &child_pci_devs, int domain, int secondary, int subordinate);
public:
    IPlatformMapping(int cpu_model, uint32_t sockets_count) : m_sockets(sockets_count), m_model(cpu_model) {}
    virtual ~IPlatformMapping() {};
    static std::unique_ptr<IPlatformMapping> getPlatformMapping(int cpu_model, uint32_t sockets_count);
    virtual bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios) = 0;

    uint32_t socketsCount() const { return m_sockets; }
    uint32_t cpuId() const { return m_model; }
};

// Mapping for SkyLake Server.
class PurleyPlatformMapping: public IPlatformMapping {
private:
    void getUboxBusNumbers(std::vector<uint32_t>& ubox);
public:
    PurleyPlatformMapping(int cpu_model, uint32_t sockets_count) : IPlatformMapping(cpu_model, sockets_count) {}
    ~PurleyPlatformMapping() = default;
    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios) override;
};

void PurleyPlatformMapping::getUboxBusNumbers(std::vector<uint32_t>& ubox)
{
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                struct pci pci_dev;
                pci_dev.bdf.busno = (uint8_t)bus;
                pci_dev.bdf.devno = device;
                pci_dev.bdf.funcno = function;
                if (probe_pci(&pci_dev)) {
                    if ((pci_dev.vendor_id == PCM_INTEL_PCI_VENDOR_ID) && (pci_dev.device_id == SKX_SOCKETID_UBOX_DID)) {
                        ubox.push_back(bus);
                    }
                }
            }
        }
    }
}

bool PurleyPlatformMapping::pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios)
{
    std::vector<uint32_t> ubox;
    getUboxBusNumbers(ubox);
    if (ubox.empty()) {
        cerr << "UBOXs were not found! Program aborted" << endl;
        return false;
    }

    for (uint32_t socket_id = 0; socket_id < socketsCount(); socket_id++) {
        if (!PciHandleType::exists(0, ubox[socket_id], SKX_UBOX_DEVICE_NUM, SKX_UBOX_FUNCTION_NUM)) {
            cerr << "No access to PCICFG\n" << endl;
            return false;
        }
        uint64 cpubusno = 0;
        struct iio_stacks_on_socket iio_on_socket;
        iio_on_socket.socket_id = socket_id;
        PciHandleType h(0, ubox[socket_id], SKX_UBOX_DEVICE_NUM, SKX_UBOX_FUNCTION_NUM);
        h.read64(ROOT_BUSES_OFFSET, &cpubusno);

        iio_on_socket.stacks.reserve(6);
        for (int stack_id = 0; stack_id < 6; stack_id++) {
            struct iio_stack stack;
            stack.iio_unit_id = stack_id;
            stack.busno = (uint8_t)(cpubusno >> (stack_id * SKX_BUS_NUM_STRIDE));
            stack.stack_name = skx_iio_stack_names[stack_id];
            for (uint8_t part_id = 0; part_id < 4; part_id++) {
                struct iio_bifurcated_part part;
                part.part_id = part_id;
                struct pci *pci = &part.root_pci_dev;
                struct bdf *bdf = &pci->bdf;
                bdf->busno = stack.busno;
                bdf->devno = part_id;
                bdf->funcno = 0;
                /* This is a workaround to catch some IIO stack does not exist */
                if (stack_id != 0 && stack.busno == 0) {
                    pci->exist = false;
                }
                else if (probe_pci(pci)) {
                    /* FIXME: for 0:0.0, we may need to scan from secondary switch down; lgtm [cpp/fixme-comment] */
                    for (uint8_t bus = pci->secondary_bus_number; bus <= pci->subordinate_bus_number; bus++) {
                        for (uint8_t device = 0; device < 32; device++) {
                            for (uint8_t function = 0; function < 8; function++) {
                                struct pci child_pci_dev;
                                child_pci_dev.bdf.busno = bus;
                                child_pci_dev.bdf.devno = device;
                                child_pci_dev.bdf.funcno = function;
                                if (probe_pci(&child_pci_dev)) {
                                    part.child_pci_devs.push_back(child_pci_dev);
                                }
                            }
                        }
                    }
                }
                stack.parts.push_back(part);
            }

            iio_on_socket.stacks.push_back(stack);
        }
        iios.push_back(iio_on_socket);
    }

    return true;
}

class IPlatformMapping10Nm: public IPlatformMapping {
private:
public:
    IPlatformMapping10Nm(int cpu_model, uint32_t sockets_count) : IPlatformMapping(cpu_model, sockets_count) {}
    ~IPlatformMapping10Nm() = default;
    bool getSadIdRootBusMap(uint32_t socket_id, std::map<uint8_t, uint8_t>& sad_id_bus_map);
};

bool IPlatformMapping10Nm::getSadIdRootBusMap(uint32_t socket_id, std::map<uint8_t, uint8_t>& sad_id_bus_map)
{
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                struct pci pci_dev;
                pci_dev.bdf.busno = (uint8_t)bus;
                pci_dev.bdf.devno = device;
                pci_dev.bdf.funcno = function;
                if (probe_pci(&pci_dev) && (pci_dev.vendor_id == PCM_INTEL_PCI_VENDOR_ID)
                    && (pci_dev.device_id == SNR_ICX_MESH2IIO_MMAP_DID)) {

                    PciHandleType h(0, bus, device, function);
                    std::uint32_t sad_ctrl_cfg;
                    h.read32(SNR_ICX_SAD_CONTROL_CFG_OFFSET, &sad_ctrl_cfg);
                    if (sad_ctrl_cfg == (std::numeric_limits<uint32_t>::max)()) {
                        cerr << "Could not read SAD_CONTROL_CFG" << endl;
                        return false;
                    }

                    if ((sad_ctrl_cfg & 0xf) == socket_id) {
                        uint8_t sid = (sad_ctrl_cfg >> 4) & 0x7;
                        sad_id_bus_map.insert(std::pair<uint8_t, uint8_t>(sid, (uint8_t)bus));
                    }
                }
            }
        }
    }

    if (sad_id_bus_map.empty()) {
        cerr << "Could not find Root Port bus numbers" << endl;
        return false;
    }

    return true;
}

// Mapping for IceLake Server.
class WhitleyPlatformMapping: public IPlatformMapping10Nm {
private:
    const bool icx_d;
    const std::map<int, int>& sad_to_pmu_id_mapping;
    const std::string * iio_stack_names;
public:
    WhitleyPlatformMapping(int cpu_model, uint32_t sockets_count) : IPlatformMapping10Nm(cpu_model, sockets_count),
        icx_d(PCM::getInstance()->getCPUFamilyModelFromCPUID() == PCM::ICX_D),
        sad_to_pmu_id_mapping(icx_d ? icx_d_sad_to_pmu_id_mapping : icx_sad_to_pmu_id_mapping),
        iio_stack_names(icx_d ? icx_d_iio_stack_names : icx_iio_stack_names)
    {
    }
    ~WhitleyPlatformMapping() = default;
    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios) override;
};

bool WhitleyPlatformMapping::pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios)
{
    for (uint32_t socket = 0; socket < socketsCount(); socket++) {
        struct iio_stacks_on_socket iio_on_socket;
        iio_on_socket.socket_id = socket;
        std::map<uint8_t, uint8_t> sad_id_bus_map;
        if (!getSadIdRootBusMap(socket, sad_id_bus_map)) {
            return false;
        }

        {
            struct iio_stack stack;
            stack.iio_unit_id = sad_to_pmu_id_mapping.at(ICX_MCP_SAD_ID);
            stack.stack_name = iio_stack_names[stack.iio_unit_id];
            iio_on_socket.stacks.push_back(stack);
        }

        for (auto sad_id_bus_pair = sad_id_bus_map.cbegin(); sad_id_bus_pair != sad_id_bus_map.cend(); ++sad_id_bus_pair) {
            int sad_id = sad_id_bus_pair->first;
            if (sad_to_pmu_id_mapping.find(sad_id) ==
                sad_to_pmu_id_mapping.end()) {
                cerr << "Unknown SAD ID: " << sad_id << endl;
                return false;
            }

            if (sad_id == ICX_MCP_SAD_ID) {
                continue;
            }

            struct iio_stack stack;
            int root_bus = sad_id_bus_pair->second;
            if (sad_id == ICX_CBDMA_DMI_SAD_ID) {
                // There is one DMA Controller on each socket
                stack.iio_unit_id = sad_to_pmu_id_mapping.at(sad_id);
                stack.busno = root_bus;
                stack.stack_name = iio_stack_names[stack.iio_unit_id];

                // PCH is on socket 0 only
                if (socket == 0) {
                    struct iio_bifurcated_part pch_part;
                    struct pci *pci = &pch_part.root_pci_dev;
                    struct bdf *bdf = &pci->bdf;
                    pch_part.part_id = ICX_PCH_PART_ID;
                    bdf->busno = root_bus;
                    bdf->devno = 0x00;
                    bdf->funcno = 0x00;
                    if (probe_pci(pci)) {
                        // Probe child devices only under PCH part.
                        for (uint8_t bus = pci->secondary_bus_number; bus <= pci->subordinate_bus_number; bus++) {
                            for (uint8_t device = 0; device < 32; device++) {
                                for (uint8_t function = 0; function < 8; function++) {
                                    struct pci child_pci_dev;
                                    child_pci_dev.bdf.busno = bus;
                                    child_pci_dev.bdf.devno = device;
                                    child_pci_dev.bdf.funcno = function;
                                    if (probe_pci(&child_pci_dev)) {
                                        pch_part.child_pci_devs.push_back(child_pci_dev);
                                    }
                                }
                            }
                        }
                        stack.parts.push_back(pch_part);
                    }
                }

                struct iio_bifurcated_part part;
                part.part_id = ICX_CBDMA_PART_ID;
                struct pci *pci = &part.root_pci_dev;
                struct bdf *bdf = &pci->bdf;
                bdf->busno = root_bus;
                bdf->devno = 0x01;
                bdf->funcno = 0x00;
                if (probe_pci(pci))
                    stack.parts.push_back(part);

                iio_on_socket.stacks.push_back(stack);
                continue;
            }
            stack.busno = root_bus;
            stack.iio_unit_id = sad_to_pmu_id_mapping.at(sad_id);
            stack.stack_name = iio_stack_names[stack.iio_unit_id];
            for (int slot = 2; slot < 6; slot++) {
                struct pci pci;
                pci.bdf.busno = root_bus;
                pci.bdf.devno = slot;
                pci.bdf.funcno = 0x00;
                if (!probe_pci(&pci)) {
                    continue;
                }
                struct iio_bifurcated_part part;
                part.part_id = slot - 2;
                part.root_pci_dev = pci;

                for (uint8_t bus = pci.secondary_bus_number; bus <= pci.subordinate_bus_number; bus++) {
                    for (uint8_t device = 0; device < 32; device++) {
                        for (uint8_t function = 0; function < 8; function++) {
                            struct pci child_pci_dev;
                            child_pci_dev.bdf.busno = bus;
                            child_pci_dev.bdf.devno = device;
                            child_pci_dev.bdf.funcno = function;
                            if (probe_pci(&child_pci_dev)) {
                                part.child_pci_devs.push_back(child_pci_dev);
                            }
                        }
                    }
                }
                stack.parts.push_back(part);
            }
            iio_on_socket.stacks.push_back(stack);
        }
        std::sort(iio_on_socket.stacks.begin(), iio_on_socket.stacks.end());
        iios.push_back(iio_on_socket);
    }
    return true;
}

// Mapping for Snowridge.
class JacobsvillePlatformMapping: public IPlatformMapping10Nm {
private:
public:
    JacobsvillePlatformMapping(int cpu_model, uint32_t sockets_count) : IPlatformMapping10Nm(cpu_model, sockets_count) {}
    ~JacobsvillePlatformMapping() = default;
    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios) override;
    bool JacobsvilleAccelerators(const std::pair<uint8_t, uint8_t>& sad_id_bus_pair, struct iio_stack& stack);
};

bool JacobsvillePlatformMapping::JacobsvilleAccelerators(const std::pair<uint8_t, uint8_t>& sad_id_bus_pair, struct iio_stack& stack)
{
    uint16_t expected_dev_id;
    auto sad_id = sad_id_bus_pair.first;
    switch (sad_id) {
    case SNR_HQM_SAD_ID:
        expected_dev_id = HQM_DID;
        break;
    case SNR_NIS_SAD_ID:
        expected_dev_id = NIS_DID;
        break;
    case SNR_QAT_SAD_ID:
        expected_dev_id = QAT_DID;
        break;
    default:
        return false;
    }
    stack.iio_unit_id = snr_sad_to_pmu_id_mapping.at(sad_id);
    stack.stack_name = snr_iio_stack_names[stack.iio_unit_id];
    for (uint16_t bus = sad_id_bus_pair.second; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                struct pci pci_dev;
                pci_dev.bdf.busno = (uint8_t)bus;
                pci_dev.bdf.devno = device;
                pci_dev.bdf.funcno = function;
                if (probe_pci(&pci_dev)) {
                    if (expected_dev_id == pci_dev.device_id) {
                        struct iio_bifurcated_part part;
                        part.part_id = SNR_ACCELERATOR_PART_ID;
                        part.root_pci_dev = pci_dev;
                        stack.busno = (uint8_t)bus;
                        stack.parts.push_back(part);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool JacobsvillePlatformMapping::pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios)
{
    std::map<uint8_t, uint8_t> sad_id_bus_map;
    if (!getSadIdRootBusMap(0, sad_id_bus_map)) {
        return false;
    }
    struct iio_stacks_on_socket iio_on_socket;
    iio_on_socket.socket_id = 0;
    if (sad_id_bus_map.size() != snr_sad_to_pmu_id_mapping.size()) {
        cerr << "Found unexpected number of stacks: " << sad_id_bus_map.size() << ", expected: " << snr_sad_to_pmu_id_mapping.size() << endl;
        return false;
    }

    for (auto sad_id_bus_pair = sad_id_bus_map.cbegin(); sad_id_bus_pair != sad_id_bus_map.cend(); ++sad_id_bus_pair) {
        int sad_id = sad_id_bus_pair->first;
        struct iio_stack stack;
        switch (sad_id) {
        case SNR_CBDMA_DMI_SAD_ID:
            {
                int root_bus = sad_id_bus_pair->second;
                stack.iio_unit_id = snr_sad_to_pmu_id_mapping.at(sad_id);
                stack.stack_name = snr_iio_stack_names[stack.iio_unit_id];
                stack.busno = root_bus;
                // DMA Controller
                struct iio_bifurcated_part part;
                part.part_id = 0;
                struct pci pci_dev;
                pci_dev.bdf.busno = root_bus;
                pci_dev.bdf.devno = 0x01;
                pci_dev.bdf.funcno = 0x00;
                if (probe_pci(&pci_dev)) {
                    part.root_pci_dev = pci_dev;
                    stack.parts.push_back(part);
                }

                part.part_id = 4;
                pci_dev.bdf.busno = root_bus;
                pci_dev.bdf.devno = 0x00;
                pci_dev.bdf.funcno = 0x00;
                if (probe_pci(&pci_dev)) {
                    for (uint8_t bus = pci_dev.secondary_bus_number; bus <= pci_dev.subordinate_bus_number; bus++) {
                        for (uint8_t device = 0; device < 32; device++) {
                            for (uint8_t function = 0; function < 8; function++) {
                                struct pci child_pci_dev;
                                child_pci_dev.bdf.busno = bus;
                                child_pci_dev.bdf.devno = device;
                                child_pci_dev.bdf.funcno = function;
                                if (probe_pci(&child_pci_dev)) {
                                    part.child_pci_devs.push_back(child_pci_dev);
                                }
                            }
                        }
                    }
                    part.root_pci_dev = pci_dev;
                    stack.parts.push_back(part);
                }
            }
            break;
        case SNR_PCIE_GEN3_SAD_ID:
            {
                int root_bus = sad_id_bus_pair->second;
                stack.busno = root_bus;
                stack.iio_unit_id = snr_sad_to_pmu_id_mapping.at(sad_id);
                stack.stack_name = snr_iio_stack_names[stack.iio_unit_id];
                for (int slot = 4; slot < 8; slot++) {
                    struct pci pci_dev;
                    pci_dev.bdf.busno = root_bus;
                    pci_dev.bdf.devno = slot;
                    pci_dev.bdf.funcno = 0x00;
                    if (!probe_pci(&pci_dev)) {
                        continue;
                    }
                    int part_id = 4 + pci_dev.device_id - SNR_ROOT_PORT_A_DID;
                    if ((part_id < 0) || (part_id > 4)) {
                        cerr << "Invalid part ID " << part_id << endl;
                        return false;
                    }
                    struct iio_bifurcated_part part;
                    part.part_id = part_id;
                    part.root_pci_dev = pci_dev;
                    for (uint8_t bus = pci_dev.secondary_bus_number; bus <= pci_dev.subordinate_bus_number; bus++) {
                        for (uint8_t device = 0; device < 32; device++) {
                            for (uint8_t function = 0; function < 8; function++) {
                                struct pci child_pci_dev;
                                child_pci_dev.bdf.busno = bus;
                                child_pci_dev.bdf.devno = device;
                                child_pci_dev.bdf.funcno = function;
                                if (probe_pci(&child_pci_dev)) {
                                    part.child_pci_devs.push_back(child_pci_dev);
                                }
                            }
                        }
                    }
                    stack.parts.push_back(part);
                }
            }
            break;
        case SNR_HQM_SAD_ID:
        case SNR_NIS_SAD_ID:
        case SNR_QAT_SAD_ID:
            JacobsvilleAccelerators(*sad_id_bus_pair, stack);
            break;
        default:
            cerr << "Unknown SAD ID: " << sad_id << endl;
            return false;
        }
        iio_on_socket.stacks.push_back(stack);
    }

    std::sort(iio_on_socket.stacks.begin(), iio_on_socket.stacks.end());

    iios.push_back(iio_on_socket);

    return true;
}

class EagleStreamPlatformMapping: public IPlatformMapping
{
private:
    bool getRootBuses(std::map<int, std::map<int, struct bdf>> &root_buses);
    bool stackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket);
    bool eagleStreamDmiStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket);
    bool eagleStreamPciStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket);
    bool eagleStreamAcceleratorStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket);
    bool isDmiStack(int unit);
    bool isPcieStack(int unit);
    bool isDinoStack(int unit);
    std::uint32_t m_chop;
    EagleStreamSupportedTypes m_es_type;
public:
    EagleStreamPlatformMapping(int cpu_model, uint32_t sockets_count) : IPlatformMapping(cpu_model, sockets_count), m_chop(0), m_es_type(estype::esInvalid) {}
    ~EagleStreamPlatformMapping() = default;
    bool setChopValue();
    bool isXccPlatform() const { return m_chop == kXccChop; }

    const std::uint32_t kXccChop = 0b11;
    const std::uint32_t kMccChop = 0b01;

    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios) override;
};

bool EagleStreamPlatformMapping::setChopValue()
{
    for (uint16_t b = 0; b < 256; b++) {
        struct pci pci_dev(0, b, SPR_PCU_CR3_REG_DEVICE, SPR_PCU_CR3_REG_FUNCTION);
        if (!probe_pci(&pci_dev)) {
            continue;
        }
        if (!((pci_dev.vendor_id == PCM_INTEL_PCI_VENDOR_ID) && (pci_dev.device_id == SPR_PCU_CR3_DID))) {
            continue;
        }
        std::uint32_t capid4;
        PciHandleType h(0, b, SPR_PCU_CR3_REG_DEVICE, SPR_PCU_CR3_REG_FUNCTION);
        h.read32(SPR_CAPID4_OFFSET, &capid4);
        if (capid4 == (std::numeric_limits<std::uint32_t>::max)()) {
            std::cerr << "Cannot read PCU RC3 register" << std::endl;
            return false;
        }
        capid4 = SPR_CAPID4_GET_PHYSICAL_CHOP(capid4);
        if (capid4 == kXccChop || capid4 == kMccChop) {
            m_chop = capid4;
            m_es_type = cpuId() == PCM::SPR ? (m_chop == kXccChop ? estype::esSprXcc : estype::esSprMcc) : estype::esEmrXcc;
        }
        else {
            std::cerr << "Unknown chop value " << capid4 << std::endl;
            return false;
        }
        return true;
    }
    std::cerr << "Cannot find PCU RC3 registers on the system. Device ID is " << std::hex << SPR_PCU_CR3_DID << std::dec << std::endl;
    return false;
}

bool EagleStreamPlatformMapping::getRootBuses(std::map<int, std::map<int, struct bdf>> &root_buses)
{
    bool mapped = true;
    for (uint32_t domain = 0; mapped; domain++) {
        mapped = false;
        for (uint16_t b = 0; b < 256; b++) {
            for (uint8_t d = 0; d < 32; d++) {
                for (uint8_t f = 0; f < 8; f++) {
                    struct pci pci_dev(domain, b, d, f);
                    if (!probe_pci(&pci_dev)) {
                        break;
                    }
                    if (!((pci_dev.vendor_id == PCM_INTEL_PCI_VENDOR_ID) && (pci_dev.device_id == SPR_MSM_DEV_ID))) {
                        continue;
                    }

                    std::uint32_t cpuBusValid;
                    std::vector<std::uint32_t> cpuBusNo;
                    int package_id;

                    if (get_cpu_bus(domain, b, d, f, cpuBusValid, cpuBusNo, package_id) == false)
                    {
                        return false;
                    }

                    const auto& sad_to_pmu_id_mapping = es_sad_to_pmu_id_mapping.at(m_es_type);
                    for (int cpuBusId = 0; cpuBusId < SPR_MSM_CPUBUSNO_MAX; ++cpuBusId) {
                        if (!((cpuBusValid >> cpuBusId) & 0x1))
                        {
                            cout << "CPU bus " << cpuBusId << " is disabled on package " << package_id << endl;
                            continue;
                        }
                        if (sad_to_pmu_id_mapping.find(cpuBusId) == sad_to_pmu_id_mapping.end())
                        {
                            cerr << "Cannot map CPU bus " << cpuBusId << " to IO PMU ID" << endl;
                            continue;
                        }
                        int pmuId = sad_to_pmu_id_mapping.at(cpuBusId);
                        int rootBus = (cpuBusNo[(int)(cpuBusId / 4)] >> ((cpuBusId % 4) * 8)) & 0xff;
                        root_buses[package_id][pmuId] = bdf(domain, rootBus, 0, 0);
                        cout << "Mapped CPU bus #" << cpuBusId << " (domain " << domain << " bus " << std::hex << rootBus << std::dec << ") to IO PMU #"
                             << pmuId << " package " << package_id << endl;
                        mapped = true;
                    }
                }
            }
        }
    }
    return !root_buses.empty();
}

bool EagleStreamPlatformMapping::eagleStreamDmiStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket)
{
    struct iio_stack stack;
    stack.iio_unit_id = unit;
    stack.stack_name = es_stack_names.at(m_es_type)[unit];
    stack.busno = address.busno;
    stack.domain = address.domainno;
    struct iio_bifurcated_part pch_part;
    struct pci *pci = &pch_part.root_pci_dev;
    auto dmi_part_id = SPR_DMI_PART_ID;
    pch_part.part_id = dmi_part_id;
    pci->bdf = address;
    if (!probe_pci(pci)) {
        cerr << "Failed to probe DMI Stack: address: " << std::setw(4) << std::setfill('0') << std::hex << address.domainno <<
                                                          std::setw(2) << std::setfill('0') << ":" << address.busno << ":" << address.devno <<
                                                          "." << address.funcno << std::dec << endl;
        return false;
    }

    /* Scan devices behind PCH port only */
    if (!iio_on_socket.socket_id)
        probeDeviceRange(pch_part.child_pci_devs, pci->bdf.domainno, pci->secondary_bus_number, pci->subordinate_bus_number);

    pci->parts_no.push_back(dmi_part_id);

    stack.parts.push_back(pch_part);
    iio_on_socket.stacks.push_back(stack);
    return true;
}

bool EagleStreamPlatformMapping::eagleStreamPciStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket)
{
    /*
     * Stacks that manage PCIe 4.0 (device 2,4,6,8) and 5.0 (device 1,3,5,7) Root Ports.
     */
    struct iio_stack stack;
    stack.domain = address.domainno;
    stack.busno = address.busno;
    stack.iio_unit_id = unit;
    stack.stack_name = es_stack_names.at(m_es_type)[unit];
    for (int slot = 1; slot < 9; ++slot)
    {
        // Check if port is enabled
        struct pci root_pci_dev;
        root_pci_dev.bdf = bdf(address.domainno, address.busno, slot, 0x0);
        if (probe_pci(&root_pci_dev))
        {
            struct iio_bifurcated_part part;
            // Bifurcated Root Ports to channel mapping on SPR
            part.part_id = slot - 1;
            part.root_pci_dev = root_pci_dev;
            for (uint8_t b = root_pci_dev.secondary_bus_number; b <= root_pci_dev.subordinate_bus_number; ++b) {
                for (uint8_t d = 0; d < 32; ++d) {
                    for (uint8_t f = 0; f < 8; ++f) {
                        struct pci child_pci_dev(address.domainno, b, d, f);
                        if (probe_pci(&child_pci_dev)) {
                            child_pci_dev.parts_no.push_back(part.part_id);
                            part.child_pci_devs.push_back(child_pci_dev);
                        }
                    }
                }
            }
            stack.parts.push_back(part);
        }
    }
    iio_on_socket.stacks.push_back(stack);
    return true;
}

bool EagleStreamPlatformMapping::eagleStreamAcceleratorStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket)
{
    struct iio_stack stack;
    stack.iio_unit_id = unit;
    stack.domain = address.domainno;
    stack.busno = address.busno;

    // Channel mappings are checked on B0 stepping
    auto rb = address.busno;
    const std::vector<int> acceleratorBuses{ rb, rb + 1, rb + 2, rb + 3 };
    stack.stack_name = es_stack_names.at(m_es_type)[unit];
    for (auto& b : acceleratorBuses) {
        for (auto d = 0; d < 32; ++d) {
            for (auto f = 0; f < 8; ++f) {
                struct iio_bifurcated_part part;
                struct pci pci_dev(address.domainno, b, d, f);

                if (probe_pci(&pci_dev)) {
                    if (pci_dev.vendor_id == PCM_INTEL_PCI_VENDOR_ID) {
                        switch (pci_dev.device_id) {
                        case DSA_DID:
                            pci_dev.parts_no.push_back(0);
                            pci_dev.parts_no.push_back(1);
                            pci_dev.parts_no.push_back(2);
                            break;
                        case IAX_DID:
                            pci_dev.parts_no.push_back(0);
                            pci_dev.parts_no.push_back(1);
                            pci_dev.parts_no.push_back(2);
                            break;
                        case HQMV2_DID:
                            pci_dev.parts_no.push_back(isXccPlatform() ? SPR_XCC_HQM_PART_ID : SPR_MCC_HQM_PART_ID);
                            break;
                        case QATV2_DID:
                            pci_dev.parts_no.push_back(isXccPlatform() ? SPR_XCC_QAT_PART_ID : SPR_MCC_QAT_PART_ID);
                            break;
                        default:
                            continue;
                        }
                        part.child_pci_devs.push_back(pci_dev);
                    }
                    stack.parts.push_back(part);
                }
            }
        }
    }

    iio_on_socket.stacks.push_back(stack);
    return true;
}

bool EagleStreamPlatformMapping::isDmiStack(int unit)
{
    const auto& stacks_enumeration = es_stacks_enumeration.at(m_es_type);

    return stacks_enumeration[esDMI] == unit;
}

bool EagleStreamPlatformMapping::isPcieStack(int unit)
{
    const auto& stacks_enumeration = es_stacks_enumeration.at(m_es_type);

    return stacks_enumeration[esPCIe0] == unit || stacks_enumeration[esPCIe1] == unit ||
           stacks_enumeration[esPCIe2] == unit || stacks_enumeration[esPCIe3] == unit ||
           stacks_enumeration[esPCIe4] == unit;
}

bool EagleStreamPlatformMapping::isDinoStack(int unit)
{
    const auto& stacks_enumeration = es_stacks_enumeration.at(m_es_type);

    return stacks_enumeration[esDINO0] == unit || stacks_enumeration[esDINO1] == unit ||
           stacks_enumeration[esDINO2] == unit || stacks_enumeration[esDINO3] == unit;
}

bool EagleStreamPlatformMapping::stackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket)
{
    if (isDmiStack(unit)) {
        return eagleStreamDmiStackProbe(unit, address, iio_on_socket);
    }
    else if (isPcieStack(unit)) {
        return eagleStreamPciStackProbe(unit, address, iio_on_socket);
    }
    else if (isDinoStack(unit)) {
        return eagleStreamAcceleratorStackProbe(unit, address, iio_on_socket);
    }

    return false;
}

bool EagleStreamPlatformMapping::pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios)
{
    if (!setChopValue()) return false;

    std::map<int, std::map<int, struct bdf>> root_buses;
    if (!getRootBuses(root_buses))
    {
        return false;
    }

    for (auto iter = root_buses.cbegin(); iter != root_buses.cend(); ++iter) {
        auto rbs_on_socket = iter->second;
        struct iio_stacks_on_socket iio_on_socket;
        iio_on_socket.socket_id = iter->first;
        for (auto rb = rbs_on_socket.cbegin(); rb != rbs_on_socket.cend(); ++rb) {
            if (!stackProbe(rb->first, rb->second, iio_on_socket)) {
                return false;
            }
        }
        std::sort(iio_on_socket.stacks.begin(), iio_on_socket.stacks.end());
        iios.push_back(iio_on_socket);
    }

    return true;
}

class LoganvillePlatform: public IPlatformMapping10Nm {
private:
    bool loganvillePchDsaPciStackProbe(struct iio_stacks_on_socket& iio_on_socket, int root_bus, int stack_pmon_id);
    bool loganvilleDlbStackProbe(struct iio_stacks_on_socket& iio_on_socket, int root_bus, int stack_pmon_id);
    bool loganvilleNacStackProbe(struct iio_stacks_on_socket& iio_on_socket, int root_bus, int stack_pmon_id);
public:
    LoganvillePlatform(int cpu_model, uint32_t sockets_count) : IPlatformMapping10Nm(cpu_model, sockets_count) {}
    ~LoganvillePlatform() = default;
    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios) override;
};

bool LoganvillePlatform::loganvillePchDsaPciStackProbe(struct iio_stacks_on_socket& iio_on_socket, int root_bus, int stack_pmon_id)
{
    struct iio_stack stack;
    stack.busno = root_bus;
    stack.iio_unit_id = stack_pmon_id;
    stack.stack_name = grr_iio_stack_names[stack_pmon_id];

    struct iio_bifurcated_part pch_part;
    pch_part.part_id = 7;
    struct pci* pci_dev = &pch_part.root_pci_dev;
    pci_dev->bdf.busno = root_bus;

    if (probe_pci(pci_dev)) {
        probeDeviceRange(pch_part.child_pci_devs, pci_dev->bdf.domainno, pci_dev->secondary_bus_number, pci_dev->subordinate_bus_number);
        stack.parts.push_back(pch_part);
        iio_on_socket.stacks.push_back(stack);
        return true;
    }

    return false;
}

bool LoganvillePlatform::loganvilleDlbStackProbe(struct iio_stacks_on_socket& iio_on_socket, int root_bus, int stack_pmon_id)
{
    struct iio_stack stack;
    stack.busno = root_bus;
    stack.iio_unit_id = stack_pmon_id;
    stack.stack_name = grr_iio_stack_names[stack_pmon_id];

    struct iio_bifurcated_part dlb_part;
    dlb_part.part_id = GRR_DLB_PART_ID;

    for (uint8_t bus = root_bus; bus < 255; bus++) {
        struct pci pci_dev(bus, 0x00, 0x00);
        if (probe_pci(&pci_dev)) {
            if ((pci_dev.vendor_id == PCM_INTEL_PCI_VENDOR_ID) && (pci_dev.device_id == HQMV25_DID)) {
                dlb_part.root_pci_dev = pci_dev;
                // Check Virtual RPs for DLB
                for (uint8_t device = 0; device < 2; device++) {
                    for (uint8_t function = 0; function < 8; function++) {
                        struct pci child_pci_dev(bus, device, function);
                        if (probe_pci(&child_pci_dev)) {
                            dlb_part.child_pci_devs.push_back(child_pci_dev);
                        }
                    }
                }
                stack.parts.push_back(dlb_part);
                iio_on_socket.stacks.push_back(stack);
                return true;
            }
        }
    }

    return false;
}

bool LoganvillePlatform::loganvilleNacStackProbe(struct iio_stacks_on_socket& iio_on_socket, int root_bus, int stack_pmon_id)
{
    struct iio_stack stack;
    stack.busno = root_bus;
    stack.iio_unit_id = stack_pmon_id;
    stack.stack_name = grr_iio_stack_names[stack_pmon_id];

    // Probe NIS
    {
        struct iio_bifurcated_part nis_part;
        nis_part.part_id = GRR_NIS_PART_ID;
        struct pci pci_dev(root_bus, 0x04, 0x00);
        if (probe_pci(&pci_dev)) {
            nis_part.root_pci_dev = pci_dev;
            for (uint8_t bus = pci_dev.secondary_bus_number; bus <= pci_dev.subordinate_bus_number; bus++) {
                for (uint8_t device = 0; device < 2; device++) {
                    for (uint8_t function = 0; function < 8; function++) {
                            struct pci child_pci_dev(bus, device, function);
                            if (probe_pci(&child_pci_dev)) {
                                nis_part.child_pci_devs.push_back(child_pci_dev);
                            }
                    }
                }
            }
            stack.parts.push_back(nis_part);
        }
    }

    // Probe QAT
    {
        struct iio_bifurcated_part qat_part;
        qat_part.part_id = GRR_QAT_PART_ID;
        struct pci pci_dev(root_bus, 0x05, 0x00);
        if (probe_pci(&pci_dev)) {
            qat_part.root_pci_dev = pci_dev;
            for (uint8_t bus = pci_dev.secondary_bus_number; bus <= pci_dev.subordinate_bus_number; bus++) {
                for (uint8_t device = 0; device < 17; device++) {
                    for (uint8_t function = 0; function < 8; function++) {
                            struct pci child_pci_dev(bus, device, function);
                            if (probe_pci(&child_pci_dev)) {
                                qat_part.child_pci_devs.push_back(child_pci_dev);
                            }
                    }
                }
            }
            stack.parts.push_back(qat_part);
        }
    }

    iio_on_socket.stacks.push_back(stack);
    return true;
}

bool LoganvillePlatform::pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios)
{
    std::map<uint8_t, uint8_t> sad_id_bus_map;
    if (!getSadIdRootBusMap(0, sad_id_bus_map)) {
        return false;
    }

    if (sad_id_bus_map.size() != grr_sad_to_pmu_id_mapping.size()) {
        cerr << "Found unexpected number of stacks: " << sad_id_bus_map.size() << ", expected: " << grr_sad_to_pmu_id_mapping.size() << endl;
        return false;
    }

    struct iio_stacks_on_socket iio_on_socket;
    iio_on_socket.socket_id = 0;

    for (auto sad_id_bus_pair = sad_id_bus_map.cbegin(); sad_id_bus_pair != sad_id_bus_map.cend(); ++sad_id_bus_pair) {
        if (grr_sad_to_pmu_id_mapping.find(sad_id_bus_pair->first) == grr_sad_to_pmu_id_mapping.end()) {
            cerr << "Cannot map SAD ID to PMON ID. Unknown ID: " << sad_id_bus_pair->first << endl;
            return false;
        }
        int stack_pmon_id = grr_sad_to_pmu_id_mapping.at(sad_id_bus_pair->first);
        int root_bus = sad_id_bus_pair->second;
        switch (stack_pmon_id) {
        case GRR_PCH_DSA_GEN4_PMON_ID:
            if (!loganvillePchDsaPciStackProbe(iio_on_socket, root_bus, stack_pmon_id)) {
                return false;
            }
            break;
        case GRR_DLB_PMON_ID:
            if (!loganvilleDlbStackProbe(iio_on_socket, root_bus, stack_pmon_id)) {
                return false;
            }
            break;
        case GRR_NIS_QAT_PMON_ID:
            if (!loganvilleNacStackProbe(iio_on_socket, root_bus, stack_pmon_id)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }

    std::sort(iio_on_socket.stacks.begin(), iio_on_socket.stacks.end());

    iios.push_back(iio_on_socket);

    return true;
}

void IPlatformMapping::probeDeviceRange(std::vector<struct pci> &pci_devs, int domain, int secondary, int subordinate)
{
    for (uint8_t bus = secondary; int(bus) <= subordinate; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                struct pci child_dev;
                child_dev.bdf.domainno = domain;
                child_dev.bdf.busno = bus;
                child_dev.bdf.devno = device;
                child_dev.bdf.funcno = function;
                if (probe_pci(&child_dev)) {
                    if (secondary < child_dev.secondary_bus_number && subordinate < child_dev.subordinate_bus_number) {
                        probeDeviceRange(child_dev.child_pci_devs, domain, child_dev.secondary_bus_number, child_dev.subordinate_bus_number);
                    }
                    pci_devs.push_back(child_dev);
                }
            }
        }
    }
}

class BirchStreamPlatform: public IPlatformMapping {
private:
    bool isPcieStack(int unit);
    bool isRootHcStack(int unit);
    bool isPartHcStack(int unit);
    bool isUboxStack(int unit);

    bool birchStreamPciStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket);
    bool birchStreamAcceleratorStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket);

    bool stackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket);
    bool getRootBuses(std::map<int, std::map<int, struct bdf>> &root_buses);
public:
    BirchStreamPlatform(int cpu_model, uint32_t sockets_count) : IPlatformMapping(cpu_model, sockets_count) {}
    ~BirchStreamPlatform() = default;
    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios) override;
};

bool BirchStreamPlatform::birchStreamPciStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket)
{
    /*
     * All stacks manage PCIe 5.0 Root Ports. Bifurcated Root Ports A-H appear as devices 2-9.
     */
    struct iio_stack stack;
    stack.domain = address.domainno;
    stack.busno = address.busno;
    stack.iio_unit_id = srf_sad_to_pmu_id_mapping.at(unit);
    stack.stack_name = srf_iio_stack_names[stack.iio_unit_id];
    for (int slot = 2; slot < 9; ++slot)
    {
        struct pci root_pci_dev;
        root_pci_dev.bdf = bdf(address.domainno, address.busno, slot, 0x0);
        if (probe_pci(&root_pci_dev))
        {
            struct iio_bifurcated_part part;
            part.part_id = slot - 2;
            part.root_pci_dev = root_pci_dev;
            for (uint8_t b = root_pci_dev.secondary_bus_number; b <= root_pci_dev.subordinate_bus_number; ++b) {
                for (uint8_t d = 0; d < 32; ++d) {
                    for (uint8_t f = 0; f < 8; ++f) {
                        struct pci child_pci_dev(address.domainno, b, d, f);
                        if (probe_pci(&child_pci_dev)) {
                            child_pci_dev.parts_no.push_back(part.part_id);
                            part.child_pci_devs.push_back(child_pci_dev);
                        }
                    }
                }
            }
            stack.parts.push_back(part);
        }
    }
    iio_on_socket.stacks.push_back(stack);
    return true;
}

bool BirchStreamPlatform::birchStreamAcceleratorStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket)
{
    struct iio_stack stack;
    stack.iio_unit_id = srf_sad_to_pmu_id_mapping.at(unit);
    stack.domain = address.domainno;
    stack.busno = address.busno;
    stack.stack_name = srf_iio_stack_names[stack.iio_unit_id];

    /*
     * Instance of DSA(0, 1, 2, 3) appears as PCIe device with SAD Bus ID (8, 12, 20, 16), device 1, function 0
     * Instance of IAX(0, 1, 2, 3) appears as PCIe device with SAD Bus ID (8, 12, 20, 16), device 2, function 0
     * Instance of QAT(0, 1, 2, 3) appears as PCIe device with SAD Bus ID (9, 13, 21, 17), device 0, function 0
     * Instance of HQM(0, 1, 2, 3) appears as PCIe device with SAD Bus ID (10, 14, 22, 18), device 0, function 0
     */
    auto process_pci_dev = [](int domainno, int busno, int devno, int part_number, iio_bifurcated_part& part)
    {
        struct pci pci_dev(domainno, busno, devno, 0);
        if (probe_pci(&pci_dev) && pci_dev.isIntelDevice()) {
            part.part_id = part_number;
            pci_dev.parts_no.push_back(part_number);
            part.child_pci_devs.push_back(pci_dev);
            return true;
        }
        return false;
    };

    {
        struct iio_bifurcated_part part;
        if (process_pci_dev(address.domainno, address.busno, 1, SRF_DSA_IAX_PART_NUMBER, part) ||
            process_pci_dev(address.domainno, address.busno, 2, SRF_DSA_IAX_PART_NUMBER, part)) {
            stack.parts.push_back(part);
        }
    }

    {
        struct iio_bifurcated_part part;
        if (process_pci_dev(address.domainno, address.busno + 1, 0, SRF_QAT_PART_NUMBER, part)) {
            stack.parts.push_back(part);
        }
    }

    {
        /* Bus number for HQM is higher on 3 than DSA bus number */
        struct iio_bifurcated_part part;
        if (process_pci_dev(address.domainno, address.busno + 3, 0, SRF_HQM_PART_NUMBER, part)) {
            stack.parts.push_back(part);
        }
    }

    if (!stack.parts.empty()) {
        iio_on_socket.stacks.push_back(stack);
    }

    return true;
}

bool BirchStreamPlatform::isPcieStack(int unit)
{
    return srf_pcie_stacks.find(unit) != srf_pcie_stacks.end();
}

/*
 * HC is the name of DINO stacks as we had on SPR
 */
bool BirchStreamPlatform::isRootHcStack(int unit)
{
    return SRF_HC0_SAD_BUS_ID == unit || SRF_HC1_SAD_BUS_ID == unit ||
           SRF_HC2_SAD_BUS_ID == unit || SRF_HC3_SAD_BUS_ID == unit;
}

bool BirchStreamPlatform::isPartHcStack(int unit)
{
    return isRootHcStack(unit - 1) || isRootHcStack(unit - 2);
}

bool BirchStreamPlatform::isUboxStack(int unit)
{
    return SRF_UBOXA_SAD_BUS_ID == unit || SRF_UBOXB_SAD_BUS_ID == unit;
}

bool BirchStreamPlatform::stackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket)
{
    if (isPcieStack(unit)) {
        return birchStreamPciStackProbe(unit, address, iio_on_socket);
    }
    else if (isRootHcStack(unit)) {
        return birchStreamAcceleratorStackProbe(unit, address, iio_on_socket);
    }
    else if (isPartHcStack(unit)) {
        cout << "Found a part of HC stack. Stack ID - " << unit << " domain " << address.domainno
             << " bus " << std::hex << std::setfill('0') << std::setw(2) << (int)address.busno << std::dec << ". Don't probe it again." << endl;
        return true;
    }
    else if (isUboxStack(unit)) {
        cout << "Found UBOX stack. Stack ID - " << unit << " domain " << address.domainno
             << " bus " << std::hex << std::setfill('0') << std::setw(2) << (int)address.busno << std::dec << endl;
        return true;
    }

    cout << "Unknown stack ID " << unit << " domain " << address.domainno << " bus " << std::hex << std::setfill('0') << std::setw(2) << (int)address.busno << std::dec << endl;

    return false;
}

bool BirchStreamPlatform::getRootBuses(std::map<int, std::map<int, struct bdf>> &root_buses)
{
    bool mapped = true;
    for (uint32_t domain = 0; mapped; domain++) {
        mapped = false;
        for (uint16_t b = 0; b < 256; b++) {
            for (uint8_t d = 0; d < 32; d++) {
                for (uint8_t f = 0; f < 8; f++) {
                    struct pci pci_dev(domain, b, d, f);
                    if (!probe_pci(&pci_dev)) {
                        break;
                    }
                    if (!((pci_dev.vendor_id == PCM_INTEL_PCI_VENDOR_ID) && (pci_dev.device_id == SPR_MSM_DEV_ID))) {
                        continue;
                    }

                    std::uint32_t cpuBusValid;
                    std::vector<std::uint32_t> cpuBusNo;
                    int package_id;

                    if (get_cpu_bus(domain, b, d, f, cpuBusValid, cpuBusNo, package_id) == false) {
                        return false;
                    }

                    for (int cpuBusId = 0; cpuBusId < SPR_MSM_CPUBUSNO_MAX; ++cpuBusId) {
                        if (!((cpuBusValid >> cpuBusId) & 0x1)) {
                            cout << "CPU bus " << cpuBusId << " is disabled on package " << package_id << endl;
                            continue;
                        }
                        int rootBus = (cpuBusNo[(int)(cpuBusId / 4)] >> ((cpuBusId % 4) * 8)) & 0xff;
                        root_buses[package_id][cpuBusId] = bdf(domain, rootBus, 0, 0);
                        cout << "Mapped CPU bus #" << cpuBusId << " (domain " << domain << " bus " << std::hex << rootBus << std::dec << ")"
                             << " package " << package_id << endl;
                        mapped = true;
                    }
                }
            }
        }
    }
    return !root_buses.empty();
}

bool BirchStreamPlatform::pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios)
{
    std::map<int, std::map<int, struct bdf>> root_buses;
    if (!getRootBuses(root_buses))
    {
        return false;
    }

    for (auto iter = root_buses.cbegin(); iter != root_buses.cend(); ++iter) {
        auto rbs_on_socket = iter->second;
        struct iio_stacks_on_socket iio_on_socket;
        iio_on_socket.socket_id = iter->first;
        for (auto rb = rbs_on_socket.cbegin(); rb != rbs_on_socket.cend(); ++rb) {
            if (!stackProbe(rb->first, rb->second, iio_on_socket)) {
                return false;
            }
        }
        std::sort(iio_on_socket.stacks.begin(), iio_on_socket.stacks.end());
        iios.push_back(iio_on_socket);
    }

    return true;
}

std::unique_ptr<IPlatformMapping> IPlatformMapping::getPlatformMapping(int cpu_family_model, uint32_t sockets_count)
{
    switch (cpu_family_model) {
    case PCM::SKX:
        return std::unique_ptr<IPlatformMapping>{new PurleyPlatformMapping(cpu_family_model, sockets_count)};
    case PCM::ICX:
        return std::unique_ptr<IPlatformMapping>{new WhitleyPlatformMapping(cpu_family_model, sockets_count)};
    case PCM::SNOWRIDGE:
        return std::unique_ptr<IPlatformMapping>{new JacobsvillePlatformMapping(cpu_family_model, sockets_count)};
    case PCM::SPR:
    case PCM::EMR:
        return std::unique_ptr<IPlatformMapping>{new EagleStreamPlatformMapping(cpu_family_model, sockets_count)};
    case PCM::GRR:
        return std::unique_ptr<IPlatformMapping>{new LoganvillePlatform(cpu_family_model, sockets_count)};
    case PCM::SRF:
    case PCM::GNR:
        return std::unique_ptr<IPlatformMapping>{new BirchStreamPlatform(cpu_family_model, sockets_count)};
    default:
        return nullptr;
    }
}

ccr* get_ccr(PCM* m, uint64_t& ccr)
{
    switch (m->getCPUFamilyModel())
    {
        case PCM::SKX:
            return new skx_ccr(ccr);
        case PCM::ICX:
        case PCM::SNOWRIDGE:
        case PCM::SPR:
        case PCM::EMR:
        case PCM::GRR:
        case PCM::SRF:
        case PCM::GNR:
            return new icx_ccr(ccr);
        default:
            cerr << m->getCPUFamilyModelString() << " is not supported! Program aborted" << endl;
            exit(EXIT_FAILURE);
    }
}

int iio_evt_parse_handler(evt_cb_type cb_type, void *cb_ctx, counter &base_ctr, std::map<std::string, uint32_t> &ofm, std::string key, uint64 numValue)
{
    iio_evt_parse_context *context = (iio_evt_parse_context *)cb_ctx;
    PCM *m = context->m;

    if (cb_type == EVT_LINE_START) //this event will be called per line(start)
    {
        context->ctr.ccr = 0;
    }
    else if (cb_type == EVT_LINE_FIELD) //this event will be called per field of line
    {
        std::unique_ptr<ccr> pccr(get_ccr(m, context->ctr.ccr));
        switch (ofm[key])
        {
            case PCM::OPCODE:
                break;
            case PCM::EVENT_SELECT:
                pccr->set_event_select(numValue);
                break;
            case PCM::UMASK:
                pccr->set_umask(numValue);
                break;
            case PCM::RESET:
                pccr->set_reset(numValue);
                break;
            case PCM::EDGE_DET:
                pccr->set_edge(numValue);
                break;
            case PCM::IGNORED:
                break;
            case PCM::OVERFLOW_ENABLE:
                pccr->set_ov_en(numValue);
                break;
            case PCM::ENABLE:
                pccr->set_enable(numValue);
                break;
            case PCM::INVERT:
                pccr->set_invert(numValue);
                break;
            case PCM::THRESH:
                pccr->set_thresh(numValue);
                break;
            case PCM::CH_MASK:
                pccr->set_ch_mask(numValue);
                break;
            case PCM::FC_MASK:
                pccr->set_fc_mask(numValue);
                break;
            case PCM::INVALID:
            default:
                std::cerr << "Field in -o file not recognized. The key is: " << key << "\n";
                return -1;
        }
    }
    else if(cb_type == EVT_LINE_COMPLETE) //this event will be called every line(end)
    {
        context->ctr.h_event_name = base_ctr.h_event_name;
        context->ctr.v_event_name = base_ctr.v_event_name;
        context->ctr.idx = base_ctr.idx;
        context->ctr.multiplier = base_ctr.multiplier;
        context->ctr.divider = base_ctr.divider;
        context->ctr.h_id = base_ctr.h_id;
        context->ctr.v_id = base_ctr.v_id;
        //std::cout << "line parse OK, ctrcfg=0x" << std::hex << context->ctr.ccr << ", h_event_name=" <<  base_ctr.h_event_name << ", v_event_name=" << base_ctr.v_event_name;
        //std::cout << ", h_id=0x" << std::hex << base_ctr.h_id << ", v_id=0x" << std::hex << base_ctr.v_id;
        //std::cout << ", idx=0x"<< std::hex << base_ctr.idx << ", multiplier=0x" << std::hex << base_ctr.multiplier << ", divider=0x" << std::hex << base_ctr.divider << std::dec << "\n";
        context->ctrs.push_back(context->ctr);
    }

    return 0;
}

result_content get_IIO_Samples(PCM *m, const std::vector<struct iio_stacks_on_socket>& iios, const struct iio_counter & ctr, uint32_t delay_ms)
{
    IIOCounterState *before, *after;
    uint64 rawEvents[4] = {0};
    auto ccrCopy = ctr.ccr;
    std::unique_ptr<ccr> pccr(get_ccr(m, ccrCopy));
    rawEvents[ctr.idx] = pccr->get_ccr_value();
    const int stacks_count = (int)m->getMaxNumOfIIOStacks();
    before = new IIOCounterState[iios.size() * stacks_count];
    after = new IIOCounterState[iios.size() * stacks_count];

    m->programIIOCounters(rawEvents);
    for (auto socket = iios.cbegin(); socket != iios.cend(); ++socket) {
        for (auto stack = socket->stacks.cbegin(); stack != socket->stacks.cend(); ++stack) {
            auto iio_unit_id = stack->iio_unit_id;
            uint32_t idx = (uint32_t)stacks_count * socket->socket_id + iio_unit_id;
            before[idx] = m->getIIOCounterState(socket->socket_id, iio_unit_id, ctr.idx);
        }
    }
    MySleepMs(delay_ms);
    for (auto socket = iios.cbegin(); socket != iios.cend(); ++socket) {
        for (auto stack = socket->stacks.cbegin(); stack != socket->stacks.cend(); ++stack) {
            auto iio_unit_id = stack->iio_unit_id;
            uint32_t idx = (uint32_t)stacks_count * socket->socket_id + iio_unit_id;
            after[idx] = m->getIIOCounterState(socket->socket_id, iio_unit_id, ctr.idx);
            uint64_t raw_result = getNumberOfEvents(before[idx], after[idx]);
            uint64_t trans_result = uint64_t (raw_result * ctr.multiplier / (double) ctr.divider * (1000 / (double) delay_ms));
            results[socket->socket_id][iio_unit_id][std::pair<h_id,v_id>(ctr.h_id,ctr.v_id)] = trans_result;
        }
    }
    deleteAndNullifyArray(before);
    deleteAndNullifyArray(after);
    return results;
}

void collect_data(PCM *m, const double delay, vector<struct iio_stacks_on_socket>& iios, vector<struct iio_counter>& ctrs)
{
    const uint32_t delay_ms = uint32_t(delay * 1000 / ctrs.size());
    for (auto counter = ctrs.begin(); counter != ctrs.end(); ++counter) {
        counter->data.clear();
        result_content sample = get_IIO_Samples(m, iios, *counter, delay_ms);
        counter->data.push_back(sample);
    }
}

void print_PCIeMapping(const std::vector<struct iio_stacks_on_socket>& iios, const PCIDB & pciDB, std::ostream& stream)
{
    uint32_t header_width = 100;
    string row;
    vector<string> buffer;

    for (auto it = iios.begin(); it != iios.end(); ++it) {
        row = "Socket " + std::to_string((*it).socket_id);
        buffer.push_back(row);
        for (auto & stack : it->stacks) {
            std::stringstream ss;
            ss << "\t" << stack.stack_name << " domain 0x" << std::hex << std::setfill('0') << std::setw(4) << stack.domain
               << "; root bus: 0x" << std::setfill('0') << std::setw(2) << (int)stack.busno << "\tflipped: " << (stack.flipped ? "true" : "false");
            buffer.push_back(ss.str());
            for (auto& part : stack.parts) {
                vector<struct pci> pp = part.child_pci_devs;
                uint8_t level = 5;
                for (std::vector<struct pci>::const_iterator iunit = pp.begin(); iunit != pp.end(); ++iunit) {
                    row = build_pci_header(pciDB, header_width, *iunit, -1, level);
                    buffer.push_back(row);
                    if (iunit->hasChildDevices()) {
                        build_pci_tree(buffer, pciDB, header_width, *iunit, -1, level + 1);
                    } else if (iunit->header_type == 1) {
                        level++;
                    }
                }
            }
        }
    }
    display(buffer, stream);
}

void print_usage(const string& progname)
{
    cout << "\n Usage: \n " << progname << " --help | [interval] [options] \n";
    cout << "   <interval>                           => time interval in seconds (floating point number is accepted)\n";
    cout << "                                        to sample performance counters.\n";
    cout << "                                        If not specified - 3.0 is used\n";
    cout << " Supported <options> are: \n";
    cout << "  -h    | --help  | /h               => print this help and exit\n";
    cout << "  -silent                            => silence information output and print only measurements\n";
    cout << "  --version                          => print application version\n";
    cout << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                        to a file, in case filename is provided\n";
    cout << "  -csv-delimiter=<value>  | /csv-delimiter=<value>   => set custom csv delimiter\n";
    cout << "  -human-readable | /human-readable  => use human readable format for output (for csv only)\n";
    cout << "  -root-port | /root-port            => add root port devices to output (for csv only)\n";
    cout << "  -list | --list                     => provide platform topology info\n";
    cout << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    cout << " Examples:\n";
    cout << "  " << progname << " 1.0 -i=10             => print counters every second 10 times and exit\n";
    cout << "  " << progname << " 0.5 -csv=test.log     => twice a second save counter values to test.log in CSV format\n";
    cout << "  " << progname << " -csv -human-readable  => every 3 second print counters in human-readable CSV format\n";
    cout << "\n";
}

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    if (print_version(argc, argv))
        exit(EXIT_SUCCESS);

    null_stream nullStream;
    check_and_set_silent(argc, argv, nullStream);

    std::cout << "\n Intel(r) Performance Counter Monitor " << PCM_VERSION << "\n";
    std::cout << "\n This utility measures IIO information\n\n";

    string program = string(argv[0]);

    vector<struct iio_counter> counters;
    bool csv = false;
    bool human_readable = false;
    bool show_root_port = false;
    std::string csv_delimiter = ",";
    std::string output_file;
    double delay = PCM_DELAY_DEFAULT;
    bool list = false;
    MainLoop mainLoop;
    iio_evt_parse_context evt_ctx;
    // Map with metrics names.
    map<string,std::pair<h_id,std::map<string,v_id>>> nameMap;

    while (argc > 1) {
        argv++;
        argc--;
        std::string arg_value;
        if (check_argument_equals(*argv, {"--help", "-h", "/h"})) {
            print_usage(program);
            exit(EXIT_FAILURE);
        }
        else if (check_argument_equals(*argv, {"-silent", "/silent"})) {
            // handled in check_and_set_silent
            continue;
        }
        else if (extract_argument_value(*argv, {"-csv-delimiter", "/csv-delimiter"}, arg_value)) {
            csv_delimiter = std::move(arg_value);
        }
        else if (check_argument_equals(*argv, {"-csv", "/csv"})) {
            csv = true;
        }
        else if (extract_argument_value(*argv, {"-csv", "/csv"}, arg_value)) {
            csv = true;
            output_file = std::move(arg_value);
        }
        else if (check_argument_equals(*argv, {"-human-readable", "/human-readable"})) {
            human_readable = true;
        }
        else if (check_argument_equals(*argv, {"-list", "--list"})) {
            list = true;
        }
        else if (check_argument_equals(*argv, {"-root-port", "/root-port"})) {
            show_root_port = true;
        }
        else if (mainLoop.parseArg(*argv)) {
            continue;
        }
        else {
            delay = parse_delay(*argv, program, (print_usage_func)print_usage);
            continue;
        }
    }

    set_signal_handlers();

    print_cpu_details();

    PCM * m = PCM::getInstance();

    PCIDB pciDB;
    load_PCIDB(pciDB);

    auto mapping = IPlatformMapping::getPlatformMapping(m->getCPUFamilyModel(), m->getNumSockets());
    if (!mapping) {
        cerr << "Failed to discover pci tree: unknown platform" << endl;
        exit(EXIT_FAILURE);
    }

    std::vector<struct iio_stacks_on_socket> iios;
    if (!mapping->pciTreeDiscover(iios)) {
        exit(EXIT_FAILURE);
    }

    std::ostream* output = &std::cout;
    std::fstream file_stream;
    if (!output_file.empty()) {
        file_stream.open(output_file.c_str(), std::ios_base::out);
        output = &file_stream;
    }

    if (list) {
        print_PCIeMapping(iios, pciDB, *output);
        return 0;
    }

    string ev_file_name;
    if (m->IIOEventsAvailable())
    {
        ev_file_name = "opCode-" + std::to_string(m->getCPUFamily()) + "-" + std::to_string(m->getInternalCPUModel()) + ".txt";
    }
    else
    {
        cerr << "This CPU is not supported by PCM IIO tool! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    map<string,uint32_t> opcodeFieldMap;
    opcodeFieldMap["opcode"] = PCM::OPCODE;
    opcodeFieldMap["ev_sel"] = PCM::EVENT_SELECT;
    opcodeFieldMap["umask"] = PCM::UMASK;
    opcodeFieldMap["reset"] = PCM::RESET;
    opcodeFieldMap["edge_det"] = PCM::EDGE_DET;
    opcodeFieldMap["ignored"] = PCM::IGNORED;
    opcodeFieldMap["overflow_enable"] = PCM::OVERFLOW_ENABLE;
    opcodeFieldMap["en"] = PCM::ENABLE;
    opcodeFieldMap["invert"] = PCM::INVERT;
    opcodeFieldMap["thresh"] = PCM::THRESH;
    opcodeFieldMap["ch_mask"] = PCM::CH_MASK;
    opcodeFieldMap["fc_mask"] = PCM::FC_MASK;
    opcodeFieldMap["hname"] =PCM::H_EVENT_NAME;
    opcodeFieldMap["vname"] =PCM::V_EVENT_NAME;
    opcodeFieldMap["multiplier"] = PCM::MULTIPLIER;
    opcodeFieldMap["divider"] = PCM::DIVIDER;
    opcodeFieldMap["ctr"] = PCM::COUNTER_INDEX;

    evt_ctx.m = m;
    evt_ctx.ctrs.clear();//fill the ctrs by evt_handler call back func.

    try
    {
        load_events(ev_file_name, opcodeFieldMap, iio_evt_parse_handler, (void *)&evt_ctx, nameMap);
    }
    catch (std::exception & e)
    {
        std::cerr << "Error info:" << e.what() << "\n";
        std::cerr << "Event configure file have the problem and cause the program exit, please double check it!\n";
        exit(EXIT_FAILURE);
    }

#ifdef PCM_DEBUG
    print_nameMap(nameMap);
#endif

    results.resize(m->getNumSockets(), stack_content(m->getMaxNumOfIIOStacks(), ctr_data()));

    mainLoop([&]()
    {
        collect_data(m, delay, iios, evt_ctx.ctrs);
        vector<string> display_buffer = csv ?
            build_csv(iios, evt_ctx.ctrs, human_readable, show_root_port, csv_delimiter, nameMap) :
            build_display(iios, evt_ctx.ctrs, pciDB, nameMap);
        display(display_buffer, *output);
        return true;
    });

    file_stream.close();

    exit(EXIT_SUCCESS);
}
