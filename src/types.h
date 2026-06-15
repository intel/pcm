// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2024, Intel Corporation
// written by Roman Dementiev
//

#ifndef CPUCounters_TYPES_H
#define CPUCounters_TYPES_H


/*!     \file types.h
        \brief Internal type and constant definitions
*/

#undef PCM_DEBUG

#ifndef KERNEL

#include <iostream>
#include <istream>
#include <sstream>
#include <iomanip>
#include <string.h>
#include <assert.h>
#include <limits>

#ifdef _MSC_VER
#include <windows.h>
#include <intrin.h>
#endif

#endif // #ifndef KERNEL

namespace pcm {

typedef unsigned long long uint64;
typedef signed long long int64;
typedef unsigned int uint32;
typedef signed int int32;

#define PCM_ULIMIT_RECOMMENDATION ("try executing 'ulimit -n 1000000' to increase the limit on the number of open files.\n")

/*
    MSR addresses from
    "Intel 64 and IA-32 Architectures Software Developers Manual Volume 3B:
    System Programming Guide, Part 2", Appendix A "PERFORMANCE-MONITORING EVENTS"
*/

constexpr auto INST_RETIRED_ADDR = 0x309;
constexpr auto CPU_CLK_UNHALTED_THREAD_ADDR = 0x30A;
constexpr auto CPU_CLK_UNHALTED_REF_ADDR = 0x30B;
constexpr auto TOPDOWN_SLOTS_ADDR = 0x30C;
constexpr auto PERF_METRICS_ADDR = 0x329;
constexpr auto IA32_CR_PERF_GLOBAL_CTRL = 0x38F;
constexpr auto IA32_CR_FIXED_CTR_CTRL = 0x38D;
constexpr auto IA32_PERFEVTSEL0_ADDR = 0x186;
constexpr auto IA32_PERFEVTSEL1_ADDR = IA32_PERFEVTSEL0_ADDR + 1;
constexpr auto IA32_PERFEVTSEL2_ADDR = IA32_PERFEVTSEL0_ADDR + 2;
constexpr auto IA32_PERFEVTSEL3_ADDR = IA32_PERFEVTSEL0_ADDR + 3;
constexpr auto IA32_PERF_GLOBAL_STATUS = 0x38E;
constexpr auto IA32_PERF_GLOBAL_OVF_CTRL = 0x390;
constexpr auto IA32_PEBS_ENABLE_ADDR = 0x3F1;

constexpr auto PERF_MAX_FIXED_COUNTERS = 3;
constexpr auto PERF_MAX_CUSTOM_COUNTERS = 8;
constexpr auto PERF_TOPDOWN_COUNTERS_L1 = 5;
constexpr auto PERF_TOPDOWN_COUNTERS = PERF_TOPDOWN_COUNTERS_L1 + 4;
constexpr auto PERF_MAX_COUNTERS = PERF_MAX_FIXED_COUNTERS + PERF_MAX_CUSTOM_COUNTERS + PERF_TOPDOWN_COUNTERS;

constexpr auto IA32_DEBUGCTL = 0x1D9;

constexpr auto IA32_PMC0 = 0xC1;
constexpr auto IA32_PMC1 = IA32_PMC0 + 1;
constexpr auto IA32_PMC2 = IA32_PMC0 + 2;
constexpr auto IA32_PMC3 = IA32_PMC0 + 3;

constexpr auto MSR_OFFCORE_RSP0 = 0x1A6;
constexpr auto MSR_OFFCORE_RSP1 = 0x1A7;
constexpr auto MSR_LOAD_LATENCY = 0x3F6;
constexpr auto MSR_FRONTEND = 0x3F7;

/* From Table B-5. of the above mentioned document */
constexpr auto PLATFORM_INFO_ADDR = 0xCE;

constexpr auto IA32_TIME_STAMP_COUNTER = 0x10;

// Event IDs

// Nehalem/Westmere on-core events
constexpr auto MEM_LOAD_RETIRED_L3_MISS_EVTNR = 0xCB;
constexpr auto MEM_LOAD_RETIRED_L3_MISS_UMASK = 0x10;

constexpr auto MEM_LOAD_RETIRED_L3_UNSHAREDHIT_EVTNR = 0xCB;
constexpr auto MEM_LOAD_RETIRED_L3_UNSHAREDHIT_UMASK = 0x04;

constexpr auto MEM_LOAD_RETIRED_L2_HITM_EVTNR = 0xCB;
constexpr auto MEM_LOAD_RETIRED_L2_HITM_UMASK = 0x08;

constexpr auto MEM_LOAD_RETIRED_L2_HIT_EVTNR = 0xCB;
constexpr auto MEM_LOAD_RETIRED_L2_HIT_UMASK = 0x02;

// Sandy Bridge on-core events

constexpr auto MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS_EVTNR = 0xD4;
constexpr auto MEM_LOAD_UOPS_MISC_RETIRED_LLC_MISS_UMASK = 0x02;

constexpr auto MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_NONE_EVTNR = 0xD2;
constexpr auto MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_NONE_UMASK = 0x08;

constexpr auto MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_HITM_EVTNR = 0xD2;
constexpr auto MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_HITM_UMASK = 0x04;

constexpr auto MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_EVTNR = 0xD2;
constexpr auto MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_UMASK = 0x07;

constexpr auto MEM_LOAD_UOPS_RETIRED_L2_HIT_EVTNR = 0xD1;
constexpr auto MEM_LOAD_UOPS_RETIRED_L2_HIT_UMASK = 0x02;
// Haswell on-core events

constexpr auto HSX_L2_RQSTS_MISS_EVTNR = 0x24;
constexpr auto HSX_L2_RQSTS_MISS_UMASK = 0x3f;
constexpr auto HSX_L2_RQSTS_REFERENCES_EVTNR = 0x24;
constexpr auto HSX_L2_RQSTS_REFERENCES_UMASK = 0xff;

// Skylake on-core events

#define SKL_MEM_LOAD_RETIRED_L3_MISS_EVTNR (0xD1)
#define SKL_MEM_LOAD_RETIRED_L3_MISS_UMASK (0x20)

#define SKL_MEM_LOAD_RETIRED_L3_HIT_EVTNR (0xD1)
#define SKL_MEM_LOAD_RETIRED_L3_HIT_UMASK (0x04)

#define SKL_MEM_LOAD_RETIRED_L2_MISS_EVTNR (0xD1)
#define SKL_MEM_LOAD_RETIRED_L2_MISS_UMASK (0x10)

#define SKL_MEM_LOAD_RETIRED_L2_HIT_EVTNR (0xD1)
#define SKL_MEM_LOAD_RETIRED_L2_HIT_UMASK (0x02)

// Crestmont on-core events

constexpr auto CMT_MEM_LOAD_RETIRED_L2_MISS_EVTNR = 0xD1;
constexpr auto CMT_MEM_LOAD_RETIRED_L2_MISS_UMASK = 0x80;

constexpr auto CMT_MEM_LOAD_RETIRED_L2_HIT_EVTNR = 0xD1;
constexpr auto CMT_MEM_LOAD_RETIRED_L2_HIT_UMASK = 0x02;

// architectural on-core events
constexpr auto ARCH_LLC_REFERENCE_EVTNR = 0x2E;
constexpr auto ARCH_LLC_REFERENCE_UMASK = 0x4F;

constexpr auto ARCH_LLC_MISS_EVTNR = 0x2E;
constexpr auto ARCH_LLC_MISS_UMASK = 0x41;

// Atom on-core events
constexpr auto ATOM_MEM_LOAD_RETIRED_L2_HIT_EVTNR = 0xCB;
constexpr auto ATOM_MEM_LOAD_RETIRED_L2_HIT_UMASK = 0x01;

constexpr auto ATOM_MEM_LOAD_RETIRED_L2_MISS_EVTNR = 0xCB;
constexpr auto ATOM_MEM_LOAD_RETIRED_L2_MISS_UMASK = 0x02;

// Offcore response events
constexpr auto OFFCORE_RESPONSE_0_EVTNR = 0xB7;
constexpr auto OFFCORE_RESPONSE_1_EVTNR = 0xBB;
constexpr auto GLC_OFFCORE_RESPONSE_0_EVTNR = 0x2A;
constexpr auto GLC_OFFCORE_RESPONSE_1_EVTNR = 0x2B;
constexpr auto OFFCORE_RESPONSE_0_UMASK = 1;
constexpr auto OFFCORE_RESPONSE_1_UMASK = 1;

constexpr auto LOAD_LATENCY_EVTNR = 0xcd;
constexpr auto LOAD_LATENCY_UMASK = 0x01;
constexpr auto FRONTEND_EVTNR = 0xC6;
constexpr auto FRONTEND_UMASK = 0x01;

/*
     For Nehalem(-EP) processors from Intel(r) 64 and IA-32 Architectures Software Developer's Manual
*/

// Uncore msrs
constexpr auto MSR_UNCORE_PERF_GLOBAL_CTRL_ADDR = 0x391;

constexpr auto MSR_UNCORE_PERFEVTSEL0_ADDR = 0x3C0;
constexpr auto MSR_UNCORE_PERFEVTSEL1_ADDR = MSR_UNCORE_PERFEVTSEL0_ADDR + 1;
constexpr auto MSR_UNCORE_PERFEVTSEL2_ADDR = MSR_UNCORE_PERFEVTSEL0_ADDR + 2;
constexpr auto MSR_UNCORE_PERFEVTSEL3_ADDR = MSR_UNCORE_PERFEVTSEL0_ADDR + 3;
constexpr auto MSR_UNCORE_PERFEVTSEL4_ADDR = MSR_UNCORE_PERFEVTSEL0_ADDR + 4;
constexpr auto MSR_UNCORE_PERFEVTSEL5_ADDR = MSR_UNCORE_PERFEVTSEL0_ADDR + 5;
constexpr auto MSR_UNCORE_PERFEVTSEL6_ADDR = MSR_UNCORE_PERFEVTSEL0_ADDR + 6;
constexpr auto MSR_UNCORE_PERFEVTSEL7_ADDR = MSR_UNCORE_PERFEVTSEL0_ADDR + 7;

constexpr auto MSR_UNCORE_PMC0 = 0x3B0;
constexpr auto MSR_UNCORE_PMC1 = MSR_UNCORE_PMC0 + 1;
constexpr auto MSR_UNCORE_PMC2 = MSR_UNCORE_PMC0 + 2;
constexpr auto MSR_UNCORE_PMC3 = MSR_UNCORE_PMC0 + 3;
constexpr auto MSR_UNCORE_PMC4 = MSR_UNCORE_PMC0 + 4;
constexpr auto MSR_UNCORE_PMC5 = MSR_UNCORE_PMC0 + 5;
constexpr auto MSR_UNCORE_PMC6 = MSR_UNCORE_PMC0 + 6;
constexpr auto MSR_UNCORE_PMC7 = MSR_UNCORE_PMC0 + 7;
// Uncore event IDs
constexpr auto UNC_QMC_WRITES_FULL_ANY_EVTNR = 0x2F;
constexpr auto UNC_QMC_WRITES_FULL_ANY_UMASK = 0x07;

constexpr auto UNC_QMC_NORMAL_READS_ANY_EVTNR = 0x2C;
constexpr auto UNC_QMC_NORMAL_READS_ANY_UMASK = 0x07;

constexpr auto UNC_QHL_REQUESTS_EVTNR = 0x20;

constexpr auto UNC_QHL_REQUESTS_IOH_READS_UMASK = 0x01;
constexpr auto UNC_QHL_REQUESTS_IOH_WRITES_UMASK = 0x02;
constexpr auto UNC_QHL_REQUESTS_REMOTE_READS_UMASK = 0x04;
constexpr auto UNC_QHL_REQUESTS_REMOTE_WRITES_UMASK = 0x08;
constexpr auto UNC_QHL_REQUESTS_LOCAL_READS_UMASK = 0x10;
constexpr auto UNC_QHL_REQUESTS_LOCAL_WRITES_UMASK = 0x20;
/*
        From "Intel(r) Xeon(r) Processor 7500 Series Uncore Programming Guide"
*/

// Beckton uncore event IDs
constexpr auto U_MSR_PMON_GLOBAL_CTL = 0x0C00;

constexpr auto MB0_MSR_PERF_GLOBAL_CTL = 0x0CA0;
constexpr auto MB0_MSR_PMU_CNT_0 = 0x0CB1;
constexpr auto MB0_MSR_PMU_CNT_CTL_0 = 0x0CB0;
constexpr auto MB0_MSR_PMU_CNT_1 = 0x0CB3;
constexpr auto MB0_MSR_PMU_CNT_CTL_1 = 0x0CB2;
constexpr auto MB0_MSR_PMU_ZDP_CTL_FVC = 0x0CAB;

constexpr auto MB1_MSR_PERF_GLOBAL_CTL = 0x0CE0;
constexpr auto MB1_MSR_PMU_CNT_0 = 0x0CF1;
constexpr auto MB1_MSR_PMU_CNT_CTL_0 = 0x0CF0;
constexpr auto MB1_MSR_PMU_CNT_1 = 0x0CF3;
constexpr auto MB1_MSR_PMU_CNT_CTL_1 = 0x0CF2;
constexpr auto MB1_MSR_PMU_ZDP_CTL_FVC = 0x0CEB;

constexpr auto BB0_MSR_PERF_GLOBAL_CTL = 0x0C20;
constexpr auto BB0_MSR_PERF_CNT_1 = 0x0C33;
constexpr auto BB0_MSR_PERF_CNT_CTL_1 = 0x0C32;

constexpr auto BB1_MSR_PERF_GLOBAL_CTL = 0x0C60;
constexpr auto BB1_MSR_PERF_CNT_1 = 0x0C73;
constexpr auto BB1_MSR_PERF_CNT_CTL_1 = 0x0C72;

constexpr auto R_MSR_PMON_CTL0 = 0x0E10;
constexpr auto R_MSR_PMON_CTR0 = 0x0E11;
constexpr auto R_MSR_PMON_CTL1 = 0x0E12;
constexpr auto R_MSR_PMON_CTR1 = 0x0E13;
constexpr auto R_MSR_PMON_CTL2 = 0x0E14;
constexpr auto R_MSR_PMON_CTR2 = 0x0E15;
constexpr auto R_MSR_PMON_CTL3 = 0x0E16;
constexpr auto R_MSR_PMON_CTR3 = 0x0E17;
constexpr auto R_MSR_PMON_CTL4 = 0x0E18;
constexpr auto R_MSR_PMON_CTR4 = 0x0E19;
constexpr auto R_MSR_PMON_CTL5 = 0x0E1A;
constexpr auto R_MSR_PMON_CTR5 = 0x0E1B;
constexpr auto R_MSR_PMON_CTL6 = 0x0E1C;
constexpr auto R_MSR_PMON_CTR6 = 0x0E1D;
constexpr auto R_MSR_PMON_CTL7 = 0x0E1E;
constexpr auto R_MSR_PMON_CTR7 = 0x0E1F;
constexpr auto R_MSR_PMON_CTL8 = 0x0E30;
constexpr auto R_MSR_PMON_CTR8 = 0x0E31;
constexpr auto R_MSR_PMON_CTL9 = 0x0E32;
constexpr auto R_MSR_PMON_CTR9 = 0x0E33;
constexpr auto R_MSR_PMON_CTL10 = 0x0E34;
constexpr auto R_MSR_PMON_CTR10 = 0x0E35;
constexpr auto R_MSR_PMON_CTL11 = 0x0E36;
constexpr auto R_MSR_PMON_CTR11 = 0x0E37;
constexpr auto R_MSR_PMON_CTL12 = 0x0E38;
constexpr auto R_MSR_PMON_CTR12 = 0x0E39;
constexpr auto R_MSR_PMON_CTL13 = 0x0E3A;
constexpr auto R_MSR_PMON_CTR13 = 0x0E3B;
constexpr auto R_MSR_PMON_CTL14 = 0x0E3C;
constexpr auto R_MSR_PMON_CTR14 = 0x0E3D;
constexpr auto R_MSR_PMON_CTL15 = 0x0E3E;
constexpr auto R_MSR_PMON_CTR15 = 0x0E3F;

constexpr auto R_MSR_PORT0_IPERF_CFG0 = 0x0E04;
constexpr auto R_MSR_PORT1_IPERF_CFG0 = 0x0E05;
constexpr auto R_MSR_PORT2_IPERF_CFG0 = 0x0E06;
constexpr auto R_MSR_PORT3_IPERF_CFG0 = 0x0E07;
constexpr auto R_MSR_PORT4_IPERF_CFG0 = 0x0E08;
constexpr auto R_MSR_PORT5_IPERF_CFG0 = 0x0E09;
constexpr auto R_MSR_PORT6_IPERF_CFG0 = 0x0E0A;
constexpr auto R_MSR_PORT7_IPERF_CFG0 = 0x0E0B;

constexpr auto R_MSR_PORT0_IPERF_CFG1 = 0x0E24;
constexpr auto R_MSR_PORT1_IPERF_CFG1 = 0x0E25;
constexpr auto R_MSR_PORT2_IPERF_CFG1 = 0x0E26;
constexpr auto R_MSR_PORT3_IPERF_CFG1 = 0x0E27;
constexpr auto R_MSR_PORT4_IPERF_CFG1 = 0x0E28;
constexpr auto R_MSR_PORT5_IPERF_CFG1 = 0x0E29;
constexpr auto R_MSR_PORT6_IPERF_CFG1 = 0x0E2A;
constexpr auto R_MSR_PORT7_IPERF_CFG1 = 0x0E2B;

constexpr auto R_MSR_PMON_GLOBAL_CTL_7_0 = 0x0E00;
constexpr auto R_MSR_PMON_GLOBAL_CTL_15_8 = 0x0E20;

constexpr auto W_MSR_PMON_GLOBAL_CTL = 0xC80;
constexpr auto W_MSR_PMON_FIXED_CTR_CTL = 0x395;
constexpr auto W_MSR_PMON_FIXED_CTR = 0x394;
/*
 * Platform QoS MSRs
 */

constexpr auto IA32_PQR_ASSOC = 0xc8f;
constexpr auto IA32_QM_EVTSEL = 0xc8d;
constexpr auto IA32_QM_CTR = 0xc8e;

#ifndef KERNEL
constexpr auto PCM_INVALID_QOS_MONITORING_DATA = (std::numeric_limits<uint64>::max)();
#endif

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
	    // CTR3
            uint64 os3 : 1;
            uint64 usr3 : 1;
            uint64 any_thread3 : 1;
            uint64 enable_pmi3 : 1;

