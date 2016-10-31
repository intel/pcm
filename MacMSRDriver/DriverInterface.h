/*
 Copyright (c) 2012, Intel Corporation
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
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