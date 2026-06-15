// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Steven Briscoe

#ifndef DAEMON_H_
#define DAEMON_H_

#include <sys/types.h>
#include <map>
#include <string>
#include <grp.h>

#include "common.h"
#include "pcm.h"

namespace PCMDaemon {

	enum Mode { DIFFERENCE, ABSOLUTE };

	class Daemon {
	public:
		Daemon(int argc, char *argv[]);
		~Daemon();
		int run();
        Daemon (const Daemon &) = delete;
        Daemon & operator = (const Daemon &) = delete;
	private:
		void setupPCM();
		void checkAccessAndProgramPCM();
		void readApplicationArguments(int argc, char *argv[]);
		void printExampleUsageAndExit(char *argv[]);
		void setupSharedMemory();
		gid_t resolveGroupName(const std::string& groupName);
		void getPCMCounters();
		void updatePCMState(SystemCounterState* systemStates, std::vector<SocketCounterState>* socketStates, std::vector<CoreCounterState>* coreStates, uint64 & t);
		void swapPCMBeforeAfterState();
		void getPCMSystem();
		void getPCMCore();
		void getPCMMemory();
		void getPCMQPI();
		uint64 getTimestamp();
		static void cleanup();

		bool debugMode_;
		uint32 pollIntervalMs_;
		std::string groupName_;
		Mode mode_;
		static std::string shmIdLocation_;

		static int sharedMemoryId_;
		static SharedPCMState* sharedPCMState_;
		PCM* pcmInstance_;
		std::map<std::string, uint32> subscribers_;
		std::vector<std::string> allowedSubscribers_;

		//Data for core, socket and system state
		uint64 collectionTimeBefore_{0ULL}, collectionTimeAfter_{0ULL};
		std::vector<CoreCounterState> coreStatesBefore_, coreStatesAfter_;
		std::vector<SocketCounterState> socketStatesBefore_, socketStatesAfter_;
		SystemCounterState systemStatesBefore_, systemStatesForQPIBefore_, systemStatesAfter_;
		ServerUncoreCounterState* serverUncoreCounterStatesBefore_;
		ServerUncoreCounterState* serverUncoreCounterStatesAfter_;
	};

}

#endif /* DAEMON_H_ */