            uint64 reserved1 : 48;
        } fields;
        uint64 value;
    };
    FixedEventControlRegister() : value(0) {}
};

#ifndef KERNEL

inline std::ostream & operator << (std::ostream & o, const FixedEventControlRegister & reg)
{
    o << "os0\t\t" << reg.fields.os0 << "\n";
    o << "usr0\t\t" << reg.fields.usr0 << "\n";
    o << "any_thread0\t" << reg.fields.any_thread0 << "\n";
    o << "enable_pmi0\t" << reg.fields.enable_pmi0 << "\n";

    o << "os1\t\t" << reg.fields.os1 << "\n";
    o << "usr1\t\t" << reg.fields.usr1 << "\n";
    o << "any_thread1\t" << reg.fields.any_thread1 << "\n";
    o << "enable_pmi10\t" << reg.fields.enable_pmi1 << "\n";

    o << "os2\t\t" << reg.fields.os2 << "\n";
    o << "usr2\t\t" << reg.fields.usr2 << "\n";
    o << "any_thread2\t" << reg.fields.any_thread2 << "\n";
    o << "enable_pmi2\t" << reg.fields.enable_pmi2 << "\n";

    o << "reserved1\t" << reg.fields.reserved1 << "\n";
    return o;
}

#endif // #ifndef KERNEL

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

constexpr auto MSR_SMI_COUNT = 0x34;

/* \brief Sandy Bridge energy counters
*/

constexpr auto MSR_PKG_ENERGY_STATUS = 0x611;
constexpr auto MSR_SYS_ENERGY_STATUS = 0x64D;
constexpr auto MSR_RAPL_POWER_UNIT = 0x606;
constexpr auto MSR_PKG_POWER_INFO = 0x614;

constexpr auto PCM_INTEL_PCI_VENDOR_ID = 0x8086;
constexpr auto PCM_PCI_VENDOR_ID_OFFSET = 0;

// server PCICFG uncore counters
constexpr auto JKTIVT_MC0_CH0_REGISTER_DEV_ADDR = 16;
constexpr auto JKTIVT_MC0_CH1_REGISTER_DEV_ADDR = 16;
constexpr auto JKTIVT_MC0_CH2_REGISTER_DEV_ADDR = 16;
constexpr auto JKTIVT_MC0_CH3_REGISTER_DEV_ADDR = 16;
constexpr auto JKTIVT_MC0_CH0_REGISTER_FUNC_ADDR = 4;
constexpr auto JKTIVT_MC0_CH1_REGISTER_FUNC_ADDR = 5;
constexpr auto JKTIVT_MC0_CH2_REGISTER_FUNC_ADDR = 0;
constexpr auto JKTIVT_MC0_CH3_REGISTER_FUNC_ADDR = 1;

