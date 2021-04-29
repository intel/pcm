/*
Copyright (c) 2009-2018, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev
//            Austen Ott

#ifndef WIDTH_EXTENDER_HEADER_
#define WIDTH_EXTENDER_HEADER_

/*!     \file width_extender.h
        \brief Provides 64-bit "virtual" counters from underlying 32-bit HW counters
*/

#include <stdlib.h>
#include "cpucounters.h"
#include "utils.h"
#include "bw.h"
#include "mutex.h"
#include <memory>
#ifndef _MSC_VER
// the header can not be included into code using CLR
#include <thread>
#else
namespace std {
     class thread;
 }
#endif

namespace pcm {

class CounterWidthExtender
{
public:
    struct AbstractRawCounter
    {
        virtual uint64 operator () () = 0;
        virtual ~AbstractRawCounter() { }
    };

    struct MsrHandleCounter : public AbstractRawCounter
    {
        std::shared_ptr<SafeMsrHandle> msr;
        uint64 msr_addr;
        MsrHandleCounter(std::shared_ptr<SafeMsrHandle> msr_, uint64 msr_addr_) : msr(msr_), msr_addr(msr_addr_) { }
        uint64 operator () ()
        {
            uint64 value = 0;
            msr->read(msr_addr, &value);
            return value;
        }
    };

    template <uint64 (FreeRunningBWCounters::*F)()>
    struct ClientImcCounter : public AbstractRawCounter
    {
        std::shared_ptr<FreeRunningBWCounters> clientBW;
        ClientImcCounter(std::shared_ptr<FreeRunningBWCounters> clientBW_) : clientBW(clientBW_) { }
        uint64 operator () () { return (clientBW.get()->*F)(); }
    };

    typedef ClientImcCounter<&FreeRunningBWCounters::getImcReads> ClientImcReadsCounter;
    typedef ClientImcCounter<&FreeRunningBWCounters::getImcWrites> ClientImcWritesCounter;
    typedef ClientImcCounter<&FreeRunningBWCounters::getIoRequests> ClientIoRequestsCounter;

    struct MBLCounter : public AbstractRawCounter
    {
        std::shared_ptr<SafeMsrHandle> msr;
        MBLCounter(std::shared_ptr<SafeMsrHandle> msr_) : msr(msr_) { }
        uint64 operator () ()
        {
            msr->lock();
            uint64 event = 3; // L3 Local External Bandwidth
            uint64 msr_qm_evtsel = 0, value = 0;
            msr->read(IA32_QM_EVTSEL, &msr_qm_evtsel);
            //std::cout << "MBLCounter reading IA32_QM_EVTSEL 0x"<< std::hex << msr_qm_evtsel << std::dec << "\n";
            msr_qm_evtsel &= 0xfffffffffffffff0ULL;
            msr_qm_evtsel |= event & ((1ULL << 8) - 1ULL);
            //std::cout << "MBL event " << msr_qm_evtsel << "\n";
            //std::cout << "MBLCounter writing IA32_QM_EVTSEL 0x"<< std::hex << msr_qm_evtsel << std::dec << "\n";
            msr->write(IA32_QM_EVTSEL, msr_qm_evtsel);
            msr->read(IA32_QM_CTR, &value);
            //std::cout << "MBLCounter reading IA32_QM_CTR "<< std::dec << value << std::dec << "\n";
            msr->unlock();
            return value;
        }
    };

    struct MBTCounter : public AbstractRawCounter
    {
        std::shared_ptr<SafeMsrHandle> msr;
        MBTCounter(std::shared_ptr<SafeMsrHandle> msr_) : msr(msr_) { }
        uint64 operator () ()
        {
            msr->lock();
            uint64 event = 2; // L3 Total External Bandwidth
            uint64 msr_qm_evtsel = 0, value = 0;
            msr->read(IA32_QM_EVTSEL, &msr_qm_evtsel);
            //std::cout << "MBTCounter reading IA32_QM_EVTSEL 0x"<< std::hex << msr_qm_evtsel << std::dec << "\n";
            msr_qm_evtsel &= 0xfffffffffffffff0ULL;
            msr_qm_evtsel |= event & ((1ULL << 8) - 1ULL);
            //std::cout << "MBR event " << msr_qm_evtsel << "\n";
            //std::cout << "MBTCounter writing IA32_QM_EVTSEL 0x"<< std::hex << msr_qm_evtsel << std::dec << "\n";
            msr->write(IA32_QM_EVTSEL, msr_qm_evtsel);
            msr->read(IA32_QM_CTR, &value);
            //std::cout << "MBTCounter reading IA32_QM_CTR "<< std::dec << value << std::dec << "\n";
            msr->unlock();
            return value;
        }
    };

private:
    std::thread * UpdateThread;

    Mutex CounterMutex;

    AbstractRawCounter * raw_counter;
    uint64 extended_value;
    uint64 last_raw_value;
    uint64 counter_width;
    uint32 watchdog_delay_ms;

    CounterWidthExtender();                                           // forbidden
    CounterWidthExtender(CounterWidthExtender &);                     // forbidden
    CounterWidthExtender & operator = (const CounterWidthExtender &); // forbidden


    uint64 internal_read()
    {
        uint64 result = 0, new_raw_value = 0;
        CounterMutex.lock();

        new_raw_value = (*raw_counter)();
        if (new_raw_value < last_raw_value)
        {
            extended_value += ((1ULL << counter_width) - last_raw_value) + new_raw_value;
        }
        else
        {
            extended_value += (new_raw_value - last_raw_value);
        }

        last_raw_value = new_raw_value;

        result = extended_value;

        CounterMutex.unlock();
        return result;
    }

public:

    CounterWidthExtender(AbstractRawCounter * raw_counter_, uint64 counter_width_, uint32 watchdog_delay_ms_);
    virtual ~CounterWidthExtender();

    uint64 read() // read extended value
    {
        return internal_read();
    }
    void reset()
    {
        CounterMutex.lock();
        extended_value = last_raw_value = (*raw_counter)();
        CounterMutex.unlock();
    }
};

} // namespace pcm

#endif
