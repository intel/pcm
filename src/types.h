/*
Copyright (c) 2009-2018, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev
//

#ifndef CPUCounters_TYPES_H
#define CPUCounters_TYPES_H


/*!     \file types.h
        \brief Internal type and constant definitions
*/

#undef PCM_DEBUG

#include <iostream>
#include <istream>
#include <sstream>
#include <iomanip>

typedef unsigned long long uint64;
typedef signed long long int64;
typedef unsigned int uint32;
typedef signed int int32;


/*
        MSR addreses from
        "Intel 64 and IA-32 Architectures Software Developers Manual Volume 3B:
        System Programming Guide, Part 2", Appendix A "PERFORMANCE-MONITORING EVENTS"
*/

#define INST_RETIRED_ANY_ADDR           (0x309)
#define CPU_CLK_UNHALTED_THREAD_ADDR    (0x30A)
#define CPU_CLK_UNHALTED_REF_ADDR       (0x30B)
#define IA32_CR_PERF_GLOBAL_CTRL        (0x38F)
#define IA32_CR_FIXED_CTR_CTRL          (0x38D)
#define IA32_PERFEVTSEL0_ADDR           (0x186)
#define IA32_PERFEVTSEL1_ADDR           (IA32_PERFEVTSEL0_ADDR + 1)
#define IA32_PERFEVTSEL2_ADDR           (IA32_PERFEVTSEL0_ADDR + 2)
#define IA32_PERFEVTSEL3_ADDR           (IA32_PERFEVTSEL0_ADDR + 3)

#define PERF_MAX_FIXED_COUNTERS          (3)
#define PERF_MAX_CUSTOM_COUNTERS         (8)
#define PERF_MAX_COUNTERS               (PERF_MAX_FIXED_COUNTERS + PERF_MAX_CUSTOM_COUNTERS)

#define IA32_DEBUGCTL                   (0x1D9)

#define IA32_PMC0                       (0xC1)
#define IA32_PMC1                       (0xC1 + 1)
#define IA32_PMC2                       (0xC1 + 2)
#define IA32_PMC3                       (0xC1 + 3)

#define MSR_OFFCORE_RSP0               (0x1A6)
#define MSR_OFFCORE_RSP1               (0x1A7)

/* From Table B-5. of the above mentioned document */
#define PLATFORM_INFO_ADDR              (0xCE)

#define IA32_TIME_STAMP_COUNTER         (0x10)

// Event IDs

// Nehalem/Westmere on-core events
#define MEM_LOAD_RETIRED_L3_MISS_EVTNR  (0xCB)
#define MEM_LOAD_RETIRED_L3_MISS_UMASK  (0x10)

#define MEM_LOAD_RETIRED_L3_UNSHAREDHIT_EVTNR   (0xCB)
#define MEM_LOAD_RETIRED_L3_UNSHAREDHIT_UMASK   (0x04)

#define MEM_LOAD_RETIRED_L2_HITM_EVTNR  (0xCB)
#define MEM_LOAD_RETIRED_L2_HITM_UMASK  (0x08)

#define MEM_LOAD_RETIRED_L2_HIT_EVTNR   (0xCB)
#define MEM_LOAD_RETIRED_L2_HIT_UMASK   (0x02)

// Sandy Bridge on-core events

#define MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS_EVTNR (0xD4)
#define MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS_UMASK (0x02)

#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_NONE_EVTNR (0xD2)
#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_NONE_UMASK (0x08)

#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_HITM_EVTNR (0xD2)
#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_HITM_UMASK (0x04)

#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_EVTNR (0xD2)
#define MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_UMASK (0x07)

#define MEM_LOAD_UOPS_RETIRED_L2_HIT_EVTNR (0xD1)
#define MEM_LOAD_UOPS_RETIRED_L2_HIT_UMASK (0x02)

// Skylake on-core events

#define SKL_MEM_LOAD_RETIRED_L3_MISS_EVTNR (0xD1)
#define SKL_MEM_LOAD_RETIRED_L3_MISS_UMASK (0x20)

#define SKL_MEM_LOAD_RETIRED_L3_HIT_EVTNR (0xD1)
#define SKL_MEM_LOAD_RETIRED_L3_HIT_UMASK (0x04)

#define SKL_MEM_LOAD_RETIRED_L2_MISS_EVTNR (0xD1)
#define SKL_MEM_LOAD_RETIRED_L2_MISS_UMASK (0x10)

#define SKL_MEM_LOAD_RETIRED_L2_HIT_EVTNR (0xD1)
#define SKL_MEM_LOAD_RETIRED_L2_HIT_UMASK (0x02)

// architectural on-core events

#define ARCH_LLC_REFERENCE_EVTNR        (0x2E)
#define ARCH_LLC_REFERENCE_UMASK        (0x4F)

#define ARCH_LLC_MISS_EVTNR     (0x2E)
#define ARCH_LLC_MISS_UMASK     (0x41)

// Atom on-core events

#define ATOM_MEM_LOAD_RETIRED_L2_HIT_EVTNR   (0xCB)
#define ATOM_MEM_LOAD_RETIRED_L2_HIT_UMASK   (0x01)

#define ATOM_MEM_LOAD_RETIRED_L2_MISS_EVTNR   (0xCB)
#define ATOM_MEM_LOAD_RETIRED_L2_MISS_UMASK   (0x02)

#define ATOM_MEM_LOAD_RETIRED_L2_HIT_EVTNR   (0xCB)
#define ATOM_MEM_LOAD_RETIRED_L2_HIT_UMASK   (0x01)

#define ATOM_MEM_LOAD_RETIRED_L2_MISS_EVTNR   (0xCB)
#define ATOM_MEM_LOAD_RETIRED_L2_MISS_UMASK   (0x02)

#define ATOM_MEM_LOAD_RETIRED_L2_HIT_EVTNR   (0xCB)
#define ATOM_MEM_LOAD_RETIRED_L2_HIT_UMASK   (0x01)

#define ATOM_MEM_LOAD_RETIRED_L2_MISS_EVTNR   (0xCB)
#define ATOM_MEM_LOAD_RETIRED_L2_MISS_UMASK   (0x02)

// Offcore response events
#define OFFCORE_RESPONSE_0_EVTNR (0xB7)
#define OFFCORE_RESPONSE_1_EVTNR (0xBB)
#define OFFCORE_RESPONSE_0_UMASK (1)
#define OFFCORE_RESPONSE_1_UMASK (1)
/*
     For Nehalem(-EP) processors from Intel(r) 64 and IA-32 Architectures Software Developer's Manual
*/

// Uncore msrs

