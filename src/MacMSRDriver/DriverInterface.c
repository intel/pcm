// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, Intel Corporation
// written by Austen Ott
//

#include "DriverInterface.h"

kern_return_t openMSRClient(io_connect_t connect)
{
        return IOConnectCallScalarMethod(connect, kOpenDriver, NULL, 0, NULL, NULL);
}

kern_return_t closeMSRClient(io_connect_t connect)
{
        return IOConnectCallScalarMethod(connect, kCloseDriver, NULL, 0, NULL, NULL);
}

kern_return_t readMSR(io_connect_t connect, pcm_msr_data_t* idata, size_t* idata_size,pcm_msr_data_t* odata, size_t* odata_size)
{
        return IOConnectCallStructMethod(connect, kReadMSR, idata, *idata_size, odata, odata_size);
}

kern_return_t writeMSR(io_connect_t connect, pcm_msr_data_t* data, size_t* idata_size)
{
        return IOConnectCallStructMethod(connect, kWriteMSR, (void*)data, *idata_size, NULL, NULL);
}

kern_return_t getTopologyInfo(io_connect_t connect, topologyEntry* data, size_t* data_size)
{
        return IOConnectCallStructMethod(connect, kBuildTopology, NULL, 0, data, data_size);
}

kern_return_t getNumClients(io_connect_t connect, uint32_t* num_insts)
{
	kern_return_t	kernResult;
        size_t          num_outputs = 1;
        uint64_t        knum_insts;

        kernResult = IOConnectCallStructMethod(connect,	kGetNumInstances, NULL, 0, &knum_insts, &num_outputs);
        *num_insts = (uint32_t)knum_insts;
        return kernResult;
}

kern_return_t incrementNumClients(io_connect_t connect, uint32_t* num_insts)
{
	kern_return_t	kernResult;
        size_t          num_outputs = 1;
        uint64_t        knum_insts;

        kernResult = IOConnectCallStructMethod(connect,	kIncrementNumInstances, NULL, 0, &knum_insts, &num_outputs);
        *num_insts = (uint32_t)knum_insts;
        return kernResult;
}

kern_return_t decrementNumClients(io_connect_t connect, uint32_t* num_insts)
{
	kern_return_t	kernResult;
        size_t          num_outputs = 1;
        uint64_t        knum_insts;

        kernResult = IOConnectCallStructMethod(connect,	kDecrementNumInstances, NULL, 0, &knum_insts, &num_outputs);
        *num_insts = (uint32_t)knum_insts;
        return kernResult;
}
