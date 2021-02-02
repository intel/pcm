/*
   Copyright (c) 2009-2018, Intel Corporation
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
// written by Steven Briscoe

#ifndef COMMON_H_
#define COMMON_H_

#include <cstring>
#include <stdint.h>

static const char DEFAULT_SHM_ID_LOCATION[] = "/tmp/opcm-daemon-shm-id";
static const char VERSION[] = "1.0.5";

#define MAX_CPU_CORES 4096
#define MAX_SOCKETS 256
#define MEMORY_MAX_IMC_CHANNELS 8
#define MEMORY_READ 0
#define MEMORY_WRITE 1
#define MEMORY_READ_RANK_A 0
#define MEMORY_WRITE_RANK_A 1
#define MEMORY_READ_RANK_B 2
#define MEMORY_WRITE_RANK_B 3
#define MEMORY_PARTIAL 2
#define QPI_MAX_LINKS MAX_SOCKETS * 4

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
		float read;
		float write;
		float partialWrite;
		float total;
		double dramEnergy;

	public:
		PCMMemorySocketCounter() :
			numOfChannels(0),
			read(-1.0),
			write(-1.0),
			partialWrite(-1.0),
			total(-1.0),
			dramEnergy(0.0) {}
	} ALIGN(ALIGNMENT);

	typedef struct PCMMemorySocketCounter PCMMemorySocketCounter;

	struct PCMMemorySystemCounter {
		float read;
		float write;
		float total;

	public:
		PCMMemorySystemCounter() :
			read(-1.0),
			write(-1.0),
			total(-1.0) {}
	} ALIGN(ALIGNMENT);

	typedef struct PCMMemorySystemCounter PCMMemorySystemCounter;

	struct PCMMemory {
		PCMMemorySocketCounter sockets[MAX_SOCKETS];
		PCMMemorySystemCounter system;
		bool dramEnergyMetricsAvailable;

	public:
		PCMMemory() :
			dramEnergyMetricsAvailable(false) {}
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
				memset(this->version, '\0', sizeof(char)*VERSION_SIZE);
			}
	} ALIGN(ALIGNMENT);

	typedef struct SharedPCMState SharedPCMState;
}

#endif /* COMMON_H_ */