#define MSR_UNCORE_PERF_GLOBAL_CTRL_ADDR        (0x391)

#define MSR_UNCORE_PERFEVTSEL0_ADDR             (0x3C0)
#define MSR_UNCORE_PERFEVTSEL1_ADDR             (MSR_UNCORE_PERFEVTSEL0_ADDR + 1)
#define MSR_UNCORE_PERFEVTSEL2_ADDR             (MSR_UNCORE_PERFEVTSEL0_ADDR + 2)
#define MSR_UNCORE_PERFEVTSEL3_ADDR             (MSR_UNCORE_PERFEVTSEL0_ADDR + 3)
#define MSR_UNCORE_PERFEVTSEL4_ADDR             (MSR_UNCORE_PERFEVTSEL0_ADDR + 4)
#define MSR_UNCORE_PERFEVTSEL5_ADDR             (MSR_UNCORE_PERFEVTSEL0_ADDR + 5)
#define MSR_UNCORE_PERFEVTSEL6_ADDR             (MSR_UNCORE_PERFEVTSEL0_ADDR + 6)
#define MSR_UNCORE_PERFEVTSEL7_ADDR             (MSR_UNCORE_PERFEVTSEL0_ADDR + 7)


#define MSR_UNCORE_PMC0                         (0x3B0)
#define MSR_UNCORE_PMC1                         (MSR_UNCORE_PMC0 + 1)
#define MSR_UNCORE_PMC2                         (MSR_UNCORE_PMC0 + 2)
#define MSR_UNCORE_PMC3                         (MSR_UNCORE_PMC0 + 3)
#define MSR_UNCORE_PMC4                         (MSR_UNCORE_PMC0 + 4)
#define MSR_UNCORE_PMC5                         (MSR_UNCORE_PMC0 + 5)
#define MSR_UNCORE_PMC6                         (MSR_UNCORE_PMC0 + 6)
#define MSR_UNCORE_PMC7                         (MSR_UNCORE_PMC0 + 7)

// Uncore event IDs

#define UNC_QMC_WRITES_FULL_ANY_EVTNR           (0x2F)
#define UNC_QMC_WRITES_FULL_ANY_UMASK           (0x07)

#define UNC_QMC_NORMAL_READS_ANY_EVTNR          (0x2C)
#define UNC_QMC_NORMAL_READS_ANY_UMASK          (0x07)

#define UNC_QHL_REQUESTS_EVTNR                  (0x20)

#define UNC_QHL_REQUESTS_IOH_READS_UMASK        (0x01)
#define UNC_QHL_REQUESTS_IOH_WRITES_UMASK       (0x02)
#define UNC_QHL_REQUESTS_REMOTE_READS_UMASK     (0x04)
#define UNC_QHL_REQUESTS_REMOTE_WRITES_UMASK    (0x08)
#define UNC_QHL_REQUESTS_LOCAL_READS_UMASK      (0x10)
#define UNC_QHL_REQUESTS_LOCAL_WRITES_UMASK     (0x20)

/*
        From "Intel(r) Xeon(r) Processor 7500 Series Uncore Programming Guide"
*/

// Beckton uncore event IDs

#define U_MSR_PMON_GLOBAL_CTL                   (0x0C00)

#define MB0_MSR_PERF_GLOBAL_CTL                 (0x0CA0)
#define MB0_MSR_PMU_CNT_0                       (0x0CB1)
#define MB0_MSR_PMU_CNT_CTL_0                   (0x0CB0)
#define MB0_MSR_PMU_CNT_1                       (0x0CB3)
#define MB0_MSR_PMU_CNT_CTL_1                   (0x0CB2)
#define MB0_MSR_PMU_ZDP_CTL_FVC                 (0x0CAB)


#define MB1_MSR_PERF_GLOBAL_CTL                 (0x0CE0)
#define MB1_MSR_PMU_CNT_0                       (0x0CF1)
#define MB1_MSR_PMU_CNT_CTL_0                   (0x0CF0)
#define MB1_MSR_PMU_CNT_1                       (0x0CF3)
#define MB1_MSR_PMU_CNT_CTL_1                   (0x0CF2)
#define MB1_MSR_PMU_ZDP_CTL_FVC                 (0x0CEB)

#define BB0_MSR_PERF_GLOBAL_CTL                 (0x0C20)
#define BB0_MSR_PERF_CNT_1                      (0x0C33)
#define BB0_MSR_PERF_CNT_CTL_1                  (0x0C32)

#define BB1_MSR_PERF_GLOBAL_CTL                 (0x0C60)
#define BB1_MSR_PERF_CNT_1                      (0x0C73)
#define BB1_MSR_PERF_CNT_CTL_1                  (0x0C72)

#define R_MSR_PMON_CTL0 (0x0E10)
#define R_MSR_PMON_CTR0 (0x0E11)
#define R_MSR_PMON_CTL1 (0x0E12)
#define R_MSR_PMON_CTR1 (0x0E13)
#define R_MSR_PMON_CTL2 (0x0E14)
#define R_MSR_PMON_CTR2 (0x0E15)
#define R_MSR_PMON_CTL3 (0x0E16)
#define R_MSR_PMON_CTR3 (0x0E17)
#define R_MSR_PMON_CTL4 (0x0E18)
#define R_MSR_PMON_CTR4 (0x0E19)
#define R_MSR_PMON_CTL5 (0x0E1A)
#define R_MSR_PMON_CTR5 (0x0E1B)
#define R_MSR_PMON_CTL6 (0x0E1C)
#define R_MSR_PMON_CTR6 (0x0E1D)
#define R_MSR_PMON_CTL7 (0x0E1E)
#define R_MSR_PMON_CTR7 (0x0E1F)
#define R_MSR_PMON_CTL8 (0x0E30)
#define R_MSR_PMON_CTR8 (0x0E31)
#define R_MSR_PMON_CTL9 (0x0E32)
#define R_MSR_PMON_CTR9 (0x0E33)
#define R_MSR_PMON_CTL10 (0x0E34)
#define R_MSR_PMON_CTR10 (0x0E35)
#define R_MSR_PMON_CTL11 (0x0E36)
#define R_MSR_PMON_CTR11 (0x0E37)
#define R_MSR_PMON_CTL12 (0x0E38)
#define R_MSR_PMON_CTR12 (0x0E39)
#define R_MSR_PMON_CTL13 (0x0E3A)
#define R_MSR_PMON_CTR13 (0x0E3B)
#define R_MSR_PMON_CTL14 (0x0E3C)
#define R_MSR_PMON_CTR14 (0x0E3D)
#define R_MSR_PMON_CTL15 (0x0E3E)
#define R_MSR_PMON_CTR15 (0x0E3F)