constexpr auto JKTIVT_MC1_CH0_REGISTER_DEV_ADDR = 30;
constexpr auto JKTIVT_MC1_CH1_REGISTER_DEV_ADDR = 30;
constexpr auto JKTIVT_MC1_CH2_REGISTER_DEV_ADDR = 30;
constexpr auto JKTIVT_MC1_CH3_REGISTER_DEV_ADDR = 30;
constexpr auto JKTIVT_MC1_CH0_REGISTER_FUNC_ADDR = 4;
constexpr auto JKTIVT_MC1_CH1_REGISTER_FUNC_ADDR = 5;
constexpr auto JKTIVT_MC1_CH2_REGISTER_FUNC_ADDR = 0;
constexpr auto JKTIVT_MC1_CH3_REGISTER_FUNC_ADDR = 1;

constexpr auto HSX_MC0_CH0_REGISTER_DEV_ADDR = 20;
constexpr auto HSX_MC0_CH1_REGISTER_DEV_ADDR = 20;
constexpr auto HSX_MC0_CH2_REGISTER_DEV_ADDR = 21;
constexpr auto HSX_MC0_CH3_REGISTER_DEV_ADDR = 21;
constexpr auto HSX_MC0_CH0_REGISTER_FUNC_ADDR = 0;
constexpr auto HSX_MC0_CH1_REGISTER_FUNC_ADDR = 1;
constexpr auto HSX_MC0_CH2_REGISTER_FUNC_ADDR = 0;
constexpr auto HSX_MC0_CH3_REGISTER_FUNC_ADDR = 1;

constexpr auto HSX_MC1_CH0_REGISTER_DEV_ADDR = 23;
constexpr auto HSX_MC1_CH1_REGISTER_DEV_ADDR = 23;
constexpr auto HSX_MC1_CH2_REGISTER_DEV_ADDR = 24;
constexpr auto HSX_MC1_CH3_REGISTER_DEV_ADDR = 24;
constexpr auto HSX_MC1_CH0_REGISTER_FUNC_ADDR = 0;
constexpr auto HSX_MC1_CH1_REGISTER_FUNC_ADDR = 1;
constexpr auto HSX_MC1_CH2_REGISTER_FUNC_ADDR = 0;
constexpr auto HSX_MC1_CH3_REGISTER_FUNC_ADDR = 1;

constexpr auto KNL_MC0_CH0_REGISTER_DEV_ADDR = 8;
constexpr auto KNL_MC0_CH1_REGISTER_DEV_ADDR = 8;
constexpr auto KNL_MC0_CH2_REGISTER_DEV_ADDR = 8;
constexpr auto KNL_MC0_CH0_REGISTER_FUNC_ADDR = 2;
constexpr auto KNL_MC0_CH1_REGISTER_FUNC_ADDR = 3;
constexpr auto KNL_MC0_CH2_REGISTER_FUNC_ADDR = 4;

constexpr auto SKX_MC0_CH0_REGISTER_DEV_ADDR = 10;
constexpr auto SKX_MC0_CH1_REGISTER_DEV_ADDR = 10;
constexpr auto SKX_MC0_CH2_REGISTER_DEV_ADDR = 11;
constexpr auto SKX_MC0_CH3_REGISTER_DEV_ADDR = -1; //Does not exist
constexpr auto SKX_MC0_CH0_REGISTER_FUNC_ADDR = 2;
constexpr auto SKX_MC0_CH1_REGISTER_FUNC_ADDR = 6;
constexpr auto SKX_MC0_CH2_REGISTER_FUNC_ADDR = 2;
constexpr auto SKX_MC0_CH3_REGISTER_FUNC_ADDR = -1; //Does not exist

constexpr auto SKX_MC1_CH0_REGISTER_DEV_ADDR = 12;
constexpr auto SKX_MC1_CH1_REGISTER_DEV_ADDR = 12;
constexpr auto SKX_MC1_CH2_REGISTER_DEV_ADDR = 13;
constexpr auto SKX_MC1_CH3_REGISTER_DEV_ADDR = -1; //Does not exist
constexpr auto SKX_MC1_CH0_REGISTER_FUNC_ADDR = 2;
constexpr auto SKX_MC1_CH1_REGISTER_FUNC_ADDR = 6;
constexpr auto SKX_MC1_CH2_REGISTER_FUNC_ADDR = 2;
constexpr auto SKX_MC1_CH3_REGISTER_FUNC_ADDR = -1; //Does not exist

constexpr auto SERVER_UBOX0_REGISTER_DEV_ADDR = 0;
constexpr auto SERVER_UBOX0_REGISTER_FUNC_ADDR = 1;

constexpr auto KNL_MC1_CH0_REGISTER_DEV_ADDR = 9;
constexpr auto KNL_MC1_CH1_REGISTER_DEV_ADDR = 9;
constexpr auto KNL_MC1_CH2_REGISTER_DEV_ADDR = 9;
constexpr auto KNL_MC1_CH0_REGISTER_FUNC_ADDR = 2;
constexpr auto KNL_MC1_CH1_REGISTER_FUNC_ADDR = 3;
constexpr auto KNL_MC1_CH2_REGISTER_FUNC_ADDR = 4;

constexpr auto KNL_EDC0_ECLK_REGISTER_DEV_ADDR = 24;
constexpr auto KNL_EDC0_ECLK_REGISTER_FUNC_ADDR = 2;
constexpr auto KNL_EDC1_ECLK_REGISTER_DEV_ADDR = 25;
constexpr auto KNL_EDC1_ECLK_REGISTER_FUNC_ADDR = 2;
constexpr auto KNL_EDC2_ECLK_REGISTER_DEV_ADDR = 26;
constexpr auto KNL_EDC2_ECLK_REGISTER_FUNC_ADDR = 2;
constexpr auto KNL_EDC3_ECLK_REGISTER_DEV_ADDR = 27;
constexpr auto KNL_EDC3_ECLK_REGISTER_FUNC_ADDR = 2;
constexpr auto KNL_EDC4_ECLK_REGISTER_DEV_ADDR = 28;
constexpr auto KNL_EDC4_ECLK_REGISTER_FUNC_ADDR = 2;
constexpr auto KNL_EDC5_ECLK_REGISTER_DEV_ADDR = 29;
constexpr auto KNL_EDC5_ECLK_REGISTER_FUNC_ADDR = 2;
constexpr auto KNL_EDC6_ECLK_REGISTER_DEV_ADDR = 30;
constexpr auto KNL_EDC6_ECLK_REGISTER_FUNC_ADDR = 2;
constexpr auto KNL_EDC7_ECLK_REGISTER_DEV_ADDR = 31;
constexpr auto KNL_EDC7_ECLK_REGISTER_FUNC_ADDR = 2;

constexpr auto HSX_HA0_REGISTER_DEV_ADDR = 18;
constexpr auto HSX_HA0_REGISTER_FUNC_ADDR = 1;
constexpr auto HSX_HA1_REGISTER_DEV_ADDR = 18;
constexpr auto HSX_HA1_REGISTER_FUNC_ADDR = 5;

constexpr auto XPF_HA_PCI_PMON_BOX_CTL_ADDR = 0xF4;
constexpr auto XPF_HA_PCI_PMON_CTL0_ADDR = 0xD8 + 4*0;
constexpr auto XPF_HA_PCI_PMON_CTL1_ADDR = 0xD8 + 4*1;
constexpr auto XPF_HA_PCI_PMON_CTL2_ADDR = 0xD8 + 4*2;
constexpr auto XPF_HA_PCI_PMON_CTL3_ADDR = 0xD8 + 4*3;
constexpr auto XPF_HA_PCI_PMON_CTR0_ADDR = 0xA0 + 8*0;
constexpr auto XPF_HA_PCI_PMON_CTR1_ADDR = 0xA0 + 8*1;
constexpr auto XPF_HA_PCI_PMON_CTR2_ADDR = 0xA0 + 8*2;
constexpr auto XPF_HA_PCI_PMON_CTR3_ADDR = 0xA0 + 8*3;
constexpr auto BHS_PCIE_GEN5_PCI_PMON_BOX_CTL_ADDR = 0x620;
constexpr auto BHS_PCIE_GEN5_PCI_PMON_CTL0_ADDR = 0x630;
constexpr auto BHS_PCIE_GEN5_PCI_PMON_CTR0_ADDR = 0x650;

/**
 * XPF_ for Xeons: SNB, IVT, HSX, BDW, etc.
 * KNX_ for Xeon Phi (Knights *) processors
 */
constexpr auto XPF_MC_CH_PCI_PMON_BOX_CTL_ADDR = 0x0F4;
constexpr auto KNX_MC_CH_PCI_PMON_BOX_CTL_ADDR = 0xB30;
constexpr auto KNX_EDC_CH_PCI_PMON_BOX_CTL_ADDR = 0xA30;

//! for Xeons
constexpr auto XPF_MC_CH_PCI_PMON_FIXED_CTL_ADDR = 0x0F0;
constexpr auto XPF_MC_CH_PCI_PMON_CTL3_ADDR = 0x0E4;
constexpr auto XPF_MC_CH_PCI_PMON_CTL2_ADDR = 0x0E0;
constexpr auto XPF_MC_CH_PCI_PMON_CTL1_ADDR = 0x0DC;
constexpr auto XPF_MC_CH_PCI_PMON_CTL0_ADDR = 0x0D8;

//! KNL IMC
constexpr auto KNX_MC_CH_PCI_PMON_FIXED_CTL_ADDR = 0xB44;
constexpr auto KNX_MC_CH_PCI_PMON_CTL3_ADDR = 0xB2C;
constexpr auto KNX_MC_CH_PCI_PMON_CTL2_ADDR = 0xB28;
constexpr auto KNX_MC_CH_PCI_PMON_CTL1_ADDR = 0xB24;
constexpr auto KNX_MC_CH_PCI_PMON_CTL0_ADDR = 0xB20;

//! KNL EDC ECLK
constexpr auto KNX_EDC_CH_PCI_PMON_FIXED_CTL_ADDR = 0xA44;
constexpr auto KNX_EDC_CH_PCI_PMON_CTL3_ADDR = 0xA2C;
constexpr auto KNX_EDC_CH_PCI_PMON_CTL2_ADDR = 0xA28;
constexpr auto KNX_EDC_CH_PCI_PMON_CTL1_ADDR = 0xA24;
constexpr auto KNX_EDC_CH_PCI_PMON_CTL0_ADDR = 0xA20;
constexpr auto KNX_EDC_ECLK_PMON_UNIT_CTL_REG = 0xA30;

