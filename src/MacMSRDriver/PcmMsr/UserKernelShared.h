// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, Intel Corporation
// written by Austen Ott
//
#define PcmMsrDriverClassName com_intel_driver_PcmMsr
#define kPcmMsrDriverClassName "com_intel_driver_PcmMsr"

#ifndef USER_KERNEL_SHARED
#define USER_KERNEL_SHARED

#define PCM_API

// kIOMainPortDefault is not supported before macOS Monterey
#if (MAC_OS_X_VERSION_MAX_ALLOWED < 120000)
    #define kIOMainPortDefault kIOMasterPortDefault
#endif

#include <stdint.h>
#include "../../topologyentry.h"

using namespace pcm;

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