#define R_MSR_PORT0_IPERF_CFG0 (0x0E04)
#define R_MSR_PORT1_IPERF_CFG0 (0x0E05)
#define R_MSR_PORT2_IPERF_CFG0 (0x0E06)
#define R_MSR_PORT3_IPERF_CFG0 (0x0E07)
#define R_MSR_PORT4_IPERF_CFG0 (0x0E08)
#define R_MSR_PORT5_IPERF_CFG0 (0x0E09)
#define R_MSR_PORT6_IPERF_CFG0 (0x0E0A)
#define R_MSR_PORT7_IPERF_CFG0 (0x0E0B)

#define R_MSR_PORT0_IPERF_CFG1 (0x0E24)
#define R_MSR_PORT1_IPERF_CFG1 (0x0E25)
#define R_MSR_PORT2_IPERF_CFG1 (0x0E26)
#define R_MSR_PORT3_IPERF_CFG1 (0x0E27)
#define R_MSR_PORT4_IPERF_CFG1 (0x0E28)
#define R_MSR_PORT5_IPERF_CFG1 (0x0E29)
#define R_MSR_PORT6_IPERF_CFG1 (0x0E2A)
#define R_MSR_PORT7_IPERF_CFG1 (0x0E2B)

#define R_MSR_PMON_GLOBAL_CTL_7_0 (0x0E00)
#define R_MSR_PMON_GLOBAL_CTL_15_8 (0x0E20)

#define W_MSR_PMON_GLOBAL_CTL    (0xC80)
#define W_MSR_PMON_FIXED_CTR_CTL (0x395)
#define W_MSR_PMON_FIXED_CTR     (0x394)

/*
 * Platform QoS MSRs
 */

#define IA32_PQR_ASSOC (0xc8f)
#define IA32_QM_EVTSEL (0xc8d)
#define IA32_QM_CTR (0xc8e)

#define PCM_INVALID_QOS_MONITORING_DATA ((std::numeric_limits<uint64>::max)())

/* \brief Event Select Register format

        According to
        "Intel 64 and IA-32 Architectures Software Developers Manual Volume 3B:
        System Programming Guide, Part 2", Figure 30-6. Layout of IA32_PERFEVTSELx
        MSRs Supporting Architectural Performance Monitoring Version 3
*/
struct EventSelectRegister
{
    union
    {
        struct
        {
            uint64 event_select : 8;
            uint64 umask : 8;
            uint64 usr : 1;
            uint64 os : 1;
            uint64 edge : 1;
            uint64 pin_control : 1;
            uint64 apic_int : 1;
            uint64 any_thread : 1;
            uint64 enable : 1;
            uint64 invert : 1;
            uint64 cmask : 8;
            uint64 in_tx : 1;
            uint64 in_txcp : 1;
            uint64 reservedX : 30;
        } fields;
        uint64 value;
    };

    EventSelectRegister() : value(0) {}
};


/* \brief Fixed Event Control Register format

        According to
        "Intel 64 and IA-32 Architectures Software Developers Manual Volume 3B:
        System Programming Guide, Part 2", Figure 30-7. Layout of
        IA32_FIXED_CTR_CTRL MSR Supporting Architectural Performance Monitoring Version 3
*/
struct FixedEventControlRegister
{
    union
    {
        struct
        {
            // CTR0
            uint64 os0 : 1;
            uint64 usr0 : 1;
            uint64 any_thread0 : 1;
            uint64 enable_pmi0 : 1;
            // CTR1
            uint64 os1 : 1;
            uint64 usr1 : 1;
            uint64 any_thread1 : 1;
            uint64 enable_pmi1 : 1;
            // CTR2
            uint64 os2 : 1;
            uint64 usr2 : 1;
            uint64 any_thread2 : 1;
            uint64 enable_pmi2 : 1;

            uint64 reserved1 : 52;
        } fields;
        uint64 value;
    };
};

inline std::ostream & operator << (std::ostream & o, const FixedEventControlRegister & reg)
{
    o << "os0\t\t" << reg.fields.os0 << std::endl;
    o << "usr0\t\t" << reg.fields.usr0 << std::endl;
    o << "any_thread0\t" << reg.fields.any_thread0 << std::endl;
    o << "enable_pmi0\t" << reg.fields.enable_pmi0 << std::endl;

    o << "os1\t\t" << reg.fields.os1 << std::endl;
    o << "usr1\t\t" << reg.fields.usr1 << std::endl;
    o << "any_thread1\t" << reg.fields.any_thread1 << std::endl;
    o << "enable_pmi10\t" << reg.fields.enable_pmi1 << std::endl;

    o << "os2\t\t" << reg.fields.os2 << std::endl;
    o << "usr2\t\t" << reg.fields.usr2 << std::endl;
    o << "any_thread2\t" << reg.fields.any_thread2 << std::endl;
    o << "enable_pmi2\t" << reg.fields.enable_pmi2 << std::endl;

    o << "reserved1\t" << reg.fields.reserved1 << std::endl;
    return o;
}

// UNCORE COUNTER CONTROL

/* \brief Uncore Event Select Register Register format

        According to
        "Intel 64 and IA-32 Architectures Software Developers Manual Volume 3B:
        System Programming Guide, Part 2", Figure 30-20. Layout of MSR_UNCORE_PERFEVTSELx MSRs
*/
struct UncoreEventSelectRegister
{
    union
    {
        struct
        {
            uint64 event_select : 8;
            uint64 umask : 8;
            uint64 reserved1 : 1;
            uint64 occ_ctr_rst : 1;
            uint64 edge : 1;
            uint64 reserved2 : 1;
            uint64 enable_pmi : 1;
            uint64 reserved3 : 1;
            uint64 enable : 1;
            uint64 invert : 1;
            uint64 cmask : 8;
            uint64 reservedx : 32;
        } fields;
        uint64 value;
    };
};

/* \brief Beckton Uncore PMU ZDP FVC Control Register format

        From "Intel(r) Xeon(r) Processor 7500 Series Uncore Programming Guide"
        Table 2-80. M_MSR_PMU_ZDP_CTL_FVC Register - Field Definitions
*/
struct BecktonUncorePMUZDPCTLFVCRegister
{
    union
    {
        struct
        {
            uint64 fvid : 5;
            uint64 bcmd : 3;
            uint64 resp : 3;
            uint64 evnt0 : 3;
            uint64 evnt1 : 3;
            uint64 evnt2 : 3;
            uint64 evnt3 : 3;
            uint64 pbox_init_err : 1;
        } fields; // nehalem-ex version
        struct
        {
            uint64 fvid : 6;
            uint64 bcmd : 3;
            uint64 resp : 3;
            uint64 evnt0 : 3;
            uint64 evnt1 : 3;
            uint64 evnt2 : 3;
            uint64 evnt3 : 3;
            uint64 pbox_init_err : 1;
        } fields_wsm; // westmere-ex version
        uint64 value;
    };
};

