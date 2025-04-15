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

extern result_content results;

typedef struct
{
    PCM *m;
    iio_counter ctr;
    vector<struct iio_counter> ctrs;
} iio_evt_parse_context;

vector<string> combine_stack_name_and_counter_names(string stack_name, const map<string,std::pair<h_id,std::map<string,v_id>>> &nameMap);

string build_pci_header(const PCIDB & pciDB, uint32_t column_width, const struct pci &p, int part = -1, uint32_t level = 0);

void build_pci_tree(vector<string> &buffer, const PCIDB & pciDB, uint32_t column_width, const struct pci &p, int part, uint32_t level = 0);

vector<string> build_display(vector<struct iio_stacks_on_socket>& iios, vector<struct iio_counter>& ctrs, const PCIDB& pciDB,
                             const map<string,std::pair<h_id,std::map<string,v_id>>> &nameMap);

std::string get_root_port_dev(const bool show_root_port, int part_id,  const pcm::iio_stack *stack);

vector<string> build_csv(vector<struct iio_stacks_on_socket>& iios, vector<struct iio_counter>& ctrs,
                         const bool human_readable, const bool show_root_port, const std::string& csv_delimiter,
                         const map<string,std::pair<h_id,std::map<string,v_id>>> &nameMap);

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

class IPlatformMapping10Nm: public IPlatformMapping {
private:
public:
    IPlatformMapping10Nm(int cpu_model, uint32_t sockets_count) : IPlatformMapping(cpu_model, sockets_count) {}
    ~IPlatformMapping10Nm() = default;
    bool getSadIdRootBusMap(uint32_t socket_id, std::map<uint8_t, uint8_t>& sad_id_bus_map);
};

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

// Mapping for Snowridge.
class JacobsvillePlatformMapping: public IPlatformMapping10Nm {
private:
public:
    JacobsvillePlatformMapping(int cpu_model, uint32_t sockets_count) : IPlatformMapping10Nm(cpu_model, sockets_count) {}
    ~JacobsvillePlatformMapping() = default;
    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios) override;
    bool JacobsvilleAccelerators(const std::pair<uint8_t, uint8_t>& sad_id_bus_pair, struct iio_stack& stack);
};

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

class Xeon6thNextGenPlatform: public IPlatformMapping {
private:
    bool getRootBuses(std::map<int, std::map<int, struct bdf>> &root_buses);
protected:
    virtual bool stackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket) = 0;
public:
    Xeon6thNextGenPlatform(int cpu_model, uint32_t sockets_count) : IPlatformMapping(cpu_model, sockets_count) {}
    virtual ~Xeon6thNextGenPlatform() = default;

    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios) override;
};

class BirchStreamPlatform: public Xeon6thNextGenPlatform {
private:
    bool isPcieStack(int unit);
    bool isRootHcStack(int unit);
    bool isPartHcStack(int unit);
    bool isUboxStack(int unit);

    bool birchStreamPciStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket);
    bool birchStreamAcceleratorStackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket);
protected:
    bool stackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket) override;
public:
    BirchStreamPlatform(int cpu_model, uint32_t sockets_count) : Xeon6thNextGenPlatform(cpu_model, sockets_count) {}
    ~BirchStreamPlatform() = default;
};

class KasseyvillePlatform: public Xeon6thNextGenPlatform {
private:
    bool stackProbe(int unit, const struct bdf &address, struct iio_stacks_on_socket &iio_on_socket);
    bool isUboxStack(int unit)
    {
        return SRF_UBOXA_SAD_BUS_ID == unit || SRF_UBOXB_SAD_BUS_ID == unit;
    }
public:
    KasseyvillePlatform(int cpu_model, uint32_t sockets_count) : Xeon6thNextGenPlatform(cpu_model, sockets_count) {}
    ~KasseyvillePlatform() = default;
};

int iio_evt_parse_handler(evt_cb_type cb_type, void *cb_ctx, counter &base_ctr, std::map<std::string, uint32_t> &ofm, std::string key, uint64 numValue);

result_content get_IIO_Samples(PCM *m, const std::vector<struct iio_stacks_on_socket>& iios, const struct iio_counter & ctr, uint32_t delay_ms);

void collect_data(PCM *m, const double delay, vector<struct iio_stacks_on_socket>& iios, vector<struct iio_counter>& ctrs);

void initializeIIOStructure( std::vector<struct iio_stacks_on_socket>& iios );

void fillOpcodeFieldMapForPCIeEvents(map<string,uint32_t>& opcodeFieldMap);

typedef map<string,std::pair<h_id,std::map<string,v_id>>> PCIeEventNameMap_t;

void setupPCIeEventContextAndNameMap( iio_evt_parse_context& evt_ctx, PCIeEventNameMap_t& nameMap);

bool initializeIIOCounters( std::vector<struct iio_stacks_on_socket>& iios, iio_evt_parse_context& evt_ctx, PCIeEventNameMap_t& nameMap );

