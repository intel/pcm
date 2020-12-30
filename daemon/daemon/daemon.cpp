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

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW             (4) /* needed for SLES11 */
#endif

#include "daemon.h"
#include "common.h"
#include "pcm.h"

namespace PCMDaemon {

	std::string Daemon::shmIdLocation_;
	int Daemon::sharedMemoryId_;
	SharedPCMState* Daemon::sharedPCMState_;

	Daemon::Daemon(int argc, char *argv[])
	: debugMode_(false), pollIntervalMs_(0), groupName_(""), mode_(Mode::DIFFERENCE), pcmInstance_(NULL)
	{
		allowedSubscribers_.push_back("core");
		allowedSubscribers_.push_back("memory");
		allowedSubscribers_.push_back("qpi");

		shmIdLocation_ = std::string(DEFAULT_SHM_ID_LOCATION);
		sharedMemoryId_ = 0;
		sharedPCMState_ = NULL;

		readApplicationArguments(argc, argv);
		setupSharedMemory();
		setupPCM();

		//Put the poll interval in shared memory so that the client knows
		sharedPCMState_->pollMs = pollIntervalMs_;

		updatePCMState(&systemStatesBefore_, &socketStatesBefore_, &coreStatesBefore_);
		systemStatesForQPIBefore_ = SystemCounterState(systemStatesBefore_);

		serverUncoreCounterStatesBefore_ = new ServerUncoreCounterState[pcmInstance_->getNumSockets()];
		serverUncoreCounterStatesAfter_ = new ServerUncoreCounterState[pcmInstance_->getNumSockets()];
	}

	int Daemon::run()
	{
		std::cout << "\n**** PCM Daemon Started *****\n";

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

				std::cout << timeBuffer << "\tFetching counters...\n";
			}

                        // Here to make sure that any output elsewhere in this class or its callees is flushed before the sleep
                        std::cout << std::flush;

			usleep(pollIntervalMs_ * 1000);

