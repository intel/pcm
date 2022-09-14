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

// The topologyEntry struct that is used by PCM in the kernel.  It
// needs to be kept in sync with the one in cpucounters.h (at least,
// the first 3 fields).  Ideally we would just include that, but
// cpucounters.h has dependencies on the platform SDK and cannot be
// compiled in the kernel on macOS today.
typedef struct
{
    int32_t os_id;
    int32_t thread_id;
    int32_t core_id;
    int32_t tile_id;
    int32_t socket;
    int32_t native_cpu_model;
    int32_t core_type; // This is an enum in the userland structure.
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
