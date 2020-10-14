/*
Copyright (c) 2009-2012, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
//
// asynchonous CPU conters
//
// contact: Thomas Willhalm

#ifndef CPUASYNCHCOUNTER_HEADER
#define CPUASYNCHCOUNTER_HEADER


/*!     \file cpuasynchcounter.h
        \brief Implementation of a POSIX thread that periodically saves the current state of counters and exposes them to other threads
*/

#include <pthread.h>
#include <stdlib.h>
#include "cpucounters.h"

#define DELAY 1 // in seconds

using namespace pcm;

void * UpdateCounters(void *);

class AsynchronCounterState {
    PCM * m;

    CoreCounterState * cstates1, * cstates2;
    SocketCounterState * skstates1, * skstates2;
    SystemCounterState sstate1, sstate2;

    pthread_t UpdateThread;
    pthread_mutex_t CounterMutex;

    friend void * UpdateCounters(void *);

//	AsynchronCounterState(const& AsynchronCounterState); //unimplemeted
//	const& AsynchronCounterState operator=(const& AsynchronCounterState); //unimplemented

public:
    AsynchronCounterState()
    {
        m = PCM::getInstance();
        PCM::ErrorCode status = m->program();
        if (status != PCM::Success)
        {
            std::cerr << "\nCannot access CPU counters. Try to run pcm.x 1 to check the PMU access status.\n\n";
            exit(-1);
        }

        cstates1 = new  CoreCounterState[m->getNumCores()];
        cstates2 = new  CoreCounterState[m->getNumCores()];
        skstates1 = new SocketCounterState[m->getNumSockets()];
        skstates2 = new SocketCounterState[m->getNumSockets()];

        for (uint32 i = 0; i < m->getNumCores(); ++i) {
            cstates1[i] = getCoreCounterState(i);
            cstates2[i] = getCoreCounterState(i);
        }

        for (uint32 i = 0; i < m->getNumSockets(); ++i) {
            skstates1[i] = getSocketCounterState(i);
            skstates2[i] = getSocketCounterState(i);
        }

        pthread_mutex_init(&CounterMutex, NULL);
        pthread_create(&UpdateThread, NULL, UpdateCounters, this);
    }
    ~AsynchronCounterState()
    {
        pthread_cancel(UpdateThread);
        pthread_mutex_destroy(&CounterMutex);
        m->cleanup();
        delete[] cstates1;
        delete[] cstates2;
        delete[] skstates1;
        delete[] skstates2;
    }

    uint32 getNumCores()
    { return m->getNumCores(); }

    uint32 getNumSockets()
    { return m->getNumSockets(); }

    uint32 getQPILinksPerSocket()
    {
        return m->getQPILinksPerSocket();
    }

    uint32 getSocketId(uint32 c)
    {
        return m->getSocketId(c);
    }

    template <typename T, T func(CoreCounterState const &)>
    T get(uint32 core)
    {
        pthread_mutex_lock(&CounterMutex);
        T value = func(cstates2[core]);
        pthread_mutex_unlock(&CounterMutex);
        return value;
    }
    template <typename T, T func(CoreCounterState const &, CoreCounterState const &)>
    T get(uint32 core)
    {
        pthread_mutex_lock(&CounterMutex);
        T value = func(cstates1[core], cstates2[core]);
        pthread_mutex_unlock(&CounterMutex);
        return value;
    }

    template <typename T, T func(int, CoreCounterState const &, CoreCounterState const &)>
    T get(int param, uint32 core)
    {
        pthread_mutex_lock(&CounterMutex);
        T value = func(param, cstates1[core], cstates2[core]);
        pthread_mutex_unlock(&CounterMutex);
        return value;
    }

    template <typename T, T func(SocketCounterState const &)>
    T getSocket(uint32 socket)
    {
        pthread_mutex_lock(&CounterMutex);
        T value = func(skstates2[socket]);
        pthread_mutex_unlock(&CounterMutex);
        return value;
    }

    template <typename T, T func(SocketCounterState const &, SocketCounterState const &)>
    T getSocket(uint32 socket)
    {
        pthread_mutex_lock(&CounterMutex);
        T value = func(skstates1[socket], skstates2[socket]);
        pthread_mutex_unlock(&CounterMutex);
        return value;
    }

    template <typename T, T func(int, SocketCounterState const &, SocketCounterState const &)>
    T getSocket(int param, uint32 socket)
    {
        pthread_mutex_lock(&CounterMutex);
        T value = func(param, skstates1[socket], skstates2[socket]);
        pthread_mutex_unlock(&CounterMutex);
        return value;
    }

    template <typename T, T func(uint32, uint32, SystemCounterState const &, SystemCounterState const &)>
    T getSocket(uint32 socket, uint32 param)
    {
        pthread_mutex_lock(&CounterMutex);
        T value = func(socket, param, sstate1, sstate2);
        pthread_mutex_unlock(&CounterMutex);
        return value;
    }

    template <typename T, T func(SystemCounterState const &, SystemCounterState const &)>
    T getSystem()
    {
        pthread_mutex_lock(&CounterMutex);
        T value = func(sstate1, sstate2);
        pthread_mutex_unlock(&CounterMutex);
        return value;
    }

    template <typename T, T func(int, SystemCounterState const &, SystemCounterState const &)>
    T getSystem(int param)
    {
        pthread_mutex_lock(&CounterMutex);
        T value = func(param, sstate1, sstate2);
        pthread_mutex_unlock(&CounterMutex);
        return value;
    }
};

void * UpdateCounters(void * state)
{
    AsynchronCounterState * s = (AsynchronCounterState *)state;

    while (true) {
        pthread_mutex_lock(&(s->CounterMutex));
        for (uint32 core = 0; core < s->m->getNumCores(); ++core) {
            s->cstates1[core] = std::move(s->cstates2[core]);
            s->cstates2[core] = s->m->getCoreCounterState(core);
        }

        for (uint32 socket = 0; socket < s->m->getNumSockets(); ++socket) {
            s->skstates1[socket] = std::move(s->skstates2[socket]);
            s->skstates2[socket] = s->m->getSocketCounterState(socket);
        }

        s->sstate1 = std::move(s->sstate2);
        s->sstate2 = s->m->getSystemCounterState();

        pthread_mutex_unlock(&(s->CounterMutex));
        sleep(1);
    }
    return NULL;
}

#endif
