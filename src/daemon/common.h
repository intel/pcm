// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2018,2022 Intel Corporation
// written by Steven Briscoe

#ifndef COMMON_H_
#define COMMON_H_

#include <cstring>
#include <stdint.h>

static const char DEFAULT_SHM_ID_LOCATION[] = "/tmp/opcm-daemon-shm-id";
static const char VERSION[] = "2.0.0";

#define MAX_CPU_CORES 4096
#define MAX_SOCKETS 256
#define MEMORY_MAX_IMC_CHANNELS (12)
#define MEMORY_READ 0
#define MEMORY_WRITE 1
#define QPI_MAX_LINKS (MAX_SOCKETS * 4)

#define VERSION_SIZE 12

#define ALIGNMENT 64
#define ALIGN(x) __attribute__((aligned((x))))

namespace PCMDaemon {
    typedef int int32;
    typedef long int64;
    typedef unsigned int uint32;
    typedef unsigned long uint64;

    struct PCMSystem {
        uint32 numOfCores;              // the number of logical cores in the system
        uint32 numOfOnlineCores;        // the number of online logical cores in the system
        uint32 numOfSockets;            // the number of CPU sockets in the system
        uint32 numOfOnlineSockets;      // the number of online CPU sockets in the system
        uint32 numOfQPILinksPerSocket;  // the number of QPI or UPI (xPI) links per socket
    public:
        PCMSystem() :
            numOfCores(0),
            numOfOnlineCores(0),
            numOfSockets(0),
            numOfOnlineSockets(0),
            numOfQPILinksPerSocket(0) {}
    } ALIGN(ALIGNMENT);

    typedef struct PCMSystem PCMSystem;

    struct PCMCoreCounter {
        uint64 coreId = 0;              // core ID
        int32 socketId = 0;             // socket ID
        double instructionsPerCycle = 0.; // instructions per cycle metric
        uint64 cycles = 0;                // cpu cycle metric
        uint64 instructionsRetired = 0;   // number of retired instructions metric
        double execUsage = 0.;            // instructions per nominal CPU cycle, i.e. in respect to the CPU frequency ignoring turbo and power saving
        double relativeFrequency = 0.;    // frequency relative to nominal CPU frequency (“clockticks”/”invariant timer ticks”)
        double activeRelativeFrequency = 0.; // frequency relative to nominal CPU frequency excluding the time when the CPU is sleeping
        uint64 l3CacheMisses = 0;            // L3 cache line misses
        uint64 l3CacheReference = 0;         // L3 cache line references (accesses)
        uint64 l2CacheMisses = 0;            // L2 cache line misses
        double l3CacheHitRatio = 0.;         // L3 cache hit ratio
        double l2CacheHitRatio = 0.;         // L2 cachhe hit ratio
        double l3CacheMPI = 0.;              // number of L3 cache misses per retired instruction
        double l2CacheMPI = 0.;              // number of L2 cache misses per retired instruction
        bool l3CacheOccupancyAvailable;      // true if L3 cache occupancy metric is available
        uint64 l3CacheOccupancy;             // L3 cache occupancy in  KBytes
        bool localMemoryBWAvailable;         // true if local memory bandwidth metric (L3 cache external bandwidth satisfied by local memory) is available
        uint64 localMemoryBW;                // L3 cache external bandwidth satisfied by local memory (in MBytes)
        bool remoteMemoryBWAvailable;        // true if remote memory bandwidth metric (L3 cache external bandwidth satisfied by remote memory) is available
        uint64 remoteMemoryBW;               // L3 cache external bandwidth satisfied by remote memory (in MBytes)
        uint64 localMemoryAccesses = 0;      // the number of local DRAM memory accesses
        uint64 remoteMemoryAccesses = 0;     // the number of remote DRAM memory accesses
        int32 thermalHeadroom = 0;           // thermal headroom in Kelvin (max design temperature – current temperature)

    public:
        PCMCoreCounter() :
            l3CacheOccupancyAvailable(false),
            l3CacheOccupancy(0),
            localMemoryBWAvailable(false),
            localMemoryBW(0),
            remoteMemoryBWAvailable(false),
            remoteMemoryBW(0) {}
    } ALIGN(ALIGNMENT);

    typedef struct PCMCoreCounter PCMCoreCounter;

    struct PCMCore {
        PCMCoreCounter cores[MAX_CPU_CORES];
        bool packageEnergyMetricsAvailable;  // true if CPU package (a.k.a. socket) energy metric is available
        double energyUsedBySockets[MAX_SOCKETS] ALIGN(ALIGNMENT); // energy consumed/used by CPU (socket) in Joules

    public:
        PCMCore() :
            packageEnergyMetricsAvailable(false) {
            for (int i = 0; i < MAX_SOCKETS; ++i)
            {
                energyUsedBySockets[i] = -1.0;
            }
        }
    } ALIGN(ALIGNMENT);

    typedef struct PCMCore PCMCore;

    struct PCMMemoryChannelCounter {
        float read;  // DRAM read traffic in MBytes/sec
        float write; // DRAM write traffic in MBytes/sec
        float total; // total traffic in MBytes/sec

    public:
        PCMMemoryChannelCounter() :
            read(-1.0),
            write(-1.0),
            total(-1.0) {}
    } ALIGN(ALIGNMENT);

    typedef struct PCMMemoryChannelCounter PCMMemoryChannelCounter;