//! for Xeons
constexpr auto XPF_MC_CH_PCI_PMON_FIXED_CTR_ADDR = 0x0D0;
constexpr auto XPF_MC_CH_PCI_PMON_CTR3_ADDR = 0x0B8;
constexpr auto XPF_MC_CH_PCI_PMON_CTR2_ADDR = 0x0B0;
constexpr auto XPF_MC_CH_PCI_PMON_CTR1_ADDR = 0x0A8;
constexpr auto XPF_MC_CH_PCI_PMON_CTR0_ADDR = 0x0A0;

//! for KNL IMC
constexpr auto KNX_MC_CH_PCI_PMON_FIXED_CTR_ADDR = 0xB3C;
constexpr auto KNX_MC_CH_PCI_PMON_CTR3_ADDR = 0xB18;
constexpr auto KNX_MC_CH_PCI_PMON_CTR2_ADDR = 0xB10;
constexpr auto KNX_MC_CH_PCI_PMON_CTR1_ADDR = 0xB08;
constexpr auto KNX_MC_CH_PCI_PMON_CTR0_ADDR = 0xB00;

//! for KNL EDC ECLK
constexpr auto KNX_EDC_CH_PCI_PMON_FIXED_CTR_ADDR = 0xA3C;
constexpr auto KNX_EDC_CH_PCI_PMON_CTR3_ADDR = 0xA18;
constexpr auto KNX_EDC_CH_PCI_PMON_CTR2_ADDR = 0xA10;
constexpr auto KNX_EDC_CH_PCI_PMON_CTR1_ADDR = 0xA08;
constexpr auto KNX_EDC_CH_PCI_PMON_CTR0_ADDR = 0xA00;

constexpr auto SERVER_HBM_CH_PMON_BASE_ADDR = 0x141c00;
constexpr auto SERVER_HBM_CH_PMON_STEP = 0x4000;
constexpr auto SERVER_HBM_CH_PMON_SIZE = 0x1000;
constexpr auto SERVER_HBM_BOX_PMON_STEP = 0x9000;

constexpr auto SERVER_MC_CH_PMON_BASE_ADDR = 0x22800;
constexpr auto SERVER_MC_CH_PMON_STEP = 0x4000;
constexpr auto SERVER_MC_CH_PMON_SIZE = 0x1000;
constexpr auto SERVER_MC_CH_PMON_BOX_CTL_OFFSET = 0x00;
constexpr auto SERVER_MC_CH_PMON_CTL0_OFFSET = 0x40;
constexpr auto SERVER_MC_CH_PMON_CTL1_OFFSET = SERVER_MC_CH_PMON_CTL0_OFFSET + 4*1;
constexpr auto SERVER_MC_CH_PMON_CTL2_OFFSET = SERVER_MC_CH_PMON_CTL0_OFFSET + 4*2;
constexpr auto SERVER_MC_CH_PMON_CTL3_OFFSET = SERVER_MC_CH_PMON_CTL0_OFFSET + 4*3;
constexpr auto SERVER_MC_CH_PMON_CTR0_OFFSET = 0x08;
constexpr auto SERVER_MC_CH_PMON_CTR1_OFFSET = SERVER_MC_CH_PMON_CTR0_OFFSET + 8*1;
constexpr auto SERVER_MC_CH_PMON_CTR2_OFFSET = SERVER_MC_CH_PMON_CTR0_OFFSET + 8*2;
constexpr auto SERVER_MC_CH_PMON_CTR3_OFFSET = SERVER_MC_CH_PMON_CTR0_OFFSET + 8*3;
constexpr auto SERVER_MC_CH_PMON_FIXED_CTL_OFFSET = 0x54;
constexpr auto SERVER_MC_CH_PMON_FIXED_CTR_OFFSET = 0x38;
constexpr auto BHS_MC_CH_PMON_BASE_ADDR = 0x024e800;

constexpr auto JKTIVT_QPI_PORT0_REGISTER_DEV_ADDR = 8;
constexpr auto JKTIVT_QPI_PORT0_REGISTER_FUNC_ADDR = 2;
constexpr auto JKTIVT_QPI_PORT1_REGISTER_DEV_ADDR = 9;
constexpr auto JKTIVT_QPI_PORT1_REGISTER_FUNC_ADDR = 2;
constexpr auto JKTIVT_QPI_PORT2_REGISTER_DEV_ADDR = 24;
constexpr auto JKTIVT_QPI_PORT2_REGISTER_FUNC_ADDR = 2;

constexpr auto HSX_QPI_PORT0_REGISTER_DEV_ADDR = 8;
constexpr auto HSX_QPI_PORT0_REGISTER_FUNC_ADDR = 2;
constexpr auto HSX_QPI_PORT1_REGISTER_DEV_ADDR = 9;
constexpr auto HSX_QPI_PORT1_REGISTER_FUNC_ADDR = 2;
constexpr auto HSX_QPI_PORT2_REGISTER_DEV_ADDR = 10;
constexpr auto HSX_QPI_PORT2_REGISTER_FUNC_ADDR = 2;

constexpr auto SKX_QPI_PORT0_REGISTER_DEV_ADDR = 14;
constexpr auto SKX_QPI_PORT0_REGISTER_FUNC_ADDR = 0;
constexpr auto SKX_QPI_PORT1_REGISTER_DEV_ADDR = 15;
constexpr auto SKX_QPI_PORT1_REGISTER_FUNC_ADDR = 0;
constexpr auto SKX_QPI_PORT2_REGISTER_DEV_ADDR = 16;
constexpr auto SKX_QPI_PORT2_REGISTER_FUNC_ADDR = 0;

constexpr auto CPX_QPI_PORT3_REGISTER_DEV_ADDR = 14;
constexpr auto CPX_QPI_PORT3_REGISTER_FUNC_ADDR = 4;
constexpr auto CPX_QPI_PORT4_REGISTER_DEV_ADDR = 15;
constexpr auto CPX_QPI_PORT4_REGISTER_FUNC_ADDR = 4;
constexpr auto CPX_QPI_PORT5_REGISTER_DEV_ADDR = 16;
constexpr auto CPX_QPI_PORT5_REGISTER_FUNC_ADDR = 4;

constexpr auto ICX_QPI_PORT0_REGISTER_DEV_ADDR = 2;
constexpr auto ICX_QPI_PORT0_REGISTER_FUNC_ADDR = 1;
constexpr auto ICX_QPI_PORT1_REGISTER_DEV_ADDR = 3;
constexpr auto ICX_QPI_PORT1_REGISTER_FUNC_ADDR = 1;
constexpr auto ICX_QPI_PORT2_REGISTER_DEV_ADDR = 4;
constexpr auto ICX_QPI_PORT2_REGISTER_FUNC_ADDR = 1;

constexpr auto SPR_QPI_PORT0_REGISTER_DEV_ADDR = 1;
constexpr auto SPR_QPI_PORT0_REGISTER_FUNC_ADDR = 1;

constexpr auto SPR_QPI_PORT1_REGISTER_DEV_ADDR = 2;
constexpr auto SPR_QPI_PORT1_REGISTER_FUNC_ADDR = 1;

constexpr auto SPR_QPI_PORT2_REGISTER_DEV_ADDR = 3;
constexpr auto SPR_QPI_PORT2_REGISTER_FUNC_ADDR = 1;

constexpr auto SPR_QPI_PORT3_REGISTER_DEV_ADDR = 4;
constexpr auto SPR_QPI_PORT3_REGISTER_FUNC_ADDR = 1;
constexpr auto BHS_QPI_PORT0_REGISTER_DEV_ADDR = 16;
constexpr auto BHS_QPI_PORT0_REGISTER_FUNC_ADDR = 1;

constexpr auto BHS_QPI_PORT1_REGISTER_DEV_ADDR = 17;
constexpr auto BHS_QPI_PORT1_REGISTER_FUNC_ADDR = 1;

constexpr auto BHS_QPI_PORT2_REGISTER_DEV_ADDR = 18;
constexpr auto BHS_QPI_PORT2_REGISTER_FUNC_ADDR = 1;

constexpr auto BHS_QPI_PORT3_REGISTER_DEV_ADDR = 19;
constexpr auto BHS_QPI_PORT3_REGISTER_FUNC_ADDR = 1;

constexpr auto BHS_QPI_PORT4_REGISTER_DEV_ADDR = 20;
constexpr auto BHS_QPI_PORT4_REGISTER_FUNC_ADDR = 1;

constexpr auto BHS_QPI_PORT5_REGISTER_DEV_ADDR = 21;
constexpr auto BHS_QPI_PORT5_REGISTER_FUNC_ADDR = 1;

constexpr auto QPI_PORT0_MISC_REGISTER_FUNC_ADDR = 0;
constexpr auto QPI_PORT1_MISC_REGISTER_FUNC_ADDR = 0;
constexpr auto QPI_PORT2_MISC_REGISTER_FUNC_ADDR = 0;

constexpr auto SKX_M3UPI_PORT0_REGISTER_DEV_ADDR = (0x12);
constexpr auto SKX_M3UPI_PORT0_REGISTER_FUNC_ADDR = (1);
constexpr auto SKX_M3UPI_PORT1_REGISTER_DEV_ADDR = (0x12);
constexpr auto SKX_M3UPI_PORT1_REGISTER_FUNC_ADDR = (2);
constexpr auto SKX_M3UPI_PORT2_REGISTER_DEV_ADDR = (0x12);
constexpr auto SKX_M3UPI_PORT2_REGISTER_FUNC_ADDR = (5);

constexpr auto CPX_M3UPI_PORT0_REGISTER_DEV_ADDR = (0x12);
constexpr auto CPX_M3UPI_PORT0_REGISTER_FUNC_ADDR = (1);
constexpr auto CPX_M3UPI_PORT1_REGISTER_DEV_ADDR = (0x12);
constexpr auto CPX_M3UPI_PORT1_REGISTER_FUNC_ADDR = (2);
constexpr auto CPX_M3UPI_PORT2_REGISTER_DEV_ADDR = (0x13);
constexpr auto CPX_M3UPI_PORT2_REGISTER_FUNC_ADDR = (1);
constexpr auto CPX_M3UPI_PORT3_REGISTER_DEV_ADDR = (0x13);
constexpr auto CPX_M3UPI_PORT3_REGISTER_FUNC_ADDR = (2);
constexpr auto CPX_M3UPI_PORT4_REGISTER_DEV_ADDR = (0x14);
constexpr auto CPX_M3UPI_PORT4_REGISTER_FUNC_ADDR = (1);
constexpr auto CPX_M3UPI_PORT5_REGISTER_DEV_ADDR = (0x14);
constexpr auto CPX_M3UPI_PORT5_REGISTER_FUNC_ADDR = (2);

