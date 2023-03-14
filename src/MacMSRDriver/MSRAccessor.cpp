// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, Intel Corporation
// written by Austen Ott
//
#include "MSRAccessor.h"
#include <exception>
#include <iostream>
#include <iomanip>

using namespace std;

MSRAccessor::MSRAccessor()
{
    service = IOServiceGetMatchingService(kIOMainPortDefault,
                                          IOServiceMatching(kPcmMsrDriverClassName));
    openConnection();
}

int32_t MSRAccessor::buildTopology(uint32_t num_cores, void* pTopos)
{
    size_t topology_struct_size = sizeof(TopologyEntry)*num_cores;

    kern_return_t ret = IOConnectCallStructMethod(connect, kBuildTopology,
                                                  NULL, 0,
                                                  pTopos, &topology_struct_size);
    return (ret == KERN_SUCCESS) ? 0 : -1;
}

int32_t MSRAccessor::read(uint32_t core_num, uint64_t msr_num, uint64_t * value)
{
    pcm_msr_data_t idatas, odatas;

    size_t struct_size = sizeof(pcm_msr_data_t);
    idatas.msr_num = (uint32_t)msr_num;
    idatas.cpu_num = core_num;

    kern_return_t ret = IOConnectCallStructMethod(connect, kReadMSR,
                                                  &idatas, struct_size,
                                                  &odatas, &struct_size);

    if(ret == KERN_SUCCESS)
    {
        *value = odatas.value;
        return sizeof(uint64_t);
    } else {
        return -1;
    }
}

int32_t MSRAccessor::write(uint32_t core_num, uint64_t msr_num, uint64_t value){
    pcm_msr_data_t idatas;

    idatas.value = value;
    idatas.msr_num = (uint32_t)msr_num;
    idatas.cpu_num = core_num;

    kern_return_t ret = IOConnectCallStructMethod(connect, kWriteMSR,
                                                  &idatas, sizeof(pcm_msr_data_t),
                                                  NULL, NULL);

    if(ret == KERN_SUCCESS)
    {
        return sizeof(uint64_t);
    } else {
        return -1;
    }
}

uint32_t MSRAccessor::getNumInstances()
{
    kern_return_t   kernResult;
    uint32_t        output_count = 1;
    uint64_t        knum_insts = 0;

    kernResult = IOConnectCallScalarMethod(connect,
                                           kGetNumInstances,
                                           NULL, 0,
                                           &knum_insts, &output_count);

    if (kernResult != KERN_SUCCESS)
    {
        cerr << "IOConnectCallScalarMethod returned 0x" << hex << setw(8) << kernResult << dec << endl;
    }
    // TODO add error handling; also, number-of-instance related
    // functions may go away as they do not appear to be used.
    return knum_insts;
}

uint32_t MSRAccessor::incrementNumInstances()
{
    kern_return_t   kernResult;
    uint32_t        output_count = 1;
    uint64_t        knum_insts = 0;

    kernResult = IOConnectCallScalarMethod(connect,
                                           kIncrementNumInstances,
                                           NULL, 0,
                                           &knum_insts, &output_count);

    if (kernResult != KERN_SUCCESS)
    {
        cerr << "IOConnectCallScalarMethod returned 0x" << hex << setw(8) << kernResult << dec << endl;
    }
    // TODO add error handling; also, these functions may go away as
    // they do not appear to be used.
    return knum_insts;
}

uint32_t MSRAccessor::decrementNumInstances()
{
    kern_return_t   kernResult;
    uint32_t        output_count = 1;
    uint64_t        knum_insts = 0;

    kernResult = IOConnectCallScalarMethod(connect, kDecrementNumInstances,
                                           NULL, 0,
                                           &knum_insts, &output_count);

    if (kernResult != KERN_SUCCESS)
    {
        cerr << "IOConnectCallScalarMethod returned 0x" << hex << setw(8) << kernResult << dec << endl;
    }
    // TODO add error handling; also, these functions may go away as
    // they do not appear to be used.
    return knum_insts;
}

MSRAccessor::~MSRAccessor()
{
    closeConnection();
}

kern_return_t MSRAccessor::openConnection()
{
    kern_return_t kernResult = IOServiceOpen(service, mach_task_self(), 0, &connect);

    if (kernResult != KERN_SUCCESS)
    {
        cerr << "IOServiceOpen returned 0x" << hex << setw(8) << kernResult << dec <<  endl;
    } else {
        kernResult = IOConnectCallScalarMethod(connect, kOpenDriver, NULL, 0, NULL, NULL);

        if (kernResult != KERN_SUCCESS)
        {
            cerr << "kOpenDriver returned 0x" << hex << setw(8) << kernResult << dec <<  endl;
        }
    }

    return kernResult;
}

void MSRAccessor::closeConnection()
{
    kern_return_t kernResult = IOConnectCallScalarMethod(connect, kCloseDriver,
                                                         NULL, 0, NULL, NULL);
    if (kernResult != KERN_SUCCESS)
    {
        cerr << "kCloseDriver returned 0x" << hex << setw(8) << kernResult << dec << endl;
    }

    kernResult = IOServiceClose(connect);
    if (kernResult != KERN_SUCCESS)
    {
        cerr << "IOServiceClose returned 0x" << hex << setw(8) << kernResult << dec << endl;
    }
}
