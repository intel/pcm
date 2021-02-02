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
		void updatePCMState(SystemCounterState* systemStates, std::vector<SocketCounterState>* socketStates, std::vector<CoreCounterState>* coreStates);
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
		uint64 collectionTimeBefore_, collectionTimeAfter_;
		std::vector<CoreCounterState> coreStatesBefore_, coreStatesAfter_;
		std::vector<SocketCounterState> socketStatesBefore_, socketStatesAfter_;
		SystemCounterState systemStatesBefore_, systemStatesForQPIBefore_, systemStatesAfter_;
		ServerUncoreCounterState* serverUncoreCounterStatesBefore_;
		ServerUncoreCounterState* serverUncoreCounterStatesAfter_;
	};

}

#endif /* DAEMON_H_ */