/* \brief Beckton Uncore PMU Counter Control Register format

        From "Intel(r) Xeon(r) Processor 7500 Series Uncore Programming Guide"
        Table 2-67. M_MSR_PMU_CNT_CTL{5-0} Register - Field Definitions
*/
struct BecktonUncorePMUCNTCTLRegister
{
    union
    {
        struct
        {
            uint64 en : 1;
            uint64 pmi_en : 1;
            uint64 count_mode : 2;
            uint64 storage_mode : 2;
            uint64 wrap_mode : 1;
            uint64 flag_mode : 1;
            uint64 rsv1 : 1;
            uint64 inc_sel : 5;
            uint64 rsv2 : 5;
            uint64 set_flag_sel : 3;
        } fields;
        uint64 value;
    };
};

#define MSR_SMI_COUNT (0x34)

/* \brief Sandy Bridge energy counters
*/

#define MSR_PKG_ENERGY_STATUS (0x611)
#define MSR_RAPL_POWER_UNIT   (0x606)
#define MSR_PKG_POWER_INFO    (0x614)

#define PCM_INTEL_PCI_VENDOR_ID (0x8086)
#define PCM_PCI_VENDOR_ID_OFFSET (0)

// server PCICFG uncore counters

#define JKTIVT_MC0_CH0_REGISTER_DEV_ADDR (16)
#define JKTIVT_MC0_CH1_REGISTER_DEV_ADDR (16)
#define JKTIVT_MC0_CH2_REGISTER_DEV_ADDR (16)
#define JKTIVT_MC0_CH3_REGISTER_DEV_ADDR (16)
#define JKTIVT_MC0_CH0_REGISTER_FUNC_ADDR (4)
#define JKTIVT_MC0_CH1_REGISTER_FUNC_ADDR (5)
#define JKTIVT_MC0_CH2_REGISTER_FUNC_ADDR (0)
#define JKTIVT_MC0_CH3_REGISTER_FUNC_ADDR (1)

#define JKTIVT_MC1_CH0_REGISTER_DEV_ADDR (30)
#define JKTIVT_MC1_CH1_REGISTER_DEV_ADDR (30)
#define JKTIVT_MC1_CH2_REGISTER_DEV_ADDR (30)
#define JKTIVT_MC1_CH3_REGISTER_DEV_ADDR (30)
#define JKTIVT_MC1_CH0_REGISTER_FUNC_ADDR (4)
#define JKTIVT_MC1_CH1_REGISTER_FUNC_ADDR (5)
#define JKTIVT_MC1_CH2_REGISTER_FUNC_ADDR (0)
#define JKTIVT_MC1_CH3_REGISTER_FUNC_ADDR (1)

#define HSX_MC0_CH0_REGISTER_DEV_ADDR (20)
#define HSX_MC0_CH1_REGISTER_DEV_ADDR (20)
#define HSX_MC0_CH2_REGISTER_DEV_ADDR (21)
#define HSX_MC0_CH3_REGISTER_DEV_ADDR (21)
#define HSX_MC0_CH0_REGISTER_FUNC_ADDR (0)
#define HSX_MC0_CH1_REGISTER_FUNC_ADDR (1)
#define HSX_MC0_CH2_REGISTER_FUNC_ADDR (0)
#define HSX_MC0_CH3_REGISTER_FUNC_ADDR (1)

#define HSX_MC1_CH0_REGISTER_DEV_ADDR (23)
#define HSX_MC1_CH1_REGISTER_DEV_ADDR (23)
#define HSX_MC1_CH2_REGISTER_DEV_ADDR (24)
#define HSX_MC1_CH3_REGISTER_DEV_ADDR (24)
#define HSX_MC1_CH0_REGISTER_FUNC_ADDR (0)
#define HSX_MC1_CH1_REGISTER_FUNC_ADDR (1)
#define HSX_MC1_CH2_REGISTER_FUNC_ADDR (0)
#define HSX_MC1_CH3_REGISTER_FUNC_ADDR (1)

#define KNL_MC0_CH0_REGISTER_DEV_ADDR (8)
#define KNL_MC0_CH1_REGISTER_DEV_ADDR (8)
#define KNL_MC0_CH2_REGISTER_DEV_ADDR (8)
#define KNL_MC0_CH0_REGISTER_FUNC_ADDR (2)
#define KNL_MC0_CH1_REGISTER_FUNC_ADDR (3)
#define KNL_MC0_CH2_REGISTER_FUNC_ADDR (4)

#define SKX_MC0_CH0_REGISTER_DEV_ADDR (10)
#define SKX_MC0_CH1_REGISTER_DEV_ADDR (10)
#define SKX_MC0_CH2_REGISTER_DEV_ADDR (11)
#define SKX_MC0_CH3_REGISTER_DEV_ADDR (-1) //Does not exist
#define SKX_MC0_CH0_REGISTER_FUNC_ADDR (2)
#define SKX_MC0_CH1_REGISTER_FUNC_ADDR (6)
#define SKX_MC0_CH2_REGISTER_FUNC_ADDR (2)
#define SKX_MC0_CH3_REGISTER_FUNC_ADDR (-1) //Does not exist

#define SKX_MC1_CH0_REGISTER_DEV_ADDR (12)
#define SKX_MC1_CH1_REGISTER_DEV_ADDR (12)
#define SKX_MC1_CH2_REGISTER_DEV_ADDR (13)
#define SKX_MC1_CH3_REGISTER_DEV_ADDR (-1) //Does not exist
#define SKX_MC1_CH0_REGISTER_FUNC_ADDR (2)
#define SKX_MC1_CH1_REGISTER_FUNC_ADDR (6)
#define SKX_MC1_CH2_REGISTER_FUNC_ADDR (2)
#define SKX_MC1_CH3_REGISTER_FUNC_ADDR (-1) //Does not exist


#define KNL_MC1_CH0_REGISTER_DEV_ADDR (9)
#define KNL_MC1_CH1_REGISTER_DEV_ADDR (9)
#define KNL_MC1_CH2_REGISTER_DEV_ADDR (9)
#define KNL_MC1_CH0_REGISTER_FUNC_ADDR (2)
#define KNL_MC1_CH1_REGISTER_FUNC_ADDR (3)
#define KNL_MC1_CH2_REGISTER_FUNC_ADDR (4)