constexpr auto ICX_M3UPI_PORT0_REGISTER_DEV_ADDR = (5);
constexpr auto ICX_M3UPI_PORT1_REGISTER_DEV_ADDR = (6);
constexpr auto ICX_M3UPI_PORT2_REGISTER_DEV_ADDR = (7);
constexpr auto ICX_M3UPI_PORT0_REGISTER_FUNC_ADDR = (1);
constexpr auto ICX_M3UPI_PORT1_REGISTER_FUNC_ADDR = (1);
constexpr auto ICX_M3UPI_PORT2_REGISTER_FUNC_ADDR = (1);

constexpr auto SPR_M3UPI_PORT0_REGISTER_DEV_ADDR = 5;
constexpr auto SPR_M3UPI_PORT1_REGISTER_DEV_ADDR = 6;
constexpr auto SPR_M3UPI_PORT2_REGISTER_DEV_ADDR = 7;
constexpr auto SPR_M3UPI_PORT3_REGISTER_DEV_ADDR = 8;
constexpr auto SPR_M3UPI_PORT0_REGISTER_FUNC_ADDR = 1;
constexpr auto SPR_M3UPI_PORT1_REGISTER_FUNC_ADDR = 1;
constexpr auto SPR_M3UPI_PORT2_REGISTER_FUNC_ADDR = 1;
constexpr auto SPR_M3UPI_PORT3_REGISTER_FUNC_ADDR = 1;

constexpr auto SKX_M2M_0_REGISTER_DEV_ADDR = 8;
constexpr auto SKX_M2M_0_REGISTER_FUNC_ADDR = 0;
constexpr auto SKX_M2M_1_REGISTER_DEV_ADDR = 9;
constexpr auto SKX_M2M_1_REGISTER_FUNC_ADDR = 0;

constexpr auto SERVER_M2M_0_REGISTER_DEV_ADDR = 12;
constexpr auto SERVER_M2M_0_REGISTER_FUNC_ADDR = 0;
constexpr auto SERVER_M2M_1_REGISTER_DEV_ADDR = 13;
constexpr auto SERVER_M2M_1_REGISTER_FUNC_ADDR = 0;
constexpr auto SERVER_M2M_2_REGISTER_DEV_ADDR = 14;
constexpr auto SERVER_M2M_2_REGISTER_FUNC_ADDR = 0;
constexpr auto SERVER_M2M_3_REGISTER_DEV_ADDR = 15;
constexpr auto SERVER_M2M_3_REGISTER_FUNC_ADDR = 0;

constexpr auto SERVER_HBM_M2M_0_REGISTER_DEV_ADDR = 12;
constexpr auto SERVER_HBM_M2M_0_REGISTER_FUNC_ADDR = 1;
constexpr auto SERVER_HBM_M2M_1_REGISTER_DEV_ADDR = 13;
constexpr auto SERVER_HBM_M2M_1_REGISTER_FUNC_ADDR = 1;
constexpr auto SERVER_HBM_M2M_2_REGISTER_DEV_ADDR = 14;
constexpr auto SERVER_HBM_M2M_2_REGISTER_FUNC_ADDR = 1;
constexpr auto SERVER_HBM_M2M_3_REGISTER_DEV_ADDR = 15;
constexpr auto SERVER_HBM_M2M_3_REGISTER_FUNC_ADDR = 1;

constexpr auto SERVER_HBM_M2M_4_REGISTER_DEV_ADDR = 12;
constexpr auto SERVER_HBM_M2M_4_REGISTER_FUNC_ADDR = 2;
constexpr auto SERVER_HBM_M2M_5_REGISTER_DEV_ADDR = 13;
constexpr auto SERVER_HBM_M2M_5_REGISTER_FUNC_ADDR = 2;
constexpr auto SERVER_HBM_M2M_6_REGISTER_DEV_ADDR = 14;
constexpr auto SERVER_HBM_M2M_6_REGISTER_FUNC_ADDR = 2;
constexpr auto SERVER_HBM_M2M_7_REGISTER_DEV_ADDR = 15;
constexpr auto SERVER_HBM_M2M_7_REGISTER_FUNC_ADDR = 2;

constexpr auto SERVER_HBM_M2M_8_REGISTER_DEV_ADDR = 12;
constexpr auto SERVER_HBM_M2M_8_REGISTER_FUNC_ADDR = 3;
constexpr auto SERVER_HBM_M2M_9_REGISTER_DEV_ADDR = 13;
constexpr auto SERVER_HBM_M2M_9_REGISTER_FUNC_ADDR = 3;
constexpr auto SERVER_HBM_M2M_10_REGISTER_DEV_ADDR = 14;
constexpr auto SERVER_HBM_M2M_10_REGISTER_FUNC_ADDR = 3;
constexpr auto SERVER_HBM_M2M_11_REGISTER_DEV_ADDR = 15;
constexpr auto SERVER_HBM_M2M_11_REGISTER_FUNC_ADDR = 3;

constexpr auto SERVER_HBM_M2M_12_REGISTER_DEV_ADDR = 12;
constexpr auto SERVER_HBM_M2M_12_REGISTER_FUNC_ADDR = 4;
constexpr auto SERVER_HBM_M2M_13_REGISTER_DEV_ADDR = 13;
constexpr auto SERVER_HBM_M2M_13_REGISTER_FUNC_ADDR = 4;
constexpr auto SERVER_HBM_M2M_14_REGISTER_DEV_ADDR = 14;
constexpr auto SERVER_HBM_M2M_14_REGISTER_FUNC_ADDR = 4;
constexpr auto SERVER_HBM_M2M_15_REGISTER_DEV_ADDR = 15;
constexpr auto SERVER_HBM_M2M_15_REGISTER_FUNC_ADDR = 4;


// BHS B2CMI (M2M)
constexpr auto BHS_M2M_0_REGISTER_DEV_ADDR = 5;
constexpr auto BHS_M2M_0_REGISTER_FUNC_ADDR = 1;
constexpr auto BHS_M2M_1_REGISTER_DEV_ADDR = 5;
constexpr auto BHS_M2M_1_REGISTER_FUNC_ADDR = 2;
constexpr auto BHS_M2M_2_REGISTER_DEV_ADDR = 5;
constexpr auto BHS_M2M_2_REGISTER_FUNC_ADDR = 3;
constexpr auto BHS_M2M_3_REGISTER_DEV_ADDR = 5;
constexpr auto BHS_M2M_3_REGISTER_FUNC_ADDR = 4;
constexpr auto BHS_M2M_4_REGISTER_DEV_ADDR = 5;
constexpr auto BHS_M2M_4_REGISTER_FUNC_ADDR = 5;
constexpr auto BHS_M2M_5_REGISTER_DEV_ADDR = 5;
constexpr auto BHS_M2M_5_REGISTER_FUNC_ADDR = 6;
constexpr auto BHS_M2M_6_REGISTER_DEV_ADDR = 5;
constexpr auto BHS_M2M_6_REGISTER_FUNC_ADDR = 7;
constexpr auto BHS_M2M_7_REGISTER_DEV_ADDR = 6;
constexpr auto BHS_M2M_7_REGISTER_FUNC_ADDR = 1;
constexpr auto BHS_M2M_8_REGISTER_DEV_ADDR = 6;
constexpr auto BHS_M2M_8_REGISTER_FUNC_ADDR = 2;
constexpr auto BHS_M2M_9_REGISTER_DEV_ADDR = 6;
constexpr auto BHS_M2M_9_REGISTER_FUNC_ADDR = 3;
constexpr auto BHS_M2M_10_REGISTER_DEV_ADDR = 6;
constexpr auto BHS_M2M_10_REGISTER_FUNC_ADDR = 4;
constexpr auto BHS_M2M_11_REGISTER_DEV_ADDR = 6;
constexpr auto BHS_M2M_11_REGISTER_FUNC_ADDR = 5;

// BHS B2UPI (M3UPI)
constexpr auto BHS_M3UPI_PORT0_REGISTER_DEV_ADDR = 24;
constexpr auto BHS_M3UPI_PORT1_REGISTER_DEV_ADDR = 25;
constexpr auto BHS_M3UPI_PORT2_REGISTER_DEV_ADDR = 26;
constexpr auto BHS_M3UPI_PORT3_REGISTER_DEV_ADDR = 27;
constexpr auto BHS_M3UPI_PORT4_REGISTER_DEV_ADDR = 28;
constexpr auto BHS_M3UPI_PORT5_REGISTER_DEV_ADDR = 29;
constexpr auto BHS_M3UPI_PORT0_REGISTER_FUNC_ADDR = 0;
constexpr auto BHS_M3UPI_PORT1_REGISTER_FUNC_ADDR = 0;
constexpr auto BHS_M3UPI_PORT2_REGISTER_FUNC_ADDR = 0;
constexpr auto BHS_M3UPI_PORT3_REGISTER_FUNC_ADDR = 0;
constexpr auto BHS_M3UPI_PORT4_REGISTER_FUNC_ADDR = 0;
constexpr auto BHS_M3UPI_PORT5_REGISTER_FUNC_ADDR = 0;

constexpr auto SKX_M2M_PCI_PMON_BOX_CTL_ADDR = 0x258;

constexpr auto SKX_M2M_PCI_PMON_CTL0_ADDR = 0x228;
constexpr auto SKX_M2M_PCI_PMON_CTL1_ADDR = 0x230;
constexpr auto SKX_M2M_PCI_PMON_CTL2_ADDR = 0x238;
constexpr auto SKX_M2M_PCI_PMON_CTL3_ADDR = 0x240;

constexpr auto SKX_M2M_PCI_PMON_CTR0_ADDR = 0x200;
constexpr auto SKX_M2M_PCI_PMON_CTR1_ADDR = 0x208;
constexpr auto SKX_M2M_PCI_PMON_CTR2_ADDR = 0x210;
constexpr auto SKX_M2M_PCI_PMON_CTR3_ADDR = 0x218;

constexpr auto SERVER_M2M_PCI_PMON_BOX_CTL_ADDR = 0x438;

