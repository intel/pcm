// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, Intel Corporation
// written by Austen Ott
//    
#define PcmMsrDriverClassName com_intel_driver_PcmMsr
#define kPcmMsrDriverClassName "com_intel_driver_PcmMsr"
#ifndef MSR_KERNEL_SHARED
#define MSR_KERNEL_SHARED
#include <stdint.h>
typedef struct {
    uint64_t value;
    uint32_t cpu_num;
    uint32_t msr_num;
} pcm_msr_data_t;

/*
// The topologyEntry struct that is used by PCM
typedef struct{
    uint32_t os_id;
    uint32_t socket;
    uint32_t core_id;
} topologyEntry;

// A kernel version of the topology entry structure. It has
// an extra unused int to explicitly align the struct on a 64bit
// boundary, preventing the compiler from adding extra padding.
enum {
    kOpenDriver,
    kCloseDriver,
    kReadMSR,
    kWriteMSR,
    kBuildTopology,
    kGetNumInstances,
    kIncrementNumInstances,
    kDecrementNumInstances,
    kNumberOfMethods 
};
*/
#endif
