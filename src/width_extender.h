// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
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
#include "tpmi.h"
#include <atomic>
#include <exception>
#include <iostream>
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
        uint64 msr_mask;
        MsrHandleCounter(   std::shared_ptr<SafeMsrHandle> msr_,
                            const uint64 msr_addr_,
                            const uint64 msr_mask_ = ~uint64(0ULL)) :
            msr(msr_),
            msr_addr(msr_addr_),
            msr_mask(msr_mask_)
        {
        }
        uint64 operator () ()
        {
            uint64 value = 0;
            msr->read(msr_addr, &value);
            return value & msr_mask;
        }
    };

    //! \brief Raw counter reading a 64-bit TPMI register (see tpmi.h)
    struct TPMICounter : public AbstractRawCounter
    {
        std::shared_ptr<TPMIHandle> tpmiHandle;
        size_t entryPos;
        uint64 mask;
        TPMICounter(const std::shared_ptr<TPMIHandle> & tpmiHandle_,
                    const size_t entryPos_,
                    const uint64 mask_ = ~uint64(0ULL)) :
            tpmiHandle(tpmiHandle_),
            entryPos(entryPos_),
            mask(mask_)
        {
        }
        uint64 operator () ()
        {
            return tpmiHandle->read64(entryPos) & mask;
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
    typedef ClientImcCounter<&FreeRunningBWCounters::getGtRequests> ClientGtRequestsCounter;
    typedef ClientImcCounter<&FreeRunningBWCounters::getIaRequests> ClientIaRequestsCounter;
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
    //! \brief set by the destructor to request the watchdog thread to finish
    std::atomic<bool> stopUpdateThread{false};

    Mutex CounterMutex;

    AbstractRawCounter * raw_counter;
    uint64 extended_value;
    uint64 last_raw_value;
    uint64 counter_width;
    uint32 watchdog_delay_ms;
    std::atomic<bool> read_failed;

    CounterWidthExtender();                                           // forbidden
    CounterWidthExtender(CounterWidthExtender &);                     // forbidden
    CounterWidthExtender & operator = (const CounterWidthExtender &); // forbidden


    uint64 internal_read()
    {
        uint64 result = 0, new_raw_value = 0;
        CounterMutex.lock();

        try {
            new_raw_value = (*raw_counter)();
        }
        catch (const std::exception & e)
        {
            // keep the extended value intact and report the failure to the callers
            read_failed.store(true, std::memory_order_relaxed);
            result = extended_value;
            CounterMutex.unlock();
            std::cerr << "PCM Error: caught exception " << e.what() << " while reading a raw counter in CounterWidthExtender\n";
            return result;
        }
        read_failed.store(false, std::memory_order_relaxed);
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
    //! \brief asks the watchdog thread to finish without waiting for it
    //! \details The destructor still joins the thread. Calling this method on many
    //! CounterWidthExtender instances before destroying them lets their watchdog threads
    //! wake up and exit concurrently instead of paying the wake-up latency for every instance.
    void requestStopUpdateThread()
    {
        stopUpdateThread.store(true, std::memory_order_relaxed);
    }
    //! \brief asks the watchdog thread to finish and waits until it has exited
    //! \details After this method returns the watchdog thread does not access the
    //! raw counter (e.g. the RMID MSRs of MBL/MBT counters) anymore.
    void stopAndJoinUpdateThread();
    //! \brief returns true if the last attempt to read the raw counter failed
    bool readFailed() const
    {
        return read_failed.load(std::memory_order_relaxed);
    }
    void reset()
    {
        CounterMutex.lock();
        try {
            extended_value = last_raw_value = (*raw_counter)();
            read_failed.store(false, std::memory_order_relaxed);
        }
        catch (const std::exception & e)
        {
            // keep the extended value intact and report the failure to the callers
            read_failed.store(true, std::memory_order_relaxed);
            CounterMutex.unlock();
            std::cerr << "PCM Error: caught exception " << e.what() << " while resetting a raw counter in CounterWidthExtender\n";
            return;
        }
        CounterMutex.unlock();
    }
};

} // namespace pcm

#endif