constexpr auto SERVER_M2M_PCI_PMON_CTL0_ADDR = 0x468;
constexpr auto SERVER_M2M_PCI_PMON_CTL1_ADDR = SERVER_M2M_PCI_PMON_CTL0_ADDR + 1*8;
constexpr auto SERVER_M2M_PCI_PMON_CTL2_ADDR = SERVER_M2M_PCI_PMON_CTL0_ADDR + 2*8;
constexpr auto SERVER_M2M_PCI_PMON_CTL3_ADDR = SERVER_M2M_PCI_PMON_CTL0_ADDR + 3*8;

constexpr auto SERVER_M2M_PCI_PMON_CTR0_ADDR = 0x440;
constexpr auto SERVER_M2M_PCI_PMON_CTR1_ADDR = SERVER_M2M_PCI_PMON_CTR0_ADDR + 1*8;
constexpr auto SERVER_M2M_PCI_PMON_CTR2_ADDR = SERVER_M2M_PCI_PMON_CTR0_ADDR + 2*8;
constexpr auto SERVER_M2M_PCI_PMON_CTR3_ADDR = SERVER_M2M_PCI_PMON_CTR0_ADDR + 3*8;

constexpr auto M3UPI_PCI_PMON_BOX_CTL_ADDR = (0xF4);

constexpr auto M3UPI_PCI_PMON_CTL0_ADDR = (0xD8);
constexpr auto M3UPI_PCI_PMON_CTL1_ADDR = (0xDC);
constexpr auto M3UPI_PCI_PMON_CTL2_ADDR = (0xE0);

constexpr auto M3UPI_PCI_PMON_CTR0_ADDR = (0xA0);
constexpr auto M3UPI_PCI_PMON_CTR1_ADDR = (0xA8);
constexpr auto M3UPI_PCI_PMON_CTR2_ADDR = (0xB0);

constexpr auto ICX_M3UPI_PCI_PMON_BOX_CTL_ADDR = (0xA0);

constexpr auto ICX_M3UPI_PCI_PMON_CTL0_ADDR = (0xD8);
constexpr auto ICX_M3UPI_PCI_PMON_CTL1_ADDR = (0xDC);
constexpr auto ICX_M3UPI_PCI_PMON_CTL2_ADDR = (0xE0);
constexpr auto ICX_M3UPI_PCI_PMON_CTL3_ADDR = (0xE4);

constexpr auto ICX_M3UPI_PCI_PMON_CTR0_ADDR = (0xA8);
constexpr auto ICX_M3UPI_PCI_PMON_CTR1_ADDR = (0xB0);
constexpr auto ICX_M3UPI_PCI_PMON_CTR2_ADDR = (0xB8);
constexpr auto ICX_M3UPI_PCI_PMON_CTR3_ADDR = (0xC0);

constexpr auto BHS_M3UPI_PCI_PMON_BOX_CTL_ADDR = (0x408);

constexpr auto BHS_M3UPI_PCI_PMON_CTL0_ADDR = (0x430);
constexpr auto BHS_M3UPI_PCI_PMON_CTL1_ADDR = (0x438);
constexpr auto BHS_M3UPI_PCI_PMON_CTL2_ADDR = (0x440);
constexpr auto BHS_M3UPI_PCI_PMON_CTL3_ADDR = (0x448);

constexpr auto BHS_M3UPI_PCI_PMON_CTR0_ADDR = (0x410);
constexpr auto BHS_M3UPI_PCI_PMON_CTR1_ADDR = (0x418);
constexpr auto BHS_M3UPI_PCI_PMON_CTR2_ADDR = (0x420);
constexpr auto BHS_M3UPI_PCI_PMON_CTR3_ADDR = (0x428);

constexpr auto MSR_UNCORE_PMON_GLOBAL_CTL = 0x700;

constexpr auto IVT_MSR_UNCORE_PMON_GLOBAL_CTL = 0x0C00;

constexpr auto SPR_MSR_UNCORE_PMON_GLOBAL_CTL = 0x2FF0;

constexpr auto PCM_INVALID_DEV_ADDR = ~(uint32)0UL;
constexpr auto PCM_INVALID_FUNC_ADDR = ~(uint32)0UL;

constexpr auto Q_P_PCI_PMON_BOX_CTL_ADDR = 0x0F4;

constexpr auto Q_P_PCI_PMON_CTL3_ADDR = 0x0E4;
constexpr auto Q_P_PCI_PMON_CTL2_ADDR = 0x0E0;
constexpr auto Q_P_PCI_PMON_CTL1_ADDR = 0x0DC;
constexpr auto Q_P_PCI_PMON_CTL0_ADDR = 0x0D8;

constexpr auto Q_P_PCI_PMON_CTR3_ADDR = 0x0B8;
constexpr auto Q_P_PCI_PMON_CTR2_ADDR = 0x0B0;
constexpr auto Q_P_PCI_PMON_CTR1_ADDR = 0x0A8;
constexpr auto Q_P_PCI_PMON_CTR0_ADDR = 0x0A0;

constexpr auto QPI_RATE_STATUS_ADDR = 0x0D4;

constexpr auto U_L_PCI_PMON_BOX_CTL_ADDR = 0x378;

constexpr auto U_L_PCI_PMON_CTL3_ADDR = 0x368;
constexpr auto U_L_PCI_PMON_CTL2_ADDR = 0x360;
constexpr auto U_L_PCI_PMON_CTL1_ADDR = 0x358;
constexpr auto U_L_PCI_PMON_CTL0_ADDR = 0x350;

constexpr auto U_L_PCI_PMON_CTR3_ADDR = 0x330;
constexpr auto U_L_PCI_PMON_CTR2_ADDR = 0x328;
constexpr auto U_L_PCI_PMON_CTR1_ADDR = 0x320;
constexpr auto U_L_PCI_PMON_CTR0_ADDR = 0x318;

constexpr auto ICX_UPI_PCI_PMON_BOX_CTL_ADDR = 0x318;

constexpr auto ICX_UPI_PCI_PMON_CTL3_ADDR = 0x368;
constexpr auto ICX_UPI_PCI_PMON_CTL2_ADDR = 0x360;
constexpr auto ICX_UPI_PCI_PMON_CTL1_ADDR = 0x358;
constexpr auto ICX_UPI_PCI_PMON_CTL0_ADDR = 0x350;

constexpr auto ICX_UPI_PCI_PMON_CTR3_ADDR = 0x338;
constexpr auto ICX_UPI_PCI_PMON_CTR2_ADDR = 0x330;
constexpr auto ICX_UPI_PCI_PMON_CTR1_ADDR = 0x328;
constexpr auto ICX_UPI_PCI_PMON_CTR0_ADDR = 0x320;
constexpr auto SPR_UPI_PCI_PMON_BOX_CTL_ADDR =  0x318;
constexpr auto SPR_UPI_PCI_PMON_CTL0_ADDR =     0x350;
constexpr auto SPR_UPI_PCI_PMON_CTR0_ADDR = 0x320;

constexpr auto UCLK_FIXED_CTR_ADDR = 0x704;
constexpr auto UCLK_FIXED_CTL_ADDR = 0x703;
constexpr auto UBOX_MSR_PMON_CTL0_ADDR = 0x705;
constexpr auto UBOX_MSR_PMON_CTL1_ADDR = 0x706;
constexpr auto UBOX_MSR_PMON_CTR0_ADDR = 0x709;
constexpr auto UBOX_MSR_PMON_CTR1_ADDR = 0x70a;

constexpr auto SPR_UCLK_FIXED_CTR_ADDR = 0x2FDF;
constexpr auto SPR_UCLK_FIXED_CTL_ADDR = 0x2FDE;
constexpr auto SPR_UBOX_MSR_PMON_BOX_CTL_ADDR = 0x2FD0;
constexpr auto SPR_UBOX_MSR_PMON_CTL0_ADDR = 0x2FD2;
constexpr auto SPR_UBOX_MSR_PMON_CTL1_ADDR = 0x2FD3;
constexpr auto SPR_UBOX_MSR_PMON_CTR0_ADDR = 0X2FD8;
constexpr auto SPR_UBOX_MSR_PMON_CTR1_ADDR = 0X2FD9;

constexpr auto BHS_UCLK_FIXED_CTR_ADDR = 0x3FFD;
constexpr auto BHS_UCLK_FIXED_CTL_ADDR = 0x3FFE;
constexpr auto BHS_UBOX_MSR_PMON_BOX_CTL_ADDR = 0x3FF0;
constexpr auto BHS_UBOX_MSR_PMON_CTL0_ADDR = 0x3FF2;
constexpr auto BHS_UBOX_MSR_PMON_CTL1_ADDR = 0x3FF3;
constexpr auto BHS_UBOX_MSR_PMON_CTR0_ADDR = 0x3FF8;
constexpr auto BHS_UBOX_MSR_PMON_CTR1_ADDR = 0x3FF9;

constexpr auto GRR_UCLK_FIXED_CTR_ADDR        = 0x3F5F;
constexpr auto GRR_UCLK_FIXED_CTL_ADDR        = 0x3F5E;
constexpr auto GRR_UBOX_MSR_PMON_BOX_CTL_ADDR = 0x3F50;
constexpr auto GRR_UBOX_MSR_PMON_CTL0_ADDR    = 0x3F52;
constexpr auto GRR_UBOX_MSR_PMON_CTL1_ADDR    = 0x3F53;
constexpr auto GRR_UBOX_MSR_PMON_CTR0_ADDR    = 0x3F58;
constexpr auto GRR_UBOX_MSR_PMON_CTR1_ADDR    = 0x3F59;

constexpr auto GRR_M2IOSF_IIO_UNIT_CTL = 0x2900;
constexpr auto GRR_M2IOSF_IIO_CTR0     = 0x2908;
constexpr auto GRR_M2IOSF_IIO_CTL0     = 0x2902;
constexpr auto GRR_M2IOSF_REG_STEP = 0x10;
constexpr auto GRR_M2IOSF_NUM      = 3;

constexpr auto JKTIVT_UCLK_FIXED_CTR_ADDR = (0x0C09);
constexpr auto JKTIVT_UCLK_FIXED_CTL_ADDR = (0x0C08);
constexpr auto JKTIVT_UBOX_MSR_PMON_CTL0_ADDR = (0x0C10);
constexpr auto JKTIVT_UBOX_MSR_PMON_CTL1_ADDR = (0x0C11);
constexpr auto JKTIVT_UBOX_MSR_PMON_CTR0_ADDR = (0x0C16);
constexpr auto JKTIVT_UBOX_MSR_PMON_CTR1_ADDR = (0x0C17);

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

#define SPR_UNC_PMON_UNIT_CTL_FRZ          (1 << 0)
#define SPR_UNC_PMON_UNIT_CTL_RST_CONTROL  (1 << 8)
#define SPR_UNC_PMON_UNIT_CTL_RST_COUNTERS (1 << 9)

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