			getPCMCounters();
		}

		return EXIT_SUCCESS;
	}

	Daemon::~Daemon()
	{
		delete serverUncoreCounterStatesBefore_;
		delete serverUncoreCounterStatesAfter_;
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

			try {
				pcmInstance_->setupCustomCoreEventsForNuma(conf);
			}
			catch (UnsupportedProcessorException& e) {
		        std::cerr << "\nPCM daemon does not support your processor currently.\n\n";
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

            if (pcmInstance_->getMaxCustomCoreEvents() == 3)
            {
                conf.nGPCounters = 2; // drop LLC metrics
            }

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
				std::cerr << "Access to Intel(r) Performance Counter Monitor has denied (no MSR or PCI CFG space access).\n";
				exit(EXIT_FAILURE);
			case PCM::PMUBusy:
				std::cerr << "Access to Intel(r) Performance Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU.\n";
				std::cerr << "Alternatively you can try to reset PMU configuration at your own risk. Try to reset? (y/n)\n";
				char yn;
				std::cin >> yn;
				if ('y' == yn)
				{
					pcmInstance_->resetPMU();
					std::cerr << "PMU configuration has been reset. Try to rerun the program again.\n";
				}
				exit(EXIT_FAILURE);
			default:
				std::cerr << "Access to Intel(r) Performance Counter Monitor has denied (Unknown error).\n";
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

		std::cout << "\n";

		while ((opt = getopt(argc, argv, "p:c:dg:m:s:")) != -1)
		{
			switch (opt) {
			case 'p':
				pollIntervalMs_ = atoi(optarg);

				std::cout << "Polling every " << pollIntervalMs_ << "ms\n";
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

					std::cout << "Listening to '" << subscriber << "' counters\n";
				}
				break;
			case 'd':
				debugMode_ = true;

				std::cout << "Debug mode enabled\n";
				break;
			case 'g':
				{
					groupName_ = std::string(optarg);

					std::cout << "Restricting to group: " << groupName_ << "\n";
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

					std::cout << "Operational mode: " << mode_ << " (";

					if(mode_ == Mode::DIFFERENCE)
						std::cout << "difference";
					else if(mode_ == Mode::ABSOLUTE)
						std::cout << "absolute";

					std::cout << ")\n";
				}
				break;
			case 's':
				{
					shmIdLocation_ = std::string(optarg);

					std::cout << "Shared memory ID location: " << shmIdLocation_ << "\n";
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

		std::cout << "PCM Daemon version: " << VERSION << "\n\n";
	}

	void Daemon::printExampleUsageAndExit(char *argv[])
	{
		std::cerr << "\n";
		std::cerr << "-------------------------------------------------------------------\n";
		std::cerr << "Example usage: " << argv[0] << " -p 50 -c numa -c memory\n";
		std::cerr << "Poll every 50ms. Fetch counters for numa and memory\n\n";

		std::cerr << "Example usage: " << argv[0] << " -p 250 -c all -g pcm -m absolute\n";
		std::cerr << "Poll every 250ms. Fetch all counters (core, numa & memory).\n";
		std::cerr << "Restrict access to user group 'pcm'. Store absolute values on each poll interval\n\n";

		std::cerr << "-p <milliseconds> for poll frequency\n";
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

		std::cerr << "\n-d flag for debug output [optional]\n";
		std::cerr << "-g <group> to restrict access to group [optional]\n";
		std::cerr << "-m <mode> stores differences or absolute values (Allowed: difference absolute) Default: difference [optional]\n";
		std::cerr << "-s <filepath> to store shared memory ID Default: " << std::string(DEFAULT_SHM_ID_LOCATION) << " [optional]\n";

		std::cerr << "\n";

		exit(EXIT_FAILURE);
	}

	void Daemon::setupSharedMemory()
	{
		int mode = 0660;
		int shmFlag = IPC_CREAT | mode;

		sharedMemoryId_ = shmget(IPC_PRIVATE, sizeof(SharedPCMState), shmFlag);
		if (sharedMemoryId_ < 0)
		{
			std::cerr << "Failed to allocate shared memory segment (errno=" << errno << ")\n";
			exit(EXIT_FAILURE);
		}

		//Store shm id in a file (shmIdLocation_)
		FILE *fp = fopen (shmIdLocation_.c_str(), "w");
		if (!fp)
		{
			std::cerr << "Failed to create/write to shared memory key location: " << shmIdLocation_ << "\n";
			exit(EXIT_FAILURE);
		}
		fprintf (fp, "%i", sharedMemoryId_);
		fclose (fp);

		if(groupName_.size() > 0)
		{
			ushort gid = (ushort)resolveGroupName(groupName_);

			struct shmid_ds shmData;
			shmData.shm_perm.gid = gid;
			shmData.shm_perm.mode = mode;

			int success = shmctl(sharedMemoryId_, IPC_SET, &shmData);
			if(success < 0)
			{
				std::cerr << "Failed to IPC_SET (errno=" << errno << ")\n";
				exit(EXIT_FAILURE);
			}

			//Change group of shared memory ID file
			uid_t uid = geteuid();
			success = chown(shmIdLocation_.c_str(), uid, gid);
			if(success < 0)
			{
				std::cerr << "Failed to change ownership of shared memory key location: " << shmIdLocation_ << "\n";
				exit(EXIT_FAILURE);
			}
		}

		sharedPCMState_ = (SharedPCMState*)shmat(sharedMemoryId_, NULL, 0);
		if (sharedPCMState_ == (void *)-1)
		{
			std::cerr << "Failed to attach shared memory segment (errno=" << errno << ")\n";
			exit(EXIT_FAILURE);
		}

		//Clear out shared memory
		sharedPCMState_ = new (sharedPCMState_) SharedPCMState(); // use placement new operator
	}

	gid_t Daemon::resolveGroupName(const std::string& groupName)
	{
		struct group* group = getgrnam(groupName.c_str());

		if(group == NULL)
		{
			std::cerr << "Failed to resolve group '" << groupName << "'\n";
			exit(EXIT_FAILURE);
		}

		return group->gr_gid;
	}

	void Daemon::getPCMCounters()
	{
		memcpy (sharedPCMState_->version, VERSION, sizeof(VERSION));
		sharedPCMState_->version[sizeof(VERSION)] = '\0';

        sharedPCMState_->lastUpdateTscBegin = RDTSC();

		updatePCMState(&systemStatesAfter_, &socketStatesAfter_, &coreStatesAfter_);

		getPCMSystem();

		if(subscribers_.find("core") != subscribers_.end())
		{
			getPCMCore();
		}
		if(subscribers_.find("memory") != subscribers_.end())
		{
			getPCMMemory();
		}
		bool fetchQPICounters = subscribers_.find("qpi") != subscribers_.end();
		if(fetchQPICounters)
		{
			getPCMQPI();
		}

		const auto lastUpdateTscEnd = RDTSC();
		sharedPCMState_->cyclesToGetPCMState = lastUpdateTscEnd - sharedPCMState_->lastUpdateTscBegin;
		sharedPCMState_->timestamp = getTimestamp();

		// As the client polls this timestamp (lastUpdateTsc)
		// All the data has to be in shm before
		sharedPCMState_->lastUpdateTscEnd = lastUpdateTscEnd;
		if(mode_ == Mode::DIFFERENCE)
		{
			swapPCMBeforeAfterState();
		}
		if(fetchQPICounters)
		{
			systemStatesForQPIBefore_ = SystemCounterState(systemStatesAfter_);
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
		std::swap(serverUncoreCounterStatesBefore_, serverUncoreCounterStatesAfter_);
	}

	void Daemon::getPCMSystem()
	{
		PCMSystem& system = sharedPCMState_->pcm.system;
		system.numOfCores = pcmInstance_->getNumCores();
		system.numOfOnlineCores = pcmInstance_->getNumOnlineCores();
		system.numOfSockets = pcmInstance_->getNumSockets();
		system.numOfOnlineSockets = pcmInstance_->getNumOnlineSockets();
		system.numOfQPILinksPerSocket = pcmInstance_->getQPILinksPerSocket();
	}

	void Daemon::getPCMCore()
	{
		PCMCore& core = sharedPCMState_->pcm.core;

		const uint32 numCores = sharedPCMState_->pcm.system.numOfCores;

		uint32 onlineCoresI(0);
		for(uint32 coreI(0); coreI < numCores ; ++coreI)
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

		const uint32 numSockets = sharedPCMState_->pcm.system.numOfSockets;

		core.packageEnergyMetricsAvailable = pcmInstance_->packageEnergyMetricsAvailable();
		if(core.packageEnergyMetricsAvailable)
		{
			for (uint32 i(0); i < numSockets; ++i)
			{
				core.energyUsedBySockets[i] = getConsumedJoules(socketStatesBefore_[i], socketStatesAfter_[i]);
			}
		}
	}

	void Daemon::getPCMMemory()
	{
		pcmInstance_->disableJKTWorkaround();

		PCMMemory& memory = sharedPCMState_->pcm.memory;
		memory.dramEnergyMetricsAvailable = pcmInstance_->dramEnergyMetricsAvailable();

		const uint32 numSockets = sharedPCMState_->pcm.system.numOfSockets;

        for(uint32 i(0); i < numSockets; ++i)
        {
        	serverUncoreCounterStatesAfter_[i] = pcmInstance_->getServerUncoreCounterState(i);
        }

        uint64 elapsedTime = collectionTimeAfter_ - collectionTimeBefore_;

		float iMC_Rd_socket_chan[MAX_SOCKETS][MEMORY_MAX_IMC_CHANNELS];
		float iMC_Wr_socket_chan[MAX_SOCKETS][MEMORY_MAX_IMC_CHANNELS];
		float iMC_Rd_socket[MAX_SOCKETS];
		float iMC_Wr_socket[MAX_SOCKETS];
		uint64 partial_write[MAX_SOCKETS];

		for(uint32 skt(0); skt < numSockets; ++skt)
		{
			iMC_Rd_socket[skt] = 0.0;
			iMC_Wr_socket[skt] = 0.0;
			partial_write[skt] = 0;

			for(uint32 channel(0); channel < MEMORY_MAX_IMC_CHANNELS; ++channel)
			{
				//In case of JKT-EN, there are only three channels. Skip one and continue.
				bool memoryReadAvailable = getMCCounter(channel,MEMORY_READ,serverUncoreCounterStatesBefore_[skt],serverUncoreCounterStatesAfter_[skt]) == 0.0;
				bool memoryWriteAvailable = getMCCounter(channel,MEMORY_WRITE,serverUncoreCounterStatesBefore_[skt],serverUncoreCounterStatesAfter_[skt]) == 0.0;
				if(memoryReadAvailable && memoryWriteAvailable)
				{
					iMC_Rd_socket_chan[skt][channel] = -1.0;
					iMC_Wr_socket_chan[skt][channel] = -1.0;
					continue;
				}

				iMC_Rd_socket_chan[skt][channel] = (float) (getMCCounter(channel,MEMORY_READ,serverUncoreCounterStatesBefore_[skt],serverUncoreCounterStatesAfter_[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));
				iMC_Wr_socket_chan[skt][channel] = (float) (getMCCounter(channel,MEMORY_WRITE,serverUncoreCounterStatesBefore_[skt],serverUncoreCounterStatesAfter_[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));

				iMC_Rd_socket[skt] += iMC_Rd_socket_chan[skt][channel];
				iMC_Wr_socket[skt] += iMC_Wr_socket_chan[skt][channel];

				partial_write[skt] += (uint64) (getMCCounter(channel,MEMORY_PARTIAL,serverUncoreCounterStatesBefore_[skt],serverUncoreCounterStatesAfter_[skt]) / (elapsedTime/1000.0));
			}
		}

	    float systemRead(0.0);
	    float systemWrite(0.0);

	    uint32 onlineSocketsI(0);
	    for(uint32 skt (0); skt < numSockets; ++skt)
		{
			if(!pcmInstance_->isSocketOnline(skt))
				continue;

			uint64 currentChannelI(0);
	    	for(uint64 channel(0); channel < MEMORY_MAX_IMC_CHANNELS; ++channel)
			{
				//If the channel read neg. value, the channel is not working; skip it.
				if(iMC_Rd_socket_chan[0][skt*MEMORY_MAX_IMC_CHANNELS+channel] < 0.0 && iMC_Wr_socket_chan[0][skt*MEMORY_MAX_IMC_CHANNELS+channel] < 0.0)
					continue;

				float socketChannelRead = iMC_Rd_socket_chan[0][skt*MEMORY_MAX_IMC_CHANNELS+channel];
				float socketChannelWrite = iMC_Wr_socket_chan[0][skt*MEMORY_MAX_IMC_CHANNELS+channel];

				memory.sockets[onlineSocketsI].channels[currentChannelI].read = socketChannelRead;
				memory.sockets[onlineSocketsI].channels[currentChannelI].write = socketChannelWrite;
				memory.sockets[onlineSocketsI].channels[currentChannelI].total = socketChannelRead + socketChannelWrite;

				++currentChannelI;
			}

			memory.sockets[onlineSocketsI].socketId = skt;
			memory.sockets[onlineSocketsI].numOfChannels = currentChannelI;
			memory.sockets[onlineSocketsI].read = iMC_Rd_socket[skt];
			memory.sockets[onlineSocketsI].write = iMC_Wr_socket[skt];
			memory.sockets[onlineSocketsI].partialWrite = partial_write[skt];
			memory.sockets[onlineSocketsI].total= iMC_Rd_socket[skt] + iMC_Wr_socket[skt];
			if(memory.dramEnergyMetricsAvailable)
			{
				memory.sockets[onlineSocketsI].dramEnergy = getDRAMConsumedJoules(socketStatesBefore_[skt], socketStatesAfter_[skt]);
			}

			systemRead += iMC_Rd_socket[skt];
			systemWrite += iMC_Wr_socket[skt];

			++onlineSocketsI;
	    }

	    memory.system.read = systemRead;
	    memory.system.write = systemWrite;
	    memory.system.total = systemRead + systemWrite;
	}

	void Daemon::getPCMQPI()
	{
		PCMQPI& qpi = sharedPCMState_->pcm.qpi;

		const uint32 numSockets = sharedPCMState_->pcm.system.numOfSockets;
		const uint32 numLinksPerSocket = sharedPCMState_->pcm.system.numOfQPILinksPerSocket;

		qpi.incomingQPITrafficMetricsAvailable = pcmInstance_->incomingQPITrafficMetricsAvailable();
		if (qpi.incomingQPITrafficMetricsAvailable)
		{
			uint32 onlineSocketsI(0);
			for (uint32 i(0); i < numSockets; ++i)
			{
				if(!pcmInstance_->isSocketOnline(i))
					continue;

				qpi.incoming[onlineSocketsI].socketId = i;

				uint64 total(0);
				for (uint32 l(0); l < numLinksPerSocket; ++l)
				{
					uint64 bytes = getIncomingQPILinkBytes(i, l, systemStatesBefore_, systemStatesAfter_);
					qpi.incoming[onlineSocketsI].links[l].bytes = bytes;
					qpi.incoming[onlineSocketsI].links[l].utilization = getIncomingQPILinkUtilization(i, l, systemStatesForQPIBefore_, systemStatesAfter_);

					total+=bytes;
				}
				qpi.incoming[i].total = total;

				++onlineSocketsI;
			}

			qpi.incomingTotal = getAllIncomingQPILinkBytes(systemStatesBefore_, systemStatesAfter_);
		}

		qpi.outgoingQPITrafficMetricsAvailable = pcmInstance_->outgoingQPITrafficMetricsAvailable();
		if (qpi.outgoingQPITrafficMetricsAvailable)
		{
			uint32 onlineSocketsI(0);
			for (uint32 i(0); i < numSockets; ++i)
			{
				if(!pcmInstance_->isSocketOnline(i))
					continue;

				qpi.outgoing[onlineSocketsI].socketId = i;

				uint64 total(0);
				for (uint32 l(0); l < numLinksPerSocket; ++l)
				{
					uint64 bytes = getOutgoingQPILinkBytes(i, l, systemStatesBefore_, systemStatesAfter_);
					qpi.outgoing[onlineSocketsI].links[l].bytes = bytes;
					qpi.outgoing[onlineSocketsI].links[l].utilization = getOutgoingQPILinkUtilization(i, l, systemStatesForQPIBefore_, systemStatesAfter_);

					total+=bytes;
				}
				qpi.outgoing[i].total = total;

				++onlineSocketsI;
			}

			qpi.outgoingTotal = getAllOutgoingQPILinkBytes(systemStatesBefore_, systemStatesAfter_);
		}
	}

	uint64 Daemon::getTimestamp()
	{
		struct timespec now;

		clock_gettime(CLOCK_MONOTONIC_RAW, &now);

		uint64 epoch = (uint64)now.tv_sec * 1E9;
		epoch+=(uint64)now.tv_nsec;

		return epoch;
	}

	void Daemon::cleanup()
	{
		if(sharedPCMState_ != NULL)
		{
			//Detatch shared memory segment
			int success = shmdt(sharedPCMState_);
			if(success != 0)
			{
				std::cerr << "Failed to detatch the shared memory segment (errno=" << errno << ")\n";
			}
			else
			{
				// Delete segment
				success = shmctl(sharedMemoryId_, IPC_RMID, NULL);
				if(success != 0)
				{
					std::cerr << "Failed to delete the shared memory segment (errno=" << errno << ")\n";
				}
			}

			//Delete shared memory ID file
			success = remove(shmIdLocation_.c_str());
			if(success != 0)
			{
				std::cerr << "Failed to delete shared memory id location: " << shmIdLocation_ << " (errno=" << errno << ")\n";
			}
		}
	}

}