#define KNL_EDC0_ECLK_REGISTER_DEV_ADDR (24)
#define KNL_EDC0_ECLK_REGISTER_FUNC_ADDR (2)
#define KNL_EDC1_ECLK_REGISTER_DEV_ADDR (25)
#define KNL_EDC1_ECLK_REGISTER_FUNC_ADDR (2)
#define KNL_EDC2_ECLK_REGISTER_DEV_ADDR (26)
#define KNL_EDC2_ECLK_REGISTER_FUNC_ADDR (2)
#define KNL_EDC3_ECLK_REGISTER_DEV_ADDR (27)
#define KNL_EDC3_ECLK_REGISTER_FUNC_ADDR (2)
#define KNL_EDC4_ECLK_REGISTER_DEV_ADDR (28)
#define KNL_EDC4_ECLK_REGISTER_FUNC_ADDR (2)
#define KNL_EDC5_ECLK_REGISTER_DEV_ADDR (29)
#define KNL_EDC5_ECLK_REGISTER_FUNC_ADDR (2)
#define KNL_EDC6_ECLK_REGISTER_DEV_ADDR (30)
#define KNL_EDC6_ECLK_REGISTER_FUNC_ADDR (2)
#define KNL_EDC7_ECLK_REGISTER_DEV_ADDR (31)
#define KNL_EDC7_ECLK_REGISTER_FUNC_ADDR (2)

/**
 * XPF_ for Xeons: SNB, IVT, HSX, BDW, etc.
 * KNX_ for Xeon Phi (Knights *) processors
 */
#define XPF_MC_CH_PCI_PMON_BOX_CTL_ADDR (0x0F4)
#define KNX_MC_CH_PCI_PMON_BOX_CTL_ADDR (0xB30)
#define KNX_EDC_CH_PCI_PMON_BOX_CTL_ADDR (0xA30)

//! for Xeons
#define XPF_MC_CH_PCI_PMON_FIXED_CTL_ADDR (0x0F0)
#define XPF_MC_CH_PCI_PMON_CTL3_ADDR (0x0E4)
#define XPF_MC_CH_PCI_PMON_CTL2_ADDR (0x0E0)
#define XPF_MC_CH_PCI_PMON_CTL1_ADDR (0x0DC)
#define XPF_MC_CH_PCI_PMON_CTL0_ADDR (0x0D8)

//! KNL IMC
#define KNX_MC_CH_PCI_PMON_FIXED_CTL_ADDR (0xB44)
#define KNX_MC_CH_PCI_PMON_CTL3_ADDR (0xB2C)
#define KNX_MC_CH_PCI_PMON_CTL2_ADDR (0xB28)
#define KNX_MC_CH_PCI_PMON_CTL1_ADDR (0xB24)
#define KNX_MC_CH_PCI_PMON_CTL0_ADDR (0xB20)

//! KNL EDC ECLK
#define KNX_EDC_CH_PCI_PMON_FIXED_CTL_ADDR (0xA44)
#define KNX_EDC_CH_PCI_PMON_CTL3_ADDR (0xA2C)
#define KNX_EDC_CH_PCI_PMON_CTL2_ADDR (0xA28)
#define KNX_EDC_CH_PCI_PMON_CTL1_ADDR (0xA24)
#define KNX_EDC_CH_PCI_PMON_CTL0_ADDR (0xA20)
#define KNX_EDC_ECLK_PMON_UNIT_CTL_REG (0xA30)

//! for Xeons
#define XPF_MC_CH_PCI_PMON_FIXED_CTR_ADDR (0x0D0)
#define XPF_MC_CH_PCI_PMON_CTR3_ADDR (0x0B8)
#define XPF_MC_CH_PCI_PMON_CTR2_ADDR (0x0B0)
#define XPF_MC_CH_PCI_PMON_CTR1_ADDR (0x0A8)
#define XPF_MC_CH_PCI_PMON_CTR0_ADDR (0x0A0)

//! for KNL IMC
#define KNX_MC_CH_PCI_PMON_FIXED_CTR_ADDR (0xB3C)
#define KNX_MC_CH_PCI_PMON_CTR3_ADDR (0xB18)
#define KNX_MC_CH_PCI_PMON_CTR2_ADDR (0xB10)
#define KNX_MC_CH_PCI_PMON_CTR1_ADDR (0xB08)
#define KNX_MC_CH_PCI_PMON_CTR0_ADDR (0xB00)

//! for KNL EDC ECLK
#define KNX_EDC_CH_PCI_PMON_FIXED_CTR_ADDR (0xA3C)
#define KNX_EDC_CH_PCI_PMON_CTR3_ADDR (0xA18)
#define KNX_EDC_CH_PCI_PMON_CTR2_ADDR (0xA10)
#define KNX_EDC_CH_PCI_PMON_CTR1_ADDR (0xA08)
#define KNX_EDC_CH_PCI_PMON_CTR0_ADDR (0xA00)

#define JKTIVT_QPI_PORT0_REGISTER_DEV_ADDR  (8)
#define JKTIVT_QPI_PORT0_REGISTER_FUNC_ADDR (2)
#define JKTIVT_QPI_PORT1_REGISTER_DEV_ADDR  (9)
#define JKTIVT_QPI_PORT1_REGISTER_FUNC_ADDR (2)
#define JKTIVT_QPI_PORT2_REGISTER_DEV_ADDR  (24)
#define JKTIVT_QPI_PORT2_REGISTER_FUNC_ADDR (2)

#define HSX_QPI_PORT0_REGISTER_DEV_ADDR  (8)
#define HSX_QPI_PORT0_REGISTER_FUNC_ADDR (2)
#define HSX_QPI_PORT1_REGISTER_DEV_ADDR  (9)
#define HSX_QPI_PORT1_REGISTER_FUNC_ADDR (2)
#define HSX_QPI_PORT2_REGISTER_DEV_ADDR  (10)
#define HSX_QPI_PORT2_REGISTER_FUNC_ADDR (2)

#define SKX_QPI_PORT0_REGISTER_DEV_ADDR  (14)
#define SKX_QPI_PORT0_REGISTER_FUNC_ADDR (0)
#define SKX_QPI_PORT1_REGISTER_DEV_ADDR  (15)
#define SKX_QPI_PORT1_REGISTER_FUNC_ADDR (0)
#define SKX_QPI_PORT2_REGISTER_DEV_ADDR  (16)
#define SKX_QPI_PORT2_REGISTER_FUNC_ADDR (0)

