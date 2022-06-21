// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, Intel Corporation
// written by Austen Ott
//    
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <IOKit/IOKitLib.h>
#include "PcmMsr/UserKernelShared.h"

kern_return_t openMSRClient(io_connect_t connect);
kern_return_t closeMSRClient(io_connect_t connect);
kern_return_t readMSR(io_connect_t connect, pcm_msr_data_t* idata, size_t* idata_size, pcm_msr_data_t* odata, size_t* odata_size);
kern_return_t writeMSR(io_connect_t connect, pcm_msr_data_t* data, size_t* data_size);
kern_return_t getTopologyInfo(io_connect_t connect, topologyEntry* data, size_t* data_size);
kern_return_t getNumClients(io_connect_t connect, uint32_t* num_insts);
kern_return_t incrementNumClients(io_connect_t connect, uint32_t* num_insts);
kern_return_t decrementNumClients(io_connect_t connect, uint32_t* num_insts);