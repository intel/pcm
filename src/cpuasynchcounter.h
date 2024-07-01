// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
//
// asynchronous CPU conters
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

    AsynchronCounterState(const AsynchronCounterState &) = delete;
    const AsynchronCounterState & operator = (const AsynchronCounterState &) = delete;

public:
    AsynchronCounterState()
    {
        m = PCM::getInstance();
        PCM::ErrorCode status = m->program();
        if (status != PCM::Success)
        {
            std::cerr << "\nCannot access CPU counters. Try to run 'pcm 1' to check the PMU access status.\n\n";
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
        if (pthread_mutex_destroy(&CounterMutex) != 0) std::cerr << "pthread_mutex_destroy failed\n";
        try {
            m->cleanup();
        } catch (const std::runtime_error & e)
        {
            std::cerr << "PCM Error in ~AsynchronCounterState(). Exception " << e.what() << "\n";
        }
        deleteAndNullifyArray(cstates1);
        deleteAndNullifyArray(cstates2);
        deleteAndNullifyArray(skstates1);
        deleteAndNullifyArray(skstates2);
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

    const char * getXpi() {
        return m->xPI();
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
        if (pthread_mutex_lock(&(s->CounterMutex)) != 0) std::cerr << "pthread_mutex_lock failed\n";
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

        if (pthread_mutex_unlock(&(s->CounterMutex)) != 0) std::cerr << "pthread_mutex_unlock failed\n";
        sleep(1);
    }
    return NULL;
}

#endif