#define QPI_PORT0_MISC_REGISTER_FUNC_ADDR (0)
#define QPI_PORT1_MISC_REGISTER_FUNC_ADDR (0)
#define QPI_PORT2_MISC_REGISTER_FUNC_ADDR (0)

#define SKX_M2M_0_REGISTER_DEV_ADDR  (8)
#define SKX_M2M_0_REGISTER_FUNC_ADDR (0)
#define SKX_M2M_1_REGISTER_DEV_ADDR  (9)
#define SKX_M2M_1_REGISTER_FUNC_ADDR (0)

#define M2M_PCI_PMON_BOX_CTL_ADDR (0x258)

#define M2M_PCI_PMON_CTL0_ADDR (0x228)
#define M2M_PCI_PMON_CTL1_ADDR (0x230)
#define M2M_PCI_PMON_CTL2_ADDR (0x238)
#define M2M_PCI_PMON_CTL3_ADDR (0x240)

#define M2M_PCI_PMON_CTR0_ADDR (0x200)
#define M2M_PCI_PMON_CTR1_ADDR (0x208)
#define M2M_PCI_PMON_CTR2_ADDR (0x210)
#define M2M_PCI_PMON_CTR3_ADDR (0x218)

#define PCM_INVALID_DEV_ADDR (~(uint32)0UL)
#define PCM_INVALID_FUNC_ADDR (~(uint32)0UL)

#define Q_P_PCI_PMON_BOX_CTL_ADDR (0x0F4)

#define Q_P_PCI_PMON_CTL3_ADDR (0x0E4)
#define Q_P_PCI_PMON_CTL2_ADDR (0x0E0)
#define Q_P_PCI_PMON_CTL1_ADDR (0x0DC)
#define Q_P_PCI_PMON_CTL0_ADDR (0x0D8)

#define Q_P_PCI_PMON_CTR3_ADDR (0x0B8)
#define Q_P_PCI_PMON_CTR2_ADDR (0x0B0)
#define Q_P_PCI_PMON_CTR1_ADDR (0x0A8)
#define Q_P_PCI_PMON_CTR0_ADDR (0x0A0)

#define QPI_RATE_STATUS_ADDR (0x0D4)

#define U_L_PCI_PMON_BOX_CTL_ADDR (0x378)

#define U_L_PCI_PMON_CTL3_ADDR (0x368)
#define U_L_PCI_PMON_CTL2_ADDR (0x360)
#define U_L_PCI_PMON_CTL1_ADDR (0x358)
#define U_L_PCI_PMON_CTL0_ADDR (0x350)

#define U_L_PCI_PMON_CTR3_ADDR (0x330)
#define U_L_PCI_PMON_CTR2_ADDR (0x328)
#define U_L_PCI_PMON_CTR1_ADDR (0x320)
#define U_L_PCI_PMON_CTR0_ADDR (0x318)

#define UCLK_FIXED_CTR_ADDR (0x704)
#define UCLK_FIXED_CTL_ADDR (0x703)
#define UBOX_MSR_PMON_CTL0_ADDR (0x705)
#define UBOX_MSR_PMON_CTL1_ADDR (0x706)
#define UBOX_MSR_PMON_CTR0_ADDR (0x709)
#define UBOX_MSR_PMON_CTR1_ADDR (0x70a)

#define JKTIVT_PCU_MSR_PMON_CTR3_ADDR (0x0C39)
#define JKTIVT_PCU_MSR_PMON_CTR2_ADDR (0x0C38)
#define JKTIVT_PCU_MSR_PMON_CTR1_ADDR (0x0C37)
#define JKTIVT_PCU_MSR_PMON_CTR0_ADDR (0x0C36)

#define JKTIVT_PCU_MSR_PMON_BOX_FILTER_ADDR (0x0C34)

#define JKTIVT_PCU_MSR_PMON_CTL3_ADDR (0x0C33)
#define JKTIVT_PCU_MSR_PMON_CTL2_ADDR (0x0C32)
#define JKTIVT_PCU_MSR_PMON_CTL1_ADDR (0x0C31)
#define JKTIVT_PCU_MSR_PMON_CTL0_ADDR (0x0C30)

#define JKTIVT_PCU_MSR_PMON_BOX_CTL_ADDR (0x0C24)

#define HSX_PCU_MSR_PMON_CTR3_ADDR (0x071A)
#define HSX_PCU_MSR_PMON_CTR2_ADDR (0x0719)
#define HSX_PCU_MSR_PMON_CTR1_ADDR (0x0718)
#define HSX_PCU_MSR_PMON_CTR0_ADDR (0x0717)

#define HSX_PCU_MSR_PMON_BOX_FILTER_ADDR (0x0715)

#define HSX_PCU_MSR_PMON_CTL3_ADDR (0x0714)
#define HSX_PCU_MSR_PMON_CTL2_ADDR (0x0713)
#define HSX_PCU_MSR_PMON_CTL1_ADDR (0x0712)
#define HSX_PCU_MSR_PMON_CTL0_ADDR (0x0711)

#define HSX_PCU_MSR_PMON_BOX_CTL_ADDR (0x0710)

#define UNC_PMON_UNIT_CTL_RST_CONTROL  (1 << 0)
#define UNC_PMON_UNIT_CTL_RST_COUNTERS     (1 << 1)
#define UNC_PMON_UNIT_CTL_FRZ  (1 << 8)
#define UNC_PMON_UNIT_CTL_FRZ_EN   (1 << 16)
#define UNC_PMON_UNIT_CTL_RSV  ((1 << 16) + (1 << 17))

#define UNC_PMON_UNIT_CTL_VALID_BITS_MASK  ((1 << 17) - 1)

#define MC_CH_PCI_PMON_FIXED_CTL_RST (1 << 19)
#define MC_CH_PCI_PMON_FIXED_CTL_EN (1 << 22)
#define EDC_CH_PCI_PMON_FIXED_CTL_EN (1 << 0)

#define MC_CH_PCI_PMON_CTL_EVENT(x) (x << 0)
#define MC_CH_PCI_PMON_CTL_UMASK(x) (x << 8)
#define MC_CH_PCI_PMON_CTL_RST (1 << 17)
#define MC_CH_PCI_PMON_CTL_EDGE_DET (1 << 18)
#define MC_CH_PCI_PMON_CTL_EN (1 << 22)
#define MC_CH_PCI_PMON_CTL_INVERT (1 << 23)
#define MC_CH_PCI_PMON_CTL_THRESH(x) (x << 24UL)