static const uint32 ICX_CHA_MSR_PMON_BOX_CTL[] = {
    0x0E00, 0x0E0E, 0x0E1C, 0x0E2A, 0x0E38, 0x0E46, 0x0E54, 0x0E62, 0x0E70, 0x0E7E, 0x0E8C, 0x0E9A,
    0x0EA8, 0x0EB6, 0x0EC4, 0x0ED2, 0x0EE0, 0x0EEE, 0x0F0A, 0x0F18, 0x0F26, 0x0F34, 0x0F42, 0x0F50,
    0x0F5E, 0x0F6C, 0x0F7A, 0x0F88, 0x0F96, 0x0FA4, 0x0FB2, 0x0FC0, 0x0FCE, 0x0FDC, 0x0B60, 0x0B6E,
    0x0B7C, 0x0B8A, 0x0B98, 0x0BA6, 0x0BB4, 0x0BC2
};

static const uint32 SNR_CHA_MSR_PMON_BOX_CTL[] = {
    0x1C00, 0x1C10, 0x1C20, 0x1C30, 0x1C40, 0x1C50
};

#define SERVER_CHA_MSR_PMON_CTL0_OFFSET        (1)
/*
#define SERVER_CHA_MSR_PMON_CTL1_OFFSET        (2)
#define SERVER_CHA_MSR_PMON_CTL2_OFFSET        (3)
#define SERVER_CHA_MSR_PMON_CTL3_OFFSET        (4)
*/

#define SERVER_CHA_MSR_PMON_BOX_FILTER_OFFSET  (5)

#define SERVER_CHA_MSR_PMON_CTR0_OFFSET        (8)
/*
#define SERVER_CHA_MSR_PMON_CTR1_OFFSET        (9)
#define SERVER_CHA_MSR_PMON_CTR2_OFFSET        (10)
#define SERVER_CHA_MSR_PMON_CTR3_OFFSET        (11)
*/

constexpr auto SPR_CHA0_MSR_PMON_BOX_CTRL   = 0x2000;
constexpr auto SPR_CHA0_MSR_PMON_CTL0       = 0x2002;
constexpr auto SPR_CHA0_MSR_PMON_CTR0       = 0x2008;
constexpr auto SPR_CHA0_MSR_PMON_BOX_FILTER = 0x200E;
constexpr auto SPR_CHA_MSR_STEP = 0x10;

#define CBO_MSR_PMON_CTL_EVENT(x) (x << 0)
#define CBO_MSR_PMON_CTL_UMASK(x) (x << 8)
#define CBO_MSR_PMON_CTL_RST    (1 << 17)
#define CBO_MSR_PMON_CTL_EDGE_DET (1 << 18)
#define CBO_MSR_PMON_CTL_TID_EN (1 << 19)
#define CBO_MSR_PMON_CTL_EN (1 << 22)
#define CBO_MSR_PMON_CTL_INVERT (1 << 23)
#define CBO_MSR_PMON_CTL_THRESH(x) (x << 24UL)
#define UNC_PMON_CTL_UMASK_EXT(x) (uint64(x) << 32ULL)
#define UNC_PMON_CTL_EVENT(x) (x << 0)
#define UNC_PMON_CTL_UMASK(x) (x << 8)

#define JKT_CBO_MSR_PMON_BOX_FILTER_OPC(x) (x << 23UL)
#define IVTHSX_CBO_MSR_PMON_BOX_FILTER1_OPC(x) (x << 20UL)
#define BDX_CBO_MSR_PMON_BOX_GET_OPC0(x) ((x >> 20) & 0x3FF)
#define BDX_CBO_MSR_PMON_BOX_GET_FLT(x) ((x >> 0x10) & 0x1)
#define BDX_CBO_MSR_PMON_BOX_GET_TID(x) ((x >> 0x11) & 0x1)

#define SKX_CHA_MSR_PMON_BOX_FILTER1_REM(x) (x << 0UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_LOC(x) (x << 1UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_NM(x) (x << 4UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_NOT_NM(x) (x << 5UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_OPC0(x) ((x) << 9UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_OPC1(x) ((x) << 19UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_NC(x) (x << 30UL)
#define SKX_CHA_MSR_PMON_BOX_FILTER1_RSV(x) (x << 2UL)
#define SKX_CHA_MSR_PMON_BOX_GET_OPC0(x) ((x >> 9) & 0x3FF)
#define SKX_CHA_MSR_PMON_BOX_GET_NC(x) ((x >> 0x1e) & 0x1)

#define SKX_CHA_TOR_INSERTS_UMASK_IRQ(x) (x << 0)
#define SKX_CHA_TOR_INSERTS_UMASK_PRQ(x) (x << 2)
#define SKX_CHA_TOR_INSERTS_UMASK_HIT(x) (x << 4)
#define SKX_CHA_TOR_INSERTS_UMASK_MISS(x) (x << 5)

/*ICX UmaskExt Filter*/
#define ICX_CHA_UMASK_EXT(x)   (x << 32UL)

#define SKX_IIO_CBDMA_UNIT_STATUS   (0x0A47)
#define SKX_IIO_CBDMA_UNIT_CTL      (0x0A40)
#define SKX_IIO_CBDMA_CTR0          (0x0A41)
#define SKX_IIO_CBDMA_CLK           (0x0A45)
#define SKX_IIO_CBDMA_CTL0          (0x0A48)
#define SKX_IIO_PM_REG_STEP         (0x0020)

#define ICX_IIO_CBDMA_UNIT_STATUS   (0x0A57)
#define ICX_IIO_CTL_REG_OFFSET      (0x0008)
#define ICX_IIO_CTR_REG_OFFSET      (0x0001)
/*
 * M2IOSF MSRs in order:
 * M2IOSF0 - PCIe0 stack
 * M2IOSF1 - PCIe1 stack
 * M2IOSF2 - MCP stack
 * M2IOSF3 - PCIe2 stack
 * M2IOSF4 - PCIe3 stack
 * M2IOSF5 - CBDMA/DMI stack
 */
static const uint32 ICX_IIO_UNIT_CTL[] = {
    0x0A50, 0x0A70, 0x0A90, 0x0AE0, 0x0B00, 0x0B20
};

static const uint32 GRR_IRP_UNIT_CTL[] = {
    0x2A00,
    0x2A10,
    0x2A20
};

#define GRR_IRP_CTL_REG_OFFSET      (0x0002)
#define GRR_IRP_CTR_REG_OFFSET      (0x0008)

static const uint32 BHS_IRP_UNIT_CTL[] = {
    0x2A00,
    0x2A10,
    0x2A20,
    0x2A30,
    0x2A40,
    0x2A50,
    0x2A60,
    0x2A70,
    0x2A80,
    0x2A90,
    0x2AA0,
    0x2AB0,
    0x2AC0,
    0x2AD0,
    0x2AE0,
    0x2AF0
};

#define BHS_IRP_CTL_REG_OFFSET      (0x0002)
#define BHS_IRP_CTR_REG_OFFSET      (0x0008)

static const uint32 SPR_IRP_UNIT_CTL[] = {
    0x3400,
    0x3410,
    0x3420,
    0x3430,
    0x3440,
    0x3450,
    0x3460,
    0x3470,
    0x3480,
    0x3490,
    0x34A0,
    0x34B0
};

#define SPR_IRP_CTL_REG_OFFSET      (0x0002)
#define SPR_IRP_CTR_REG_OFFSET      (0x0008)

static const uint32 ICX_IRP_UNIT_CTL[] = {
    0x0A4A,
    0x0A6A,
    0x0A8A,
    0x0ADA,
    0x0AFA,
    0x0B1A
};

#define ICX_IRP_CTL_REG_OFFSET      (0x0003)
#define ICX_IRP_CTR_REG_OFFSET      (0x0001)


static const uint32 SNR_IRP_UNIT_CTL[] = {
    0x1EA0,
    0x1EB0,
    0x1EC0,
    0x1ED0,
    0x1EE0
};

#define SNR_IRP_CTL_REG_OFFSET      (0x0008)
#define SNR_IRP_CTR_REG_OFFSET      (0x0001)

static const uint32 SKX_IRP_UNIT_CTL[] = {
    0x0A58,
    0x0A78,
    0x0A98,
    0x0AB8,
    0x0AD8,
    0x0AF8
};

#define SKX_IRP_CTL_REG_OFFSET      (0x0003)
#define SKX_IRP_CTR_REG_OFFSET      (0x0001)

#define SNR_IIO_CBDMA_UNIT_STATUS   (0x1E07)
#define SNR_IIO_CBDMA_UNIT_CTL      (0x1E00)
#define SNR_IIO_CBDMA_CTR0          (0x1E01)
#define SNR_IIO_CBDMA_CTL0          (0x1E08)
#define SNR_IIO_PM_REG_STEP         (0x0010)

constexpr auto SPR_M2IOSF_IIO_UNIT_CTL = 0x3000;
constexpr auto SPR_M2IOSF_IIO_CTR0     = 0x3008;
constexpr auto SPR_M2IOSF_IIO_CTL0     = 0x3002;
constexpr auto SPR_M2IOSF_REG_STEP = 0x10;
constexpr auto SPR_M2IOSF_NUM      = 12;

constexpr auto BHS_M2IOSF_IIO_UNIT_CTL = 0x2900;
constexpr auto BHS_M2IOSF_IIO_CTR0     = 0x2908;
constexpr auto BHS_M2IOSF_IIO_CTL0     = 0x2902;
constexpr auto BHS_M2IOSF_REG_STEP = 0x10;
constexpr auto BHS_M2IOSF_NUM      = 16;

constexpr auto CXL_PMON_SIZE = 0x1000;

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

#define ICX_IIO_MSR_PMON_CTL_EVENT(x)   ((x) << 0)
#define ICX_IIO_MSR_PMON_CTL_UMASK(x)   ((x) << 8)
#define ICX_IIO_MSR_PMON_CTL_RST        (1 << 17)
#define ICX_IIO_MSR_PMON_CTL_EDGE_DET   (1 << 18)
#define ICX_IIO_MSR_PMON_CTL_OV_EN      (1 << 20)
#define ICX_IIO_MSR_PMON_CTL_EN         (1 << 22)
#define ICX_IIO_MSR_PMON_CTL_INVERT     (1 << 23)
#define ICX_IIO_MSR_PMON_CTL_THRESH(x)  ((x) << 24ULL)
#define ICX_IIO_MSR_PMON_CTL_CH_MASK(x) ((x) << 36ULL)
#define ICX_IIO_MSR_PMON_CTL_FC_MASK(x) ((x) << 48ULL)

