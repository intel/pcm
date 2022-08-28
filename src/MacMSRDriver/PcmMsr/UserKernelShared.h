// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, Intel Corporation
// written by Austen Ott
//    
#define PcmMsrDriverClassName com_intel_driver_PcmMsr
#define kPcmMsrDriverClassName "com_intel_driver_PcmMsr"
#ifndef USER_KERNEL_SHARED
#define USER_KERNEL_SHARED
#include <stdint.h>
typedef struct {
    uint64_t value;
    uint32_t cpu_num;
    uint32_t msr_num;
} pcm_msr_data_t;

typedef struct {
    uint64_t value;
    uint32_t msr_num;
    bool mask;
    char padding[115];
} k_pcm_msr_data_t;

// The topologyEntry struct that is used by PCM
typedef struct
{
    uint32_t os_id;
    uint32_t thread_id;
    uint32_t core_id;
    uint32_t tile_id;
    uint32_t socket;
    uint32_t native_cpu_model;
    uint32_t core_type; // This is an enum in the userland structure.
    uint32_t padding;
} topologyEntry;

enum {
    kOpenDriver,
    kCloseDriver,
    kReadMSR,
    kWriteMSR,
    kBuildTopology,
    kGetNumInstances,
    kIncrementNumInstances,
    kDecrementNumInstances,
    // PCI functions
    kRead,
    kWrite,
    kMapMemory,
    kUnmapMemory,
    kReadMemory,
    kNumberOfMethods
};
#endif
