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
		uint32 numOfCores;
		uint32 numOfOnlineCores;
		uint32 numOfSockets;
		uint32 numOfOnlineSockets;
		uint32 numOfQPILinksPerSocket;
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
		uint64 coreId = 0;
		int32 socketId = 0;
		double instructionsPerCycle = 0.;
		uint64 cycles = 0;
		uint64 instructionsRetired = 0;
		double execUsage = 0.;
		double relativeFrequency = 0.;
		double activeRelativeFrequency = 0.;
		uint64 l3CacheMisses = 0;
		uint64 l3CacheReference = 0;
		uint64 l2CacheMisses = 0;
		double l3CacheHitRatio = 0.;
		double l2CacheHitRatio = 0.;
		double l3CacheMPI = 0.;
		double l2CacheMPI = 0.;
		bool l3CacheOccupancyAvailable;
		uint64 l3CacheOccupancy;
		bool localMemoryBWAvailable;
		uint64 localMemoryBW;
		bool remoteMemoryBWAvailable;
		uint64 remoteMemoryBW;
		uint64 localMemoryAccesses = 0;
		uint64 remoteMemoryAccesses = 0;
		int32 thermalHeadroom = 0;

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
		bool packageEnergyMetricsAvailable;
		double energyUsedBySockets[MAX_SOCKETS] ALIGN(ALIGNMENT);

	public:
		PCMCore() :
			packageEnergyMetricsAvailable(false) {
			for(int i = 0; i < MAX_SOCKETS; ++i)
			{
				energyUsedBySockets[i] = -1.0;
			}
		}
	} ALIGN(ALIGNMENT);

	typedef struct PCMCore PCMCore;

	struct PCMMemoryChannelCounter {
		float read;
		float write;
		float total;

	public:
		PCMMemoryChannelCounter() :
			read(-1.0),
			write(-1.0),
			total(-1.0) {}
	} ALIGN(ALIGNMENT);

	typedef struct PCMMemoryChannelCounter PCMMemoryChannelCounter;

	struct PCMMemorySocketCounter {
		uint64 socketId = 0;
		PCMMemoryChannelCounter channels[MEMORY_MAX_IMC_CHANNELS];
		uint32 numOfChannels;
		float read;     // DRAM read traffic in MBytes/sec
		float write;    // DRAM write traffic in MBytes/sec
        float pmmRead;  // PMM read traffic in MBytes/sec
        float pmmWrite; // PMM write traffic in MBytes/sec
		float total;    // total traffic in MBytes/sec
        float pmmMemoryModeHitRate; // PMM memory mode hit rate estimation. Metric value range is [0..1]
		double dramEnergy;

	public:
		PCMMemorySocketCounter() :
			numOfChannels(0),
			read(-1.0),
			write(-1.0),
            pmmRead(-1.0),
            pmmWrite(-1.0),
			total(-1.0),
            pmmMemoryModeHitRate(-1.0),
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
		uint64 bytes;
		double utilization;

	public:
		PCMQPILinkCounter() :
			bytes(0),
			utilization(-1.0) {}
	} ALIGN(ALIGNMENT);

	typedef struct PCMQPILinkCounter PCMQPILinkCounter;

	struct PCMQPISocketCounter {
		uint64 socketId = 0;
		PCMQPILinkCounter links[QPI_MAX_LINKS];
		uint64 total;

	public:
		PCMQPISocketCounter() :
			total(0) {}
	} ALIGN(ALIGNMENT);

	typedef struct PCMQPISocketCounter PCMQPISocketCounter;

	struct PCMQPI {
		PCMQPISocketCounter incoming[MAX_SOCKETS];
		uint64 incomingTotal;
		PCMQPISocketCounter outgoing[MAX_SOCKETS];
		uint64 outgoingTotal;
		bool incomingQPITrafficMetricsAvailable;
		bool outgoingQPITrafficMetricsAvailable;

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
		char version[VERSION_SIZE];
		uint64 lastUpdateTscBegin;
		uint64 timestamp;
		uint64 cyclesToGetPCMState;
		uint32 pollMs;
		SharedPCMCounters pcm;
		uint64 lastUpdateTscEnd;

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
