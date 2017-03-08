/*
   Copyright (c) 2009-2016, Intel Corporation
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
// written by Steven Briscoe

#include <cstdlib>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <time.h>

#include "daemon.h"
#include "common.h"
#include "pcm.h"

namespace PCMDaemon {

	int Daemon::sharedMemoryId_;
	SharedPCMState* Daemon::sharedPCMState_;

	Daemon::Daemon(int argc, char *argv[])
	: debugMode_(false), pollIntervalMs_(0), groupName_(""), mode_(Mode::DIFFERENCE), pcmInstance_(NULL)
	{
		allowedSubscribers_.push_back("core");
		allowedSubscribers_.push_back("memory");
		allowedSubscribers_.push_back("qpi");

		sharedMemoryId_ = 0;
		sharedPCMState_ = NULL;

		readApplicationArguments(argc, argv);

		setupPCM();
		
		setupSharedMemory();

		//Put the poll interval in shared memory so that the client knows
		sharedPCMState_->pollMs = pollIntervalMs_;

		updatePCMState(&systemStatesBefore_, &socketStatesBefore_, &coreStatesBefore_);

		serverUncorePowerStatesBefore_ = new ServerUncorePowerState[pcmInstance_->getNumSockets()];
		serverUncorePowerStatesAfter_ = new ServerUncorePowerState[pcmInstance_->getNumSockets()];
	}

	int Daemon::run()
	{
		std::cout << std::endl << "**** PCM Daemon Started *****" << std::endl;

		while(true)
		{
			if(debugMode_)
			{
                time_t rawtime;
                struct tm timeinfo;
                char timeBuffer[200];
                time(&rawtime);
                localtime_r(&rawtime, &timeinfo);

                snprintf(timeBuffer, 200, "[%02d %02d %04d %02d:%02d:%02d]", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

				std::cout << timeBuffer << "\tFetching counters..." << std::endl;
			}

			usleep(pollIntervalMs_ * 1000);

			getPCMCounters();
		}

		return EXIT_SUCCESS;
	}

	Daemon::~Daemon()
	{
		delete serverUncorePowerStatesBefore_;
		delete serverUncorePowerStatesAfter_;
	}

	void Daemon::setupPCM()
	{
		pcmInstance_ = PCM::getInstance();
		pcmInstance_->setBlocked(false);
		set_signal_handlers();
		set_post_cleanup_callback(&Daemon::cleanup);

		checkAccessAndProgramPCM();
	}

	void Daemon::checkAccessAndProgramPCM()
	{
	    PCM::ErrorCode status;

	    if(subscribers_.find("core") != subscribers_.end())
		{
		    EventSelectRegister defEventSelectRegister;
		    defEventSelectRegister.value = 0;
		    defEventSelectRegister.fields.usr = 1;
		    defEventSelectRegister.fields.os = 1;
		    defEventSelectRegister.fields.enable = 1;

		    uint32 numOfCustomCounters = 4;

		    EventSelectRegister regs[numOfCustomCounters];
		    PCM::ExtendedCustomCoreEventDescription conf;
		    conf.nGPCounters = numOfCustomCounters;
		    conf.gpCounterCfg = regs;

		    // TODO: This needs to be abstracted somewhere else
		    switch (pcmInstance_->getCPUModel())
		    {
		    case PCM::WESTMERE_EX:
		        conf.OffcoreResponseMsrValue[0] = 0x40FF;                // OFFCORE_RESPONSE.ANY_REQUEST.LOCAL_DRAM:  Offcore requests satisfied by the local DRAM
		        conf.OffcoreResponseMsrValue[1] = 0x20FF;                // OFFCORE_RESPONSE.ANY_REQUEST.REMOTE_DRAM: Offcore requests satisfied by a remote DRAM
		        break;
		    case PCM::JAKETOWN:
		    case PCM::IVYTOWN:
		        conf.OffcoreResponseMsrValue[0] = 0x780400000 | 0x08FFF; // OFFCORE_RESPONSE.*.LOCAL_DRAM
		        conf.OffcoreResponseMsrValue[1] = 0x7ff800000 | 0x08FFF; // OFFCORE_RESPONSE.*.REMOTE_DRAM
		        break;
		    case PCM::HASWELLX:
		        conf.OffcoreResponseMsrValue[0] = 0x600400000 | 0x08FFF; // OFFCORE_RESPONSE.*.LOCAL_DRAM
		        conf.OffcoreResponseMsrValue[1] = 0x63f800000 | 0x08FFF; // OFFCORE_RESPONSE.*.REMOTE_DRAM
		        break;
		    case PCM::BDX:
		        conf.OffcoreResponseMsrValue[0] = 0x0604008FFF; // OFFCORE_RESPONSE.*.LOCAL_DRAM
		        conf.OffcoreResponseMsrValue[1] = 0x067BC08FFF; // OFFCORE_RESPONSE.*.REMOTE_DRAM
		        break;
		    default:
		        std::cerr << std::endl << "PCM daemon does not support your processor currently." << std::endl << std::endl;
		        exit(EXIT_FAILURE);
		    }

			// Set default values for event select registers
			for (uint32 i(0); i < numOfCustomCounters; ++i)
				regs[i] = defEventSelectRegister;

			regs[0].fields.event_select = 0xB7; // OFFCORE_RESPONSE 0 event
			regs[0].fields.umask = 0x01;
			regs[1].fields.event_select = 0xBB; // OFFCORE_RESPONSE 1 event
			regs[1].fields.umask = 0x01;
			regs[2].fields.event_select = ARCH_LLC_MISS_EVTNR;
			regs[2].fields.umask = ARCH_LLC_MISS_UMASK;
			regs[3].fields.event_select = ARCH_LLC_REFERENCE_EVTNR;
			regs[3].fields.umask = ARCH_LLC_REFERENCE_UMASK;

		    status = pcmInstance_->program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf);
		}
		else
		{
			status = pcmInstance_->program();
		}

		switch (status)
		{
			case PCM::Success:
				break;
			case PCM::MSRAccessDenied:
				std::cerr << "Access to Intel(r) Performance Counter Monitor has denied (no MSR or PCI CFG space access)." << std::endl;
				exit(EXIT_FAILURE);
			case PCM::PMUBusy:
				std::cerr << "Access to Intel(r) Performance Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU." << std::endl;
				std::cerr << "Alternatively you can try to reset PMU configuration at your own risk. Try to reset? (y/n)" << std::endl;
				char yn;
				std::cin >> yn;
				if ('y' == yn)
				{
					pcmInstance_->resetPMU();
					std::cerr << "PMU configuration has been reset. Try to rerun the program again." << std::endl;
				}
				exit(EXIT_FAILURE);
			default:
				std::cerr << "Access to Intel(r) Performance Counter Monitor has denied (Unknown error)." << std::endl;
				exit(EXIT_FAILURE);
		}
	}

	void Daemon::readApplicationArguments(int argc, char *argv[])
	{
		int opt;
		int counterCount(0);

		if(argc == 1)
		{
			printExampleUsageAndExit(argv);
		}

		std::cout << std::endl;

		while ((opt = getopt(argc, argv, "p:c:dg:m:")) != -1)
		{
			switch (opt) {
			case 'p':
				pollIntervalMs_ = atoi(optarg);

				std::cout << "Polling every " << pollIntervalMs_ << "ms" << std::endl;
				break;
			case 'c':
				{
					std::string subscriber(optarg);

					if(subscriber == "all")
					{
						for(std::vector<std::string>::const_iterator it = allowedSubscribers_.begin(); it != allowedSubscribers_.end(); ++it)
						{
							subscribers_.insert(std::pair<std::string, uint32>(*it, 1));
							++counterCount;
						}
					}
					else
					{
						if(std::find(allowedSubscribers_.begin(), allowedSubscribers_.end(), subscriber) == allowedSubscribers_.end())
						{
							printExampleUsageAndExit(argv);
						}

						subscribers_.insert(std::pair<std::string, uint32>(subscriber, 1));
						++counterCount;
					}

					std::cout << "Listening to '" << subscriber << "' counters" << std::endl;
				}
				break;
			case 'd':
				debugMode_ = true;

				std::cout << "Debug mode enabled" << std::endl;
				break;
			case 'g':
				{
					groupName_ = std::string(optarg);

					std::cout << "Restricting to group: " << groupName_ << std::endl;
				}
				break;
			case 'm':
				{
					std::string mode = std::string(optarg);
					std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);

					if(mode == "difference")
					{
						mode_ = Mode::DIFFERENCE;
					}
					else if(mode == "absolute")
					{
						mode_ = Mode::ABSOLUTE;
					}
					else
					{
						printExampleUsageAndExit(argv);
					}

					std::cout << "Operational mode: " << mode << std::endl;
				}
				break;
			default:
				printExampleUsageAndExit(argv);
				break;
			}
		}

		if(pollIntervalMs_ <= 0 || counterCount == 0)
		{
			printExampleUsageAndExit(argv);
		}

		std::cout << "PCM Daemon version: " << VERSION << std::endl;
	}

	void Daemon::printExampleUsageAndExit(char *argv[])
	{
		std::cerr << std::endl;
		std::cerr << "-------------------------------------------------------------------" << std::endl;
		std::cerr << "Example usage: " << argv[0] << " -p 50 -c numa -c memory" << std::endl;
		std::cerr << "Poll every 50ms. Fetch counters for numa and memory" << std::endl << std::endl;

		std::cerr << "Example usage: " << argv[0] << " -p 250 -c all -g pcm -m absolute" << std::endl;
		std::cerr << "Poll every 250ms. Fetch all counters (core, numa & memory)." << std::endl;
		std::cerr << "Restrict access to user group 'pcm'. Store absolute values on each poll interval" << std::endl << std::endl;
		
		std::cerr << "-p <milliseconds> for poll frequency" << std::endl;
		std::cerr << "-c <counter> to request specific counters (Allowed counters: all ";

		for(std::vector<std::string>::const_iterator it = allowedSubscribers_.begin(); it != allowedSubscribers_.end(); ++it)
		{
			std::cerr << *it;

			if(it+1 != allowedSubscribers_.end())
			{
				std::cerr << " ";
			}
		}

		std::cerr << ")";

		std::cerr << std::endl << "-d flag for debug output [optional]" << std::endl;
		std::cerr << "-g <group> to restrict access to group [optional]" << std::endl;
		std::cerr << "-m <mode> stores differences or absolute values (Allowed: difference absolute) Default: difference [optional]" << std::endl << std::endl;

		exit(EXIT_FAILURE);
	}

	void Daemon::setupSharedMemory(key_t key)
	{
		int mode = 0660;
		int shmFlag = IPC_CREAT | mode;

		sharedMemoryId_ = shmget(key, sizeof(SharedPCMState), shmFlag);
		if (sharedMemoryId_ < 0)
		{
			std::cerr << "Failed to allocate shared memory segment (errno=" << errno << ")" << std::endl;
			exit(EXIT_FAILURE);
		}

		if(groupName_.size() > 0)
		{
			ushort gid = (ushort)resolveGroupName(groupName_);

			struct shmid_ds shmData;
			shmData.shm_perm.gid = gid;
			shmData.shm_perm.mode = mode;

			int success = shmctl(sharedMemoryId_, IPC_SET, &shmData);
			if(success < 0)
			{
				std::cerr << "Failed to IPC_SET (errno=" << errno << ")" << std::endl;
				exit(EXIT_FAILURE);
			}
		}

		sharedPCMState_ = (SharedPCMState*)shmat(sharedMemoryId_, NULL, 0);
		if (sharedPCMState_ == (void *)-1)
		{
			std::cerr << "Failed to attach shared memory segment (errno=" << errno << ")" << std::endl;
			exit(EXIT_FAILURE);
		}

		//Clear out shared memory
		std::memset(sharedPCMState_, 0, sizeof(SharedPCMState));
	}

	gid_t Daemon::resolveGroupName(std::string& groupName)
	{
		struct group* group = getgrnam(groupName.c_str());

		if(group == NULL)
		{
			std::cerr << "Failed to resolve group '" << groupName << "'" << std::endl;
			exit(EXIT_FAILURE);
		}

		return group->gr_gid;
	}

	void Daemon::getPCMCounters()
	{
		memcpy (sharedPCMState_->version, VERSION, sizeof(VERSION));
		sharedPCMState_->version[sizeof(VERSION)] = '\0';

		sharedPCMState_->lastUpdateTsc = RDTSC();
		sharedPCMState_->timestamp = getTimestamp();

		updatePCMState(&systemStatesAfter_, &socketStatesAfter_, &coreStatesAfter_);

		if(subscribers_.find("core") != subscribers_.end())
		{
			getPCMCore();
		}
		if(subscribers_.find("memory") != subscribers_.end())
		{
			getPCMMemory();
		}
		if(subscribers_.find("qpi") != subscribers_.end())
		{
			getPCMQPI();
		}

		sharedPCMState_->cyclesToGetPCMState = RDTSC() - sharedPCMState_->lastUpdateTsc;

		if(mode_ == Mode::DIFFERENCE)
		{
			swapPCMBeforeAfterState();
		}

		std::swap(collectionTimeBefore_, collectionTimeAfter_);
	}

	void Daemon::updatePCMState(SystemCounterState* systemStates, std::vector<SocketCounterState>* socketStates, std::vector<CoreCounterState>* coreStates)
	{
		if(subscribers_.find("core") != subscribers_.end())
		{
			pcmInstance_->getAllCounterStates(*systemStates, *socketStates, *coreStates);
		}
		else
		{
			if(subscribers_.find("memory") != subscribers_.end() || subscribers_.find("qpi") != subscribers_.end())
			{
				pcmInstance_->getUncoreCounterStates(*systemStates, *socketStates);
			}
		}
		collectionTimeAfter_ = pcmInstance_->getTickCount();
	}

	void Daemon::swapPCMBeforeAfterState()
	{
		//After state now becomes before state (for the next iteration)
		std::swap(coreStatesBefore_, coreStatesAfter_);
		std::swap(socketStatesBefore_, socketStatesAfter_);
		std::swap(systemStatesBefore_, systemStatesAfter_);
		std::swap(serverUncorePowerStatesBefore_, serverUncorePowerStatesAfter_);
	}

	void Daemon::getPCMCore()
	{
		PCMCore& core = sharedPCMState_->pcm.core;

		const uint32 numCores = pcmInstance_->getNumCores();

		uint32 onlineCoresI = 0;
		for(uint32 coreI = 0; coreI < numCores ; ++coreI)
		{
			if(!pcmInstance_->isCoreOnline(coreI))
				continue;

			PCMCoreCounter& coreCounters = core.cores[onlineCoresI];

			int32 socketId = pcmInstance_->getSocketId(coreI);
			double instructionsPerCycle = getIPC(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			uint64 cycles = getCycles(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			uint64 instructionsRetired = getInstructionsRetired(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			double execUsage = getExecUsage(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			double relativeFrequency = getRelativeFrequency(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			double activeRelativeFrequency = getActiveRelativeFrequency(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			uint64 l3CacheMisses = getNumberOfCustomEvents(2, coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			uint64 l3CacheReference = getNumberOfCustomEvents(3, coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			uint64 l2CacheMisses = getL2CacheMisses(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			double l3CacheHitRatio = getL3CacheHitRatio(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			double l2CacheHitRatio = getL2CacheHitRatio(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			double l3CacheMPI = double(l3CacheMisses) / instructionsRetired;
			double l2CacheMPI = double(l2CacheMisses) / instructionsRetired;
			int32 thermalHeadroom = coreStatesAfter_[coreI].getThermalHeadroom();

			coreCounters.coreId = coreI;
			coreCounters.socketId = socketId;
			coreCounters.instructionsPerCycle = instructionsPerCycle;
			coreCounters.cycles = cycles;
			coreCounters.instructionsRetired = instructionsRetired;
			coreCounters.execUsage = execUsage;
			coreCounters.relativeFrequency = relativeFrequency;
			coreCounters.activeRelativeFrequency = activeRelativeFrequency;
			coreCounters.l3CacheMisses = l3CacheMisses;
			coreCounters.l3CacheReference = l3CacheReference;
			coreCounters.l2CacheMisses = l2CacheMisses;
			coreCounters.l3CacheHitRatio = l3CacheHitRatio;
			coreCounters.l2CacheHitRatio = l2CacheHitRatio;
			coreCounters.l3CacheMPI = l3CacheMPI;
			coreCounters.l2CacheMPI = l2CacheMPI;
			coreCounters.thermalHeadroom = thermalHeadroom;

			coreCounters.l3CacheOccupancyAvailable = pcmInstance_->L3CacheOccupancyMetricAvailable();
			if (coreCounters.l3CacheOccupancyAvailable)
			{
				uint64 l3CacheOccupancy = getL3CacheOccupancy(coreStatesAfter_[coreI]);
				coreCounters.l3CacheOccupancy = l3CacheOccupancy;
			}

			coreCounters.localMemoryBWAvailable = pcmInstance_->CoreLocalMemoryBWMetricAvailable();
			if (coreCounters.localMemoryBWAvailable)
			{
				uint64 localMemoryBW = getLocalMemoryBW(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
				coreCounters.localMemoryBW = localMemoryBW;
			}

			coreCounters.remoteMemoryBWAvailable = pcmInstance_->CoreRemoteMemoryBWMetricAvailable();
			if (coreCounters.remoteMemoryBWAvailable)
			{
				uint64 remoteMemoryBW = getRemoteMemoryBW(coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
				coreCounters.remoteMemoryBW = remoteMemoryBW;
			}

			coreCounters.localMemoryAccesses = getNumberOfCustomEvents(0, coreStatesBefore_[coreI], coreStatesAfter_[coreI]);
			coreCounters.remoteMemoryAccesses = getNumberOfCustomEvents(1, coreStatesBefore_[coreI], coreStatesAfter_[coreI]);

			++onlineCoresI;
		}

		core.numOfCores = numCores;
		core.numOfOnlineCores = onlineCoresI;
		core.numOfSockets = pcmInstance_->getNumSockets();

		core.packageEnergyMetricsAvailable = pcmInstance_->packageEnergyMetricsAvailable();
		if(core.packageEnergyMetricsAvailable)
		{
			for (uint32 i(0); i < core.numOfSockets; ++i)
			{
				core.energyUsedBySockets[i] = getConsumedJoules(socketStatesBefore_[i], socketStatesAfter_[i]);
			}
		}
	}

	void Daemon::getPCMMemory()
	{
		pcmInstance_->disableJKTWorkaround();

        for(uint32 i(0); i < pcmInstance_->getNumSockets(); ++i)
        {
        	serverUncorePowerStatesAfter_[i] = pcmInstance_->getServerUncorePowerState(i);
        }

        calculateMemoryBandwidth(serverUncorePowerStatesBefore_, serverUncorePowerStatesAfter_, collectionTimeAfter_ - collectionTimeBefore_);

        PCMMemory memory = sharedPCMState_->pcm.memory;
        memory.dramEnergyMetricsAvailable = pcmInstance_->dramEnergyMetricsAvailable();
        if(memory.dramEnergyMetricsAvailable)
        {
			for (uint32 i(0); i < memory.numOfSockets; ++i)
			{
				memory.dramEnergyForSockets[i] = getDRAMConsumedJoules(socketStatesBefore_[i], socketStatesAfter_[i]);
			}
        }
	}

	void Daemon::calculateMemoryBandwidth(ServerUncorePowerState* uncState1, ServerUncorePowerState* uncState2, uint64 elapsedTime)
	{
		float iMC_Rd_socket_chan[MAX_SOCKETS][MEMORY_MAX_IMC_CHANNELS];
		float iMC_Wr_socket_chan[MAX_SOCKETS][MEMORY_MAX_IMC_CHANNELS];
		float iMC_Rd_socket[MAX_SOCKETS];
		float iMC_Wr_socket[MAX_SOCKETS];
		uint64 partial_write[MAX_SOCKETS];

		uint32 numOfSockets = pcmInstance_->getNumSockets();

		for(uint32 skt = 0; skt < numOfSockets; ++skt)
		{
			iMC_Rd_socket[skt] = 0.0;
			iMC_Wr_socket[skt] = 0.0;
			partial_write[skt] = 0;

			for(uint32 channel(0); channel < MEMORY_MAX_IMC_CHANNELS; ++channel)
			{
				if(getMCCounter(channel,MEMORY_READ,uncState1[skt],uncState2[skt]) == 0.0 && getMCCounter(channel,MEMORY_WRITE,uncState1[skt],uncState2[skt]) == 0.0) //In case of JKT-EN, there are only three channels. Skip one and continue.
				{
					iMC_Rd_socket_chan[skt][channel] = -1.0;
					iMC_Wr_socket_chan[skt][channel] = -1.0;
					continue;
				}

				iMC_Rd_socket_chan[skt][channel] = (float) (getMCCounter(channel,MEMORY_READ,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));
				iMC_Wr_socket_chan[skt][channel] = (float) (getMCCounter(channel,MEMORY_WRITE,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));

				iMC_Rd_socket[skt] += iMC_Rd_socket_chan[skt][channel];
				iMC_Wr_socket[skt] += iMC_Wr_socket_chan[skt][channel];

				partial_write[skt] += (uint64) (getMCCounter(channel,MEMORY_PARTIAL,uncState1[skt],uncState2[skt]) / (elapsedTime/1000.0));
			}
		}

		PCMMemory& memory = sharedPCMState_->pcm.memory;
		memory.numOfSockets = numOfSockets;

	    float sysRead(0.0);
	    float sysWrite(0.0);

	    for(uint32 skt = 0; skt < numOfSockets; ++skt)
		{
			uint64 currentChannelI(0);
	    	for(uint64 channel(0); channel < MEMORY_MAX_IMC_CHANNELS; ++channel)
			{
				if(iMC_Rd_socket_chan[0][skt*MEMORY_MAX_IMC_CHANNELS+channel] < 0.0 && iMC_Wr_socket_chan[0][skt*MEMORY_MAX_IMC_CHANNELS+channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
					continue;

				memory.sockets[skt].channels[currentChannelI].read = iMC_Rd_socket_chan[0][skt*MEMORY_MAX_IMC_CHANNELS+channel];
				memory.sockets[skt].channels[currentChannelI].write = iMC_Wr_socket_chan[0][skt*MEMORY_MAX_IMC_CHANNELS+channel];
				memory.sockets[skt].channels[currentChannelI].total = memory.sockets[skt].channels[currentChannelI].read + memory.sockets[skt].channels[currentChannelI].write;

				++currentChannelI;
			}

			memory.sockets[skt].read = iMC_Rd_socket[skt];
			memory.sockets[skt].write = iMC_Wr_socket[skt];
			memory.sockets[skt].partialWrite = partial_write[skt];
			memory.sockets[skt].total= iMC_Rd_socket[skt] + iMC_Wr_socket[skt];

			sysRead += iMC_Rd_socket[skt];
			sysWrite += iMC_Wr_socket[skt];
	    }

	    memory.system.read = sysRead;
	    memory.system.write = sysWrite;
	    memory.system.total = sysRead + sysWrite;
	}

	void Daemon::getPCMQPI()
	{
		PCMQPI& qpi = sharedPCMState_->pcm.qpi;

		qpi.numOfSockets = pcmInstance_->getNumSockets();
		qpi.numOfLinksPerSocket = pcmInstance_->getQPILinksPerSocket();

		qpi.incomingQPITrafficMetricsAvailable = pcmInstance_->getNumSockets() > 1 && pcmInstance_->incomingQPITrafficMetricsAvailable();
		if (qpi.incomingQPITrafficMetricsAvailable) // QPI info only for multi socket systems
		{
			int64 qpiLinks = qpi.numOfLinksPerSocket;

			for (uint32 i(0); i < qpi.numOfSockets; ++i)
			{
				uint64 total(0);
				for (uint32 l(0); l < qpiLinks; ++l)
				{
					qpi.outgoing[i].links[l].bytes = getIncomingQPILinkBytes(i, l, systemStatesBefore_, systemStatesAfter_);
					qpi.outgoing[i].links[l].utilization = getIncomingQPILinkUtilization(i, l, systemStatesBefore_, systemStatesAfter_);

					total+=qpi.incoming[i].links[l].bytes;
				}
				qpi.incoming[i].total = total;
			}

			qpi.outgoingTotal = getAllIncomingQPILinkBytes(systemStatesBefore_, systemStatesAfter_);
		}

		qpi.outgoingQPITrafficMetricsAvailable = pcmInstance_->getNumSockets() > 1 && pcmInstance_->outgoingQPITrafficMetricsAvailable();
		if (qpi.outgoingQPITrafficMetricsAvailable) // QPI info only for multi socket systems
		{
			uint64 qpiLinks = qpi.numOfLinksPerSocket;

			for (uint32 i(0); i < qpi.numOfSockets; ++i)
			{
				uint64 total(0);
				for (uint32 l(0); l < qpiLinks; ++l)
				{
					qpi.outgoing[i].links[l].bytes = getOutgoingQPILinkBytes(i, l, systemStatesBefore_, systemStatesAfter_);
					qpi.outgoing[i].links[l].utilization = getOutgoingQPILinkUtilization(i, l, systemStatesBefore_, systemStatesAfter_);

					total+=qpi.outgoing[i].links[l].bytes;
				}
				qpi.outgoing[i].total = total;
			}

			qpi.outgoingTotal = getAllOutgoingQPILinkBytes(systemStatesBefore_, systemStatesAfter_);
		}
	}

	uint64 Daemon::getTimestamp()
	{
		struct timespec now;

		clock_gettime(CLOCK_MONOTONIC_RAW, &now);

		uint64 epoch = (uint64)now.tv_sec * 10E9;
		epoch+=(uint64)now.tv_nsec;

		return epoch;
	}

	void Daemon::cleanup()
	{
		if(sharedPCMState_ != NULL)
		{
			// Detatch shared memory segment
			int success = shmdt(sharedPCMState_);
			if(success != 0)
			{
				std::cerr << "An error occurred when detatching the shared memory segment (errno=" << errno << ")" << std::endl;
			}
			else
			{
				// Delete segment
				success = shmctl(sharedMemoryId_, IPC_RMID, NULL);
				if(success != 0)
				{
					std::cerr << "An error occurred when deleting the shared memory segment (errno=" << errno << ")" << std::endl;
				}
			}
		}
	}

}