#define Q_P_PCI_PMON_CTL_EVENT(x)   (x << 0)
#define Q_P_PCI_PMON_CTL_UMASK(x)   (x << 8)
#define Q_P_PCI_PMON_CTL_RST        (1 << 17)
#define Q_P_PCI_PMON_CTL_EDGE_DET   (1 << 18)
#define Q_P_PCI_PMON_CTL_EVENT_EXT  (1 << 21)
#define Q_P_PCI_PMON_CTL_EN         (1 << 22)
#define Q_P_PCI_PMON_CTL_INVERT     (1 << 23)
#define Q_P_PCI_PMON_CTL_THRESH(x)  (x << 24UL)

#define PCU_MSR_PMON_BOX_FILTER_BAND_0(x) (x << 0)
#define PCU_MSR_PMON_BOX_FILTER_BAND_1(x) (x << 8)
#define PCU_MSR_PMON_BOX_FILTER_BAND_2(x) (x << 16)
#define PCU_MSR_PMON_BOX_FILTER_BAND_3(x) (x << 24)

#define PCU_MSR_PMON_CTL_EVENT(x) (x << 0)
#define PCU_MSR_PMON_CTL_OCC_SEL(x) (x << 14)
#define PCU_MSR_PMON_CTL_RST    (1 << 17)
#define PCU_MSR_PMON_CTL_EDGE_DET (1 << 18)
#define PCU_MSR_PMON_CTL_EXTRA_SEL (1 << 21)
#define PCU_MSR_PMON_CTL_EN (1 << 22)
#define PCU_MSR_PMON_CTL_INVERT (1 << 23)
#define PCU_MSR_PMON_CTL_THRESH(x) (x << 24UL)
#define PCU_MSR_PMON_CTL_OCC_INVERT (1UL << 30UL)
#define PCU_MSR_PMON_CTL_OCC_EDGE_DET (1UL << 31UL)


#define JKT_C0_MSR_PMON_CTR3        0x0D19 // CBo 0 PMON Counter 3
#define JKT_C0_MSR_PMON_CTR2        0x0D18 // CBo 0 PMON Counter 2
#define JKT_C0_MSR_PMON_CTR1        0x0D17 // CBo 0 PMON Counter 1
#define JKT_C0_MSR_PMON_CTR0        0x0D16 // CBo 0 PMON Counter 0
#define JKT_C0_MSR_PMON_BOX_FILTER  0x0D14 // CBo 0 PMON Filter
#define JKT_C0_MSR_PMON_CTL3        0x0D13 // CBo 0 PMON Control for Counter 3
#define JKT_C0_MSR_PMON_CTL2        0x0D12 // CBo 0 PMON Control for Counter 2
#define JKT_C0_MSR_PMON_CTL1        0x0D11 // CBo 0 PMON Control for Counter 1
#define JKT_C0_MSR_PMON_CTL0        0x0D10 // CBo 0 PMON Control for Counter 0
#define JKT_C0_MSR_PMON_BOX_CTL     0x0D04 // CBo 0 PMON Box-Wide Control

#define JKTIVT_CBO_MSR_STEP         0x0020 // CBo MSR Step

#define IVT_C0_MSR_PMON_BOX_FILTER1 0x0D1A // CBo 0 PMON Filter 1

#define HSX_C0_MSR_PMON_CTR3 0x0E0B        // CBo 0 PMON Counter 3
#define HSX_C0_MSR_PMON_CTR2 0x0E0A        // CBo 0 PMON Counter 2
#define HSX_C0_MSR_PMON_CTR1 0x0E09        // CBo 0 PMON Counter 1
#define HSX_C0_MSR_PMON_CTR0 0x0E08        // CBo 0 PMON Counter 0

#define HSX_C0_MSR_PMON_BOX_FILTER1 0x0E06 // CBo 0 PMON Filter1
#define HSX_C0_MSR_PMON_BOX_FILTER 0x0E05  // CBo 0 PMON Filter0

#define HSX_C0_MSR_PMON_CTL3 0x0E04        // CBo 0 PMON Control for Counter 3
#define HSX_C0_MSR_PMON_CTL2 0x0E03        // CBo 0 PMON Control for Counter 2
#define HSX_C0_MSR_PMON_CTL1 0x0E02        // CBo 0 PMON Control for Counter 1
#define HSX_C0_MSR_PMON_CTL0 0x0E01        // CBo 0 PMON Control for Counter 0

#define HSX_C0_MSR_PMON_BOX_STATUS 0x0E07  // CBo 0 PMON Box-Wide Status
#define HSX_C0_MSR_PMON_BOX_CTL 0x0E00     // CBo 0 PMON Box-Wide Control

#define HSX_CBO_MSR_STEP         0x0010    // CBo MSR Step

#define KNL_CHA_MSR_STEP             0x000C // CHA MSR Step
#define KNL_CHA0_MSR_PMON_BOX_CTRL   0x0E00 // CHA 0 PMON Control

#define KNL_CHA0_MSR_PMON_EVT_SEL0   0x0E01 // CHA 0 PMON Event Select for Counter 0
#define KNL_CHA0_MSR_PMON_EVT_SEL1   0x0E02 // CHA 0 PMON Event Select for Counter 1
#define KNL_CHA0_MSR_PMON_EVT_SEL2   0x0E03 // CHA 0 PMON Event Select for Counter 2
#define KNL_CHA0_MSR_PMON_EVT_SEL3   0x0E04 // CHA 0 PMON Event Select for Counter 3

#define KNL_CHA0_MSR_PMON_BOX_CTL    0x0E05 // PERF_UNIT_CTL_CHA_0
#define KNL_CHA0_MSR_PMON_BOX_CTL1   0x0E06 // PERF_UNIT_CTL_1_CHA_0
#define KNL_CHA0_MSR_PMON_BOX_STATUS 0x0E07 // CHA 0 PMON Status

#define KNL_CHA0_MSR_PMON_CTR0       0x0E08 // CHA 0 PMON Counter 0
#define KNL_CHA0_MSR_PMON_CTR1       0x0E09 // CHA 0 PMON Counter 1
#define KNL_CHA0_MSR_PMON_CTR2       0x0E0A // CHA 0 PMON Counter 2
#define KNL_CHA0_MSR_PMON_CTR3       0x0E0B // CHA 0 PMON Counter 3

#define CBO_MSR_PMON_CTL_EVENT(x) (x << 0)
#define CBO_MSR_PMON_CTL_UMASK(x) (x << 8)
#define CBO_MSR_PMON_CTL_RST    (1 << 17)
#define CBO_MSR_PMON_CTL_EDGE_DET (1 << 18)
#define CBO_MSR_PMON_CTL_TID_EN (1 << 19)
#define CBO_MSR_PMON_CTL_EN (1 << 22)
#define CBO_MSR_PMON_CTL_INVERT (1 << 23)
#define CBO_MSR_PMON_CTL_THRESH(x) (x << 24UL)