    struct PCMMemorySocketCounter {
        uint64 socketId = 0; // socket ID
        PCMMemoryChannelCounter channels[MEMORY_MAX_IMC_CHANNELS];
        uint32 numOfChannels; // number of memory channels in the CPU socket
        float read;     // DRAM read traffic in MBytes/sec
        float write;    // DRAM write traffic in MBytes/sec
        float pmmRead;  // PMM read traffic in MBytes/sec
        float pmmWrite; // PMM write traffic in MBytes/sec
        float total;    // total traffic in MBytes/sec
        float memoryModeHitRate; // PMM memory mode hit rate estimation. Metric value range is [0..1]
        double dramEnergy; // energy consumed/used by DRAM memory in Joules

    public:
        PCMMemorySocketCounter() :
            numOfChannels(0),
            read(-1.0),
            write(-1.0),
            pmmRead(-1.0),
            pmmWrite(-1.0),
            total(-1.0),
            memoryModeHitRate(-1.0),
            dramEnergy(0.0) {}
    } ALIGN(ALIGNMENT);

    typedef struct PCMMemorySocketCounter PCMMemorySocketCounter;

    struct PCMMemorySystemCounter {
        float read;     // DRAM read traffic in MBytes/sec
        float write;    // DRAM write traffic in MBytes/sec
        float pmmRead;  // PMM read traffic in MBytes/sec
        float pmmWrite; // PMM write traffic in MBytes/sec
        float total;    // total traffic in MBytes/sec

    public:
        PCMMemorySystemCounter() :
            read(-1.0),
            write(-1.0),
            pmmRead(-1.0),
            pmmWrite(-1.0),
            total(-1.0) {}
    } ALIGN(ALIGNMENT);

    typedef struct PCMMemorySystemCounter PCMMemorySystemCounter;

    struct PCMMemory {
        PCMMemorySocketCounter sockets[MAX_SOCKETS];
        PCMMemorySystemCounter system;
        bool dramEnergyMetricsAvailable; // true if DRAM energy metrics are available
        bool pmmMetricsAvailable; // true if PMM metrics are available

    public:
        PCMMemory() :
            dramEnergyMetricsAvailable(false),
            pmmMetricsAvailable(false)
        {}
    } ALIGN(ALIGNMENT);

    typedef struct PCMMemory PCMMemory;

    struct PCMQPILinkCounter {
        uint64 bytes;  // bytes of certain traffic class transferred over QPI or UPI link
        double utilization; // utilization of the link caused by the certain traffic class

    public:
        PCMQPILinkCounter() :
            bytes(0),
            utilization(-1.0) {}
    } ALIGN(ALIGNMENT);

    typedef struct PCMQPILinkCounter PCMQPILinkCounter;

    struct PCMQPISocketCounter {
        uint64 socketId = 0; // socket ID
        PCMQPILinkCounter links[QPI_MAX_LINKS];
        uint64 total; // total number of transferred bytes of a certain traffic class

    public:
        PCMQPISocketCounter() :
            total(0) {}
    } ALIGN(ALIGNMENT);

    typedef struct PCMQPISocketCounter PCMQPISocketCounter;

    struct PCMQPI {
        PCMQPISocketCounter incoming[MAX_SOCKETS]; // incoming data traffic class statistics
        uint64 incomingTotal;                      // incoming data traffic total bytes
        PCMQPISocketCounter outgoing[MAX_SOCKETS]; // outgoing data+"non-data" traffic class statistics
        uint64 outgoingTotal;                      // outgoing data+"non-data" traffic total bytes
        bool incomingQPITrafficMetricsAvailable; // true if incoming data traffic class statistics metrics are available
        bool outgoingQPITrafficMetricsAvailable; // true if outgoing data+"non-data" class statistics metrics are available

    public:
        PCMQPI() :
            incomingTotal(0),
            outgoingTotal(0),
            incomingQPITrafficMetricsAvailable(false),
            outgoingQPITrafficMetricsAvailable(false) {}
    } ALIGN(ALIGNMENT);

    typedef struct PCMQPI PCMQPI;

    struct SharedPCMCounters {
        PCMSystem system;
        PCMCore core;
        PCMMemory memory;
        PCMQPI qpi;
    } ALIGN(ALIGNMENT);

    typedef struct SharedPCMCounters SharedPCMCounters;

    struct SharedPCMState {
        char version[VERSION_SIZE]; // version (null-terminated string)
        uint64 lastUpdateTscBegin;  // time stamp counter (TSC) obtained via rdtsc instruction *before* the state update
        uint64 timestamp;           // monotonic time since some unspecified starting point in nanoseconds *after* the state update
        uint64 cyclesToGetPCMState; // time it took to update the state measured in TSC cycles
        uint32 pollMs;              // the poll interval in shared memory in milliseconds
        SharedPCMCounters pcm;
        uint64 lastUpdateTscEnd;    // time stamp counter (TSC) obtained via rdtsc instruction *after* the state update

    public:
        SharedPCMState() :
            lastUpdateTscBegin(0),
            timestamp(0),
            cyclesToGetPCMState(0),
            pollMs(-1),
            lastUpdateTscEnd(0)
        {
            std::fill(this->version, this->version + VERSION_SIZE, 0);
        }
    } ALIGN(ALIGNMENT);

    typedef struct SharedPCMState SharedPCMState;
}

#endif /* COMMON_H_ */
