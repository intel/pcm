// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Roman Dementiev
//            Austen Ott

#ifndef CPUCounters_MSR_H
#define CPUCounters_MSR_H

/*!     \file msr.h
        \brief Low level interface to access hardware model specific registers

        Implemented and tested for Linux and 64-bit Windows 7
*/

#include "types.h"

#ifdef _MSC_VER
#include "windows.h"
#elif __APPLE__
#include <MSRAccessor.h>
#endif

#include "mutex.h"
#include <memory>

namespace pcm {

bool noMSRMode();

class MsrHandle
{
#ifdef _MSC_VER
    HANDLE hDriver;
#elif __APPLE__
    static MSRAccessor * driver;
    static int num_handles;
#else
    int32 fd;
#endif
    uint32 cpu_id;
    MsrHandle();                                // forbidden
    MsrHandle(const MsrHandle &);               // forbidden
    MsrHandle & operator = (const MsrHandle &); // forbidden

public:
    MsrHandle(uint32 cpu);
    int32 read(uint64 msr_number, uint64 * value);
    int32 write(uint64 msr_number, uint64 value);
    int32 getCoreId() { return (int32)cpu_id; }
#ifdef __APPLE__
    int32 buildTopology(uint32 num_cores, void *);
    uint32 getNumInstances();
    uint32 incrementNumInstances();
    uint32 decrementNumInstances();
#endif
    virtual ~MsrHandle();
};

class SafeMsrHandle
{
    std::shared_ptr<MsrHandle> pHandle;
    Mutex mutex;

    SafeMsrHandle(const SafeMsrHandle &);               // forbidden
    SafeMsrHandle & operator = (const SafeMsrHandle &); // forbidden

public:
    SafeMsrHandle() { }

    SafeMsrHandle(uint32 core_id) : pHandle(new MsrHandle(core_id))
    { }

    int32 read(uint64 msr_number, uint64 * value)
    {
        if (pHandle)
            return pHandle->read(msr_number, value);

        *value = 0;

        return (int32)sizeof(uint64);
    }

    int32 write(uint64 msr_number, uint64 value)
    {
        if (pHandle)
            return pHandle->write(msr_number, value);

        return (int32)sizeof(uint64);
    }

    int32 getCoreId()
    {
        if (pHandle)
            return pHandle->getCoreId();

        throw std::runtime_error("Core is offline");
    }

    void lock()
    {
        mutex.lock();
    }

    void unlock()
    {
        mutex.unlock();
    }

#ifdef __APPLE__
    int32 buildTopology(uint32 num_cores, void * p)
    {
        if (pHandle)
            return pHandle->buildTopology(num_cores, p);

        throw std::exception();
    }
    uint32 getNumInstances()
    {
        if (pHandle)
            return pHandle->getNumInstances();

        throw std::exception();
    }
    uint32 incrementNumInstances()
    {
        if (pHandle)
            return pHandle->incrementNumInstances();

        throw std::exception();
    }
    uint32 decrementNumInstances()
    {
        if (pHandle)
            return pHandle->decrementNumInstances();

        throw std::exception();
    }
#endif
    virtual ~SafeMsrHandle()
    { }
};

} // namespace pcm

#endif