#define JKT_CBO_MSR_PMON_BOX_FILTER_OPC(x) (x << 23UL)
#define IVTHSX_CBO_MSR_PMON_BOX_FILTER1_OPC(x) (x << 20UL)

#define SKX_CHA_MSR_PMON_BOX_FILTER1_REM(x) (x << 0UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_LOC(x) (x << 1UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_NM(x) (x << 4UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_NOT_NM(x) (x << 5UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_OPC0(x) ((x) << 9UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_OPC1(x) ((x) << 19UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_NC(x) (x << 30UL)

#define SKX_CHA_TOR_INSERTS_UMASK_IRQ(x) (x << 0)
#define SKX_CHA_TOR_INSERTS_UMASK_PRQ(x) (x << 2)
#define SKX_CHA_TOR_INSERTS_UMASK_HIT(x) (x << 4)
#define SKX_CHA_TOR_INSERTS_UMASK_MISS(x) (x << 5)

#define SKX_IIO_CBDMA_UNIT_STATUS   (0x0A47)
#define SKX_IIO_CBDMA_UNIT_CTL      (0x0A40)
#define SKX_IIO_CBDMA_CTR0          (0x0A41)
#define SKX_IIO_CBDMA_CLK           (0x0A45)
#define SKX_IIO_CBDMA_CTL0          (0x0A48)
#define SKX_IIO_PM_REG_STEP         (0x0020)

#define IIO_MSR_PMON_CTL_EVENT(x)   ((x) << 0)
#define IIO_MSR_PMON_CTL_UMASK(x)   ((x) << 8)
#define IIO_MSR_PMON_CTL_RST        (1 << 17)
#define IIO_MSR_PMON_CTL_EDGE_DET   (1 << 18)
#define IIO_MSR_PMON_CTL_OV_EN      (1 << 20)
#define IIO_MSR_PMON_CTL_EN         (1 << 22)
#define IIO_MSR_PMON_CTL_INVERT     (1 << 23)
#define IIO_MSR_PMON_CTL_THRESH(x)  ((x) << 24ULL)
#define IIO_MSR_PMON_CTL_CH_MASK(x) ((x) << 36ULL)
#define IIO_MSR_PMON_CTL_FC_MASK(x) ((x) << 44ULL)

#define M2M_PCI_PMON_CTL_EVENT(x)   ((x) << 0)
#define M2M_PCI_PMON_CTL_UMASK(x)   ((x) << 8)
#define M2M_PCI_PMON_CTL_RST        (1 << 17)
#define M2M_PCI_PMON_CTL_EDGE_DET   (1 << 18)
#define M2M_PCI_PMON_CTL_OV_EN      (1 << 20)
#define M2M_PCI_PMON_CTL_EN         (1 << 22)
#define M2M_PCI_PMON_CTL_INVERT     (1 << 23)
#define M2M_PCI_PMON_CTL_THRESH(x)  ((x) << 24ULL)

#define UCLK_FIXED_CTL_OV_EN (1 << 20)
#define UCLK_FIXED_CTL_EN    (1 << 22)

/* \brief IIO Performance Monitoring Control Register format

    IIOn_MSR_PMON_CTL{3-0} Register- Field Definitions
*/
struct IIOPMUCNTCTLRegister
{
    union
    {
        struct
        {
            uint64 event_select : 8;
            uint64 umask : 8;
            uint64 reserved1 : 1;
            uint64 reset : 1;
            uint64 edge_det : 1;
            uint64 ignored : 1;
            uint64 overflow_enable : 1;
            uint64 reserved2 : 1;
            uint64 enable : 1;
            uint64 invert : 1;
            uint64 thresh : 12;
            uint64 ch_mask : 8;
            uint64 fc_mask : 3;
            uint64 reservedX : 17;
        } fields;
        uint64 value;
    };
    IIOPMUCNTCTLRegister() : value(0) { }
};

#define MSR_PACKAGE_THERM_STATUS (0x01B1)
#define MSR_IA32_THERM_STATUS    (0x019C)
#define PCM_INVALID_THERMAL_HEADROOM ((std::numeric_limits<int32>::min)())

#define MSR_IA32_BIOS_SIGN_ID   (0x8B)

#define MSR_DRAM_ENERGY_STATUS (0x0619)

#define MSR_PKG_C2_RESIDENCY    (0x60D)
#define MSR_PKG_C3_RESIDENCY    (0x3F8)
#define MSR_PKG_C6_RESIDENCY    (0x3F9)
#define MSR_PKG_C7_RESIDENCY    (0x3FA)
#define MSR_CORE_C3_RESIDENCY   (0x3FC)
#define MSR_CORE_C6_RESIDENCY   (0x3FD)
#define MSR_CORE_C7_RESIDENCY   (0x3FE)

#define MSR_PERF_GLOBAL_INUSE   (0x392)

#define MSR_IA32_SPEC_CTRL         (0x48)
#define MSR_IA32_ARCH_CAPABILITIES (0x10A)

#define MSR_TSX_FORCE_ABORT (0x10f)

#ifdef _MSC_VER
#include <windows.h>
// data structure for converting two uint32s <-> uin64
union cvt_ds
{
    UINT64 ui64;
    struct
    {
        DWORD low;
        DWORD high;
    } ui32;
};

#endif

struct MCFGRecord
{
    unsigned long long baseAddress;
    unsigned short PCISegmentGroupNumber;
    unsigned char startBusNumber;
    unsigned char endBusNumber;
    char reserved[4];
    void print()
    {
        std::cout << "BaseAddress=" << (std::hex) << "0x" << baseAddress << " PCISegmentGroupNumber=0x" << PCISegmentGroupNumber <<
            " startBusNumber=0x" << (unsigned)startBusNumber << " endBusNumber=0x" << (unsigned)endBusNumber << std::endl;
    }
};

struct MCFGHeader
{
    char signature[4];
    unsigned length;
    unsigned char revision;
    unsigned char checksum;
    char OEMID[6];
    char OEMTableID[8];
    unsigned OEMRevision;
    unsigned creatorID;
    unsigned creatorRevision;
    char reserved[8];

    unsigned nrecords() const
    {
        return (length - sizeof(MCFGHeader)) / sizeof(MCFGRecord);
    }

    void print()
    {
        std::cout << "Header: length=" << length << " nrecords=" << nrecords() << std::endl;
    }
};

#endif