#define M2M_PCI_PMON_CTL_EVENT(x)   ((x) << 0)
#define M2M_PCI_PMON_CTL_UMASK(x)   ((x) << 8)
#define M2M_PCI_PMON_CTL_RST        (1 << 17)
#define M2M_PCI_PMON_CTL_EDGE_DET   (1 << 18)
#define M2M_PCI_PMON_CTL_OV_EN      (1 << 20)
#define M2M_PCI_PMON_CTL_EN         (1 << 22)
#define M2M_PCI_PMON_CTL_INVERT     (1 << 23)
#define M2M_PCI_PMON_CTL_THRESH(x)  ((x) << 24ULL)

#define HA_PCI_PMON_CTL_EVENT(x)   ((x) << 0)
#define HA_PCI_PMON_CTL_UMASK(x)   ((x) << 8)
#define HA_PCI_PMON_CTL_RST        (1 << 17)
#define HA_PCI_PMON_CTL_EDGE_DET   (1 << 18)
#define HA_PCI_PMON_CTL_OV_EN      (1 << 20)
#define HA_PCI_PMON_CTL_EN         (1 << 22)
#define HA_PCI_PMON_CTL_INVERT     (1 << 23)
#define HA_PCI_PMON_CTL_THRESH(x)  ((x) << 24ULL)

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
    IIOPMUCNTCTLRegister(const uint64 v) : value(v) { }
    operator uint64() { return value; }
};

struct ICX_IIOPMUCNTCTLRegister
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
            uint64 ch_mask : 12;
            uint64 fc_mask : 3;
            uint64 reservedX : 13;
        } fields;
        uint64 value;
    };
    ICX_IIOPMUCNTCTLRegister() : value(0) { }
};

constexpr auto MSR_PACKAGE_THERM_STATUS = 0x01B1;
constexpr auto MSR_IA32_THERM_STATUS = 0x019C;
#ifndef KERNEL
constexpr auto PCM_INVALID_THERMAL_HEADROOM = (std::numeric_limits<int32_t>::min)();
#endif

constexpr auto MSR_IA32_BIOS_SIGN_ID = 0x8B;

constexpr auto MSR_DRAM_ENERGY_STATUS = 0x0619;
constexpr auto MSR_PP0_ENERGY_STATUS = 0x639;
constexpr auto MSR_PP1_ENERGY_STATUS = 0x641;

constexpr auto MSR_PKG_C2_RESIDENCY    = 0x60D;
constexpr auto MSR_PKG_C3_RESIDENCY    = 0x3F8;
constexpr auto MSR_PKG_C6_RESIDENCY    = 0x3F9;
constexpr auto MSR_PKG_C7_RESIDENCY    = 0x3FA;
constexpr auto MSR_CORE_C3_RESIDENCY   = 0x3FC;
constexpr auto MSR_CORE_C6_RESIDENCY   = 0x3FD;
constexpr auto MSR_CORE_C7_RESIDENCY   = 0x3FE;

constexpr auto MSR_PERF_GLOBAL_INUSE   = 0x392;

constexpr auto MSR_IA32_SPEC_CTRL         = 0x48;
constexpr auto MSR_IA32_ARCH_CAPABILITIES = 0x10A;

constexpr auto MSR_TSX_FORCE_ABORT = 0x10f;

constexpr auto MSR_PERF_CAPABILITIES = 0x345;

// data structure for converting two uint32s <-> uin64
union cvt_ds
{
#ifndef _MSC_VER
    typedef uint64 UINT64;
    typedef uint32 DWORD;
#endif
    UINT64 ui64;
    struct
    {
        DWORD low;
        DWORD high;
    } ui32;
};

#ifndef KERNEL

struct MCFGRecord
{
    unsigned long long baseAddress;
    unsigned short PCISegmentGroupNumber;
    unsigned char startBusNumber;
    unsigned char endBusNumber;
    char reserved[4];
    MCFGRecord() : baseAddress(0), PCISegmentGroupNumber(0), startBusNumber(0), endBusNumber(0)
    {
        std::fill(reserved, reserved + 4, 0);
    }
    void print()
    {
        std::cout << "BaseAddress=" << (std::hex) << "0x" << baseAddress << " PCISegmentGroupNumber=0x" << PCISegmentGroupNumber <<
            " startBusNumber=0x" << (unsigned)startBusNumber << " endBusNumber=0x" << (unsigned)endBusNumber << "\n" << std::dec;
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
        std::cout << "Header: length=" << length << " nrecords=" << nrecords() << "\n";
    }
};

#endif // #ifndef KERNEL


inline uint32 build_bit_ui(uint32 beg, uint32 end)
{
    assert(end <= 31);
    uint32 myll = 0;
    if (end > 31)
    {
        end = 31;
    }
    if (beg > 31)
    {
        return 0;
    }
    if (end == 31)
    {
        myll = (uint32)(-1);
    }
    else
    {
        myll = (1 << (end + 1)) - 1;
    }
    myll = myll >> beg;
    return myll;
}

inline uint32 extract_bits_ui(uint32 myin, uint32 beg, uint32 end)
{
    uint32 myll = 0;
    uint32 beg1, end1;

    // Let the user reverse the order of beg & end.
    if (beg <= end)
    {
        beg1 = beg;
        end1 = end;
    }
    else
    {
        beg1 = end;
        end1 = beg;
    }
    myll = myin >> beg1;
    myll = myll & build_bit_ui(beg1, end1);
    return myll;
}

inline uint64 build_bit(uint32 beg, uint32 end)
{
    uint64 myll = 0;
    if (end > 63)
    {
        end = 63;
    }
    if (end == 63)
    {
        myll = static_cast<uint64>(-1);
    }
    else
    {
        myll = (1LL << (end + 1)) - 1;
    }
    myll = myll >> beg;
    return myll;
}

inline uint64 extract_bits(uint64 myin, uint32 beg, uint32 end)
{
    uint64 myll = 0;
    uint32 beg1, end1;

    // Let the user reverse the order of beg & end.
    if (beg <= end)
    {
        beg1 = beg;
        end1 = end;
    }
    else
    {
        beg1 = end;
        end1 = beg;
    }
    myll = myin >> beg1;
    myll = myll & build_bit(beg1, end1);
    return myll;
}

union PCM_CPUID_INFO
{
    int array[4];
    struct { unsigned int eax, ebx, ecx, edx; } reg;
};

inline void pcm_cpuid(int leaf, PCM_CPUID_INFO& info)
{
#ifdef _MSC_VER
    // version for Windows
    __cpuid(info.array, leaf);
#else
    __asm__ __volatile__("cpuid" : \
        "=a" (info.reg.eax), "=b" (info.reg.ebx), "=c" (info.reg.ecx), "=d" (info.reg.edx) : "a" (leaf));
#endif
}

/* Adding the new version of cpuid with leaf and subleaf as an input */
inline void pcm_cpuid(const unsigned leaf, const unsigned subleaf, PCM_CPUID_INFO & info)
{
    #ifdef _MSC_VER
    __cpuidex(info.array, leaf, subleaf);
    #else
    __asm__ __volatile__ ("cpuid" : \
                          "=a" (info.reg.eax), "=b" (info.reg.ebx), "=c" (info.reg.ecx), "=d" (info.reg.edx) : "a" (leaf), "c" (subleaf));
    #endif
}

//IDX accel device/func number(PCIe).
//The device/function number from SPR register guide.
#define SPR_IDX_IAA_REGISTER_DEV_ADDR  (2)
#define SPR_IDX_IAA_REGISTER_FUNC_ADDR (0)

#define SPR_IDX_DSA_REGISTER_DEV_ADDR  (1)
#define SPR_IDX_DSA_REGISTER_FUNC_ADDR (0)

#define SPR_IDX_QAT_REGISTER_DEV_ADDR  (0)
#define SPR_IDX_QAT_REGISTER_FUNC_ADDR (0)

//IDX accel perfmon register offset
//The offset of register from DSA external architecture spec(intel-data-streaming-accelerator-preliminary-architecture-specification).
#define SPR_IDX_ACCEL_PCICMD_OFFSET        (0x4)
#define SPR_IDX_ACCEL_BAR0_OFFSET          (0x10)
#define SPR_IDX_ACCEL_BAR0_SIZE            (0x10000)
#define SPR_IDX_ACCEL_TABLE_OFFSET         (0x60)
#define SPR_IDX_ACCEL_PMON_BASE_OFFSET     (0x68)
#define SPR_IDX_ACCEL_PMON_BASE_MASK       (0xFFFF)
#define SPR_IDX_ACCEL_PMON_BASE_RATIO      (0x100)
#define SPR_IDX_ACCEL_PMCSR_OFFSET         (0x94)

#define SPR_IDX_PMON_RESET_CTL_OFFSET              (0x10)
#define SPR_IDX_PMON_FREEZE_CTL_OFFSET             (0x20)
#define SPR_IDX_PMON_CTL_OFFSET(x)                 (0x100 + ((x)*8))
#define SPR_IDX_PMON_CTR_OFFSET(x)                 (0x200 + ((x)*8))
#define SPR_IDX_PMON_FILTER_WQ_OFFSET(x)           (0x300 + ((x)*32))
#define SPR_IDX_PMON_FILTER_TC_OFFSET(x)           (0x304 + ((x)*32))
#define SPR_IDX_PMON_FILTER_PGSZ_OFFSET(x)         (0x308 + ((x)*32))
#define SPR_IDX_PMON_FILTER_XFERSZ_OFFSET(x)       (0x30C + ((x)*32))
#define SPR_IDX_PMON_FILTER_ENG_OFFSET(x)          (0x310 + ((x)*32))

//MSM device/func number and register offset from SPR register guide.
constexpr auto SPR_MSM_DEV_ID                       = 0x09a6;
constexpr auto SPR_MSM_DEV_ADDR                     = 0x03;
constexpr auto SPR_MSM_FUNC_ADDR                    = 0x00;
constexpr auto SPR_MSM_REG_CPUBUSNO_VALID_OFFSET    = 0x1a0;
constexpr auto SPR_MSM_REG_CPUBUSNO0_OFFSET         = 0x190;
constexpr auto SPR_MSM_REG_CPUBUSNO4_OFFSET         = 0x1c0;
constexpr auto SPR_MSM_CPUBUSNO_MAX                 = 32;

//SAD register offset from SPR register guide.
constexpr auto SPR_SAD_REG_CTL_CFG_OFFSET           = 0x3F4;

} // namespace pcm

#endif
