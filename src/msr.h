/*
Copyright (c) 2009-2012, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
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
    PCM_Util::Mutex mutex;

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

        throw std::exception();
        return -1;
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
        return 0;
    }
    uint32 getNumInstances()
    {
        if (pHandle)
            return pHandle->getNumInstances();

        throw std::exception();
        return 0;
    }
    uint32 incrementNumInstances()
    {
        if (pHandle)
            return pHandle->incrementNumInstances();

        throw std::exception();
        return 0;
    }
    uint32 decrementNumInstances()
    {
        if (pHandle)
            return pHandle->decrementNumInstances();

        throw std::exception();
        return 0;
    }
#endif
    virtual ~SafeMsrHandle()
    { }
};

#endif
